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

// Presses are latched by a pin interrupt, so none are lost while the panel refreshes
// or a progress file is written. Polling only maintains the held state.
class ButtonManager {
 public:
  void begin();
  void update();

  // Presses since the last call, cleared by reading. A burst arriving during one
  // refresh lets the reader skip pages instead of repainting per press.
  uint8_t consumePresses(ButtonId id);
  bool consumeShortPress(ButtonId id);

  bool isDown(ButtonId id) const;
  bool anyRawDown() const;   // straight from the pins, for the light sleep decision
  bool anyPending() const;   // does not consume, for the light sleep decision

  // Light sleep wakes on a level, and arming one rewrites the pin's trigger type.
  // These own that dance so it cannot collide with the press handler.
  void prepareForLightSleep();
  void resumeAfterLightSleep();

 private:
  struct ButtonState {
    uint8_t pin = 0;
    bool rawDown = false;
    bool lastDown = false;
    unsigned long lastChangeAt = 0;
  };

  ButtonState states[static_cast<uint8_t>(ButtonId::Count)];

  ButtonState& state(ButtonId id);
  const ButtonState& state(ButtonId id) const;
};
