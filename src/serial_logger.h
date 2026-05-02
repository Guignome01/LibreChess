#ifndef SERIAL_LOGGER_H
#define SERIAL_LOGGER_H

#include "logger.h"

using namespace LibreChess;

// Concrete ILogger implementation that writes to Arduino Serial.
class SerialLogger : public ILogger {
 public:
  void info(const char* message) override;
  void error(const char* message) override;
};

#endif  // SERIAL_LOGGER_H
