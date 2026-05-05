#include "board.h"

#include "gui.h"
#include "system.h"

#include <Arduino.h>

struct Board::Impl {
  BoardSystem system;
  BoardGui gui;

  Impl() : system(), gui(system) {}
};

Board::Board() : impl_(std::make_unique<Impl>()) {}

Board::~Board() = default;

void Board::begin() {
  if (!impl_->system.begin()) {
    Serial.println("Board scheduler initialization failed");
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
  impl_->gui.waitForBoardSetup(game, logger);
}

void Board::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game) {
  impl_->gui.showLegalMoveHighlights(fromRow, fromCol, moves, game);
}

void Board::showCapturePlacementPrompt(int row, int col) {
  impl_->gui.showCapturePlacementPrompt(row, col);
}

void Board::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                          const LibreChess::CastlingInfo& castling,
                          bool waitForKingCompletion, LibreChess::Log& logger) {
  impl_->gui.guideCastling(kingFromRow, kingFromCol, kingToRow, kingToCol,
                           castling, waitForKingCompletion, logger);
}

void Board::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                      bool isCapture, bool isEnPassant,
                                      int enPassantCapturedPawnRow,
                                      LibreChess::Log& logger) {
  impl_->gui.guideRemoteMoveCompletion(fromRow, fromCol, toRow, toCol,
                                       isCapture, isEnPassant,
                                       enPassantCapturedPawnRow, logger);
}

void Board::clearBoardFeedback(bool show) {
  impl_->gui.clearBoardFeedback(show);
}

void Board::clearFeedbackSquare(int row, int col) {
  impl_->gui.clearFeedbackSquare(row, col);
}

void Board::showMoveResultFeedback(const LibreChess::MoveResult& result, int toRow,
                                   int toCol, const LibreChess::Game& game) {
  impl_->gui.showMoveResultFeedback(result, toRow, toCol, game);
}

void Board::showIllegalMoveFeedback(int row, int col) {
  impl_->gui.showIllegalMoveFeedback(row, col);
}

void Board::showResignProgress(int row, int col, int level, bool clearFirst) {
  impl_->gui.showResignProgress(row, col, level, clearFirst);
}

void Board::clearResignFeedback(int row, int col) {
  impl_->gui.clearResignFeedback(row, col);
}

void Board::showWinner(LibreChess::Color winnerColor) {
  impl_->gui.showWinner(winnerColor);
}

void Board::showRemoteGameEnd(char winnerColor) {
  impl_->gui.showRemoteGameEnd(winnerColor);
}

void Board::showErrorFeedback() {
  impl_->gui.showErrorFeedback();
}

std::atomic<bool>* Board::startThinkingStatus() {
  return impl_->gui.startThinkingStatus();
}

std::atomic<bool>* Board::startWaitingStatus() {
  return impl_->gui.startWaitingStatus();
}

void Board::stopStatusAnimation(std::atomic<bool>*& stopFlag) {
  impl_->gui.stopStatusAnimation(stopFlag);
}

void Board::clearAllLEDs(bool show) {
  impl_->gui.clearAllLEDs(show);
}

void Board::showConnectingAnimation() {
  impl_->gui.showConnectingAnimation();
}

void Board::startGameSelectionMenu() {
  impl_->gui.startGameSelectionMenu();
}

void Board::clearGameSelectionMenu() {
  impl_->gui.clearGameSelectionMenu();
}

Board::GameSelection Board::pollGameSelectionMenu() {
  return impl_->gui.pollGameSelectionMenu();
}

bool Board::confirmAction(bool flipped) {
  return impl_->gui.confirmAction(flipped);
}

bool Board::confirmResume(GameSelectionMode mode, bool flipped) {
  return impl_->gui.confirmResume(mode, flipped);
}

void Board::beginDiagnostics() {
  impl_->gui.beginDiagnostics();
}

void Board::updateDiagnostics() {
  impl_->gui.updateDiagnostics();
}

bool Board::diagnosticsComplete() const {
  return impl_->gui.diagnosticsComplete();
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
