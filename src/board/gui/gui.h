#ifndef BOARD_GUI_H
#define BOARD_GUI_H

#include "assistance.h"
#include "board/diagnostics.h"
#include "feedback.h"
#include "board/core/layering.h"
#include "board/core/system.h"
#include "menu.h"
#include "selection.h"
#include "stack.h"

#include "game.h"
#include "logger.h"

#include <atomic>
#include <cstdint>

/// Board-internal coordinator for visual workflows, menus, and feedback.
class BoardGui {
 public:
  explicit BoardGui(BoardSystem& system);

  BoardGui(const BoardGui&) = delete;
  BoardGui& operator=(const BoardGui&) = delete;

  void waitForBoardSetup(const LibreChess::Game& game, LibreChess::Log& logger);
  void showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game);
  void showCapturePlacementPrompt(int row, int col);
  void guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                     const LibreChess::CastlingInfo& castling, bool waitForKingCompletion,
                     LibreChess::Log& logger);
  void guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                 bool isCapture, bool isEnPassant,
                                 int enPassantCapturedPawnRow,
                                 LibreChess::Log& logger);

  void clearBoardFeedback(bool show = true);
  void clearFeedbackSquare(int row, int col);
  void showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow, int toCol,
                              const LibreChess::Game& game);
  void showIllegalMoveFeedback(int row, int col);
  void showResignProgress(int row, int col, int level, bool clearFirst = false);
  void clearResignFeedback(int row, int col);
  void showWinner(LibreChess::Color winnerColor);
  void showRemoteGameEnd(char winnerColor);
  void showErrorFeedback();

  std::atomic<bool>* startThinkingStatus();
  std::atomic<bool>* startWaitingStatus();
  void stopStatusAnimation(std::atomic<bool>*& stopFlag);

  void clearAllLEDs(bool show = true);
  void showConnectingAnimation();

  void startGameSelectionMenu();
  void clearGameSelectionMenu();
  BoardGameSelection pollGameSelectionMenu();

  bool confirmAction(bool flipped = false);
  bool confirmResume(BoardGameSelectionMode mode, bool flipped = false);

  void beginDiagnostics();
  void updateDiagnostics();
  bool diagnosticsComplete() const;

 private:
  BoardSystem& system_;
  BoardLayering layering_;
  BoardFeedback feedback_;
  BoardAssistance assistance_;
  BoardDiagnostics diagnostics_;
  BoardMenu gameMenu_;
  BoardMenu botDifficultyMenu_;
  BoardMenu botColorMenu_;
  BoardStack stack_;
  uint8_t pendingBotDifficulty_;
};

#endif  // BOARD_GUI_H
