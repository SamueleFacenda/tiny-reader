#include "WebPortal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

#include "Storage.h"

#include "Bzip2Js.h"

static WebServer server(80);
static DNSServer dns;
static bool active = false;
static unsigned long startMs = 0;
static UploadCompleteCallback uploadCallback = nullptr;
static bool uploadFailed = false;
static String uploadPath;
static File uploadFile;
// Space left for this upload, measured once at UPLOAD_FILE_START: freeSpace() is a full
// littlefs metadata traversal, far too slow to run per 1436-byte chunk.
static size_t uploadBudget = 0;
// Guards the abort path: until a START arrives uploadPath still names the *previous*
// upload, which an abort during the headers must not delete.
static bool uploadInProgress = false;

// The page lives in upload.h, framed by the raw-literal delimiters so the
// preprocessor pastes it in as one string token. The .h name is not cosmetic:
// the build carries only source types into its copy of the sketch, and a
// directive inside the literal would never run, while #embed needs a GCC
// this toolchain predates.
static const char* uploadPage =
#include "upload.h"
;

// Space left for book data, keeping a margin for progress files and metadata.
static size_t freeSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes() + Config::FS_RESERVE_BYTES;
  return (total > used) ? total - used : 0;
}

static void handleRoot() {
  // send_P streams straight from flash. send() would copy the whole page into a String
  // first, and the heap is at its most fragmented right after WiFi comes up.
  server.send_P(200, "text/html", uploadPage);
}

// The bzip2 compressor bundle, streamed from flash like the page itself.
static void handleBzJs() {
  server.send_P(200, "application/javascript", BZIP2_JS);
}

// Everything else, including the connectivity probes a desktop OS fires on join, is
// redirected to the upload page, which is what makes this a captive portal.
static void handleNotFound() {
  server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/");
  server.send(302, "text/plain", "");
}

static void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    uploadInProgress = true;
    String safeName = storageSanitizeFilename(upload.filename);
    uploadPath = String(Config::BOOKS_DIR) + "/" + safeName;
    if (LittleFS.exists(uploadPath)) {
      LittleFS.remove(uploadPath);
    }
    uploadFile = LittleFS.open(uploadPath, "w");
    if (!uploadFile) {
      uploadFailed = true;
    }
    uploadBudget = freeSpace();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed) {
      return;
    }
    if (!uploadFile) {
      uploadFailed = true;
      return;
    }
    if (upload.currentSize > uploadBudget) {
      Serial.println("Upload aborted: not enough space");
      uploadFailed = true;
      uploadFile.close();
      LittleFS.remove(uploadPath);
      return;
    }
    uploadBudget -= upload.currentSize;
    if (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
      Serial.println("Upload aborted: short write");
      uploadFailed = true;
      uploadFile.close();
      LittleFS.remove(uploadPath);
      return;
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    uploadInProgress = false;
    if (uploadFile) {
      uploadFile.close();
    }
    if (uploadFailed) {
      server.send(413, "text/plain", "Not enough space or write error");
      if (uploadCallback) {
        uploadCallback(String(), false);
      }
      return;
    }
    server.send(200, "text/plain", "OK");
    if (uploadCallback) {
      uploadCallback(uploadPath, true);
    }
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    // WebServer sends no response on abort, so the only job here is to not leave
    // a half-written book in the library.
    if (!uploadInProgress) {
      return;
    }
    uploadInProgress = false;
    Serial.println("Upload aborted by client");
    uploadFailed = true;
    if (uploadFile) {
      uploadFile.close();
    }
    if (uploadPath.length()) {
      LittleFS.remove(uploadPath);
    }
    if (uploadCallback) {
      uploadCallback(String(), false);
    }
  }
}

// The association handshake runs in the WiFi task on core 0, so these events are the
// sketch's only view of why a client failed to join; the client's own supplicant log is
// the other half. Takes arduino_event_t* because that is the only callback shape
// WiFi.removeEvent() also accepts.
static void onWifiEvent(arduino_event_t* event) {
  if (!event) {
    return;
  }
  switch (event->event_id) {
    case ARDUINO_EVENT_WIFI_AP_START:
      Serial.println("AP start");
      break;
    case ARDUINO_EVENT_WIFI_AP_STOP:
      Serial.println("AP stop");
      break;
    case ARDUINO_EVENT_WIFI_AP_STACONNECTED: {
      const uint8_t* mac = event->event_info.wifi_ap_staconnected.mac;
      Serial.printf("AP client joined %02x:%02x:%02x:%02x:%02x:%02x aid=%u, now %u\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                    event->event_info.wifi_ap_staconnected.aid,
                    WiFi.softAPgetStationNum());
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STADISCONNECTED: {
      // IDF 4.4 carries no reason code on this event, so the MAC and aid are all there
      // is to correlate against the client's own log.
      const uint8_t* mac = event->event_info.wifi_ap_stadisconnected.mac;
      Serial.printf("AP client left %02x:%02x:%02x:%02x:%02x:%02x aid=%u, now %u\n",
                    mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                    event->event_info.wifi_ap_stadisconnected.aid,
                    WiFi.softAPgetStationNum());
      break;
    }
    case ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED:
      Serial.println("AP client got a DHCP lease");
      break;
    default:
      break;
  }
}

static bool startAccessPoint() {
  // Without this every portal start rewrites the WiFi config to NVS: a flash write,
  // which disables the flash cache and stalls non-IRAM code on both cores exactly
  // while the AP is coming up.
  WiFi.persistent(false);
  WiFi.onEvent(onWifiEvent);
  if (!WiFi.mode(WIFI_AP)) {
    Serial.println("AP failed: mode");
    return false;
  }
  const IPAddress gateway(192, 168, 4, 1);
  const IPAddress subnet(255, 255, 255, 0);
  if (!WiFi.softAPConfig(gateway, gateway, subnet)) {
    Serial.println("AP failed: softAPConfig");
    return false;
  }
  if (!WiFi.softAP(Config::WIFI_SSID, Config::WIFI_PASS, Config::WIFI_AP_CHANNEL,
                   false, Config::WIFI_AP_MAX_CONN)) {
    Serial.println("AP failed: softAP");
    return false;
  }
  delay(200);
  Serial.printf("AP up on %s ch%u heap=%u maxalloc=%u\n",
                WiFi.softAPIP().toString().c_str(), Config::WIFI_AP_CHANNEL,
                ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  return true;
}

static void stopAccessPoint() {
  WiFi.removeEvent(onWifiEvent);
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
}

bool webPortalStart(UploadCompleteCallback cb) {
  if (active) {
    return true;
  }
  uploadCallback = cb;
  if (!startAccessPoint()) {
    stopAccessPoint();
    return false;
  }
  server.on("/", handleRoot);
  server.on("/bz.js", HTTP_GET, handleBzJs);
  server.on("/upload", HTTP_POST, []() {}, handleUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  // Resolves every name to the portal, so a client's connectivity probe gets an answer
  // instead of timing out.
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  active = true;
  startMs = millis();
  Serial.println("Web portal started");
  return true;
}

void webPortalStop() {
  if (!active) {
    return;
  }
  dns.stop();
  server.stop();
  active = false;
  stopAccessPoint();
  Serial.println("Web portal stopped");
}

void webPortalHandle() {
  if (!active) {
    return;
  }
  dns.processNextRequest();
  server.handleClient();
}

bool webPortalActive() {
  return active;
}

unsigned long webPortalUptimeMs() {
  if (!active) {
    return 0;
  }
  return millis() - startMs;
}

size_t webPortalClientCount() {
  return active ? WiFi.softAPgetStationNum() : 0;
}

String webPortalIp() {
  if (!active) {
    return String();
  }
  return WiFi.softAPIP().toString();
}
