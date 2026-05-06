#ifndef SHARED_SERIAL_LOGGER_H
#define SHARED_SERIAL_LOGGER_H

#include "logger.h"

// Concrete ILogger implementation that writes to Arduino Serial.
class SerialLogger : public LibreChess::ILogger {
 public:
  void info(const char* message) override;
  void error(const char* message) override;
};

#endif  // SHARED_SERIAL_LOGGER_H
