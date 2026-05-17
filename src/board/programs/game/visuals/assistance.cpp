#include "board/programs/game/visuals/assistance.h"

#include "board/runtime/runtime.h"
#include "board/services/visual/animations.h"

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

BoardAssistance::BoardAssistance(BoardRuntime& runtime, BoardAnimations& animations,
                                 BoardAssistanceLevel level)
    : BoardVisual(runtime),
      animations_(animations),
      level_(level) {}

void BoardAssistance::clear() {
  clearSurface();
}

void BoardAssistance::paintMovePrompt(int fromRow, int fromCol, int toRow, int toCol,
                                      LedRGB destColor, int extraRow, int extraCol,
                                      LedRGB extraColor) {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  g.canvas.setPixel(surface, fromRow, fromCol, LedColors::Cyan);
  g.canvas.setPixel(surface, toRow, toCol, destColor);
  if (extraRow >= 0 && extraCol >= 0) {
    g.canvas.setPixel(surface, extraRow, extraCol, extraColor);
  }
}

void BoardAssistance::paintDestinationOnly(int row, int col, LedRGB color) {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  g.canvas.setPixel(surface, row, col, color);
}

void BoardAssistance::waitForSetup(const BoardSetupSnapshot& setup) {
  Serial.println("Set up the board in the required position...");

  const uint16_t cadence = runtime_.cadenceMs();
  bool allCorrect = false;
  while (!allCorrect) {
    bool occupied[BoardInput::ROWS][BoardInput::COLS];
    runtime_.copyInputOccupancy(occupied);
    {
      auto g = runtime_.lockCanvas();
      BoardCanvasHandle surface = writableSurface(g.canvas);
      g.canvas.clearSurface(surface);
      allCorrect = true;
      for (int row = 0; row < BoardInput::ROWS; ++row) {
        for (int col = 0; col < BoardInput::COLS; ++col) {
          const BoardPiece piece = setup.squares[row][col];
          const bool shouldHavePiece = piece.occupied();
          const bool hasPiece = occupied[row][col];
          if (shouldHavePiece != hasPiece) allCorrect = false;
          if (shouldHavePiece && !hasPiece) {
            const LedRGB pieceColor = piece.color == BoardPieceColor::WHITE ? LedColors::White
                                                                            : LedColors::Blue;
            g.canvas.setPixel(surface, row, col, pieceColor);
          } else if (!shouldHavePiece && hasPiece) {
            g.canvas.setPixel(surface, row, col, LedColors::Red);
          }
        }
      }
    }
    if (!allCorrect) delay(cadence);
  }

  Serial.println("Board setup complete! Game starting...");
  {
    auto g = runtime_.lockCanvas();
    clearSurface(g.canvas);
    animations_.startFirework(LedColors::Yellow, millis());
  }
  runtime_.clearInputEvents();
}

void BoardAssistance::showLegalMoveHighlights(int fromRow, int fromCol,
                                              const BoardMoveTargetList& targets) {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  if (level_ != BoardAssistanceLevel::LEGAL_MOVES) return;

  g.canvas.setPixel(surface, fromRow, fromCol, LedColors::Cyan);
  for (uint8_t i = 0; i < targets.count; ++i) {
    const BoardMoveTarget& target = targets.targets[i];
    if (!target.capture) {
      g.canvas.setPixel(surface, target.row, target.col, LedColors::White);
    } else {
      g.canvas.setPixel(surface, target.row, target.col, LedColors::Red);
      if (target.enPassant) {
        g.canvas.setPixel(surface, target.capturedRow, target.capturedCol, LedColors::Purple);
      }
    }
  }
}

void BoardAssistance::showBestMoveHighlights(int fromRow, int fromCol,
                                             const BoardMoveTargetList& targets,
                                             const BoardMoveTargetRanking& ranking) {
  auto g = runtime_.lockCanvas();
  BoardCanvasHandle surface = writableSurface(g.canvas);
  g.canvas.clearSurface(surface);
  if (level_ != BoardAssistanceLevel::BEST_MOVE) return;

  g.canvas.setPixel(surface, fromRow, fromCol, LedColors::Cyan);
  for (uint8_t i = 0; i < targets.count; ++i) {
    const BoardMoveTarget& target = targets.targets[i];
    LedRGB color = LedColors::White;
    if (ranking.isBest(target.row, target.col)) {
      color = LedColors::Green;
    } else if (ranking.isWorst(target.row, target.col)) {
      color = LedColors::Red;
    }
    g.canvas.setPixel(surface, target.row, target.col, color);
    if (target.enPassant) {
      g.canvas.setPixel(surface, target.capturedRow, target.capturedCol, LedColors::Purple);
    }
  }
}

void BoardAssistance::showCapturePlacementPrompt(int row, int col) {
  auto g = runtime_.lockCanvas();
  animations_.startBlink(row, col, LedColors::Red, 1, millis());
}

void BoardAssistance::guideCastling(int kingFromRow, int kingFromCol, int kingToRow,
                                    int kingToCol,
                                    const BoardCastlingGuide& castling,
                                    bool waitForKingCompletion) {
  if (!castling.isCastling) return;
  const uint16_t cadence = runtime_.cadenceMs();

  if (waitForKingCompletion) {
    Serial.printf("Castling: please move king from %c%c to %c%c\n",
                  boardFileChar(kingFromCol), boardRankChar(kingFromRow),
                  boardFileChar(kingToCol), boardRankChar(kingToRow));

    paintMovePrompt(kingFromRow, kingFromCol, kingToRow, kingToCol, LedColors::White);
    waitSquareEmpty(runtime_, kingFromRow, kingFromCol, cadence);
    paintDestinationOnly(kingToRow, kingToCol, LedColors::White);
    waitSquareOccupied(runtime_, kingToRow, kingToCol, cadence);
    {
      auto g = runtime_.lockCanvas();
      clearSurface(g.canvas);
    }
  }

  const int rookFromRow = castling.rookFromRow;
  const int rookFromCol = castling.rookFromCol;
  const int rookToRow = castling.rookToRow;
  const int rookToCol = castling.rookToCol;

  Serial.printf("Castling: please move rook from %c%c to %c%c\n",
                boardFileChar(rookFromCol), boardRankChar(rookFromRow),
                boardFileChar(rookToCol), boardRankChar(rookToRow));

  paintMovePrompt(rookFromRow, rookFromCol, rookToRow, rookToCol, LedColors::White);
  waitSquareEmpty(runtime_, rookFromRow, rookFromCol, cadence);
  paintDestinationOnly(rookToRow, rookToCol, LedColors::White);
  waitSquareOccupied(runtime_, rookToRow, rookToCol, cadence);
  {
    auto g = runtime_.lockCanvas();
    clearSurface(g.canvas);
  }
  runtime_.clearInputEvents();
}

void BoardAssistance::guideRemoteMoveCompletion(int fromRow, int fromCol, int toRow, int toCol,
                                                const BoardMoveCompletion& completion) {
  paintMovePrompt(fromRow, fromCol, toRow, toCol,
                  completion.isCapture ? LedColors::Red : LedColors::White,
                  completion.isEnPassant ? completion.enPassantCapturedPawnRow : -1,
                  completion.isEnPassant ? toCol : -1,
                  LedColors::Purple);

  bool piecePickedUp = false;
  bool capturedPieceRemoved = false;
  bool moveCompleted = false;

  Serial.println("Waiting for you to complete the remote move...");
  const uint16_t cadence = runtime_.cadenceMs();

  while (!moveCompleted) {
    if (completion.isCapture && !capturedPieceRemoved) {
      const int captureCheckRow = completion.isEnPassant ? completion.enPassantCapturedPawnRow : toRow;
      if (!runtime_.inputOccupied(captureCheckRow, toCol)) {
        capturedPieceRemoved = true;
        Serial.println(completion.isEnPassant ? "En passant captured pawn removed, now complete the move..."
                                             : "Captured piece removed, now complete the move...");
      }
    }

    if (!piecePickedUp && !runtime_.inputOccupied(fromRow, fromCol)) {
      piecePickedUp = true;
      Serial.println("Piece picked up, now place it on the destination...");
    }

    if (piecePickedUp && runtime_.inputOccupied(toRow, toCol)) {
      if (!completion.isCapture || capturedPieceRemoved) {
        moveCompleted = true;
        Serial.println("Move completed on physical board!");
      }
    }

    delay(cadence);
  }

  auto g = runtime_.lockCanvas();
  clearSurface(g.canvas);
  runtime_.clearInputEvents();
}
