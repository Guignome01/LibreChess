#include "services.h"

#include "calibration.h"

BoardServices::BoardServices(BoardSystem& system, BoardLayering& layering, BoardFeedback& feedback,
                             BoardAssistance& assistance, BoardStack& stack)
    : system_(system), layering_(layering), feedback_(feedback), assistance_(assistance), stack_(stack) {}

BoardCalibrationWorkflow BoardServices::makeCalibrationWorkflow() {
  return system_.makeCalibrationWorkflow();
}
