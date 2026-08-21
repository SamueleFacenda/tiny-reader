#include "Input.h"

#include <driver/gpio.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <soc/gpio_struct.h>

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
static volatile int64_t lastOpenUs[kButtonCount];   // last time the contact was seen open
static uint8_t buttonPins[kButtonCount];
static portMUX_TYPE pressMux = portMUX_INITIALIZER_UNLOCKED;

// Must live in IRAM: a progress file is written on every page turn, and the
// flash cache is disabled during a flash write, so a handler resident in flash
// would crash the chip if a button were pressed in that window. For the same
// reason it calls nothing from the Arduino HAL, only esp_timer_get_time.
static const int64_t kDebounceUs = static_cast<int64_t>(Config::BUTTON_DEBOUNCE_MS) * 1000;

// Straight from the memory mapped register, because digitalRead() is not
// IRAM-safe. Every button pin is below GPIO32, so one register covers them.
static inline bool ARDUINO_ISR_ATTR pinIsLow(uint8_t pin) {
  return ((GPIO.in >> pin) & 0x1) == 0;
}

// Both edges are watched, and a press counts only once the contact has been open
// for the debounce interval. That is what makes one click exactly one press: the
// chatter while the contact closes never satisfies it, nor does the separate
// burst it makes on release however long the button was held in between, and a
// button held steady produces no edges at all.
static void ARDUINO_ISR_ATTR onButtonEdge(void* arg) {
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  const int64_t now = esp_timer_get_time();
  const bool closed = pinIsLow(buttonPins[index]);

  portENTER_CRITICAL_ISR(&pressMux);
  if (closed) {
    if (now - lastOpenUs[index] >= kDebounceUs) {
      pressCounts[index]++;
    }
  } else {
    lastOpenUs[index] = now;
  }
  portEXIT_CRITICAL_ISR(&pressMux);
}

static void attachPress(uint8_t index, uint8_t pin) {
  attachInterruptArg(pin, onButtonEdge, reinterpret_cast<void*>(static_cast<uintptr_t>(index)), CHANGE);
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
    lastOpenUs[i] = 0;
    buttonPins[i] = st.pin;
    pinMode(st.pin, Config::BUTTON_PULLUP ? INPUT_PULLUP : INPUT);
    // Buttons pull the pin low, so a press is a falling edge.
    attachPress(i, st.pin);
  }
}

// Light sleep wakes on a level, not an edge, and gpio_wakeup_enable() rewrites
// the pin's trigger type to that level. Leaving the press handler attached
// across the wake means it re-fires for as long as the button is held -- the
// handler cannot even reach the code that would restore the edge, so the
// interrupt watchdog eventually panics the core. Detaching first makes the wake
// silent.
void ButtonManager::prepareForLightSleep() {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    detachInterrupt(states[i].pin);
    gpio_wakeup_enable(static_cast<gpio_num_t>(states[i].pin), GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
}

void ButtonManager::resumeAfterLightSleep() {
  const bool wokeOnButton = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO);
  const int64_t now = esp_timer_get_time();

  for (uint8_t i = 0; i < kButtonCount; ++i) {
    ButtonState& st = states[i];
    gpio_wakeup_disable(static_cast<gpio_num_t>(st.pin));

    // The falling edge that woke us happened with no handler attached, so count
    // it here or the press is lost. Light sleep sits between every press, so
    // losing it would mean losing all of them. Stamping the debounce window also
    // swallows any level interrupt still pending when the handler returns.
    if (wokeOnButton && digitalRead(st.pin) == LOW) {
      portENTER_CRITICAL(&pressMux);
      pressCounts[i]++;
      lastOpenUs[i] = now;   // so the still-closed contact cannot count twice
      portEXIT_CRITICAL(&pressMux);
    }

    attachPress(i, st.pin);
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
