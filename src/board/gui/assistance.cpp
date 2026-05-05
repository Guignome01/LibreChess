#include "assistance.h"

#include "board/core/layering.h"

#include <Arduino.h>

namespace {

void waitForSquareEmpty(BoardSystem* system, int row, int col) {
  while (system->occupied(row, col)) {
    system->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

void waitForSquareOccupied(BoardSystem* system, int row, int col) {
  while (!system->occupied(row, col)) {
    system->readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

template <typename LEDWriter>
void showMovePrompt(LEDWriter& leds, int fromRow, int fromCol, int toRow, int toCol, LedRGB destinationColor) {
  leds.clearAllLEDs(false);
  leds.setSquareLED(fromRow, fromCol, LedColors::Cyan);
  leds.setSquareLED(toRow, toCol, destinationColor);
  leds.showLEDs();
}

template <typename LEDWriter>
void showDestinationPrompt(LEDWriter& leds, int row, int col, LedRGB color) {
  leds.clearAllLEDs(false);
  leds.setSquareLED(row, col, color);
  leds.showLEDs();
}

}  // namespace

BoardAssistance::BoardAssistance(BoardSystem* system, BoardAssistanceLevel level, BoardLayering* layering)
  : system_(system), level_(level), layering_(layering) {}

void BoardAssistance::waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  logger.info("Set up the board in the required position...");

  bool allCorrect = false;
  while (!allCorrect) {
    system_->readSensors();

    auto drawSetup = [&](auto& leds) {
      allCorrect = true;

      game.forEachSquare([&](int row, int col, LibreChess::Piece piece) {
        bool shouldHavePiece = !LibreChess::Game::isEmptySquare(piece);
        bool hasPiece = system_->occupied(row, col);

        if (shouldHavePiece != hasPiece) allCorrect = false;

        if (shouldHavePiece && !hasPiece) {
          leds.setSquareLED(row, col, LedColors::forPieceColor(LibreChess::Game::pieceColor(piece)));
        } else if (!shouldHavePiece && hasPiece) {
          leds.setSquareLED(row, col, LedColors::Red);
        } else {
          leds.setSquareLED(row, col, LedColors::Off);
        }
      });
      leds.showLEDs();
    };

    if (layering_)
      layering_->replaceBase(drawSetup);
    else
      system_->batchLEDs(drawSetup);

    delay(SENSOR_READ_DELAY_MS);
  }

  logger.info("Board setup complete! Game starting...");
  if (layering_) layering_->clearBase(false);
  system_->runAnimation(AnimationJob::firework());
  system_->readSensors();
  system_->syncOccupancyBaseline();
}

void BoardAssistance::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves, const LibreChess::Game& game) {
  system_->waitForAnimationQueueDrain();

  if (level_ != BoardAssistanceLevel::LEGAL_MOVES) {
    if (layering_)
      layering_->clearBase();
    else
      system_->clearAllLEDs();
    return;
  }

  auto drawHighlights = [&](auto& leds) {
    leds.setSquareLED(fromRow, fromCol, LedColors::Cyan);

    for (int i = 0; i < moves.count; i++) {
      int row = LibreChess::squareToRow(moves.moves[i].to);
      int col = LibreChess::squareToCol(moves.moves[i].to);

      auto enPassant = game.checkEnPassant(fromRow, fromCol, row, col);
      if (LibreChess::Game::isEmptySquare(game.getSquare(row, col)) && !enPassant.isCapture) {
        leds.setSquareLED(row, col, LedColors::White);
      } else {
        leds.setSquareLED(row, col, LedColors::Red);
        if (enPassant.isCapture)
          leds.setSquareLED(LibreChess::squareToRow(enPassant.capturedPawnSq), col, LedColors::Purple);
      }
    }
    leds.showLEDs();
  };

  if (layering_)
    layering_->replaceBase(drawHighlights);
  else
    system_->batchLEDs(drawHighlights);
}

void BoardAssistance::showCapturePlacementPrompt(int row, int col) {
  system_->runAnimation(AnimationJob::blink(row, col, LedColors::Red, 1, false));
}

void BoardAssistance::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol, const LibreChess::CastlingInfo& castling, bool waitForKingCompletion, LibreChess::Log& logger) {
  if (!castling.isCastling) return;

  if (waitForKingCompletion) {
    logger.infof("Castling: please move king from %s to %s",
                 LibreChess::Game::squareName(kingFromRow, kingFromCol).c_str(),
                 LibreChess::Game::squareName(kingToRow, kingToCol).c_str());

    auto drawKingPrompt = [&](auto& leds) {
      showMovePrompt(leds, kingFromRow, kingFromCol, kingToRow, kingToCol, LedColors::White);
    };
    if (layering_)
      layering_->replaceBase(drawKingPrompt);
    else
      system_->batchLEDs(drawKingPrompt);
    waitForSquareEmpty(system_, kingFromRow, kingFromCol);
    auto drawKingDestination = [&](auto& leds) {
      showDestinationPrompt(leds, kingToRow, kingToCol, LedColors::White);
    };
    if (layering_)
      layering_->replaceBase(drawKingDestination);
    else
      system_->batchLEDs(drawKingDestination);
    waitForSquareOccupied(system_, kingToRow, kingToCol);
    if (layering_)
      layering_->clearBase();
    else
      system_->clearAllLEDs();
  }

  int rookFromRow = LibreChess::squareToRow(castling.rookFromSq);
  int rookFromCol = LibreChess::squareToCol(castling.rookFromSq);
  int rookToRow = LibreChess::squareToRow(castling.rookToSq);
  int rookToCol = LibreChess::squareToCol(castling.rookToSq);

  logger.infof("Castling: please move rook from %s to %s",
               LibreChess::Game::squareName(rookFromRow, rookFromCol).c_str(),
               LibreChess::Game::squareName(rookToRow, rookToCol).c_str());

  auto drawRookPrompt = [&](auto& leds) {
    showMovePrompt(leds, rookFromRow, rookFromCol, rookToRow, rookToCol, LedColors::White);
  };
  if (layering_)
    layering_->replaceBase(drawRookPrompt);
  else
    system_->batchLEDs(drawRookPrompt);
  waitForSquareEmpty(system_, rookFromRow, rookFromCol);
  auto drawRookDestination = [&](auto& leds) {
    showDestinationPrompt(leds, rookToRow, rookToCol, LedColors::White);
  };
  if (layering_)
    layering_->replaceBase(drawRookDestination);
  else
    system_->batchLEDs(drawRookDestination);
  waitForSquareOccupied(system_, rookToRow, rookToCol);
  if (layering_)
    layering_->clearBase();
  else
    system_->clearAllLEDs();
}


void BoardAssistance::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol, bool isCapture, bool isEnPassant, int enPassantCapturedPawnRow, LibreChess::Log& logger) {
  auto drawRemotePrompt = [&](auto& leds) {
    showMovePrompt(leds, fromRow, fromCol, toRow, toCol, isCapture ? LedColors::Red : LedColors::White);
    if (isEnPassant)
      leds.setSquareLED(enPassantCapturedPawnRow, toCol, LedColors::Purple);
    leds.showLEDs();
  };

  if (layering_)
    layering_->replaceBase(drawRemotePrompt);
  else
    system_->batchLEDs(drawRemotePrompt);

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  logger.info("Waiting for you to complete the remote move...");

  while (!moveCompleted) {
    system_->readSensors();

    if (isCapture && !capturedPieceRemoved) {
      int captureCheckRow = isEnPassant ? enPassantCapturedPawnRow : toRow;
      if (!system_->occupied(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        logger.info(isEnPassant ? "En passant captured pawn removed, now complete the move..."
                                : "Captured piece removed, now complete the move...");
      }
    }

    if (!piecePickedUp && !system_->occupied(fromRow, fromCol)) {
      piecePickedUp = true;
      logger.info("Piece picked up, now place it on the destination...");
    }

    if (piecePickedUp && system_->occupied(toRow, toCol))
      if (!isCapture || capturedPieceRemoved) {
        moveCompleted = true;
        logger.info("Move completed on physical board!");
      }

    delay(SENSOR_READ_DELAY_MS);
    system_->syncOccupancyBaseline();
  }

  if (layering_)
    layering_->clearBase();
  else
    system_->clearAllLEDs();
}