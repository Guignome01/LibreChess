#include "gui.h"

BoardGui::BoardGui(BoardSystem& system)
    : layering_(system),
      feedback_(system, layering_),
      assistance_(system, layering_),
      stack_(),
      services_(system, layering_, feedback_, assistance_, stack_) {}
