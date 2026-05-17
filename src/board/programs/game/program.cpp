#include "board/programs/game/program.h"

#include "board/runtime/colors.h"
#include "board/services/menu/menu.h"
#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"
#include "board/menus/confirm.h"

#include <Arduino.h>

namespace {

bool sameSquare(int row, int col, int otherRow, int otherCol) {
  return row == otherRow && col == otherCol;
}

bool placedOnSquare(const BoardInput::Event& event, int row, int col) {
  return event.kind == BoardInput::EventKind::PLACED && sameSquare(event.row, event.col, row, col);
}

bool placedOnQuietTarget(const BoardInput::Event& event, const BoardMoveTargetList& targets) {
  return event.kind == BoardInput::EventKind::PLACED && targets.hasQuietTarget(event.row, event.col);
}

bool liftedCaptureTarget(const BoardInput::Event& event, const BoardMoveTargetList& targets,
                         BoardMoveTarget& capture) {
  return event.kind == BoardInput::EventKind::LIFTED &&
         targets.captureForLiftedSquare(event.row, event.col, capture);
}

void waitForCapturePlacement(BoardRuntime& runtime, uint16_t cadence, const BoardMoveTarget& capture,
                             int sourceRow, int sourceCol, int& targetRow, int& targetCol) {
  while (!runtime.inputOccupied(capture.row, capture.col)) {
    if (runtime.inputOccupied(sourceRow, sourceCol)) {
      Serial.println("Capture cancelled");
      targetRow = sourceRow;
      targetCol = sourceCol;
      return;
    }
    delay(cadence);
  }
}

/// Reports input-queue overflow to serial and shows error feedback. Returns
/// true when an overflow was handled, so callers can abort the in-flight
/// gesture and resync from current occupancy.
bool handleInputOverflow(const BoardInputEventBatch& batch, BoardFeedback& feedback) {
  if (!batch.overflowed) return false;
  Serial.printf(
      "Physical board input queue overflowed; dropped %lu event(s), max depth %u; "
      "ignoring partial gesture and resyncing.\n",
      static_cast<unsigned long>(batch.droppedEventCount), batch.maxQueueDepth);
  feedback.showError();
  return true;
}

}  // namespace

BoardGame::BoardGame(BoardRuntime& runtime, BoardAnimations& animations,
                             BoardMenuRunner& menuRunner)
    : runtime_(runtime),
      animations_(animations),
      menuRunner_(menuRunner),
      feedback_(runtime, animations),
      assistance_(runtime, animations) {}

void BoardGame::reset() { cancelAssistance(); }

void BoardGame::setAssistanceProvider(BoardAssistanceProvider* provider) {
  if (assistanceProvider_ == provider) return;
  cancelAssistance();
  assistanceProvider_ = provider;
  assistance_.setLevel(provider ? provider->level() : BoardAssistanceLevel::NONE);
}

void BoardGame::serviceAssistance() {
  if (!assistanceProvider_) return;
  if (assistance_.level() != assistanceProvider_->level()) {
    assistance_.setLevel(assistanceProvider_->level());
  }

  BoardBestMoveHint hint;
  if (assistanceProvider_->service(hint)) {
    assistance_.showBestMoveHint(hint);
  }
}

void BoardGame::cancelAssistance() {
  if (assistanceProvider_) assistanceProvider_->cancel();
  assistance_.clear();
}

void BoardGame::waitForSetup(const BoardGameProvider& gameRules) {
  BoardSetupSnapshot setup;
  gameRules.setupSnapshot(setup);
  assistance_.waitForSetup(setup);
}

BoardGameResult BoardGame::tryPlayerMove(const BoardGameProvider& gameRules,
                                                 BoardPieceColor playerColor,
                                                 BoardGameMove& selection) {
  const uint16_t cadence = runtime_.cadenceMs();
  BoardInputEventBatch batch = runtime_.drainInputEvents();
  if (handleInputOverflow(batch, feedback_)) return BoardGameResult::NONE;

  // Look for a recently-lifted piece of the player's colour.
  for (uint8_t i = 0; i < batch.count; ++i) {
    BoardInput::Event event = batch.events[i];
    if (event.kind != BoardInput::EventKind::LIFTED) continue;
    int row = event.row;
    int col = event.col;

    BoardPiece piece = gameRules.pieceAt(row, col);
    if (!piece.occupied()) continue;

    if (piece.color != playerColor) {
      Serial.printf("Wrong turn! It's %s's turn to move.\n", boardColorName(playerColor));
      feedback_.showIllegalMoveFeedback(row, col);
      return BoardGameResult::NONE;
    }

    Serial.printf("Piece pickup from %c%c\n", boardFileChar(col), boardRankChar(row));

    BoardMoveTargetList targets;
    gameRules.legalTargets(row, col, targets);
    assistance_.showLegalMoveHighlights(row, col, targets);

    int targetRow = -1;
    int targetCol = -1;
    bool piecePlaced = false;
    bool isKing = (piece.type == BoardPieceType::KING);
    unsigned long liftTimestamp = millis();
    bool resignTransitioned = false;
    unsigned long resignFlagTimestamp = 0;
    uint8_t eventIndex = i + 1;

    while (!piecePlaced) {
      if (isKing && !resignTransitioned && (millis() - liftTimestamp >= RESIGN_HOLD_MS)) {
        resignTransitioned = true;
        resignFlagTimestamp = millis();
        Serial.println("King held off square for 3s - resign gesture initiated");
        feedback_.showResignProgress(row, col, 0);
      }

      if (eventIndex >= batch.count) {
        batch = runtime_.drainInputEvents();
        if (handleInputOverflow(batch, feedback_)) {
          cancelAssistance();
          return BoardGameResult::NONE;
        }
        eventIndex = 0;
      }

      for (; eventIndex < batch.count; ++eventIndex) {
        BoardInput::Event event = batch.events[eventIndex];

        // Source square placed back: pickup cancelled.
        if (placedOnSquare(event, row, col)) {
          targetRow = row;
          targetCol = col;
          piecePlaced = true;
          break;
        }

        // Quiet placement on a different square.
        if (placedOnQuietTarget(event, targets)) {
          targetRow = event.row;
          targetCol = event.col;
          piecePlaced = true;
          break;
        }

        // Capture: opponent piece lifted.
        BoardMoveTarget capture;
        if (liftedCaptureTarget(event, targets, capture)) {
          Serial.printf("Capture initiated at %c%c\n", boardFileChar(capture.col),
                        boardRankChar(capture.row));
          targetRow = capture.row;
          targetCol = capture.col;
          piecePlaced = true;
          if (capture.enPassant)
            feedback_.clearSquare(capture.capturedRow, capture.capturedCol);
          assistance_.showCapturePlacementPrompt(capture.row, capture.col);

          // Wait for the player to drop the moving piece on the capture square
          // (or restore it to the source = cancel).
          waitForCapturePlacement(runtime_, cadence, capture, row, col, targetRow, targetCol);
          break;
        }
      }
      if (!piecePlaced) delay(cadence);
    }

    runtime_.clearInputEvents();

    if (!(resignTransitioned && targetRow == row && targetCol == col)) cancelAssistance();

    if (targetRow == row && targetCol == col) {
      return handleSourceRestore(row, col, piece.color, resignTransitioned, resignFlagTimestamp,
                                 selection);
    }

    if (!targets.hasTarget(targetRow, targetCol)) {
      Serial.println("Illegal move, reverting");
      return BoardGameResult::NONE;
    }

    selection.fromRow = row;
    selection.fromCol = col;
    selection.toRow = targetRow;
    selection.toCol = targetCol;
    return BoardGameResult::MOVE;
  }

  return BoardGameResult::NONE;
}

void BoardGame::completeAppliedMove(const BoardMoveCompletion& completion,
                                        const BoardMoveFeedbackData& feedback, int fromRow,
                                        int fromCol, int toRow, int toCol) {
  cancelAssistance();

  if (completion.isRemoteMove && !completion.castling.isCastling)
    assistance_.guideRemoteMoveCompletion(fromRow, fromCol, toRow, toCol, completion);

  if (completion.castling.isCastling)
    assistance_.guideCastling(fromRow, fromCol, toRow, toCol, completion.castling,
                              completion.isRemoteMove);

  feedback_.showMoveResultFeedback(feedback);
}

bool BoardGame::confirmResign(BoardPieceColor resignColor, bool flipped) {
  Serial.printf("Resign confirmation for %s...\n", boardColorName(resignColor));

  ConfirmMenu menu;
  menuRunner_.run(menu, flipped);
  const bool confirmed = menu.accepted();
  if (!confirmed) {
    Serial.println("Resign cancelled");
  }
  return confirmed;
}

void BoardGame::showResignWinner(BoardPieceColor resignColor) {
  feedback_.showWinner(oppositeBoardColor(resignColor));
}

BoardAnimationToken BoardGame::startThinkingStatus() {
  return BoardAnimationToken(&runtime_, &animations_, feedback_.startThinking());
}

BoardAnimationToken BoardGame::startWaitingStatus() {
  return BoardAnimationToken(&runtime_, &animations_, feedback_.startWaiting());
}

void BoardGame::showRemoteGameEnd(char winnerColor) { feedback_.showRemoteGameEnd(winnerColor); }

void BoardGame::showErrorFeedback() { feedback_.showError(); }

BoardGameResult BoardGame::handleSourceRestore(int row, int col, BoardPieceColor color,
                                                       bool resignTransitioned,
                                                       unsigned long resignFlagTimestamp,
                                                       BoardGameMove& selection) {
  if (!resignTransitioned) {
    Serial.println("Pickup cancelled");
    return BoardGameResult::NONE;
  }

  if (millis() - resignFlagTimestamp > RESIGN_LIFT_WINDOW_MS) {
    feedback_.clearBoard();
    return BoardGameResult::NONE;
  }

  feedback_.showResignProgress(row, col, 1, true);
  if (continueResignGesture(row, col, color)) {
    selection.resignColor = color;
    return BoardGameResult::RESIGN_REQUESTED;
  }
  return BoardGameResult::NONE;
}

bool BoardGame::continueResignGesture(int row, int col, BoardPieceColor color) {
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

  Serial.printf("Resign gesture completed by %s\n", boardColorName(color));
  delay(500);
  feedback_.clearResignFeedback(row, col);
  runtime_.clearInputEvents();
  return true;
}
