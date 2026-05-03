#include "board.h"

#include "assistance.h"
#include "colors.h"
#include "config.h"
#include "diagnostics.h"
#include "feedback.h"
#include "menu.h"
#include "navigator.h"
#include "system.h"

#include <Arduino.h>

struct Board::Impl {
  BoardSystem system;
  BoardFeedback feedback;
  BoardAssistance assistance;
  BoardDiagnostics diagnostics;
  BoardMenu gameMenu;
  BoardMenu botDifficultyMenu;
  BoardMenu botColorMenu;
  MenuNavigator navigator;
  uint8_t pendingBotDifficulty;

  Impl()
      : system(),
        feedback(&system),
        assistance(&system),
        diagnostics(&system),
        gameMenu(&system),
        botDifficultyMenu(&system),
        botColorMenu(&system),
        navigator(),
        pendingBotDifficulty(4) {
    configureMenus(gameMenu, botDifficultyMenu, botColorMenu);
  }
};

namespace {

LedRGB resumeIndicatorColor(Board::GameSelectionMode mode) {
  switch (mode) {
    case Board::GameSelectionMode::CHESS_MOVES:
      return LedColors::Blue;
    case Board::GameSelectionMode::BOT:
      return LedColors::Green;
    case Board::GameSelectionMode::LICHESS:
      return LedColors::Yellow;
    case Board::GameSelectionMode::BOARD_DIAGNOSTICS:
      return LedColors::Red;
    case Board::GameSelectionMode::NONE:
    default:
      return LedColors::White;
  }
}

}  // namespace

Board::Board() : impl_(std::make_unique<Impl>()) {}

Board::~Board() = default;

void Board::begin() {
  if (!impl_->system.begin()) {
    Serial.println("Board animation lifecycle initialization failed");
  }
  impl_->system.syncOccupancyBaseline();
}

void Board::tick() {
  impl_->system.readSensors();
}

void Board::readSensors() {
  tick();
}

bool Board::occupied(int row, int col) const {
  return impl_->system.occupied(row, col);
}

bool Board::wasOccupied(int row, int col) const {
  return impl_->system.wasOccupied(row, col);
}

bool Board::wasLifted(int row, int col) const {
  return impl_->system.wasLifted(row, col);
}

bool Board::wasPlaced(int row, int col) const {
  return impl_->system.wasPlaced(row, col);
}

bool Board::changed(int row, int col) const {
  return impl_->system.changed(row, col);
}

uint8_t Board::changedCount() const {
  return impl_->system.changedCount();
}

bool Board::changedSquare(uint8_t index, int& row, int& col) const {
  auto square = impl_->system.changedSquare(index);
  if (!square.valid())
    return false;

  row = square.row;
  col = square.col;
  return true;
}

void Board::syncOccupancyBaseline() {
  impl_->system.syncOccupancyBaseline();
}

void Board::waitForBoardSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  impl_->assistance.waitForSetup(game, logger);
}

void Board::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game) {
  impl_->assistance.showLegalMoveHighlights(fromRow, fromCol, moves, game);
}

void Board::showCapturePlacementPrompt(int row, int col) {
  impl_->assistance.showCapturePlacementPrompt(row, col);
}

void Board::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                          const LibreChess::CastlingInfo& castling,
                          bool waitForKingCompletion, LibreChess::Log& logger) {
  impl_->assistance.guideCastling(kingFromRow, kingFromCol, kingToRow, kingToCol,
                                  castling, waitForKingCompletion, logger);
}

void Board::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                      bool isCapture, bool isEnPassant,
                                      int enPassantCapturedPawnRow,
                                      LibreChess::Log& logger) {
  impl_->assistance.guideRemoteMoveCompletion(fromRow, fromCol, toRow, toCol,
                                              isCapture, isEnPassant,
                                              enPassantCapturedPawnRow, logger);
}

void Board::clearBoardFeedback(bool show) {
  impl_->feedback.clearBoard(show);
}

void Board::clearFeedbackSquare(int row, int col) {
  impl_->feedback.clearSquare(row, col);
}

void Board::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow,
                                   int toCol, const LibreChess::Game& game) {
  impl_->feedback.showMoveResultFeedback(result, toRow, toCol, game);
}

void Board::showIllegalMoveFeedback(int row, int col) {
  impl_->feedback.showIllegalMoveFeedback(row, col);
}

void Board::showResignProgress(int row, int col, int level, bool clearFirst) {
  impl_->feedback.showResignProgress(row, col, level, clearFirst);
}

void Board::clearResignFeedback(int row, int col) {
  impl_->feedback.clearResignFeedback(row, col);
}

void Board::showWinner(LibreChess::Color winnerColor) {
  impl_->feedback.showWinner(winnerColor);
}

void Board::showRemoteGameEnd(char winnerColor) {
  impl_->feedback.showRemoteGameEnd(winnerColor);
}

void Board::showErrorFeedback() {
  impl_->feedback.showError();
}

std::atomic<bool>* Board::startThinkingStatus() {
  return impl_->feedback.startThinking();
}

std::atomic<bool>* Board::startWaitingStatus() {
  return impl_->feedback.startWaiting();
}

void Board::stopStatusAnimation(std::atomic<bool>*& stopFlag) {
  impl_->feedback.stopAnimation(stopFlag);
}

void Board::clearAllLEDs(bool show) {
  impl_->system.clearAllLEDs(show);
}

void Board::showConnectingAnimation() {
  impl_->system.runAnimationNow(AnimationJob::connecting());
}

void Board::startGameSelectionMenu() {
  clearGameSelectionMenu();
  impl_->pendingBotDifficulty = 4;
  impl_->navigator.push(&impl_->gameMenu);
}

void Board::clearGameSelectionMenu() {
  impl_->navigator.clear();
}

Board::GameSelection Board::pollGameSelectionMenu() {
  readSensors();

  GameSelection selection;
  int result = impl_->navigator.poll();
  if (result == BoardMenu::RESULT_NONE || result == BoardMenu::RESULT_BACK)
    return selection;

  switch (result) {
    case MenuId::CHESS_MOVES:
      selection.mode = GameSelectionMode::CHESS_MOVES;
      clearGameSelectionMenu();
      return selection;
    case MenuId::BOT:
      impl_->navigator.push(&impl_->botDifficultyMenu);
      return selection;
    case MenuId::LICHESS:
      selection.mode = GameSelectionMode::LICHESS;
      clearGameSelectionMenu();
      return selection;
    case MenuId::BOARD_DIAGNOSTICS:
      selection.mode = GameSelectionMode::BOARD_DIAGNOSTICS;
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
      impl_->pendingBotDifficulty = static_cast<uint8_t>(result - MenuId::DIFF_1 + 1);
      impl_->navigator.push(&impl_->botColorMenu);
      return selection;
    case MenuId::PLAY_WHITE:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = impl_->pendingBotDifficulty;
      selection.playerColor = 'w';
      clearGameSelectionMenu();
      return selection;
    case MenuId::PLAY_BLACK:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = impl_->pendingBotDifficulty;
      selection.playerColor = 'b';
      clearGameSelectionMenu();
      return selection;
    case MenuId::PLAY_RANDOM:
      selection.mode = GameSelectionMode::BOT;
      selection.botDifficulty = impl_->pendingBotDifficulty;
      selection.playerColor = (random(2) == 0) ? 'w' : 'b';
      clearGameSelectionMenu();
      return selection;
    default:
      return selection;
  }
}

bool Board::confirmAction(bool flipped) {
  return boardConfirm(&impl_->system, flipped);
}

bool Board::confirmResume(GameSelectionMode mode, bool flipped) {
  impl_->system.runAnimation(AnimationJob::blink(3, 3, resumeIndicatorColor(mode), 2));
  impl_->system.waitForAnimationQueueDrain();
  return confirmAction(flipped);
}

void Board::beginDiagnostics() {
  impl_->diagnostics.begin();
}

void Board::updateDiagnostics() {
  impl_->diagnostics.update();
}

bool Board::diagnosticsComplete() const {
  return impl_->diagnostics.isComplete();
}

uint8_t Board::getBrightness() const {
  return impl_->system.getBrightness();
}

uint8_t Board::getDimMultiplier() const {
  return impl_->system.getDimMultiplier();
}

void Board::setBrightness(uint8_t value) {
  impl_->system.setBrightness(value);
}

void Board::setDimMultiplier(uint8_t value) {
  impl_->system.setDimMultiplier(value);
}

void Board::saveLedSettings() {
  impl_->system.saveLedSettings();
}

void Board::triggerCalibration() {
  impl_->system.triggerCalibration();
}

uint16_t Board::sensorReadDelayMs() const {
  return impl_->system.sensorReadDelayMs();
}
