#include "game_mode.h"
#include "../board/board.h"
#include "../board/menu.h"
#include "game.h"
#include "wifi_manager_esp32.h"

namespace {

using LibreChess::board::BoardSquare;

BoardSquare makeBoardSquare(int row, int col) {
  return BoardSquare{static_cast<int8_t>(row), static_cast<int8_t>(col)};
}

bool sameSquare(BoardSquare square, int row, int col) {
  return square == makeBoardSquare(row, col);
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

bool isQuietLegalPlacement(const Game& game, const MoveList& moves, int fromRow, int fromCol, BoardSquare placedSquare) {
  if (!hasLegalMoveTo(moves, placedSquare.row, placedSquare.col))
    return false;

  auto enPassant = game.checkEnPassant(fromRow, fromCol, placedSquare.row, placedSquare.col);
  return Game::isEmptySquare(game.getSquare(placedSquare.row, placedSquare.col)) && !enPassant.isCapture;
}

struct CaptureSelection {
  int targetRow;
  int targetCol;
  int capturedRow;
  int capturedCol;
  bool isEnPassant;
};

bool captureSelectionForLiftedSquare(const Game& game, const MoveList& moves, int fromRow, int fromCol, BoardSquare liftedSquare, CaptureSelection& selection) {
  for (int moveIndex = 0; moveIndex < moves.count; ++moveIndex) {
    int targetRow = squareToRow(moves.moves[moveIndex].to);
    int targetCol = squareToCol(moves.moves[moveIndex].to);
    auto enPassant = game.checkEnPassant(fromRow, fromCol, targetRow, targetCol);

    bool capturesBoardPiece = !Game::isEmptySquare(game.getSquare(targetRow, targetCol));
    if (!capturesBoardPiece && !enPassant.isCapture)
      continue;

    int capturedRow = enPassant.isCapture ? squareToRow(enPassant.capturedPawnSq) : targetRow;
    int capturedCol = targetCol;
    if (!sameSquare(liftedSquare, capturedRow, capturedCol))
      continue;

    selection = {targetRow, targetCol, capturedRow, capturedCol, enPassant.isCapture};
    return true;
  }

  return false;
}

}  // namespace

GameMode::GameMode(Board* board, WiFiManagerESP32* wm, Game* cg, ILogger* logger)
  : board_(board), wifiManager_(wm), chess_(cg), logger_(logger) {}

bool GameMode::isGameOver() const { return chess_->isGameOver(); }

bool GameMode::tryResumeGame() {
  if (chess_->hasActiveGame()) {
    logger_.info("Resuming live game...");
    return chess_->resumeGame();
  }
  return false;
}

void GameMode::waitForBoardSetup() {
  board_->assistance().waitForSetup(*chess_, logger_);
}

MoveResult GameMode::applyMove(int fromRow, int fromCol, int toRow, int toCol, char promotion, bool isRemoteMove) {
  // Compute castling info before the move (piece still at from square)
  auto castleInfo = chess_->checkCastling(fromRow, fromCol, toRow, toCol);

  MoveResult result = chess_->makeMove(fromRow, fromCol, toRow, toCol, promotion);
  if (!result.valid()) return result;

  // --- Hardware feedback ---

  // Remote move: guide the player to physically execute the move on the board
  if (isRemoteMove && !result.isCastling())
    waitForRemoteMoveCompletion(fromRow, fromCol, toRow, toCol,
                                result.isCapture(), result.isEnPassant(),
                                result.epCapturedSq == SQ_NONE ? -1 : squareToRow(result.epCapturedSq));

  // Castling: guide the player to move the rook
  if (result.isCastling())
    board_->assistance().guideCastling(fromRow, fromCol, toRow, toCol, castleInfo, isRemoteMove, logger_);

  board_->feedback().showMoveResultFeedback(result, toRow, toCol, *chess_);

  return result;
}

MoveResult GameMode::applyMove(const std::string& move) {
  int fromRow, fromCol, toRow, toCol;
  char promotion = ' ';
  if (!Game::parseCoordinate(move, fromRow, fromCol, toRow, toCol, promotion)) {
    logger_.errorf("Failed to parse move: %s", move.c_str());
    return invalidMoveResult();
  }
  return applyMove(fromRow, fromCol, toRow, toCol, promotion, true);
}

bool GameMode::tryPlayerMove(Color playerColor, int& fromRow, int& fromCol, int& toRow, int& toCol) {
  for (uint8_t changeIndex = 0; changeIndex < board_->changedCount(); ++changeIndex) {
    BoardSquare originSquare = board_->changedSquare(changeIndex);
    if (!originSquare.valid())
      continue;

    int row = originSquare.row;
    int col = originSquare.col;

    // Continue if nothing was picked up from this square
    if (!board_->wasLifted(row, col))
      continue;

    Piece piece = chess_->getSquare(row, col);

    // Skip empty squares
    if (Game::isEmptySquare(piece))
      continue;

    // Check if it's the correct player's piece
    if (Game::pieceColor(piece) != playerColor) {
      logger_.infof("Wrong turn! It's %s's turn to move.", Game::colorName(playerColor));
      board_->feedback().showIllegalMoveFeedback(row, col);
      continue;
    }

    logger_.infof("Piece pickup from %s", Game::squareName(row, col).c_str());

    // Generate possible moves
    MoveList moves;
    chess_->getPossibleMoves(row, col, moves);

    board_->assistance().showLegalMoveHighlights(row, col, moves, *chess_);

    // Wait for piece placement - handle both normal moves and captures
    int targetRow = -1, targetCol = -1;
    bool piecePlaced = false;
    bool isKing = (Game::pieceType(piece) == PieceType::KING);
    unsigned long liftTimestamp = millis();
    bool resignTransitioned = false; // True once 3s hold switches LEDs to dim orange
    unsigned long resignFlagTimestamp = 0; // When the resign flag was raised
    bool shouldPollSensors = false;

    while (!piecePlaced) {
      if (shouldPollSensors)
        board_->readSensors();
      shouldPollSensors = true;

      // --- King resign hold detection ---
      // If a king is off its square for RESIGN_HOLD_MS, transition LEDs from
      // valid-move highlights to a dim orange on the king's origin square.
      if (isKing && !resignTransitioned && (millis() - liftTimestamp >= RESIGN_HOLD_MS)) {
        resignTransitioned = true;
        resignFlagTimestamp = millis();
        logger_.info("King held off square for 3s \xe2\x80\x94 resign gesture initiated");
        board_->feedback().showResignProgress(row, col, 0);
      }

      for (uint8_t placedIndex = 0; placedIndex < board_->changedCount(); ++placedIndex) {
        BoardSquare changedSquare = board_->changedSquare(placedIndex);
        if (!changedSquare.valid())
          continue;

        if (sameSquare(changedSquare, row, col) && board_->wasPlaced(row, col)) {
          targetRow = row;
          targetCol = col;
          piecePlaced = true;
          break;
        }

        if (sameSquare(changedSquare, row, col))
          continue;

        if (board_->wasPlaced(changedSquare.row, changedSquare.col) &&
            isQuietLegalPlacement(*chess_, moves, row, col, changedSquare)) {
          targetRow = changedSquare.row;
          targetCol = changedSquare.col;
          piecePlaced = true;
          break;
        }

        if (!board_->wasLifted(changedSquare.row, changedSquare.col))
          continue;

        CaptureSelection captureSelection;
        if (!captureSelectionForLiftedSquare(*chess_, moves, row, col, changedSquare, captureSelection))
          continue;

        logger_.infof("Capture initiated at %s", Game::squareName(captureSelection.targetRow, captureSelection.targetCol).c_str());
        targetRow = captureSelection.targetRow;
        targetCol = captureSelection.targetCol;
        piecePlaced = true;
        if (captureSelection.isEnPassant) {
          board_->feedback().clearSquare(captureSelection.capturedRow, captureSelection.capturedCol);
        }
        board_->assistance().showCapturePlacementPrompt(captureSelection.targetRow, captureSelection.targetCol);

        while (!board_->occupied(captureSelection.targetRow, captureSelection.targetCol)) {
          board_->readSensors();
          if (board_->wasPlaced(row, col)) {
            logger_.info("Capture cancelled");
            targetRow = row;
            targetCol = col;
            break;
          }
          delay(SENSOR_READ_DELAY_MS);
        }

        break;
      }

      delay(SENSOR_READ_DELAY_MS);
    }

    // Clear highlights (single cleanup for all exit paths)
    // When resign was triggered and king returned, defer clear to showResignProgress to avoid flash.
    if (!(resignTransitioned && targetRow == row && targetCol == col)) {
      board_->feedback().clearBoard();
    }

    if (targetRow == row && targetCol == col) {
      // King put back — check if the 3s resign hold was completed
      if (resignTransitioned) {
        // If king was returned too late, silently cancel
        if (millis() - resignFlagTimestamp > RESIGN_LIFT_WINDOW_MS) {
          board_->feedback().clearBoard();
        } else {
          // First landing: brighten to 50%
          board_->feedback().showResignProgress(row, col, 1, true);
          // Run remaining 2 quick lifts inline (blocking)
          continueResignGesture(row, col, Game::pieceColor(piece));
        }
      } else {
        logger_.info("Pickup cancelled");
      }
      return false;
    }

    bool legalMove = hasLegalMoveTo(moves, targetRow, targetCol);

    if (!legalMove) {
      logger_.info("Illegal move, reverting");
      return false;
    }

    fromRow = row;
    fromCol = col;
    toRow = targetRow;
    toCol = targetCol;

    return true;
  }

  return false;
}

void GameMode::setBoardStateFromFEN(const std::string& fen) {
  chess_->loadFEN(fen);
  logger_.infof("Board state set from FEN: %s", fen.c_str());
  logger_.info(chess_->boardToText().c_str());
}

// ---------------------------
// Resign Feature
// ---------------------------

bool GameMode::processResign() {
  if (!resignPending_) return false;
  resignPending_ = false;
  handleResign(chess_->sideToMove());
  board_->syncOccupancyBaseline();
  return true;
}

bool GameMode::continueResignGesture(int row, int col, Color color) {
  // Called inline from tryPlayerMove after king returned (50% showing).
  // Need 2 more lift-and-return cycles. Each return brightens: 75% then 100%.

  for (int lift = 1; lift <= 2; lift++) {
    unsigned long waitStart = millis();
    bool lifted = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      board_->readSensors();
      if (!board_->occupied(row, col)) {
        lifted = true;
        break;
      }
      delay(SENSOR_READ_DELAY_MS);
    }

    if (!lifted) {
      board_->feedback().clearResignFeedback(row, col);
      return false;
    }

    waitStart = millis();
    bool returned = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      board_->readSensors();
      if (board_->occupied(row, col)) {
        returned = true;
        break;
      }
      delay(SENSOR_READ_DELAY_MS);
    }

    if (!returned) {
      board_->feedback().clearResignFeedback(row, col);
      return false;
    }

    board_->feedback().showResignProgress(row, col, lift + 1);
  }

  logger_.infof("Resign gesture completed by %s", Game::colorName(color));
  delay(500);
  board_->feedback().clearResignFeedback(row, col);
  return handleResign(color);
}

bool GameMode::handleResign(Color resignColor) {
  onBeforeResignConfirm();

  bool flipped = isFlipped();
  logger_.infof("Resign confirmation for %s...", Game::colorName(resignColor));

  if (!boardConfirm(board_, flipped)) {
    logger_.info("Resign cancelled");
    onResignCancelled();
    return false;
  }

  onResignConfirmed(resignColor);

  Color winnerColor = ~resignColor;
  board_->feedback().showWinner(winnerColor);
  chess_->endGame(GameResult::RESIGNATION, winnerColor == Color::WHITE ? 'w' : 'b');
  return true;
}
