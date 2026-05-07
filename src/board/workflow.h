#ifndef BOARD_WORKFLOW_H
#define BOARD_WORKFLOW_H

class BoardController;

/// Shared base for public board workflows that operate on one internal board
/// controller instance owned by the public Board package root.
class BoardWorkflow {
 protected:
  BoardWorkflow();
  explicit BoardWorkflow(BoardController& controller);

  /// Access the shared board runtime. Only workflow implementations call this.
  BoardController& board() const;

 private:
  BoardController* controller_;
};

#endif  // BOARD_WORKFLOW_H