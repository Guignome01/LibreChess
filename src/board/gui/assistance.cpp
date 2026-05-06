#include "assistance.h"

#include "animations.h"
#include "layering.h"

#include <Arduino.h>

namespace {

void waitForSquareEmpty(BoardSystem& system, int row, int col) {
  while (system.occupied(row, col)) {
    system.readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

void waitForSquareOccupied(BoardSystem& system, int row, int col) {
  while (!system.occupied(row, col)) {
    system.readSensors();
    delay(SENSOR_READ_DELAY_MS);
  }
}

template <typename LEDWriter>
void showMovePrompt(LEDWriter& leds, int fromRow, int fromCol, int toRow, int toCol,
                    LedRGB destinationColor) {
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

BoardAssistance::BoardAssistance(BoardSystem& system, BoardLayering& layering, BoardAssistanceLevel level)
    : system_(system), layering_(layering), level_(level) {}

void BoardAssistance::waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  logger.info("Set up the board in the required position...");

  bool allCorrect = false;
  while (!allCorrect) {
    system_.readSensors();

    layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
      allCorrect = true;
      game.forEachSquare([&](int row, int col, LibreChess::Piece piece) {
        bool shouldHavePiece = !LibreChess::Game::isEmptySquare(piece);
        bool hasPiece = system_.occupied(row, col);
        if (shouldHavePiece != hasPiece) allCorrect = false;
        if (shouldHavePiece && !hasPiece)
          leds.setSquareLED(row, col, LedColors::forPieceColor(LibreChess::Game::pieceColor(piece)));
        else if (!shouldHavePiece && hasPiece)
          leds.setSquareLED(row, col, LedColors::Red);
        else
          leds.setSquareLED(row, col, LedColors::Off);
      });
      leds.showLEDs();
    });

    delay(SENSOR_READ_DELAY_MS);
  }

  logger.info("Board setup complete! Game starting...");
  layering_.clearBase(false);
  system_.runAnimation(AnimationJob::firework());
  system_.readSensors();
}

void BoardAssistance::showLegalMoveHighlights(int fromRow, int fromCol, const LibreChess::MoveList& moves,
                                              const LibreChess::Game& game) {
  system_.waitForAnimationQueueDrain();

  if (level_ != BoardAssistanceLevel::LEGAL_MOVES) {
    layering_.clearBase();
    return;
  }

  layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
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
  });
}

void BoardAssistance::showCapturePlacementPrompt(int row, int col) {
  system_.runAnimation(AnimationJob::blink(row, col, LedColors::Red, 1, false));
}

void BoardAssistance::guideCastling(int kingFromRow, int kingFromCol, int kingToRow, int kingToCol,
                                    const LibreChess::CastlingInfo& castling, bool waitForKingCompletion,
                                    LibreChess::Log& logger) {
  if (!castling.isCastling) return;

  if (waitForKingCompletion) {
    logger.infof("Castling: please move king from %s to %s",
                 LibreChess::Game::squareName(kingFromRow, kingFromCol).c_str(),
                 LibreChess::Game::squareName(kingToRow, kingToCol).c_str());

    layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
      showMovePrompt(leds, kingFromRow, kingFromCol, kingToRow, kingToCol, LedColors::White);
    });
    waitForSquareEmpty(system_, kingFromRow, kingFromCol);
    layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
      showDestinationPrompt(leds, kingToRow, kingToCol, LedColors::White);
    });
    waitForSquareOccupied(system_, kingToRow, kingToCol);
    layering_.clearBase();
  }

  int rookFromRow = LibreChess::squareToRow(castling.rookFromSq);
  int rookFromCol = LibreChess::squareToCol(castling.rookFromSq);
  int rookToRow = LibreChess::squareToRow(castling.rookToSq);
  int rookToCol = LibreChess::squareToCol(castling.rookToSq);

  logger.infof("Castling: please move rook from %s to %s",
               LibreChess::Game::squareName(rookFromRow, rookFromCol).c_str(),
               LibreChess::Game::squareName(rookToRow, rookToCol).c_str());

  layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
    showMovePrompt(leds, rookFromRow, rookFromCol, rookToRow, rookToCol, LedColors::White);
  });
  waitForSquareEmpty(system_, rookFromRow, rookFromCol);
  layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
    showDestinationPrompt(leds, rookToRow, rookToCol, LedColors::White);
  });
  waitForSquareOccupied(system_, rookToRow, rookToCol);
  layering_.clearBase();
}

void BoardAssistance::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                                bool isCapture, bool isEnPassant,
                                                int enPassantCapturedPawnRow, LibreChess::Log& logger) {
  layering_.replaceBase([&](BoardLayering::LayerWriter& leds) {
    showMovePrompt(leds, fromRow, fromCol, toRow, toCol, isCapture ? LedColors::Red : LedColors::White);
    if (isEnPassant)
      leds.setSquareLED(enPassantCapturedPawnRow, toCol, LedColors::Purple);
    leds.showLEDs();
  });

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  logger.info("Waiting for you to complete the remote move...");

  while (!moveCompleted) {
    system_.readSensors();

    if (isCapture && !capturedPieceRemoved) {
      int captureCheckRow = isEnPassant ? enPassantCapturedPawnRow : toRow;
      if (!system_.occupied(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        logger.info(isEnPassant ? "En passant captured pawn removed, now complete the move..."
                                : "Captured piece removed, now complete the move...");
      }
    }

    if (!piecePickedUp && !system_.occupied(fromRow, fromCol)) {
      piecePickedUp = true;
      logger.info("Piece picked up, now place it on the destination...");
    }

    if (piecePickedUp && system_.occupied(toRow, toCol))
      if (!isCapture || capturedPieceRemoved) {
        moveCompleted = true;
        logger.info("Move completed on physical board!");
      }

    delay(SENSOR_READ_DELAY_MS);
  }

  layering_.clearBase();
}
