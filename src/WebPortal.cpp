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
    if (upload.currentSize > freeSpace()) {
      Serial.println("Upload aborted: not enough space");
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
