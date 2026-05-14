#include "board/menus/prompt.h"

#include "board/core/runtime.h"
#include "board/gui/animations.h"
#include "board/menus/options.h"
#include "board/menus/panel.h"

#include <Arduino.h>

bool MenuPrompt::confirm(BoardRuntime& runtime, BoardAnimations& animations, bool flipped) {
  MenuPanel panel(runtime, animations);
  panel.setFlipped(flipped);
  panel.reset();
  panel.show(CONFIRM_OPTIONS);

  while (true) {
    int result = panel.poll();
    if (result != MENU_RESULT_NONE) {
      panel.erase();
      return result == MenuOptionId::CONFIRM_YES;
    }
    delay(runtime.cadenceMs());
  }
}
