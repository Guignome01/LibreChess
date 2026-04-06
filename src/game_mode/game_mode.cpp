#include "game_mode.h"
#include "board_menu.h"
#include "game.h"
#include "system_utils.h"
#include "wifi_manager_esp32.h"

// ---------------------------
// Brightness progression for resign gesture (25%, 50%, 75%, 100%)
// Level 0 = flag raised (mid-lift), levels 1-3 = each king return to square
// ---------------------------
static constexpr float RESIGN_BRIGHTNESS_LEVELS[] = {0.25f, 0.50f, 0.75f, 1.0f};

GameMode::GameMode(BoardDriver* bd, WiFiManagerESP32* wm, Game* cg, ILogger* logger)
    : boardDriver_(bd), wifiManager_(wm), chess_(cg), logger_(logger) {}

bool GameMode::isGameOver() const { return chess_->isGameOver(); }

bool GameMode::tryResumeGame() {
  if (chess_->hasActiveGame()) {
    logger_.info("Resuming live game...");
    return chess_->resumeGame();
  }
  return false;
}

void GameMode::waitForBoardSetup() {
  logger_.info("Set up the board in the required position...");

  {
    BoardDriver::LedGuard guard(boardDriver_);
    boardDriver_->clearAllLEDs(false);
    bool allCorrect = false;
    while (!allCorrect) {
      boardDriver_->readSensors();
      allCorrect = true;

      // Single pass: check correctness and update LEDs
      chess_->forEachSquare([&](int row, int col, Piece piece) {
        bool shouldHavePiece = !Game::isEmptySquare(piece);
        bool hasPiece = boardDriver_->getSensorState(row, col);

        if (shouldHavePiece != hasPiece) allCorrect = false;

        if (shouldHavePiece && !hasPiece) {
          boardDriver_->setSquareLED(row, col, SystemUtils::colorLed(Game::pieceColor(piece)));
        } else if (!shouldHavePiece && hasPiece) {
          boardDriver_->setSquareLED(row, col, LedColors::Red);
        } else {
          boardDriver_->setSquareLED(row, col, LedColors::Off);
        }
      });
      boardDriver_->showLEDs();

      delay(SENSOR_READ_DELAY_MS);
    }
  } // LedGuard released

  logger_.info("Board setup complete! Game starting...");
  boardDriver_->fireworkAnimation();
  boardDriver_->readSensors();
  boardDriver_->updateSensorPrev();
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
    applyCastlingHardware(fromRow, fromCol, toRow, toCol, castleInfo, isRemoteMove);

  // Capture / normal-move confirmation
  if (result.isCapture())
    boardDriver_->captureAnimation(toRow, toCol);
  else
    confirmSquareCompletion(toRow, toCol);

  // Promotion
  if (result.isPromotion())
    boardDriver_->promotionAnimation(toCol);

  // --- Game-end / check hardware feedback ---
  if (result.gameResult == GameResult::CHECKMATE) {
    boardDriver_->fireworkAnimation(SystemUtils::colorLed(result.winnerColor == 'w' ? Color::WHITE : Color::BLACK));
  } else if (result.gameResult != GameResult::IN_PROGRESS) {
    boardDriver_->fireworkAnimation(LedColors::Cyan);
  } else if (result.isCheck()) {
    Color turn = chess_->currentTurn();
    boardDriver_->blinkSquare(chess_->kingRow(turn), chess_->kingCol(turn), LedColors::Yellow, 3, true, true);
  }

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
  for (int row = 0; row < 8; row++)
    for (int col = 0; col < 8; col++) {
      // Continue if nothing was picked up from this square
      if (!boardDriver_->getSensorPrev(row, col) || boardDriver_->getSensorState(row, col))
        continue;

      Piece piece = chess_->getSquare(row, col);

      // Skip empty squares
      if (Game::isEmptySquare(piece))
        continue;

      // Check if it's the correct player's piece
      if (Game::pieceColor(piece) != playerColor) {
        logger_.infof("Wrong turn! It's %s's turn to move.", Game::colorName(playerColor));
        showIllegalMoveFeedback(row, col);
        continue;
      }

      logger_.infof("Piece pickup from %s", Game::squareName(row, col).c_str());

      // Generate possible moves
      MoveList moves;
      chess_->getPossibleMoves(row, col, moves);

      // Drain any stale queued animations so highlights appear on a clean strip
      boardDriver_->waitForAnimationQueueDrain();

      // Light up current square and possible move squares
      {
        BoardDriver::LedGuard guard(boardDriver_);
        boardDriver_->setSquareLED(row, col, LedColors::Cyan);

      // Highlight possible move squares (different colors for empty vs capture)
      for (int i = 0; i < moves.count; i++) {
        int r = squareToRow(moves.moves[i].to);
        int c = squareToCol(moves.moves[i].to);

        auto ei = chess_->checkEnPassant(row, col, r, c);
        if (Game::isEmptySquare(chess_->getSquare(r, c)) && !ei.isCapture) {
          boardDriver_->setSquareLED(r, c, LedColors::White);
        } else {
          boardDriver_->setSquareLED(r, c, LedColors::Red);
          if (ei.isCapture)
            boardDriver_->setSquareLED(squareToRow(ei.capturedPawnSq), c, LedColors::Purple);
        }
      }
      boardDriver_->showLEDs();
      } // LedGuard released

      // Wait for piece placement - handle both normal moves and captures
      int targetRow = -1, targetCol = -1;
      bool piecePlaced = false;
      bool isKing = (Game::pieceType(piece) == PieceType::KING);
      unsigned long liftTimestamp = millis();
      bool resignTransitioned = false; // True once 3s hold switches LEDs to dim orange
      unsigned long resignFlagTimestamp = 0; // When the resign flag was raised

      while (!piecePlaced) {
        boardDriver_->readSensors();

        // --- King resign hold detection ---
        // If a king is off its square for RESIGN_HOLD_MS, transition LEDs from
        // valid-move highlights to a dim orange on the king's origin square.
        if (isKing && !resignTransitioned && (millis() - liftTimestamp >= RESIGN_HOLD_MS)) {
          resignTransitioned = true;
          resignFlagTimestamp = millis();
          logger_.info("King held off square for 3s \xe2\x80\x94 resign gesture initiated");
          showResignProgress(row, col, 0);
        }

        // First check if the original piece was placed back
        if (boardDriver_->getSensorState(row, col)) {
          targetRow = row;
          targetCol = col;
          piecePlaced = true;
          break;
        }

        // Then check all squares for a regular move or capture initiation
        for (int r2 = 0; r2 < 8; r2++) {
          for (int c2 = 0; c2 < 8; c2++) {
            // Skip the original square which was already checked
            if (r2 == row && c2 == col)
              continue;

            // Check if this would be a legal move
            bool isLegalMove = false;
            for (int i = 0; i < moves.count; i++)
              if (squareToRow(moves.moves[i].to) == r2 && squareToCol(moves.moves[i].to) == c2) {
                isLegalMove = true;
                break;
              }

            // If not a legal move, no need to check further
            if (!isLegalMove)
              continue;

            // For capture moves: detect when the target square is empty (captured piece removed)
            // This works whether the piece was just removed or was already removed before pickup
            auto epInfo = chess_->checkEnPassant(row, col, r2, c2);
            int enPassantCapturedPawnRow = epInfo.isCapture ? squareToRow(epInfo.capturedPawnSq) : -1;
            auto isCapturedPiecePickedUp = [&]() -> bool {
              if (epInfo.isCapture)
                return !boardDriver_->getSensorState(enPassantCapturedPawnRow, c2);
              else
                return !boardDriver_->getSensorState(r2, c2);
            };
            if ((!Game::isEmptySquare(chess_->getSquare(r2, c2)) || epInfo.isCapture) && isCapturedPiecePickedUp()) {
              logger_.infof("Capture initiated at %s", Game::squareName(r2, c2).c_str());
              // Store the target square and wait for the capturing piece to be placed there
              targetRow = r2;
              targetCol = c2;
              piecePlaced = true;
              if (epInfo.isCapture) {
                BoardDriver::LedGuard guard(boardDriver_);
                boardDriver_->setSquareLED(enPassantCapturedPawnRow, c2, LedColors::Off);
                boardDriver_->showLEDs();
              }
              // Blink the capture square to indicate waiting for piece placement
              boardDriver_->blinkSquare(r2, c2, LedColors::Red, 1, false);
              // Wait for the capturing piece to be placed (or returned to origin to cancel)
              while (!boardDriver_->getSensorState(r2, c2)) {
                boardDriver_->readSensors();
                // Allow cancellation by placing the piece back to its original position
                if (boardDriver_->getSensorState(row, col)) {
                  logger_.info("Capture cancelled");
                  targetRow = row;
                  targetCol = col;
                  break;
                }
                delay(SENSOR_READ_DELAY_MS);
              }
              break;
            }

            // For normal non-capture moves: detect when a piece is placed on an empty square
            if ((Game::isEmptySquare(chess_->getSquare(r2, c2)) && !epInfo.isCapture) && boardDriver_->getSensorState(r2, c2)) {
              targetRow = r2;
              targetCol = c2;
              piecePlaced = true;
              break;
            }
          }
        }

        delay(SENSOR_READ_DELAY_MS);
      }

      // Clear highlights (single cleanup for all exit paths)
      // When resign was triggered and king returned, defer clear to showResignProgress to avoid flash.
      if (!(resignTransitioned && targetRow == row && targetCol == col)) {
        BoardDriver::LedGuard guard(boardDriver_);
        boardDriver_->clearAllLEDs();
      }

      if (targetRow == row && targetCol == col) {
        // King put back — check if the 3s resign hold was completed
        if (resignTransitioned) {
          // If king was returned too late, silently cancel
          if (millis() - resignFlagTimestamp > RESIGN_LIFT_WINDOW_MS) {
            BoardDriver::LedGuard guard(boardDriver_);
            boardDriver_->clearAllLEDs();
          } else {
            // First landing: brighten to 50%
            showResignProgress(row, col, 1, true);
            // Run remaining 2 quick lifts inline (blocking)
            continueResignGesture(row, col, Game::pieceColor(piece));
          }
        } else {
          logger_.info("Pickup cancelled");
        }
        return false;
      }

      bool legalMove = false;
      for (int i = 0; i < moves.count; i++)
        if (squareToRow(moves.moves[i].to) == targetRow && squareToCol(moves.moves[i].to) == targetCol) {
          legalMove = true;
          break;
        }

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

// --- Hardware-only castling interactions ---
// Board already updated by Position — this only handles LED prompts and sensor waits.

void GameMode::applyCastlingHardware(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol, const CastlingInfo& ci, bool waitForKingCompletion) {
  if (!ci.isCastling) return;

  BoardDriver::LedGuard guard(boardDriver_);

  if (waitForKingCompletion) {
    logger_.infof("Castling: please move king from %s to %s",
                                Game::squareName(kingFromRow, kingFromCol).c_str(),
                                Game::squareName(kingToRow, kingToCol).c_str());

    boardDriver_->clearAllLEDs(false);
    boardDriver_->setSquareLED(kingFromRow, kingFromCol, LedColors::Cyan);
    boardDriver_->setSquareLED(kingToRow, kingToCol, LedColors::White);
    boardDriver_->showLEDs();

    while (boardDriver_->getSensorState(kingFromRow, kingFromCol)) {
      boardDriver_->readSensors();
      delay(SENSOR_READ_DELAY_MS);
    }

    boardDriver_->clearAllLEDs(false);
    boardDriver_->setSquareLED(kingToRow, kingToCol, LedColors::White);
    boardDriver_->showLEDs();

    while (!boardDriver_->getSensorState(kingToRow, kingToCol)) {
      boardDriver_->readSensors();
      delay(SENSOR_READ_DELAY_MS);
    }

    boardDriver_->clearAllLEDs();
  }

  int rookFromRow = squareToRow(ci.rookFromSq);
  int rookFromCol = squareToCol(ci.rookFromSq);
  int rookToRow = squareToRow(ci.rookToSq);
  int rookToCol = squareToCol(ci.rookToSq);

  logger_.infof("Castling: please move rook from %s to %s",
                              Game::squareName(rookFromRow, rookFromCol).c_str(),
                              Game::squareName(rookToRow, rookToCol).c_str());

  boardDriver_->clearAllLEDs(false);
  boardDriver_->setSquareLED(rookFromRow, rookFromCol, LedColors::Cyan);
  boardDriver_->setSquareLED(rookToRow, rookToCol, LedColors::White);
  boardDriver_->showLEDs();

  while (boardDriver_->getSensorState(rookFromRow, rookFromCol)) {
    boardDriver_->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }

  boardDriver_->clearAllLEDs(false);
  boardDriver_->setSquareLED(rookToRow, rookToCol, LedColors::White);
  boardDriver_->showLEDs();

  while (!boardDriver_->getSensorState(rookToRow, rookToCol)) {
    boardDriver_->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }

  boardDriver_->clearAllLEDs();
} // LedGuard released

void GameMode::confirmSquareCompletion(int row, int col) {
  boardDriver_->blinkSquare(row, col, LedColors::Green, 1);
}

// ---------------------------
// Resign Feature
// ---------------------------

void GameMode::showIllegalMoveFeedback(int row, int col) {
  boardDriver_->blinkSquare(row, col, LedColors::Red, 2);
}

void GameMode::showResignProgress(int row, int col, int level, bool clearFirst) {
  BoardDriver::LedGuard guard(boardDriver_);
  if (clearFirst) boardDriver_->clearAllLEDs(false);
  boardDriver_->setSquareLED(row, col, LedColors::scaleColor(LedColors::Orange, RESIGN_BRIGHTNESS_LEVELS[level]));
  boardDriver_->showLEDs();
}

void GameMode::clearResignFeedback(int row, int col) {
  BoardDriver::LedGuard guard(boardDriver_);
  boardDriver_->setSquareLED(row, col, LedColors::Off);
  boardDriver_->showLEDs();
}

bool GameMode::processResign() {
  if (!resignPending_) return false;
  resignPending_ = false;
  handleResign(chess_->currentTurn());
  boardDriver_->updateSensorPrev();
  return true;
}

bool GameMode::continueResignGesture(int row, int col, Color color) {
  // Called inline from tryPlayerMove after king returned (50% showing).
  // Need 2 more lift-and-return cycles. Each return brightens: 75% then 100%.

  for (int lift = 1; lift <= 2; lift++) {
    unsigned long waitStart = millis();
    bool lifted = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      boardDriver_->readSensors();
      if (!boardDriver_->getSensorState(row, col)) {
        lifted = true;
        break;
      }
      delay(SENSOR_READ_DELAY_MS);
    }

    if (!lifted) {
      clearResignFeedback(row, col);
      return false;
    }

    waitStart = millis();
    bool returned = false;
    while (millis() - waitStart < RESIGN_LIFT_WINDOW_MS) {
      boardDriver_->readSensors();
      if (boardDriver_->getSensorState(row, col)) {
        returned = true;
        break;
      }
      delay(SENSOR_READ_DELAY_MS);
    }

    if (!returned) {
      clearResignFeedback(row, col);
      return false;
    }

    showResignProgress(row, col, lift + 1);
  }

  logger_.infof("Resign gesture completed by %s", Game::colorName(color));
  delay(500);
  clearResignFeedback(row, col);
  return handleResign(color);
}

bool GameMode::handleResign(Color resignColor) {
  onBeforeResignConfirm();

  bool flipped = isFlipped();
  logger_.infof("Resign confirmation for %s...", Game::colorName(resignColor));

  if (!boardConfirm(boardDriver_, flipped)) {
    logger_.info("Resign cancelled");
    onResignCancelled();
    return false;
  }

  onResignConfirmed(resignColor);

  Color winnerColor = ~resignColor;
  boardDriver_->fireworkAnimation(SystemUtils::colorLed(winnerColor));
  chess_->endGame(GameResult::RESIGNATION, winnerColor == Color::WHITE ? 'w' : 'b');
  return true;
}
