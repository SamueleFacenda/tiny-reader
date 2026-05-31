#include "WebPortal.h"

#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>

#include "Storage.h"

static WebServer server(80);
static bool active = false;
static unsigned long startMs = 0;
static UploadCompleteCallback uploadCallback = nullptr;
static bool uploadFailed = false;
static String uploadPath;
static File uploadFile;

static const char* uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<body>
<h2>TinyReader Upload</h2>
<p>Upload a TXT file to add it to the library.</p>
<form method="POST" action="/upload" enctype="multipart/form-data">
  <input type="file" name="file">
  <input type="submit" value="Upload">
</form>
</body>
</html>
)rawliteral";

static void handleRoot() {
  server.send(200, "text/html", uploadPage);
}

static void handleUpload() {
  HTTPUpload& upload = server.upload();

  if (upload.status == UPLOAD_FILE_START) {
    uploadFailed = false;
    String safeName = storageSanitizeFilename(upload.filename);
    uploadPath = String(Config::BOOKS_DIR) + "/" + safeName;
    if (LittleFS.exists(uploadPath)) {
      LittleFS.remove(uploadPath);
    }
    uploadFile = LittleFS.open(uploadPath, "w");
    if (!uploadFile) {
      uploadFailed = true;
    }
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFailed) {
      return;
    }
    if (!uploadFile) {
      uploadFailed = true;
      return;
    }
    if (uploadFile.size() + upload.currentSize > Config::MAX_UPLOAD_BYTES) {
      uploadFailed = true;
      uploadFile.close();
      LittleFS.remove(uploadPath);
      return;
    }
    uploadFile.write(upload.buf, upload.currentSize);
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    if (uploadFailed) {
      server.send(413, "text/plain", "File too large or write error");
      if (uploadCallback) {
        uploadCallback(String(), false);
      }
      return;
    }
    server.send(200, "text/plain", "OK");
    if (uploadCallback) {
      uploadCallback(uploadPath, true);
    }
  }
}

static void startAccessPoint() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(Config::WIFI_SSID, Config::WIFI_PASS);
  delay(200);
}

static void stopAccessPoint() {
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
  startAccessPoint();
  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST, []() {}, handleUpload);
  server.begin();
  active = true;
  startMs = millis();
  Serial.println("Web portal started");
  return true;
}

void webPortalStop() {
  if (!active) {
    return;
  }
  server.stop();
  active = false;
  stopAccessPoint();
  Serial.println("Web portal stopped");
}

void webPortalHandle() {
  if (!active) {
    return;
  }
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

String webPortalIp() {
  if (!active) {
    return String();
  }
  return WiFi.softAPIP().toString();
}
