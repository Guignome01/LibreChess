#include "serial_logger.h"

#include <Arduino.h>

void SerialLogger::info(const char* message) {
  Serial.println(message);
}

void SerialLogger::error(const char* message) {
  Serial.printf("[ERROR] %s\n", message);
}
