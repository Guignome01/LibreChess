#ifndef LIBRECHESS_FEN_H
#define LIBRECHESS_FEN_H

// ---------------------------------------------------------------------------
// FEN (Forsyth-Edwards Notation) parsing and serialization.
// ---------------------------------------------------------------------------

#include <string>

#include "bitboard.h"
#include "types.h"

namespace LibreChess {
namespace fen {

// Convert board state to FEN string.
std::string boardToFEN(const Piece mailbox[], Color currentTurn,
                       const PositionState* state = nullptr);

// Parse FEN string and populate bitboards, mailbox, turn, and state.
void fenToBoard(const std::string& fen, BitboardSet& bb, Piece mailbox[],
                Color& currentTurn, PositionState* state = nullptr);

// Validate FEN string format.
bool validateFEN(const std::string& fen);

}  // namespace fen
}  // namespace LibreChess

#endif  // LIBRECHESS_FEN_H
