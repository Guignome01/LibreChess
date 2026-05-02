#include "assistance.h"

#include "board.h"

#include <Arduino.h>

namespace {

void waitForSquareEmpty(Board* board, int row, int col) {
  while (board->occupied(row, col)) {
    board->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

void waitForSquareOccupied(Board* board, int row, int col) {
  while (!board->occupied(row, col)) {
    board->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

void showMovePrompt(Board* board, int fromRow, int fromCol, int toRow, int toCol, LedRGB destinationColor) {
  board->clearAllLEDs(false);
  board->setSquareLED(fromRow, fromCol, LedColors::Cyan);
  board->setSquareLED(toRow, toCol, destinationColor);
  board->showLEDs();
}

void showDestinationPrompt(Board* board, int row, int col, LedRGB color) {
  board->clearAllLEDs(false);
  board->setSquareLED(row, col, color);
  board->showLEDs();
}

}  // namespace

BoardAssistance::BoardAssistance(Board* board, BoardAssistanceLevel level) : board_(board), level_(level) {}

void BoardAssistance::waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  logger.info("Set up the board in the required position...");

  bool allCorrect = false;
  while (!allCorrect) {
    board_->readSensors();

    {
      Board::LedGuard guard(board_);
      allCorrect = true;

      game.forEachSquare([&](int row, int col, LibreChess::Piece piece) {
        bool shouldHavePiece = !LibreChess::Game::isEmptySquare(piece);
        bool hasPiece = board_->occupied(row, col);

        if (shouldHavePiece != hasPiece) allCorrect = false;

        if (shouldHavePiece && !hasPiece) {
          board_->setSquareLED(row, col, LedColors::forPieceColor(LibreChess::Game::pieceColor(piece)));
        } else if (!shouldHavePiece && hasPiece) {
          board_->setSquareLED(row, col, LedColors::Red);
        } else {
          board_->setSquareLED(row, col, LedColors::Off);
        }
      });
      board_->showLEDs();
    }

    delay(SENSOR_READ_DELAY_MS);
  }

  logger.info("Board setup complete! Game starting...");
  board_->fireworkAnimation();
  board_->readSensors();
  board_->syncOccupancyBaseline();
}

void BoardAssistance::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game) {
  board_->waitForAnimationQueueDrain();

  if (level_ != BoardAssistanceLevel::LEGAL_MOVES) {
    Board::LedGuard guard(board_);
    board_->clearAllLEDs();
    return;
  }

  Board::LedGuard guard(board_);
  board_->setSquareLED(fromRow, fromCol, LedColors::Cyan);

  for (int i = 0; i < moves.count; i++) {
    int row = LibreChess::squareToRow(moves.moves[i].to);
    int col = LibreChess::squareToCol(moves.moves[i].to);

    auto enPassant = game.checkEnPassant(fromRow, fromCol, row, col);
    if (LibreChess::Game::isEmptySquare(game.getSquare(row, col)) && !enPassant.isCapture) {
      board_->setSquareLED(row, col, LedColors::White);
    } else {
      board_->setSquareLED(row, col, LedColors::Red);
      if (enPassant.isCapture)
        board_->setSquareLED(LibreChess::squareToRow(enPassant.capturedPawnSq), col, LedColors::Purple);
    }
  }
  board_->showLEDs();
}

void BoardAssistance::showCapturePlacementPrompt(int row, int col) {
  board_->blinkSquare(row, col, LedColors::Red, 1, false);
}

void BoardAssistance::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol, const LibreChess::CastlingInfo& castling, bool waitForKingCompletion, LibreChess::Log& logger) {
  if (!castling.isCastling) return;

  if (waitForKingCompletion) {
    logger.infof("Castling: please move king from %s to %s",
                 LibreChess::Game::squareName(kingFromRow, kingFromCol).c_str(),
                 LibreChess::Game::squareName(kingToRow, kingToCol).c_str());

    {
      Board::LedGuard guard(board_);
      showMovePrompt(board_, kingFromRow, kingFromCol, kingToRow, kingToCol, LedColors::White);
    }
    waitForSquareEmpty(board_, kingFromRow, kingFromCol);
    {
      Board::LedGuard guard(board_);
      showDestinationPrompt(board_, kingToRow, kingToCol, LedColors::White);
    }
    waitForSquareOccupied(board_, kingToRow, kingToCol);
    {
      Board::LedGuard guard(board_);
      board_->clearAllLEDs();
    }
  }

  int rookFromRow = LibreChess::squareToRow(castling.rookFromSq);
  int rookFromCol = LibreChess::squareToCol(castling.rookFromSq);
  int rookToRow = LibreChess::squareToRow(castling.rookToSq);
  int rookToCol = LibreChess::squareToCol(castling.rookToSq);

  logger.infof("Castling: please move rook from %s to %s",
               LibreChess::Game::squareName(rookFromRow, rookFromCol).c_str(),
               LibreChess::Game::squareName(rookToRow, rookToCol).c_str());

  {
    Board::LedGuard guard(board_);
    showMovePrompt(board_, rookFromRow, rookFromCol, rookToRow, rookToCol, LedColors::White);
  }
  waitForSquareEmpty(board_, rookFromRow, rookFromCol);
  {
    Board::LedGuard guard(board_);
    showDestinationPrompt(board_, rookToRow, rookToCol, LedColors::White);
  }
  waitForSquareOccupied(board_, rookToRow, rookToCol);
  {
    Board::LedGuard guard(board_);
    board_->clearAllLEDs();
  }
}

void BoardAssistance::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant, int enPassantCapturedPawnRow, LibreChess::Log& logger) {
  {
    Board::LedGuard guard(board_);
    showMovePrompt(board_, fromRow, fromCol, toRow, toCol, isCapture ? LedColors::Red : LedColors::White);
    if (isEnPassant)
      board_->setSquareLED(enPassantCapturedPawnRow, toCol, LedColors::Purple);
    board_->showLEDs();
  }

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  logger.info("Waiting for you to complete the remote move...");

  while (!moveCompleted) {
    board_->readSensors();

    if (isCapture && !capturedPieceRemoved) {
      int captureCheckRow = isEnPassant ? enPassantCapturedPawnRow : toRow;
      if (!board_->occupied(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        logger.info(isEnPassant ? "En passant captured pawn removed, now complete the move..."
                                : "Captured piece removed, now complete the move...");
      }
    }

    if (!piecePickedUp && !board_->occupied(fromRow, fromCol)) {
      piecePickedUp = true;
      logger.info("Piece picked up, now place it on the destination...");
    }

    if (piecePickedUp && board_->occupied(toRow, toCol))
      if (!isCapture || capturedPieceRemoved) {
        moveCompleted = true;
        logger.info("Move completed on physical board!");
      }

    delay(SENSOR_READ_DELAY_MS);
    board_->syncOccupancyBaseline();
  }

  {
    Board::LedGuard guard(board_);
    board_->clearAllLEDs();
  }
}