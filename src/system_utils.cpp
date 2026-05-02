#include "system_utils.h"

#include <Arduino.h>

extern "C" {
#include "nvs_flash.h"
}

LedRGB SystemUtils::colorLed(Color color) {
  return (color == Color::WHITE) ? LedColors::White : LedColors::Blue;
}

bool SystemUtils::ensureNvsInitialized() {
  esp_err_t err = nvs_flash_init();
  if (err != ESP_OK) {
    Serial.printf("[NVS] Init failed with error 0x%x, erasing and retrying...\n", err);
    nvs_flash_erase();
    err = nvs_flash_init();
    if (err == ESP_OK) {
      Serial.println("[NVS] Successfully initialized after erase");
    } else {
      Serial.printf("[NVS] Still failed after erase: 0x%x\n", err);
    }
  }
  return err == ESP_OK;
}
