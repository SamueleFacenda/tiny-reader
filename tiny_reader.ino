#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <LittleFS.h>


// Use GxEPD2 + Adafruit GFX for e-paper
#include <GxEPD2_BW.h>

#define GxEPD2_DRIVER_CLASS GxEPD2_213_BN // DEPG0213BN  122x250, SSD1680, (FPC-7528B), TTGO T5 V2.4.1, V2.3.1
#define GxEPD2_DRIVER_CLASS GxEPD2_213_GDEY0213B74 // GDEY0213B74 122x250, SSD1680, (FPC-A002 20.04.08)
// Hardware wiring from factory spi.h
// SCK = 12, MOSI = 11, RES = 10, DC = 13, CS = 14, BUSY = 9
// Construct a GxEPD2 display object for the 213 (122x250 visible) panel
GxEPD2_BW<GxEPD2_DRIVER_CLASS, GxEPD2_DRIVER_CLASS::HEIGHT> display(GxEPD2_DRIVER_CLASS(/*CS=*/ 14, /*DC=*/ 13, /*RST=*/ 10, /*BUSY=*/ 9));

// Pins - align with factory mappings and use HOME (GPIO2) for wake
// HOME (wake): 2, EXIT:1, PRV:6, NEXT:4, OK:5
#define BTN_HOME 2
#define BTN_EXIT 1
#define BTN_PREV 6
#define BTN_NEXT 4
#define BTN_OK 5

// Server configuration
WebServer server(80);
bool serverActive = false;
unsigned long serverStart = 0;
const unsigned long SERVER_TIMEOUT_MS = 5 * 60UL * 1000UL; // 5 minutes auto-shutdown

File book;
size_t fileSize = 0;
uint32_t pagePos = 0;

unsigned long lastActivity = 0;

// storage
#define MAX_FILE_SIZE (1500 * 1024)

const char* uploadPage = R"rawliteral(
<!DOCTYPE html>
<html>
<body>
<h2>Upload TXT</h2>
<form method="POST" action="/upload" enctype="multipart/form-data">
<input type="file" name="file">
<input type="submit">
</form>
</body>
</html>
)rawliteral";

// Forward
void renderPage();
void savePosition();

// ---------------- WIFI control ----------------
void startAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("PocketReader", "12345678");
  delay(200);
}

void stopWiFi() {
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_OFF);
    delay(100);
  }
}

// ---------------- WEB ----------------
void handleRoot() {
  server.send(200, "text/html", uploadPage);
}

void handleUpload() {
  HTTPUpload& upload = server.upload();
  static File f;

  if (upload.status == UPLOAD_FILE_START) {
    if (LittleFS.exists("/book.txt")) LittleFS.remove("/book.txt");
    f = LittleFS.open("/book.txt", "w");
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (f) {
      if (f.size() + upload.currentSize > MAX_FILE_SIZE) {
        f.close();
        LittleFS.remove("/book.txt");
        server.send(413, "text/plain", "File too large");
        return;
      }
      f.write(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (f) f.close();
    server.send(200, "text/plain", "OK");

    // reload and render immediately
    book = LittleFS.open("/book.txt", "r");
    if (book) {
      fileSize = book.size();
      pagePos = 0;
      savePosition();
      book.seek(pagePos);
      renderPage();
    }
  }
}

// ---------------- STORAGE ----------------
void loadBook() {
  if (!LittleFS.exists("/book.txt")) return;
  book = LittleFS.open("/book.txt", "r");
  fileSize = book.size();
  File posFile = LittleFS.open("/pos.txt", "r");
  if (posFile) {
    pagePos = posFile.parseInt();
    posFile.close();
  }
  book.seek(pagePos);
}

void savePosition() {
  File posFile = LittleFS.open("/pos.txt", "w");
  if (posFile) {
    posFile.print(pagePos);
    posFile.close();
  }
}

// ---------------- DISPLAY ----------------
void renderPage() {
  if (!book) return;

  // Initialize display and clear to white
  display.init(115200);
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    int y = 10;
    String line = "";
    while (book.available() && y < display.height() - 16) {
      char c = book.read();
      if (c == '\n' || line.length() > 35) {
        display.setCursor(5, y);
        display.print(line);
        line = "";
        y += 16;
      } else {
        line += c;
      }
    }
  } while (display.nextPage());

  pagePos = book.position();
  savePosition();

  // put panel in low power if supported
  display.powerOff();
}

// Display a simple self-test screen so we can verify the panel works even when
// no book is loaded. This helps debug wiring and driver issues.
void displaySelfTest() {
  Serial.println("Display self-test");
  display.init(115200);
  display.setRotation(3);
  display.setTextColor(GxEPD_BLACK);
  display.setTextSize(2);
  display.setFullWindow();

  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setCursor(10, 24);
    display.print("TinyReader");
    display.setCursor(10, 54);
    display.print("Self-test");
  } while (display.nextPage());

  delay(500);
  display.hibernate();
}

// ---------------- BUTTONS ----------------
// Read button state. Avoid touching UART0 TX (GPIO1) because configuring or
// reusing that pin breaks the USB serial console used for logs/monitoring.
bool isPressed(int pin) {
  if (pin == 1) {
    // Temporarily ignore the EXIT button if it is wired to GPIO1 (TX)
    // so the Serial console remains usable while debugging.
    return false;
  }
  return digitalRead(pin) == LOW;
}

void startServer() {
  if (serverActive) return;
  startAP();
  server.on("/", handleRoot);
  server.on("/upload", HTTP_POST, []() {}, handleUpload);
  server.begin();
  serverActive = true;
  serverStart = millis();
}

void stopServer() {
  if (!serverActive) return;
  // WebServer::reset() doesn't exist in this core; stop() is sufficient to shut down the server.
  server.stop();
  server.stop();
  serverActive = false;
  stopWiFi();
}

void handleButtons() {
  static unsigned long pressStartNext = 0;
  static unsigned long pressStartPrev = 0;

  // Next: short tap = next page, long press = wake/deep-sleep toggle handled elsewhere
  if (isPressed(BTN_NEXT)) {
    if (pressStartNext == 0) pressStartNext = millis();
  } else {
    if (pressStartNext != 0) {
      unsigned long dur = millis() - pressStartNext;
      if (dur < 800) {
        // tap: render next page
        if (book) {
          renderPage();
        }
      }
    }
    pressStartNext = 0;
  }

  // Prev: long-press to start webserver
  if (isPressed(BTN_PREV)) {
    if (pressStartPrev == 0) pressStartPrev = millis();
    if (!serverActive && millis() - pressStartPrev > 1200) {
      // long press: activate web server
      startServer();
    }
  } else {
    pressStartPrev = 0;
  }
}

// ---------------- SLEEP ----------------
void maybeDeepSleep() {
  // If no activity for a while, deep sleep and wake on HOME button (EXT0)
  if (millis() - lastActivity > 30000) {
    // prepare EPD for low power
    display.hibernate();
    // Use HOME (GPIO2) as RTC wake pin (matches factory firmware)
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_2, 0); // wake on HOME (LOW)
    delay(50);
    esp_deep_sleep_start();
  }
}

// ---------------- SETUP ----------------
void setup() {
  Serial.begin(115200);
  // Configure pins. Avoid configuring GPIO1 (UART0 TX) as an input because
  // that can override the UART function and corrupt the Serial monitor output.
  auto configureButtonPin = [](int pin) {
    if (pin == 1) {
      Serial.println("NOTE: skipping configuration for pin 1 (UART TX) to preserve Serial");
      return;
    }
    pinMode(pin, INPUT_PULLUP);
  };

  // Configure pin 7 for screen power control
  pinMode(7, OUTPUT);        // Set pin 7 as output
  digitalWrite(7, HIGH);     // Activate screen power by setting pin 7 high

  configureButtonPin(BTN_HOME);
  configureButtonPin(BTN_EXIT);
  configureButtonPin(BTN_PREV);
  configureButtonPin(BTN_NEXT);
  configureButtonPin(BTN_OK);

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS Initialization Failed completely!");
    while (1) delay(1000);
  } else {
    Serial.println("LittleFS mounted successfully.");
  }
  // Do not start WiFi/server by default

  // Initialize display library (no full update here)
  // LittleFS may be needed by EPD font routines

  loadBook();
  if (!book) {
    // No book loaded: run a visible self-test to help debug hardware wiring.
    displaySelfTest();
  } else {
    renderPage();
  }
  lastActivity = millis();
}

// ---------------- LOOP ----------------
void loop() {
  if (serverActive) {
    server.handleClient();
    // auto-shutdown server if idle
    if (millis() - serverStart > SERVER_TIMEOUT_MS) {
      stopServer();
    }
  }

  handleButtons();
  maybeDeepSleep();
}
