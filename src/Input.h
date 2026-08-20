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

// Presses are latched by a pin interrupt rather than by polling, so none are
// lost while the panel is refreshing (500ms partial, 1700ms full) or while a
// progress file is being written. Polling remains only for the held state.
class ButtonManager {
 public:
  void begin();
  void update();

  // Number of presses since the last call, cleared by reading. Several presses
  // arriving during one refresh let the reader skip pages instead of queueing
  // a refresh per press.
  uint8_t consumePresses(ButtonId id);
  bool consumeShortPress(ButtonId id);

  bool isDown(ButtonId id) const;
  bool anyDown() const;
  bool anyPending() const;   // does not consume, for the light sleep decision

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
