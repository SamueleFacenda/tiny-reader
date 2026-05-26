#include "Input.h"

static uint8_t pinFor(ButtonId id) {
  switch (id) {
    case ButtonId::Home:
      return Config::PIN_BTN_OK;
    case ButtonId::Exit:
      return Config::PIN_BTN_EXIT;
    case ButtonId::Prev:
      return Config::PIN_BTN_PREV;
    case ButtonId::Next:
      return Config::PIN_BTN_NEXT;
    case ButtonId::Ok:
      return Config::PIN_BTN_HOME;
    default:
      return 0;
  }
}

ButtonManager::ButtonState& ButtonManager::state(ButtonId id) {
  return states[static_cast<uint8_t>(id)];
}

const ButtonManager::ButtonState& ButtonManager::state(ButtonId id) const {
  return states[static_cast<uint8_t>(id)];
}

void ButtonManager::begin() {
  for (uint8_t i = 0; i < static_cast<uint8_t>(ButtonId::Count); ++i) {
    ButtonId id = static_cast<ButtonId>(i);
    ButtonState& st = states[i];
    st.pin = pinFor(id);
    st.enabled = (id != ButtonId::Home);
    st.rawDown = false;
    st.lastDown = false;
    st.pressedAt = 0;
    st.lastChangeAt = 0;
    st.longFired = false;
    st.shortPress = false;
    st.longPress = false;

    pinMode(st.pin, Config::BUTTON_PULLUP ? INPUT_PULLUP : INPUT);
  }
}

void ButtonManager::update() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < static_cast<uint8_t>(ButtonId::Count); ++i) {
    ButtonState& st = states[i];
    if (!st.enabled) {
      continue;
    }

    bool rawDown = (digitalRead(st.pin) == LOW);
    if (rawDown != st.rawDown) {
      st.rawDown = rawDown;
      st.lastChangeAt = now;
    }

    if (now - st.lastChangeAt < Config::BUTTON_DEBOUNCE_MS) {
      continue;
    }

    bool down = st.rawDown;
    if (down != st.lastDown) {
      st.lastDown = down;
      if (down) {
        st.pressedAt = now;
        st.longFired = false;
      } else {
        unsigned long held = now - st.pressedAt;
        if (!st.longFired && held < Config::LONG_PRESS_MS) {
          st.shortPress = true;
        }
      }
    }

    if (down && !st.longFired) {
      if (now - st.pressedAt >= Config::LONG_PRESS_MS) {
        st.longPress = true;
        st.longFired = true;
      }
    }

    st.lastDown = down;
  }
}

bool ButtonManager::consumeShortPress(ButtonId id) {
  ButtonState& st = state(id);
  if (st.shortPress) {
    st.shortPress = false;
    return true;
  }
  return false;
}

bool ButtonManager::consumeLongPress(ButtonId id) {
  ButtonState& st = state(id);
  if (st.longPress) {
    st.longPress = false;
    return true;
  }
  return false;
}

bool ButtonManager::isDown(ButtonId id) const {
  const ButtonState& st = state(id);
  if (!st.enabled) {
    return false;
  }
  return st.lastDown;
}

unsigned long ButtonManager::downDuration(ButtonId id) const {
  const ButtonState& st = state(id);
  if (!st.lastDown) {
    return 0;
  }
  return millis() - st.pressedAt;
}
