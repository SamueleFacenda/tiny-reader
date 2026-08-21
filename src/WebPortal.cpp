#include "WebPortal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

#include "Storage.h"

static WebServer server(80);
static DNSServer dns;
static bool active = false;
static unsigned long startMs = 0;
static UploadCompleteCallback uploadCallback = nullptr;
static bool uploadFailed = false;
static String uploadPath;
static File uploadFile;
// Space left for this upload, measured once at UPLOAD_FILE_START. Recomputing it
// per chunk means a full littlefs metadata traversal every 1436 bytes.
static size_t uploadBudget = 0;
// Guards the abort path: uploadPath still names the *previous* upload until a
// START arrives, and an abort during the headers must not delete that book.
static bool uploadInProgress = false;

static const char* uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>TinyReader Upload</title>
<style>
body { font-family: sans-serif; margin: 2em auto; max-width: 30em; padding: 0 1em; }
button { padding: 0.6em 1.2em; font-size: 1em; margin-top: 1em; }
#status { min-height: 3em; }
</style>
</head>
<body>
<h2>TinyReader Upload</h2>
<p>Pick a plain text book. Your browser converts it to Latin-1 and reflows the
paragraphs before sending, so the reader stores text it can display as is.</p>
<input type="file" id="file" accept=".txt,text/plain">
<button id="go">Upload</button>
<p id="status"></p>
<script>
// Characters the reader font (0x20-0xFE) lacks and that NFKD does not decompose.
var EXTRA = {
  0x2018:"'", 0x2019:"'", 0x201A:"'", 0x201B:"'",
  0x201C:'"', 0x201D:'"', 0x201E:'"', 0x00AB:'"', 0x00BB:'"',
  0x2013:'-', 0x2014:'-', 0x2015:'-', 0x2212:'-',
  0x2022:'*', 0x00B7:'*', 0x2192:'->', 0x2190:'<-',
  0x00A0:' ', 0x2007:' ', 0x2009:' ', 0x202F:' ',
  0x00AD:'',  0xFEFF:'',  0x00FF:'y'
};

// True for text that already holds one paragraph per line, so that reflowing it
// a second time cannot collapse its paragraphs into one. Books are hard wrapped
// at 60 to 80 columns, so a line past 120 characters can only be a paragraph
// that some earlier pass already unwrapped.
function alreadyReflowed(text) {
  if (/\n[ \t]*\n/.test(text)) return false;   // blank lines mean raw wrapping
  return text.split('\n').some(function (line) { return line.length > 120; });
}

// Reflow to one line per paragraph: the reader wraps text itself, and its screen
// is far narrower than the ~70 columns books are usually hard wrapped at.
function reflow(text) {
  var unwrap = !alreadyReflowed(text);
  return text
    .replace(/[\u0000-\u0008\u000B-\u001F]/g, '')
    .replace(/\r\n?/g, '\n')
    .replace(/[ \t]+$/gm, '')                             // trailing blanks
    .replace(/([a-zß-ÿ])-\n(?=[a-zß-ÿ])/g, '$1')  // join hyphenated words
    .replace(/\n[ \t]+/g, '\n\n')                         // an indent starts a paragraph
    .replace(/\n{2,}/g, '\u0001')                        // park the real breaks
    .replace(/\n/g, unwrap ? ' ' : '\u0001')             // unwrap everything else
    .replace(/\u0001/g, '\n')                            // and put them back
    .replace(/^[ \t]+/gm, '')                             // never leave an indent
    .replace(/\t/g, ' ')
    .replace(/ {2,}/g, ' ')
    .trim() + '\n';
}

// The iconv -t ISO-8859-1//TRANSLIT equivalent: keep what Latin-1 covers and
// approximate the rest by stripping the combining marks NFKD exposes.
function toLatin1(text) {
  var out = [];
  for (var ch of text) {
    var cp = ch.codePointAt(0);
    if (cp === 10) { out.push(10); continue; }
    var rep = EXTRA[cp];
    if (rep === undefined) {
      if (cp >= 32 && cp <= 0xFE) { out.push(cp); continue; }
      rep = ch.normalize('NFKD').replace(/\p{M}/gu, '');
    }
    for (var r of rep) {
      var rc = r.codePointAt(0);
      out.push(rc >= 32 && rc <= 0xFE ? rc : 63);         // 63 is '?'
    }
  }
  return new Uint8Array(out);
}

function say(msg) { document.getElementById('status').textContent = msg; }

async function upload() {
  var picked = document.getElementById('file').files[0];
  if (!picked) { say('Pick a file first.'); return; }
  say('Converting...');
  var buffer = await picked.arrayBuffer();
  var text;
  try {
    text = new TextDecoder('utf-8', {fatal: true}).decode(buffer);
  } catch (e) {
    text = new TextDecoder('iso-8859-1').decode(buffer);  // already converted
  }
  var bytes = toLatin1(reflow(text));
  say('Uploading ' + bytes.length + ' bytes...');
  var form = new FormData();
  form.append('file', new Blob([bytes], {type: 'text/plain'}), picked.name);
  try {
    var response = await fetch('/upload', {method: 'POST', body: form});
    var body = await response.text();
    say(response.ok
      ? 'Stored ' + picked.name + ' (' + buffer.byteLength + ' -> ' + bytes.length + ' bytes)'
      : 'Failed: ' + body);
  } catch (e) {
    say('Failed: ' + e.message);
  }
}

document.getElementById('go').addEventListener('click', upload);
</script>
</body>
</html>
)rawliteral";

// Space left for book data, keeping a margin for progress files and metadata.
static size_t freeSpace() {
  size_t total = LittleFS.totalBytes();
  size_t used = LittleFS.usedBytes() + Config::FS_RESERVE_BYTES;
  return (total > used) ? total - used : 0;
}

static void handleRoot() {
  // send_P streams straight from flash: send() would copy the whole page into a
  // String first, and the heap is at its most fragmented right after WiFi came up.
  server.send_P(200, "text/html", uploadPage);
}

// Anything else, including the connectivity probes a desktop OS fires on join,
// goes to the upload page so the portal looks like a captive portal.
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

// The association handshake runs entirely in the WiFi task on core 0, so these
// events are the only view the sketch has of why a client failed to join. Pair
// them with the supplicant log on the client side.
// Takes arduino_event_t* rather than the (id, info) pair because that is the only
// callback shape WiFi.removeEvent() can also match.
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
      // IDF 4.4 carries no reason code on this event (added in 5.x), so the MAC
      // and aid are all there is to correlate against the client's own log.
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
  // Without this every portal start rewrites the WiFi config to NVS: a flash
  // write, which disables the flash cache and stalls non-IRAM code on both cores
  // exactly while the AP is coming up.
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
  server.on("/upload", HTTP_POST, []() {}, handleUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  // Resolves every name to the portal, so the client stops waiting on DNS it is
  // never going to get an answer for.
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
