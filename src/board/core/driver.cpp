#include "driver.h"

#include "shared/utils.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

static constexpr uint8_t MIN_BRIGHTNESS = 10;
static constexpr uint8_t MIN_DIM_MULTIPLIER = 20;
static constexpr uint8_t MAX_DIM_MULTIPLIER = 100;

static constexpr uint8_t clampBrightness(uint8_t value) {
  return value < MIN_BRIGHTNESS ? MIN_BRIGHTNESS : value;
}

static constexpr uint8_t clampDimMultiplier(uint8_t value) {
  return value < MIN_DIM_MULTIPLIER ? MIN_DIM_MULTIPLIER : (value > MAX_DIM_MULTIPLIER ? MAX_DIM_MULTIPLIER : value);
}

static constexpr uint8_t defaultLedIndex(int row, int col) {
  return static_cast<uint8_t>(row * NUM_COLS + ((row % 2 == 0) ? col : (NUM_COLS - 1 - col)));
}

static constexpr uint8_t rawIdentityLedIndex(int row, int col) {
  return static_cast<uint8_t>(row * NUM_COLS + col);
}

static constexpr uint8_t scaleChannel(uint8_t channel, uint8_t percent) {
  return static_cast<uint8_t>((static_cast<uint16_t>(channel) * percent + 50) / 100);
}

}  // namespace

BoardDriver::BoardDriver()
    : strip(LED_COUNT, LED_PIN),
      lastEnabledCol(-2),
      brightness(BRIGHTNESS),
      dimMultiplier(70),
      swapAxes(0) {
  resetCalibrationMapping();
  loadDefaultLedMapping();
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      currentColors[row][col] = LedColors::Off;
}

void BoardDriver::resetCalibrationMapping() {
  for (int i = 0; i < NUM_ROWS; i++)
    toLogicalRow[i] = i;
  for (int i = 0; i < NUM_COLS; i++)
    toLogicalCol[i] = i;
  swapAxes = 0;
}

void BoardDriver::loadDefaultLedMapping() {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      ledIndexMap[row][col] = defaultLedIndex(row, col);
}

void BoardDriver::loadRawIdentityCalibrationLedMapping() {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      ledIndexMap[row][col] = rawIdentityLedIndex(row, col);
}

void BoardDriver::begin() {
  strip.Begin();
  showLEDs();
  loadLedSettings();
  strip.SetLuminance(brightness);

  pinMode(SR_SER_DATA_PIN, OUTPUT);
  pinMode(SR_CLK_PIN, OUTPUT);
  pinMode(SR_LATCH_PIN, OUTPUT);
  disableAllCols();

  for (int row = 0; row < NUM_ROWS; row++)
    pinMode(rowPins[row], INPUT);

  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++) {
      sensorState[row][col] = false;
      sensorRaw[row][col] = false;
      sensorDebounceTime[row][col] = 0;
    }
}

void BoardDriver::loadShiftRegister(byte data, int bits) {
#if defined(SR_INVERT_OUTPUTS) && SR_INVERT_OUTPUTS != 0
  data = ~data;
#endif
  digitalWrite(SR_LATCH_PIN, LOW);
  for (int i = bits - 1; i >= 0; i--) {
    digitalWrite(SR_SER_DATA_PIN, !!(data & (1 << i)));
    delayMicroseconds(10);
    digitalWrite(SR_CLK_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SR_CLK_PIN, LOW);
    delayMicroseconds(10);
  }
  digitalWrite(SR_LATCH_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SR_LATCH_PIN, LOW);
}

void BoardDriver::disableAllCols() {
  if (lastEnabledCol == 7) {
    loadShiftRegister(0x00, 1);
  } else {
    loadShiftRegister(0);
  }
  lastEnabledCol = -1;
}

void BoardDriver::enableCol(int col) {
  if (col == lastEnabledCol + 1) {
    if (col == 0)
      loadShiftRegister(0x01, 1);
    else
      loadShiftRegister(0x00, 1);
  } else {
    loadShiftRegister(static_cast<byte>(1 << col));
  }
  lastEnabledCol = col;
  delayMicroseconds(100);
}

void BoardDriver::readSensors() {
  unsigned long currentTime = millis();

  for (int col = 0; col < NUM_COLS; col++) {
    enableCol(col);
    for (int row = 0; row < NUM_ROWS; row++) {
      bool newReading = digitalRead(rowPins[row]) == LOW;
      uint8_t logicalRow = toLogicalRow[swapAxes ? col : row];
      uint8_t logicalCol = toLogicalCol[swapAxes ? row : col];
      if (newReading != sensorState[logicalRow][logicalCol]) {
        if (newReading != sensorRaw[logicalRow][logicalCol]) {
          sensorRaw[logicalRow][logicalCol] = newReading;
          sensorDebounceTime[logicalRow][logicalCol] = currentTime;
        } else if (currentTime - sensorDebounceTime[logicalRow][logicalCol] >= DEBOUNCE_MS) {
          sensorState[logicalRow][logicalCol] = newReading;
        }
      } else {
        sensorRaw[logicalRow][logicalCol] = newReading;
        sensorDebounceTime[logicalRow][logicalCol] = currentTime;
      }
    }
  }
  disableAllCols();
}

bool BoardDriver::getSensorState(int row, int col) const {
  return sensorState[row][col];
}

void BoardDriver::readRawSensors(bool (&rawState)[NUM_ROWS][NUM_COLS]) {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      rawState[row][col] = false;

  for (int col = 0; col < NUM_COLS; col++) {
    enableCol(col);
    for (int row = 0; row < NUM_ROWS; row++)
      rawState[row][col] = (digitalRead(rowPins[row]) == LOW);
  }
  disableAllCols();
}

void BoardDriver::setRawCalibrationLED(int pixelIndex, LedRGB color) {
  if (pixelIndex < 0 || pixelIndex >= LED_COUNT) return;
  strip.SetPixelColor(pixelIndex, RgbColor(color.r, color.g, color.b));
}

void BoardDriver::clearRawCalibrationLEDs(bool show) {
  for (int pixelIndex = 0; pixelIndex < LED_COUNT; pixelIndex++)
    strip.SetPixelColor(pixelIndex, RgbColor(0));
  if (show)
    showLEDs();
}

int BoardDriver::getPixelIndex(int row, int col) const {
  return ledIndexMap[row][col];
}

void BoardDriver::clearAllLEDs(bool show) {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      currentColors[row][col] = LedColors::Off;
  for (int i = 0; i < LED_COUNT; i++)
    strip.SetPixelColor(i, RgbColor(0));
  if (show)
    showLEDs();
}

void BoardDriver::setSquareLED(int row, int col, LedRGB color) {
  currentColors[row][col] = color;
  const uint8_t percent = ((row + col) % 2 == 1) ? dimMultiplier : 100;
  strip.SetPixelColor(getPixelIndex(row, col),
                      RgbColor(scaleChannel(color.r, percent), scaleChannel(color.g, percent),
                               scaleChannel(color.b, percent)));
}

void BoardDriver::showLEDs() {
  strip.Show();
}

void BoardDriver::setBrightness(uint8_t value) {
  brightness = clampBrightness(value);
  strip.SetLuminance(brightness);
  showLEDs();
}

void BoardDriver::setDimMultiplier(uint8_t value) {
  dimMultiplier = clampDimMultiplier(value);
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      setSquareLED(row, col, currentColors[row][col]);
  showLEDs();
}

void BoardDriver::loadLedSettings() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - LED settings not loaded");
    return;
  }
  Preferences prefs;
  if (!prefs.begin("ledSettings", true)) {
    Serial.println("LED settings namespace could not be opened");
    return;
  }
  brightness = clampBrightness(prefs.getUChar("brightness", BRIGHTNESS));
  dimMultiplier = clampDimMultiplier(prefs.getUChar("dimMult", 70));
  prefs.end();
  Serial.printf("LED settings loaded: brightness=%d, dimMultiplier=%d\n", brightness, dimMultiplier);
}

void BoardDriver::saveLedSettings() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - LED settings not saved");
    return;
  }
  Preferences prefs;
  if (!prefs.begin("ledSettings", false)) {
    Serial.println("LED settings namespace could not be opened for writing");
    return;
  }
  prefs.putUChar("brightness", brightness);
  prefs.putUChar("dimMult", dimMultiplier);
  prefs.end();
  Serial.printf("LED settings saved: brightness=%d, dimMultiplier=%d\n", brightness, dimMultiplier);
}
