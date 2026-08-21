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
static volatile bool contactClosed[kButtonCount];   // whether the contact is believed closed
static uint8_t buttonPins[kButtonCount];
static portMUX_TYPE pressMux = portMUX_INITIALIZER_UNLOCKED;

static const int64_t kDebounceUs = static_cast<int64_t>(Config::BUTTON_DEBOUNCE_MS) * 1000;

// Straight from the memory mapped register, because digitalRead() is not
// IRAM-safe. Assumes every button pin is below GPIO32, so one register covers them.
static inline bool ARDUINO_ISR_ATTR pinIsLow(uint8_t pin) {
  return ((GPIO.in >> pin) & 0x1) == 0;
}

// Fires on both edges and counts a press only on a transition from open to closed
// where the contact stayed open for the debounce interval. Both halves are needed:
// lastOpenUs rejects the bounces that reopen the contact as it closes, contactClosed
// rejects a low level seen while the button is already held. The handler reads the
// pin level rather than the edge that fired it, so the two disagree whenever an
// interrupt is coalesced or masked (this handler is IRAM-resident, but is masked for
// the length of a flash write, and a page turn writes a progress file). Under-counting
// is the safe direction: never manufacture a press.
//
// Must stay in IRAM and call nothing from the Arduino HAL, since a press can arrive
// while the flash cache is disabled.
static void ARDUINO_ISR_ATTR onButtonEdge(void* arg) {
  const uint32_t index = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
  const int64_t now = esp_timer_get_time();
  const bool closed = pinIsLow(buttonPins[index]);

  portENTER_CRITICAL_ISR(&pressMux);
  if (closed) {
    if (!contactClosed[index] && now - lastOpenUs[index] >= kDebounceUs) {
      pressCounts[index]++;
    }
    contactClosed[index] = true;
  } else {
    contactClosed[index] = false;
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
    // Seeded from the level so a button held across boot does not count the first
    // bounce of its release as a press.
    contactClosed[i] = (digitalRead(st.pin) == LOW);
    // Buttons pull the pin low, so a press is a falling edge.
    attachPress(i, st.pin);
  }
}

// Light sleep wakes on a level, and gpio_wakeup_enable() rewrites the pin's trigger
// type to it. A handler left attached would re-fire for as long as the button is held,
// starving the core until the interrupt watchdog panics, so detach before arming.
void ButtonManager::prepareForLightSleep() {
  const int64_t now = esp_timer_get_time();
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    // Last look at the pins before the handlers come off: a contactClosed left set by
    // a press whose release was never seen would swallow the next press on resume.
    if (digitalRead(states[i].pin) != LOW) {
      portENTER_CRITICAL(&pressMux);
      contactClosed[i] = false;
      lastOpenUs[i] = now;
      portEXIT_CRITICAL(&pressMux);
    }
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

    // The falling edge that woke us arrived with no handler attached, and light sleep
    // sits between every press, so counting it here is what keeps presses from being
    // lost. Stamping lastOpenUs also swallows any level interrupt still pending.
    // Only an open-to-closed transition counts: the wake is by level, so a button still
    // held from the press just handled satisfies it the moment sleep is armed.
    if (wokeOnButton && digitalRead(st.pin) == LOW && !contactClosed[i]) {
      portENTER_CRITICAL(&pressMux);
      pressCounts[i]++;
      contactClosed[i] = true;
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

// Reads the pins rather than the polled state, which lags: update() advances lastDown
// once per loop and skips the iteration on which the level changed. Sleeping while a
// button is held would arm a level wake that is already satisfied.
bool ButtonManager::anyRawDown() const {
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (digitalRead(states[i].pin) == LOW) {
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
