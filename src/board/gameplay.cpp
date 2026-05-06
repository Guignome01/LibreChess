#include "gameplay.h"

#include "board.h"
#include "core/colors.h"
#include "gui/menu.h"
#include "services.h"

#include <Arduino.h>

using namespace LibreChess;

namespace {

bool sameSquare(int row, int col, int otherRow, int otherCol) {
  return row == otherRow && col == otherCol;
}

bool moveTargetsSquare(const Move& move, int row, int col) {
  return squareToRow(move.to) == row && squareToCol(move.to) == col;
}

bool hasLegalMoveTo(const MoveList& moves, int row, int col) {
  for (int moveIndex = 0; moveIndex < moves.count; ++moveIndex)
    if (moveTargetsSquare(moves.moves[moveIndex], row, col))
      return true;
  return false;
}

bool isQuietLegalPlacement(const Game& game, const MoveList& moves, int fromRow, int fromCol, int placedRow, int placedCol) {
  if (!hasLegalMoveTo(moves, placedRow, placedCol))
    return false;

  auto enPassant = game.checkEnPassant(fromRow, fromCol, placedRow, placedCol);
  return Game::isEmptySquare(game.getSquare(placedRow, placedCol)) && !enPassant.isCapture;
}

struct CaptureSelection {
  int targetRow;
  int targetCol;
  int capturedRow;
  int capturedCol;
  bool isEnPassant;
};

bool captureSelectionForLiftedSquare(const Game& game, const MoveList& moves, int fromRow, int fromCol, int liftedRow, int liftedCol, CaptureSelection& selection) {
  for (int moveIndex = 0; moveIndex < moves.count; ++moveIndex) {
    int targetRow = squareToRow(moves.moves[moveIndex].to);
    int targetCol = squareToCol(moves.moves[moveIndex].to);
    auto enPassant = game.checkEnPassant(fromRow, fromCol, targetRow, targetCol);

    bool capturesBoardPiece = !Game::isEmptySquare(game.getSquare(targetRow, targetCol));
    if (!capturesBoardPiece && !enPassant.isCapture)
      continue;

    int capturedRow = enPassant.isCapture ? squareToRow(enPassant.capturedPawnSq) : targetRow;
    int capturedCol = targetCol;
    if (!sameSquare(liftedRow, liftedCol, capturedRow, capturedCol))
      continue;

    selection = {targetRow, targetCol, capturedRow, capturedCol, enPassant.isCapture};
    return true;
  }

  return false;
}

/// Two-square modal yes/no prompt (green=yes at d4, red=no at e4) rendered
/// through a transient MenuView pushed onto the board's modal stack via
/// MenuView::waitForSelection. Duplicated lightly with BoardMenu::confirmAction
/// so gameplay does not need to take a BoardMenu reference; the handful of
/// lines is preferred over coupling.
bool boardConfirmYesNo(BoardServices& services, bool flipped) {
  static constexpr MenuItem confirmItems[] = {
      {4, 3, LedColors::Green, 1},  // Yes -- d4
      {4, 4, LedColors::Red, 0},    // No  -- e4
  };
  MenuView prompt(services.system(), services.layering());
  prompt.setItems(confirmItems, 2);
  prompt.setFlipped(flipped);
  return prompt.waitForSelection() == 1;
}

}  // namespace

BoardGameplay::BoardGameplay(Board& board) : services_(board.services()), snapshot_() {}

void BoardGameplay::readSensors() {
  services_.readSensors();
  snapshot_.update([&](int row, int col) { return services_.occupied(row, col); });
}

void BoardGameplay::syncOccupancyBaseline() {
  services_.readSensors();
  snapshot_.sync([&](int row, int col) { return services_.occupied(row, col); });
}

void BoardGameplay::waitForSetup(const Game& game, Log& logger) {
  services_.assistance().waitForSetup(game, logger);
  syncOccupancyBaseline();
}

BoardGameplayResult BoardGameplay::tryPlayerMove(const Game& game, Color playerColor, Log& logger, BoardGameplayMove& selection) {
  for (uint8_t changeIndex = 0; changeIndex < snapshot_.changedCount(); ++changeIndex) {
    auto changed = snapshot_.changedSquare(changeIndex);
    if (!changed.valid())
      continue;

    int row = changed.row;
    int col = changed.col;
    if (!snapshot_.wasLifted(row, col))
      continue;

    Piece piece = game.getSquare(row, col);
    if (Game::isEmptySquare(piece))
      continue;

    if (Game::pieceColor(piece) != playerColor) {
      logger.infof("Wrong turn! It's %s's turn to move.", Game::colorName(playerColor));
      services_.feedback().showIllegalMoveFeedback(row, col);
      continue;
    }

    logger.infof("Piece pickup from %s", Game::squareName(row, col).c_str());

    MoveList moves;
    game.getPossibleMoves(row, col, moves);
    services_.assistance().showLegalMoveHighlights(row, col, moves, game);

    int targetRow = -1;
    int targetCol = -1;
    bool piecePlaced = false;
    bool isKing = (Game::pieceType(piece) == PieceType::KING);
    unsigned long liftTimestamp = millis();
    bool resignTransitioned = false;
    unsigned long resignFlagTimestamp = 0;
    bool shouldPollSensors = false;

    while (!piecePlaced) {
      if (shouldPollSensors)
        readSensors();
      shouldPollSensors = true;

      if (isKing && !resignTransitioned && (millis() - liftTimestamp >= RESIGN_HOLD_MS)) {
        resignTransitioned = true;
        resignFlagTimestamp = millis();
        logger.info("King held off square for 3s - resign gesture initiated");
        services_.feedback().showResignProgress(row, col, 0);
      }

      for (uint8_t placedIndex = 0; placedIndex < snapshot_.changedCount(); ++placedIndex) {
        auto placedChange = snapshot_.changedSquare(placedIndex);
        if (!placedChange.valid())
          continue;

        int changedRow = placedChange.row;
        int changedCol = placedChange.col;
        if (sameSquare(changedRow, changedCol, row, col) && snapshot_.wasPlaced(row, col)) {
          targetRow = row;
          targetCol = col;
          piecePlaced = true;
          break;
        }

        if (sameSquare(changedRow, changedCol, row, col))
          continue;

        if (snapshot_.wasPlaced(changedRow, changedCol) &&
            isQuietLegalPlacement(game, moves, row, col, changedRow, changedCol)) {
          targetRow = changedRow;
          targetCol = changedCol;
          piecePlaced = true;
          break;
        }

        if (!snapshot_.wasLifted(changedRow, changedCol))
          continue;

        CaptureSelection captureSelection;
        if (!captureSelectionForLiftedSquare(game, moves, row, col, changedRow, changedCol, captureSelection))
          continue;

        logger.infof("Capture initiated at %s", Game::squareName(captureSelection.targetRow, captureSelection.targetCol).c_str());
        targetRow = captureSelection.targetRow;
        targetCol = captureSelection.targetCol;
        piecePlaced = true;
        if (captureSelection.isEnPassant)
          services_.feedback().clearSquare(captureSelection.capturedRow, captureSelection.capturedCol);
        services_.assistance().showCapturePlacementPrompt(captureSelection.targetRow, captureSelection.targetCol);

        while (!snapshot_.occupied(captureSelection.targetRow, captureSelection.targetCol)) {
          readSensors();
          if (snapshot_.wasPlaced(row, col)) {
            logger.info("Capture cancelled");
            targetRow = row;
            targetCol = col;
            break;
          }
          delay(services_.sensorReadDelayMs());
        }

        break;
      }

      delay(services_.sensorReadDelayMs());
    }

    if (!(resignTransitioned && targetRow == row && targetCol == col))
      services_.feedback().clearBoard();

    if (targetRow == row && targetCol == col) {
      if (resignTransitioned) {
        if (millis() - resignFlagTimestamp > RESIGN_LIFT_WINDOW_MS) {
          services_.feedback().clearBoard();
        } else {
          services_.feedback().showResignProgress(row, col, 1, true);
          if (continueResignGesture(row, col, Game::pieceColor(piece), logger)) {
            selection.resignColor = Game::pieceColor(piece);
            return BoardGameplayResult::RESIGN_REQUESTED;
          }
        }
      } else {
        logger.info("Pickup cancelled");
      }
      return BoardGameplayResult::NONE;
    }

    if (!hasLegalMoveTo(moves, targetRow, targetCol)) {
      logger.info("Illegal move, reverting");
      return BoardGameplayResult::NONE;
    }

    selection.fromRow = row;
    selection.fromCol = col;
    selection.toRow = targetRow;
    selection.toCol = targetCol;
    return BoardGameplayResult::MOVE;
  }

  return BoardGameplayResult::NONE;
}

void BoardGameplay::completeAppliedMove(const Game& game, const MoveResult& result, const CastlingInfo& castling,
                                        int fromRow, int fromCol, int toRow, int toCol, bool isRemoteMove, Log& logger) {
  if (isRemoteMove && !result.isCastling())
    services_.assistance().guideRemoteMoveCompletion(
        fromRow, fromCol, toRow, toCol, result.isCapture(), result.isEnPassant(),
        result.epCapturedSq == SQ_NONE ? -1 : squareToRow(result.epCapturedSq), logger);

  if (result.isCastling())
    services_.assistance().guideCastling(fromRow, fromCol, toRow, toCol, castling, isRemoteMove, logger);

  services_.feedback().showMoveResultFeedback(result, toRow, toCol, game);
}

bool BoardGameplay::confirmResign(Color resignColor, bool flipped, Log& logger) {
  logger.infof("Resign confirmation for %s...", Game::colorName(resignColor));

  if (!boardConfirmYesNo(services_, flipped)) {
    logger.info("Resign cancelled");
    return false;
  }

  return true;
}

void BoardGameplay::showResignWinner(Color resignColor) {
  services_.feedback().showWinner(~resignColor);
}

std::atomic<bool>* BoardGameplay::startThinkingStatus() {
  return services_.feedback().startThinking();
}

std::atomic<bool>* BoardGameplay::startWaitingStatus() {
  return services_.feedback().startWaiting();
}

void BoardGameplay::stopStatusAnimation(std::atomic<bool>*& stopFlag) {
  services_.feedback().stopAnimation(stopFlag);
}

void BoardGameplay::showRemoteGameEnd(char winnerColor) {
  services_.feedback().showRemoteGameEnd(winnerColor);
}

void BoardGameplay::showErrorFeedback() {
  services_.feedback().showError();
}

bool BoardGameplay::continueResignGesture(int row, int col, Color color, Log& logger) {
  for (int lift = 1; lift <= 2; lift++) {
    unsigned long waitStart = millis();
    bool lifted = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      readSensors();
      if (!snapshot_.occupied(row, col)) {
        lifted = true;
        break;
      }
      delay(services_.sensorReadDelayMs());
    }

    if (!lifted) {
      services_.feedback().clearResignFeedback(row, col);
      return false;
    }

    waitStart = millis();
    bool returned = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      readSensors();
      if (snapshot_.occupied(row, col)) {
        returned = true;
        break;
      }
      delay(services_.sensorReadDelayMs());
    }

    if (!returned) {
      services_.feedback().clearResignFeedback(row, col);
      return false;
    }

    services_.feedback().showResignProgress(row, col, lift + 1);
  }

  logger.infof("Resign gesture completed by %s", Game::colorName(color));
  delay(500);
  services_.feedback().clearResignFeedback(row, col);
  return true;
}
