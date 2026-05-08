#include "board/gui/assistance.h"

#include "board/core/runtime.h"
#include "board/gui/layers.h"

#include <Arduino.h>

// ---------------------------------------------------------------------------
// BoardAssistance implementation
// ---------------------------------------------------------------------------
// Painting flows acquire the canvas guard for a single mutation, then
// release. Wait loops use BoardRuntime's synchronized input helpers for
// occupancy queries. The renderer keeps the painted highlights visible at
// ~30 Hz without our involvement.
// ---------------------------------------------------------------------------

namespace {

void waitSquareEmpty(BoardRuntime& runtime, int row, int col, uint16_t cadenceMs) {
  while (runtime.inputOccupied(row, col)) delay(cadenceMs);
}

void waitSquareOccupied(BoardRuntime& runtime, int row, int col, uint16_t cadenceMs) {
  while (!runtime.inputOccupied(row, col)) delay(cadenceMs);
}

}  // namespace

BoardAssistance::BoardAssistance(BoardRuntime& runtime, BoardAssistanceLevel level)
    : runtime_(runtime), level_(level) {}

void BoardAssistance::paintMovePrompt(int fromRow, int fromCol, int toRow, int toCol,
                                      LedRGB destColor, int extraRow, int extraCol,
                                      LedRGB extraColor) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::ASSISTANCE);
  g.canvas.setPixel(BoardLayer::ASSISTANCE, fromRow, fromCol, LedColors::Cyan);
  g.canvas.setPixel(BoardLayer::ASSISTANCE, toRow, toCol, destColor);
  if (extraRow >= 0 && extraCol >= 0) {
    g.canvas.setPixel(BoardLayer::ASSISTANCE, extraRow, extraCol, extraColor);
  }
}

void BoardAssistance::paintDestinationOnly(int row, int col, LedRGB color) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::ASSISTANCE);
  g.canvas.setPixel(BoardLayer::ASSISTANCE, row, col, color);
}

void BoardAssistance::waitForSetup(const LibreChess::Game& game, LibreChess::Log& logger) {
  logger.info("Set up the board in the required position...");

  const uint16_t cadence = runtime_.cadenceMs();
  bool allCorrect = false;
  while (!allCorrect) {
    bool occupied[8][8];
    runtime_.copyInputOccupancy(occupied);
    {
      auto g = runtime_.lockCanvas();
      g.canvas.clearLayer(BoardLayer::ASSISTANCE);
      allCorrect = true;
      game.forEachSquare([&](int row, int col, LibreChess::Piece piece) {
        const bool shouldHavePiece = !LibreChess::Game::isEmptySquare(piece);
        const bool hasPiece = occupied[row][col];
        if (shouldHavePiece != hasPiece) allCorrect = false;
        if (shouldHavePiece && !hasPiece) {
          g.canvas.setPixel(BoardLayer::ASSISTANCE, row, col,
                            LedColors::forPieceColor(LibreChess::Game::pieceColor(piece)));
        } else if (!shouldHavePiece && hasPiece) {
          g.canvas.setPixel(BoardLayer::ASSISTANCE, row, col, LedColors::Red);
        }
      });
    }
    if (!allCorrect) delay(cadence);
  }

  logger.info("Board setup complete! Game starting...");
  {
    auto g = runtime_.lockCanvas();
    g.canvas.clearLayer(BoardLayer::ASSISTANCE);
    g.effects.startFirework(LedColors::Yellow, millis());
  }
  runtime_.clearInputEvents();
}

void BoardAssistance::showLegalMoveHighlights(int fromRow, int fromCol,
                                              const LibreChess::MoveList& moves,
                                              const LibreChess::Game& game) {
  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::ASSISTANCE);
  if (level_ != BoardAssistanceLevel::LEGAL_MOVES) return;

  g.canvas.setPixel(BoardLayer::ASSISTANCE, fromRow, fromCol, LedColors::Cyan);
  for (int i = 0; i < moves.count; i++) {
    const int row = LibreChess::squareToRow(moves.moves[i].to);
    const int col = LibreChess::squareToCol(moves.moves[i].to);
    auto enPassant = game.checkEnPassant(fromRow, fromCol, row, col);
    if (LibreChess::Game::isEmptySquare(game.getSquare(row, col)) && !enPassant.isCapture) {
      g.canvas.setPixel(BoardLayer::ASSISTANCE, row, col, LedColors::White);
    } else {
      g.canvas.setPixel(BoardLayer::ASSISTANCE, row, col, LedColors::Red);
      if (enPassant.isCapture) {
        g.canvas.setPixel(BoardLayer::ASSISTANCE,
                          LibreChess::squareToRow(enPassant.capturedPawnSq), col,
                          LedColors::Purple);
      }
    }
  }
}

void BoardAssistance::showCapturePlacementPrompt(int row, int col) {
  auto g = runtime_.lockCanvas();
  g.effects.startBlink(row, col, LedColors::Red, 1, millis(), BoardLayer::ASSISTANCE);
}

void BoardAssistance::guideCastling(int kingFromRow, int kingFromCol, int kingToRow,
                                    int kingToCol,
                                    const LibreChess::CastlingInfo& castling,
                                    bool waitForKingCompletion, LibreChess::Log& logger) {
  if (!castling.isCastling) return;
  const uint16_t cadence = runtime_.cadenceMs();

  if (waitForKingCompletion) {
    logger.infof("Castling: please move king from %s to %s",
                 LibreChess::Game::squareName(kingFromRow, kingFromCol).c_str(),
                 LibreChess::Game::squareName(kingToRow, kingToCol).c_str());

    paintMovePrompt(kingFromRow, kingFromCol, kingToRow, kingToCol, LedColors::White);
    waitSquareEmpty(runtime_, kingFromRow, kingFromCol, cadence);
    paintDestinationOnly(kingToRow, kingToCol, LedColors::White);
    waitSquareOccupied(runtime_, kingToRow, kingToCol, cadence);
    {
      auto g = runtime_.lockCanvas();
      g.canvas.clearLayer(BoardLayer::ASSISTANCE);
    }
  }

  const int rookFromRow = LibreChess::squareToRow(castling.rookFromSq);
  const int rookFromCol = LibreChess::squareToCol(castling.rookFromSq);
  const int rookToRow = LibreChess::squareToRow(castling.rookToSq);
  const int rookToCol = LibreChess::squareToCol(castling.rookToSq);

  logger.infof("Castling: please move rook from %s to %s",
               LibreChess::Game::squareName(rookFromRow, rookFromCol).c_str(),
               LibreChess::Game::squareName(rookToRow, rookToCol).c_str());

  paintMovePrompt(rookFromRow, rookFromCol, rookToRow, rookToCol, LedColors::White);
  waitSquareEmpty(runtime_, rookFromRow, rookFromCol, cadence);
  paintDestinationOnly(rookToRow, rookToCol, LedColors::White);
  waitSquareOccupied(runtime_, rookToRow, rookToCol, cadence);
  {
    auto g = runtime_.lockCanvas();
    g.canvas.clearLayer(BoardLayer::ASSISTANCE);
  }
  runtime_.clearInputEvents();
}

void BoardAssistance::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                                bool isCapture, bool isEnPassant,
                                                int enPassantCapturedPawnRow,
                                                LibreChess::Log& logger) {
  paintMovePrompt(fromRow, fromCol, toRow, toCol,
                  isCapture ? LedColors::Red : LedColors::White,
                  isEnPassant ? enPassantCapturedPawnRow : -1,
                  isEnPassant ? toCol : -1,
                  LedColors::Purple);

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  logger.info("Waiting for you to complete the remote move...");
  const uint16_t cadence = runtime_.cadenceMs();

  while (!moveCompleted) {
    if (isCapture && !capturedPieceRemoved) {
      const int captureCheckRow = isEnPassant ? enPassantCapturedPawnRow : toRow;
      if (!runtime_.inputOccupied(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        logger.info(isEnPassant ? "En passant captured pawn removed, now complete the move..."
                                : "Captured piece removed, now complete the move...");
      }
    }

    if (!piecePickedUp && !runtime_.inputOccupied(fromRow, fromCol)) {
      piecePickedUp = true;
      logger.info("Piece picked up, now place it on the destination...");
    }

    if (piecePickedUp && runtime_.inputOccupied(toRow, toCol)) {
      if (!isCapture || capturedPieceRemoved) {
        moveCompleted = true;
        logger.info("Move completed on physical board!");
      }
    }

    delay(cadence);
  }

  auto g = runtime_.lockCanvas();
  g.canvas.clearLayer(BoardLayer::ASSISTANCE);
  runtime_.clearInputEvents();
}
