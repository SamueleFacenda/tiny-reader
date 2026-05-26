#pragma once

#include <Arduino.h>
#include "Config.h"

enum class ButtonId : uint8_t {
  Home = 0,
  Exit = 1,
  Prev = 2,
  Next = 3,
  Ok = 4,
  Count = 5
};

class ButtonManager {
 public:
  void begin();
  void update();
  bool consumeShortPress(ButtonId id);
  bool consumeLongPress(ButtonId id);
  bool isDown(ButtonId id) const;
  unsigned long downDuration(ButtonId id) const;

 private:
  struct ButtonState {
    uint8_t pin = 0;
    bool enabled = false;
    bool rawDown = false;
    bool lastDown = false;
    unsigned long pressedAt = 0;
    unsigned long lastChangeAt = 0;
    bool longFired = false;
    bool shortPress = false;
    bool longPress = false;
  };

  ButtonState states[static_cast<uint8_t>(ButtonId::Count)];

  ButtonState& state(ButtonId id);
  const ButtonState& state(ButtonId id) const;
};
