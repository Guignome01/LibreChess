#ifndef LIBRECHESS_HASH_TABLE_H
#define LIBRECHESS_HASH_TABLE_H

// ---------------------------------------------------------------------------
// Generic hash table base — power-of-2 sized, index = hash & mask.
//
// Provides the common memory management (resize, free, clear) shared by
// PawnHashTable, EvalHashTable, and TranspositionTable.  Derived types add
// their own probe/store methods with type-specific lookup and replacement
// logic.
//
// Entry type must be trivially copyable (memset-zero is used for clear).
//
// Reference: https://www.chessprogramming.org/Hash_Table
// ---------------------------------------------------------------------------

#include <cstring>
#include <new>

#include "utils.h"

namespace LibreChess {

template <typename Entry>
struct HashTableBase {
  Entry* entries = nullptr;
  int size = 0;   // Number of entries (power of 2)
  int mask = 0;   // size - 1

  // Allocate entries.  `numEntries` is rounded down to nearest power of 2.
  void resize(int numEntries) {
    free();
    size = utils::roundDownPow2(numEntries);
    if (size == 0) return;
    mask = size - 1;
    entries = new (std::nothrow) Entry[size];
    if (!entries) { size = 0; mask = 0; return; }
    clear();
  }

  // Release memory.
  void free() {
    delete[] entries;
    entries = nullptr;
    size = 0;
    mask = 0;
  }

  // Clear all entries (zero-fill).
  void clear() {
    if (entries) std::memset(entries, 0, size * sizeof(Entry));
  }
};

}  // namespace LibreChess

#endif  // LIBRECHESS_HASH_TABLE_H
