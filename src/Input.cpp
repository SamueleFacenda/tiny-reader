#include "Input.h"

#include <esp_timer.h>

static uint8_t pinFor(ButtonId id) {
  switch (id) {
    case ButtonId::Home:
      return Config::PIN_HOME;
    case ButtonId::Exit:
      return Config::PIN_EXIT;
    case ButtonId::Prev:
      return Config::PIN_PREV;
    case ButtonId::Next:
      return Config::PIN_NEXT;
    case ButtonId::Ok:
      return Config::PIN_OK;
    default:
      return 0;
  }
}

static const uint8_t kButtonCount = static_cast<uint8_t>(ButtonId::Count);
static volatile uint32_t pressCounts[kButtonCount];
static volatile int64_t lastEdgeUs[kButtonCount];
static portMUX_TYPE pressMux = portMUX_INITIALIZER_UNLOCKED;

// Must live in IRAM: a progress file is written on every page turn, and the
// flash cache is disabled during a flash write, so a handler resident in flash
// would crash the chip if a button were pressed in that window. For the same
// reason it calls nothing from the Arduino HAL, only esp_timer_get_time.
static void ARDUINO_ISR_ATTR onButtonEdge(void* arg) {
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  const int64_t now = esp_timer_get_time();
  portENTER_CRITICAL_ISR(&pressMux);
  if (now - lastEdgeUs[index] >= static_cast<int64_t>(Config::BUTTON_DEBOUNCE_MS) * 1000) {
    lastEdgeUs[index] = now;
    pressCounts[index]++;
  }
  portEXIT_CRITICAL_ISR(&pressMux);
}

ButtonManager::ButtonState& ButtonManager::state(ButtonId id) {
  return states[static_cast<uint8_t>(id)];
}

const ButtonManager::ButtonState& ButtonManager::state(ButtonId id) const {
  return states[static_cast<uint8_t>(id)];
}

void ButtonManager::begin() {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    ButtonId id = static_cast<ButtonId>(i);
    ButtonState& st = states[i];
    st.pin = pinFor(id);
    st.rawDown = false;
    st.lastDown = false;
    st.lastChangeAt = 0;
    pressCounts[i] = 0;
    lastEdgeUs[i] = 0;
    pinMode(st.pin, Config::BUTTON_PULLUP ? INPUT_PULLUP : INPUT);
    // Buttons pull the pin low, so a press is a falling edge.
    attachInterruptArg(st.pin, onButtonEdge, reinterpret_cast<void*>(static_cast<uintptr_t>(i)), FALLING);
  }
}

void ButtonManager::update() {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    ButtonState& st = states[i];
    const bool rawDown = (digitalRead(st.pin) == LOW);
    if (rawDown != st.rawDown) {
      st.rawDown = rawDown;
      st.lastChangeAt = now;
      continue;
    }
    if (now - st.lastChangeAt >= Config::BUTTON_DEBOUNCE_MS) {
      st.lastDown = rawDown;
    }
  }
}

uint8_t ButtonManager::consumePresses(ButtonId id) {
  const uint8_t index = static_cast<uint8_t>(id);
  portENTER_CRITICAL(&pressMux);
  const uint32_t count = pressCounts[index];
  pressCounts[index] = 0;
  portEXIT_CRITICAL(&pressMux);
  return (count > 255) ? 255 : static_cast<uint8_t>(count);
}

bool ButtonManager::consumeShortPress(ButtonId id) {
  return consumePresses(id) > 0;
}

bool ButtonManager::isDown(ButtonId id) const {
  return state(id).lastDown;
}

bool ButtonManager::anyDown() const {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (states[i].lastDown) {
      return true;
    }
  }
  return false;
}

bool ButtonManager::anyPending() const {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (pressCounts[i] != 0) {
      return true;
    }
  }
  return false;
}
