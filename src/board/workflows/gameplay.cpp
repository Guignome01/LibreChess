#include "board/workflows/gameplay.h"

#include "board/core/colors.h"
#include "board/core/runtime.h"
#include "board/menus/prompt.h"

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
  for (int i = 0; i < moves.count; ++i)
    if (moveTargetsSquare(moves.moves[i], row, col)) return true;
  return false;
}

bool isQuietLegalPlacement(const Game& game, const MoveList& moves, int fromRow, int fromCol,
                           int placedRow, int placedCol) {
  if (!hasLegalMoveTo(moves, placedRow, placedCol)) return false;
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

bool captureSelectionForLiftedSquare(const Game& game, const MoveList& moves, int fromRow,
                                     int fromCol, int liftedRow, int liftedCol,
                                     CaptureSelection& selection) {
  for (int i = 0; i < moves.count; ++i) {
    int targetRow = squareToRow(moves.moves[i].to);
    int targetCol = squareToCol(moves.moves[i].to);
    auto enPassant = game.checkEnPassant(fromRow, fromCol, targetRow, targetCol);

    bool capturesBoardPiece = !Game::isEmptySquare(game.getSquare(targetRow, targetCol));
    if (!capturesBoardPiece && !enPassant.isCapture) continue;

    int capturedRow = enPassant.isCapture ? squareToRow(enPassant.capturedPawnSq) : targetRow;
    int capturedCol = targetCol;
    if (!sameSquare(liftedRow, liftedCol, capturedRow, capturedCol)) continue;

    selection = {targetRow, targetCol, capturedRow, capturedCol, enPassant.isCapture};
    return true;
  }
  return false;
}

bool inputOverflowed(const BoardInputEventBatch& batch, BoardFeedback& feedback, Log& logger) {
  if (!batch.overflowed) return false;
  logger.infof(
      "Physical board input queue overflowed; dropped %lu event(s), max depth %u; "
      "ignoring partial gesture and resyncing.",
      static_cast<unsigned long>(batch.droppedEventCount), batch.maxQueueDepth);
  feedback.showError();
  return true;
}

}  // namespace

BoardGameplay::BoardGameplay(BoardRuntime& runtime, BoardAnimations& animations)
    : runtime_(runtime),
      animations_(animations),
      feedback_(runtime, animations),
      assistance_(runtime, animations) {}

void BoardGameplay::waitForSetup(const Game& game, Log& logger) {
  assistance_.waitForSetup(game, logger);
}

BoardGameplayResult BoardGameplay::tryPlayerMove(const Game& game, Color playerColor, Log& logger,
                                                 BoardGameplayMove& selection) {
  const uint16_t cadence = runtime_.cadenceMs();
  BoardInputEventBatch batch = runtime_.drainInputEvents();
  if (inputOverflowed(batch, feedback_, logger)) return BoardGameplayResult::NONE;

  // Look for a recently-lifted piece of the player's colour.
  for (uint8_t i = 0; i < batch.count; ++i) {
    BoardInput::Event ev = batch.events[i];
    if (ev.kind != BoardInput::EventKind::LIFTED) continue;
    int row = ev.row;
    int col = ev.col;

    Piece piece = game.getSquare(row, col);
    if (Game::isEmptySquare(piece)) continue;

    if (Game::pieceColor(piece) != playerColor) {
      logger.infof("Wrong turn! It's %s's turn to move.", Game::colorName(playerColor));
      feedback_.showIllegalMoveFeedback(row, col);
      return BoardGameplayResult::NONE;
    }

    logger.infof("Piece pickup from %s", Game::squareName(row, col).c_str());

    MoveList moves;
    game.getPossibleMoves(row, col, moves);
    assistance_.showLegalMoveHighlights(row, col, moves, game);

    int targetRow = -1;
    int targetCol = -1;
    bool piecePlaced = false;
    bool isKing = (Game::pieceType(piece) == PieceType::KING);
    unsigned long liftTimestamp = millis();
    bool resignTransitioned = false;
    unsigned long resignFlagTimestamp = 0;
    uint8_t eventIndex = i + 1;

    while (!piecePlaced) {
      if (isKing && !resignTransitioned && (millis() - liftTimestamp >= RESIGN_HOLD_MS)) {
        resignTransitioned = true;
        resignFlagTimestamp = millis();
        logger.info("King held off square for 3s - resign gesture initiated");
        feedback_.showResignProgress(row, col, 0);
      }

      if (eventIndex >= batch.count) {
        batch = runtime_.drainInputEvents();
        if (inputOverflowed(batch, feedback_, logger)) {
          feedback_.clearBoard();
          return BoardGameplayResult::NONE;
        }
        eventIndex = 0;
      }

      for (; eventIndex < batch.count; ++eventIndex) {
        BoardInput::Event e = batch.events[eventIndex];

        // Source square placed back: pickup cancelled.
        if (e.kind == BoardInput::EventKind::PLACED && sameSquare(e.row, e.col, row, col)) {
          targetRow = row;
          targetCol = col;
          piecePlaced = true;
          break;
        }

        // Quiet placement on a different square.
        if (e.kind == BoardInput::EventKind::PLACED &&
            isQuietLegalPlacement(game, moves, row, col, e.row, e.col)) {
          targetRow = e.row;
          targetCol = e.col;
          piecePlaced = true;
          break;
        }

        // Capture: opponent piece lifted.
        if (e.kind == BoardInput::EventKind::LIFTED) {
          CaptureSelection capture;
          if (!captureSelectionForLiftedSquare(game, moves, row, col, e.row, e.col, capture))
            continue;

          logger.infof("Capture initiated at %s",
                       Game::squareName(capture.targetRow, capture.targetCol).c_str());
          targetRow = capture.targetRow;
          targetCol = capture.targetCol;
          piecePlaced = true;
          if (capture.isEnPassant)
            feedback_.clearSquare(capture.capturedRow, capture.capturedCol);
          assistance_.showCapturePlacementPrompt(capture.targetRow, capture.targetCol);

          // Wait for the player to drop the moving piece on the capture square
          // (or restore it to the source = cancel).
          while (!runtime_.inputOccupied(capture.targetRow, capture.targetCol)) {
            if (runtime_.inputOccupied(row, col)) {
              logger.info("Capture cancelled");
              targetRow = row;
              targetCol = col;
              break;
            }
            delay(cadence);
          }
          break;
        }
      }
      if (!piecePlaced) delay(cadence);
    }

    runtime_.clearInputEvents();

    if (!(resignTransitioned && targetRow == row && targetCol == col)) feedback_.clearBoard();

    if (targetRow == row && targetCol == col) {
      if (resignTransitioned) {
        if (millis() - resignFlagTimestamp > RESIGN_LIFT_WINDOW_MS) {
          feedback_.clearBoard();
        } else {
          feedback_.showResignProgress(row, col, 1, true);
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

void BoardGameplay::completeAppliedMove(const Game& game, const MoveResult& result,
                                        const CastlingInfo& castling, int fromRow, int fromCol,
                                        int toRow, int toCol, bool isRemoteMove, Log& logger) {
  if (isRemoteMove && !result.isCastling())
    assistance_.guideRemoteMoveCompletion(
        fromRow, fromCol, toRow, toCol, result.isCapture(), result.isEnPassant(),
        result.epCapturedSq == SQ_NONE ? -1 : squareToRow(result.epCapturedSq), logger);

  if (result.isCastling())
    assistance_.guideCastling(fromRow, fromCol, toRow, toCol, castling, isRemoteMove, logger);

  feedback_.showMoveResultFeedback(result, toRow, toCol, game);
}

bool BoardGameplay::confirmResign(Color resignColor, bool flipped, Log& logger) {
  logger.infof("Resign confirmation for %s...", Game::colorName(resignColor));

  const bool confirmed = MenuPrompt::confirm(runtime_, animations_, flipped);
  if (!confirmed) {
    logger.info("Resign cancelled");
  }
  return confirmed;
}

void BoardGameplay::showResignWinner(Color resignColor) {
  feedback_.showWinner(~resignColor);
}

BoardAnimationHandle BoardGameplay::startThinkingStatus() { return feedback_.startThinking(); }

BoardAnimationHandle BoardGameplay::startWaitingStatus() { return feedback_.startWaiting(); }

void BoardGameplay::stopStatusAnimation(BoardAnimationHandle& handle) {
  feedback_.stopAnimation(handle);
}

void BoardGameplay::showRemoteGameEnd(char winnerColor) { feedback_.showRemoteGameEnd(winnerColor); }

void BoardGameplay::showErrorFeedback() { feedback_.showError(); }

bool BoardGameplay::continueResignGesture(int row, int col, Color color, Log& logger) {
  const uint16_t cadence = runtime_.cadenceMs();

  for (int lift = 1; lift <= 2; lift++) {
    unsigned long waitStart = millis();
    bool lifted = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      if (!runtime_.inputOccupied(row, col)) {
        lifted = true;
        break;
      }
      delay(cadence);
    }

    if (!lifted) {
      feedback_.clearResignFeedback(row, col);
      return false;
    }

    waitStart = millis();
    bool returned = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      if (runtime_.inputOccupied(row, col)) {
        returned = true;
        break;
      }
      delay(cadence);
    }

    if (!returned) {
      feedback_.clearResignFeedback(row, col);
      return false;
    }

    feedback_.showResignProgress(row, col, lift + 1);
  }

  logger.infof("Resign gesture completed by %s", Game::colorName(color));
  delay(500);
  feedback_.clearResignFeedback(row, col);
  runtime_.clearInputEvents();
  return true;
}
