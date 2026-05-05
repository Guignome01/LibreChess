#include "gui.h"

#include "board/config.h"
#include "board/core/animations.h"
#include "board/core/colors.h"

#include <Arduino.h>

namespace {

LedRGB resumeIndicatorColor(BoardGameSelectionMode mode) {
  switch (mode) {
    case BoardGameSelectionMode::CHESS_MOVES:
      return LedColors::Blue;
    case BoardGameSelectionMode::BOT:
      return LedColors::Green;
    case BoardGameSelectionMode::LICHESS:
      return LedColors::Yellow;
    case BoardGameSelectionMode::BOARD_DIAGNOSTICS:
      return LedColors::Red;
    case BoardGameSelectionMode::NONE:
    default:
      return LedColors::White;
  }
}

}  // namespace

BoardGui::BoardGui(BoardSystem& system)
    : system_(system),
      layering_(system_),
      feedback_(&system_, &layering_),
      assistance_(&system_, BoardAssistanceLevel::LEGAL_MOVES, &layering_),
      diagnostics_(&system_, &layering_),
      gameMenu_(&system_, &layering_),
      botDifficultyMenu_(&system_, &layering_),
      botColorMenu_(&system_, &layering_),
      stack_(),
      pendingBotDifficulty_(4) {
  configureMenus(gameMenu_, botDifficultyMenu_, botColorMenu_);
}

void BoardGui::waitForBoardSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  assistance_.waitForSetup(game, logger);
}

void BoardGui::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game) {
  assistance_.showLegalMoveHighlights(fromRow, fromCol, moves, game);
}

void BoardGui::showCapturePlacementPrompt(int row, int col) {
  assistance_.showCapturePlacementPrompt(row, col);
}

void BoardGui::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                             const LibreChess::CastlingInfo& castling,
                             bool waitForKingCompletion, LibreChess::Log& logger) {
  assistance_.guideCastling(kingFromRow, kingFromCol, kingToRow, kingToCol,
                            castling, waitForKingCompletion, logger);
}

void BoardGui::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                         bool isCapture, bool isEnPassant,
                                         int enPassantCapturedPawnRow,
                                         LibreChess::Log& logger) {
  assistance_.guideRemoteMoveCompletion(fromRow, fromCol, toRow, toCol,
                                        isCapture, isEnPassant,
                                        enPassantCapturedPawnRow, logger);
}

void BoardGui::clearBoardFeedback(bool show) {
  feedback_.clearBoard(show);
}

void BoardGui::clearFeedbackSquare(int row, int col) {
  feedback_.clearSquare(row, col);
}

void BoardGui::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow,
                                      int toCol, const LibreChess::Game& game) {
  feedback_.showMoveResultFeedback(result, toRow, toCol, game);
}

void BoardGui::showIllegalMoveFeedback(int row, int col) {
  feedback_.showIllegalMoveFeedback(row, col);
}

void BoardGui::showResignProgress(int row, int col, int level, bool clearFirst) {
  feedback_.showResignProgress(row, col, level, clearFirst);
}

void BoardGui::clearResignFeedback(int row, int col) {
  feedback_.clearResignFeedback(row, col);
}

void BoardGui::showWinner(LibreChess::Color winnerColor) {
  feedback_.showWinner(winnerColor);
}

void BoardGui::showRemoteGameEnd(char winnerColor) {
  feedback_.showRemoteGameEnd(winnerColor);
}

void BoardGui::showErrorFeedback() {
  feedback_.showError();
}

std::atomic<bool>* BoardGui::startThinkingStatus() {
  return feedback_.startThinking();
}

std::atomic<bool>* BoardGui::startWaitingStatus() {
  return feedback_.startWaiting();
}

void BoardGui::stopStatusAnimation(std::atomic<bool>*& stopFlag) {
  feedback_.stopAnimation(stopFlag);
}

void BoardGui::clearAllLEDs(bool show) {
  layering_.clearAll(show);
}

void BoardGui::showConnectingAnimation() {
  layering_.clearAll(false);
  system_.runAnimationNow(AnimationJob::connecting());
}

void BoardGui::startGameSelectionMenu() {
  clearGameSelectionMenu();
  pendingBotDifficulty_ = 4;
  stack_.push(&gameMenu_);
}

void BoardGui::clearGameSelectionMenu() {
  stack_.clear();
}

BoardGameSelection BoardGui::pollGameSelectionMenu() {
  system_.readSensors();

  BoardGameSelection selection;
  int result = stack_.poll();
  if (result == BoardDrawable::RESULT_NONE || result == BoardDrawable::RESULT_BACK)
    return selection;

  switch (result) {
    case MenuId::CHESS_MOVES:
      selection.mode = BoardGameSelectionMode::CHESS_MOVES;
      clearGameSelectionMenu();
      return selection;
    case MenuId::BOT:
      stack_.push(&botDifficultyMenu_);
      return selection;
    case MenuId::LICHESS:
      selection.mode = BoardGameSelectionMode::LICHESS;
      clearGameSelectionMenu();
      return selection;
    case MenuId::BOARD_DIAGNOSTICS:
      selection.mode = BoardGameSelectionMode::BOARD_DIAGNOSTICS;
      clearGameSelectionMenu();
      return selection;
    case MenuId::DIFF_1:
    case MenuId::DIFF_2:
    case MenuId::DIFF_3:
    case MenuId::DIFF_4:
    case MenuId::DIFF_5:
    case MenuId::DIFF_6:
    case MenuId::DIFF_7:
    case MenuId::DIFF_8:
      pendingBotDifficulty_ = static_cast<uint8_t>(result - MenuId::DIFF_1 + 1);
      stack_.push(&botColorMenu_);
      return selection;
    case MenuId::PLAY_WHITE:
      selection.mode = BoardGameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'w';
      clearGameSelectionMenu();
      return selection;
    case MenuId::PLAY_BLACK:
      selection.mode = BoardGameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = 'b';
      clearGameSelectionMenu();
      return selection;
    case MenuId::PLAY_RANDOM:
      selection.mode = BoardGameSelectionMode::BOT;
      selection.botDifficulty = pendingBotDifficulty_;
      selection.playerColor = (random(2) == 0) ? 'w' : 'b';
      clearGameSelectionMenu();
      return selection;
    default:
      return selection;
  }
}

bool BoardGui::confirmAction(bool flipped) {
  return boardConfirm(&system_, &layering_, flipped);
}

bool BoardGui::confirmResume(BoardGameSelectionMode mode, bool flipped) {
  system_.runAnimation(AnimationJob::blink(3, 3, resumeIndicatorColor(mode), 2));
  system_.waitForAnimationQueueDrain();
  return confirmAction(flipped);
}

void BoardGui::beginDiagnostics() {
  diagnostics_.begin();
}

void BoardGui::updateDiagnostics() {
  diagnostics_.update();
}

bool BoardGui::diagnosticsComplete() const {
  return diagnostics_.isComplete();
}
