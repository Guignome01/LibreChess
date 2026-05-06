#ifndef BOARD_GUI_H
#define BOARD_GUI_H

#include "assistance.h"
#include "board/core/system.h"
#include "board/services.h"
#include "feedback.h"
#include "layering.h"
#include "stack.h"

/// Board-internal coordinator. Owns the persistent visual modules and the
/// modal stack, and exposes a single BoardServices facade that is the only
/// way internal workflow classes touch the board's hardware/visual surface.
class BoardGui {
 public:
  explicit BoardGui(BoardSystem& system);

  BoardGui(const BoardGui&) = delete;
  BoardGui& operator=(const BoardGui&) = delete;

  /// Internal services facade. Returned by Board::services() to a bounded
  /// friend list of workflow classes.
  BoardServices& services() { return services_; }

 private:
  BoardLayering layering_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;
  BoardStack stack_;
  BoardServices services_;
};

#endif  // BOARD_GUI_H
