#ifndef LIBRECHESS_NOTATION_H
#define LIBRECHESS_NOTATION_H

#include <string>

#include "bitboard.h"
#include "move.h"
#include "types.h"

// ---------------------------------------------------------------------------
// Move notation conversion — transforms between internal array coordinates
// and standard chess notation formats.
//
// Supports three notation formats (see MoveFormat in types.h):
//   - Coordinate: "e2e4", "e7e8q"  — origin + destination squares, optional
//     promotion suffix.  UCI protocol format.
//   - SAN (Standard Algebraic Notation): "e4", "Nxf3", "O-O", "e8=Q+"
//     Compact, human-readable.
//   - LAN (Long Algebraic Notation): "e2-e4", "Ng1xf3", "O-O", "e7-e8=Q+"
//     Like SAN but always includes the origin square.
//
// All functions are pure — board state and position are passed in as
// parameters.
//
// Output functions produce notation without check/checkmate suffixes (+ / #).
// The caller appends those after testing for check/checkmate.
// ---------------------------------------------------------------------------
namespace LibreChess {
namespace notation {

// ---------------------------------------------------------------------------
// Output — array coordinates / MoveEntry → string
// ---------------------------------------------------------------------------

// Coordinate notation: "e2e4", "e7e8q".
std::string toCoordinate(int fromRow, int fromCol, int toRow, int toCol,
                         char promotion = ' ');

// Long Algebraic Notation: "e2-e4", "Ng1xf3", "O-O", "e7-e8=Q".
std::string toLAN(const MoveEntry& move);

// Standard Algebraic Notation: "e4", "Nxf3", "O-O", "e8=Q".
std::string toSAN(const BitboardSet& bb, const Piece mailbox[],
                  const PositionState& state, const MoveEntry& move);

// ---------------------------------------------------------------------------
// Input — string → array coordinates
// ---------------------------------------------------------------------------

// Parse coordinate notation: "e2e4", "e7e8q".
bool parseCoordinate(const std::string& move,
                     int& fromRow, int& fromCol,
                     int& toRow, int& toCol,
                     char& promotion);

// Parse Long Algebraic Notation: "e2-e4", "Ng1xf3", "e7-e8=Q", "O-O".
bool parseLAN(const std::string& move,
              int& fromRow, int& fromCol,
              int& toRow, int& toCol,
              char& promotion);

// Parse Standard Algebraic Notation: "e4", "Nxf3", "O-O", "e8=Q".
bool parseSAN(const BitboardSet& bb, const Piece mailbox[],
              const PositionState& state,
              Color currentTurn, const std::string& san,
              int& fromRow, int& fromCol,
              int& toRow, int& toCol,
              char& promotion);

// Auto-detect format and parse.  Tries coordinate first (fast regex match),
// then LAN, then SAN.  Returns false if no parser succeeds.
bool parseMove(const BitboardSet& bb, const Piece mailbox[],
               const PositionState& state,
               Color currentTurn, const std::string& move,
               int& fromRow, int& fromCol,
               int& toRow, int& toCol,
               char& promotion);

}  // namespace notation
}  // namespace LibreChess

#endif  // CORE_NOTATION_H
