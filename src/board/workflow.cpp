#include "workflow.h"

BoardWorkflow::BoardWorkflow() : controller_(nullptr) {}

BoardWorkflow::BoardWorkflow(BoardController& controller)
  : controller_(&controller) {}

BoardController& BoardWorkflow::board() const {
  return *controller_;
}