// Shared test globals.
// Compiled into every test binary (PlatformIO links test-root files into all suites).

#include "test_helpers.h"

// Shared globals — accessible via extern from test translation units
BitboardSet bb;
Piece mailbox[64];
bool needsDefaultKings = false;
