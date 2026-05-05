#include "calibration.h"

#include "core/driver.h"
#include "game.h"
#include "system_utils.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

// 74HC595 shift register pin mapping: bits are sent MSB first, so bit 7 shifts to QH, bit 0 stays at QA.
static int shiftRegPin(int col) {
  const int pins[] = {15, 1, 2, 3, 4, 5, 6, 7};
  return (col >= 0 && col < 8) ? pins[col] : -1;
}

static char shiftRegOutput(int col) {
  return (col >= 0 && col < 8) ? static_cast<char>('A' + col) : '?';
}

}  // namespace

BoardCalibration::BoardCalibration(BoardDriver* driver) : driver_(driver) {}

bool BoardCalibration::load() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - calibration not loaded");
    return false;
  }
  Preferences prefs;
  prefs.begin("boardCal", false);
  uint8_t ver = prefs.getUChar("ver", 0);
  if (ver != 1) {
    prefs.end();
    return false;
  }

  size_t rowPinsLen = prefs.getBytesLength("rowPins");
  if (rowPinsLen != NUM_ROWS) {
    prefs.end();
    return false;
  }
  uint8_t savedRowPins[NUM_ROWS];
  prefs.getBytes("rowPins", savedRowPins, sizeof(savedRowPins));
  for (int i = 0; i < NUM_ROWS; i++)
    if (savedRowPins[i] != static_cast<uint8_t>(rowPins[i])) {
      prefs.end();
      return false;
    }
  uint8_t savedSRPins[3];
  prefs.getBytes("srPins", savedSRPins, sizeof(savedSRPins));
  if (savedSRPins[0] != static_cast<uint8_t>(SR_CLK_PIN) || savedSRPins[1] != static_cast<uint8_t>(SR_LATCH_PIN) || savedSRPins[2] != static_cast<uint8_t>(SR_SER_DATA_PIN)) {
    prefs.end();
    return false;
  }

  size_t rowLen = prefs.getBytesLength("row");
  size_t colLen = prefs.getBytesLength("col");
  size_t ledLen = prefs.getBytesLength("led");
  if (rowLen != NUM_ROWS || colLen != NUM_COLS || ledLen != LED_COUNT) {
    prefs.end();
    return false;
  }
  driver_->swapAxes = prefs.getUChar("swap", 0);
  prefs.getBytes("row", driver_->toLogicalRow, NUM_ROWS);
  prefs.getBytes("col", driver_->toLogicalCol, NUM_COLS);
  uint8_t ledFlat[LED_COUNT];
  prefs.getBytes("led", ledFlat, LED_COUNT);
  int idx = 0;
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      driver_->ledIndexMap[row][col] = ledFlat[idx++];
  prefs.end();
  driver_->calibrationLoaded = true;
  Serial.println("Board calibration loaded from NVS");
  return true;
}

void BoardCalibration::save() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - calibration not saved");
    return;
  }
  Preferences prefs;
  prefs.begin("boardCal", false);
  prefs.putUChar("ver", 1);
  uint8_t rowPinsU8[NUM_ROWS];
  for (int i = 0; i < NUM_ROWS; i++)
    rowPinsU8[i] = static_cast<uint8_t>(rowPins[i]);
  prefs.putBytes("rowPins", rowPinsU8, sizeof(rowPinsU8));
  uint8_t srPins[3] = {static_cast<uint8_t>(SR_CLK_PIN), static_cast<uint8_t>(SR_LATCH_PIN), static_cast<uint8_t>(SR_SER_DATA_PIN)};
  prefs.putBytes("srPins", srPins, sizeof(srPins));
  prefs.putUChar("swap", driver_->swapAxes);
  prefs.putBytes("row", driver_->toLogicalRow, NUM_ROWS);
  prefs.putBytes("col", driver_->toLogicalCol, NUM_COLS);
  uint8_t ledFlat[LED_COUNT];
  int idx = 0;
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      ledFlat[idx++] = driver_->ledIndexMap[row][col];
  prefs.putBytes("led", ledFlat, LED_COUNT);
  prefs.end();
  driver_->calibrationLoaded = true;
  Serial.println("Board calibration saved to NVS");
}

bool BoardCalibration::run() {
  for (int i = 0; i < LED_COUNT; i++) {
    driver_->strip.SetPixelColor(i, RgbColor(LedColors::White.r, LedColors::White.g, LedColors::White.b));
    driver_->showLEDs();
    delay(50);
  }
  delay(500);
  driver_->clearAllLEDs();

  Serial.println("========================== Board calibration required ==========================");
  Serial.println("- Type 'skip' within 5 seconds to temporarily skip it (reboot to calibrate later)");
  Serial.println("  This allows testing the web UI but LEDs and sensors won't have correct mapping");
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (Serial.available()) {
      String input = Serial.readStringUntil('\n');
      input.trim();
      input.toLowerCase();
      if (input == "skip") {
        Serial.println("[SKIP] Calibration skipped - using raw identity mapping");
        Serial.println("[SKIP] Sensors/LEDs will NOT work correctly!");
        Serial.println("[SKIP] You will be asked to calibrate again on next reboot");
        driver_->resetLogicalMapping();
        driver_->loadRawIdentityLedMapping();
        driver_->calibrationLoaded = true;
        return true;
      } else {
        Serial.println("Unknown command \"" + input + "\" Type \"skip\" to skip calibration or wait 5 seconds for calibration to begin");
      }
    }
    delay(50);
  }
  Serial.println("");
  Serial.println("- Empty the board to begin calibration - instructions will follow once an empty board is detected");
  Serial.println("- WARNING: Low GPIO voltage can cause unreliable shift register behavior (74HC595 needs Vih > 0.7*Vcc) use a level shifter or HCT variant");
  Serial.println("- WARNING: Shift register outputs shouldn't power 8 sensors directly from 1 output pin, use transistors! (max 35mA per pin but each A3144 draws ~10mA");
  Serial.println("- WARNING: If powering multiple sensors from one shift register pin, expect voltage drop and shift register failure");
  Serial.println("- TIP: Try both magnet sides and move magnet closer if sensor doesn't trigger");
  Serial.println("================================================================================");
  waitForBoardEmpty();

  bool swapAxes1 = calibrateAxis(Axis::ROWS, driver_->toLogicalRow, NUM_ROWS, false);
  bool swapAxes2 = calibrateAxis(Axis::COLS, driver_->toLogicalCol, NUM_COLS, swapAxes1);
  if (swapAxes1 != swapAxes2) {
    Serial.println("Inconsistent axis orientation detected during calibration. Restarting calibration.");
    showCalibrationError();
    return run();
  }
  driver_->swapAxes = swapAxes1 ? 1 : 0;

  Serial.println("LED mapping calibration:");

  bool logicalUsed[NUM_ROWS][NUM_COLS] = {false};

  auto displayCalibrationLEDs = [&](int currentPixel) {
    for (int i = 0; i < LED_COUNT; i++)
      driver_->strip.SetPixelColor(i, RgbColor(0));
    for (int row = 0; row < NUM_ROWS; row++)
      for (int col = 0; col < NUM_COLS; col++)
        if (logicalUsed[row][col])
          driver_->strip.SetPixelColor(driver_->ledIndexMap[row][col], RgbColor(LedColors::Green.r, LedColors::Green.g, LedColors::Green.b));
    if (currentPixel < LED_COUNT)
      driver_->strip.SetPixelColor(currentPixel, RgbColor(LedColors::White.r, LedColors::White.g, LedColors::White.b));
    driver_->showLEDs();
  };

  for (int pixelIndex = 0; pixelIndex < LED_COUNT; pixelIndex++) {
    int row = 0;
    int col = 0;
    displayCalibrationLEDs(pixelIndex);
    Serial.println("Place a piece on the white LED");
    waitForSingleRawPress(row, col);

    uint8_t logicalRow = driver_->toLogicalRow[driver_->swapAxes ? col : row];
    uint8_t logicalCol = driver_->toLogicalCol[driver_->swapAxes ? row : col];
    if (logicalUsed[logicalRow][logicalCol]) {
      Serial.printf("Duplicate square %c%c detected. Retry LED %d.\n", static_cast<char>('a' + logicalCol), static_cast<char>('8' - logicalRow), pixelIndex);
      showCalibrationError();
      pixelIndex--;
      continue;
    }
    logicalUsed[logicalRow][logicalCol] = true;
    driver_->ledIndexMap[logicalRow][logicalCol] = pixelIndex;
    Serial.printf("  LED %d -> %c%c\n", pixelIndex, static_cast<char>('a' + logicalCol), static_cast<char>('8' - logicalRow));

    displayCalibrationLEDs(pixelIndex + 1);

    Serial.println("Remove the piece");
    waitForBoardEmpty(100);
  }

  driver_->clearAllLEDs();
  Serial.println("Calibration complete");
  return false;
}

void BoardCalibration::trigger() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - cannot trigger calibration");
    return;
  }
  Preferences prefs;
  prefs.begin("boardCal", false);
  prefs.clear();
  prefs.end();
  Serial.println("Board calibration cleared - rebooting ...");
  ESP.restart();
}

void BoardCalibration::readRawSensors(bool (&rawState)[8][8]) {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      rawState[row][col] = false;

  for (int col = 0; col < NUM_COLS; col++) {
    driver_->enableCol(col);
    for (int row = 0; row < NUM_ROWS; row++)
      rawState[row][col] = (digitalRead(rowPins[row]) == LOW);
  }
  driver_->disableAllCols();
}

bool BoardCalibration::waitForBoardEmpty(unsigned long stableMs) {
  bool rawState[NUM_ROWS][NUM_COLS];
  unsigned long lastWarningTime = millis();
  unsigned long stableStart = 0;

  while (true) {
    readRawSensors(rawState);
    int pressedCount = 0;
    for (int row = 0; row < NUM_ROWS; row++)
      for (int col = 0; col < NUM_COLS; col++)
        if (rawState[row][col])
          pressedCount++;

    if (pressedCount == 0) {
      if (stableStart == 0)
        stableStart = millis();
      if (millis() - stableStart >= stableMs)
        return true;
    } else {
      stableStart = 0;
      unsigned long now = millis();
      if (now - lastWarningTime >= CALIBRATION_WARNING_INTERVAL_MS) {
        lastWarningTime = now;
        Serial.printf("Board not empty - %d sensor(s) still detecting a magnet:\n", pressedCount);
        for (int row = 0; row < NUM_ROWS; row++)
          for (int col = 0; col < NUM_COLS; col++)
            if (rawState[row][col])
              Serial.printf("  GPIO %d + 74HC595 Q%c (pin %d)\n", rowPins[row], shiftRegOutput(col), shiftRegPin(col));
      }
    }
    delay(SENSOR_READ_DELAY_MS);
  }
}

bool BoardCalibration::waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs) {
  bool rawState[NUM_ROWS][NUM_COLS];
  int lastRow = -1;
  int lastCol = -1;
  unsigned long stableStart = 0;
  unsigned long lastWarningTime = millis();

  while (true) {
    readRawSensors(rawState);
    int count = 0;
    int foundRow = -1;
    int foundCol = -1;
    for (int row = 0; row < NUM_ROWS; row++)
      for (int col = 0; col < NUM_COLS; col++)
        if (rawState[row][col]) {
          count++;
          foundRow = row;
          foundCol = col;
        }
    if (count == 1) {
      if (foundRow == lastRow && foundCol == lastCol) {
        if (stableStart == 0) {
          stableStart = millis();
          Serial.printf("  Detect start: GPIO %d + 74HC595 Q%c (pin %d)\n", rowPins[foundRow], shiftRegOutput(foundCol), shiftRegPin(foundCol));
        }
        if (millis() - stableStart >= stableMs) {
          rawRow = foundRow;
          rawCol = foundCol;
          return true;
        }
      } else {
        if (lastRow >= 0 && lastCol >= 0) {
          Serial.println("Sensor reading unstable - detected square changed. Hold piece steady on one square.");
          Serial.printf("  Previous: GPIO %d + 74HC595 Q%c (pin %d), Current: GPIO %d + 74HC595 Q%c (pin %d)\n", rowPins[lastRow], shiftRegOutput(lastCol), shiftRegPin(lastCol), rowPins[foundRow], shiftRegOutput(foundCol), shiftRegPin(foundCol));
        }
        lastRow = foundRow;
        lastCol = foundCol;
        stableStart = 0;
      }
    } else {
      unsigned long now = millis();
      if (now - lastWarningTime >= CALIBRATION_WARNING_INTERVAL_MS) {
        lastWarningTime = now;
        if (count == 0) {
          Serial.println("No sensor detecting a magnet - place a piece on the requested square");
        } else {
          Serial.printf("Multiple sensors (%d) detected simultaneously but need exactly 1:\n", count);
          for (int row = 0; row < NUM_ROWS; row++)
            for (int col = 0; col < NUM_COLS; col++)
              if (rawState[row][col])
                Serial.printf("  GPIO %d + 74HC595 Q%c (pin %d)\n", rowPins[row], shiftRegOutput(col), shiftRegPin(col));
        }
      }
      stableStart = 0;
    }
    delay(SENSOR_READ_DELAY_MS);
  }
}

void BoardCalibration::showCalibrationError() {
  for (int i = 0; i < LED_COUNT; i++)
    driver_->strip.SetPixelColor(i, RgbColor(LedColors::Red.r, LedColors::Red.g, LedColors::Red.b));
  driver_->showLEDs();
  delay(500);
  waitForBoardEmpty();
  driver_->clearAllLEDs();
}

bool BoardCalibration::calibrateAxis(Axis axis, uint8_t* axisPinsOrder, size_t pinCount, bool firstAxisSwapped) {
  if ((NUM_ROWS != NUM_COLS) || (NUM_ROWS != static_cast<int>(pinCount))) {
    Serial.println("Non-square boards not supported for calibration");
    return false;
  }

  Axis detectedAxis = Axis::UNKNOWN;
  int firstRow = -1;
  int firstCol = -1;
  uint8_t counts[NUM_ROWS] = {0};
  for (size_t i = 0; i < pinCount; i++)
    axisPinsOrder[i] = 0xFF;

  int expectedRawPin = -1;
  bool useRow = true;
  if (axis == Axis::COLS)
    for (int i = 0; i < NUM_ROWS; i++)
      if (driver_->toLogicalRow[i] == 7) {
        expectedRawPin = i;
        useRow = !firstAxisSwapped;
        break;
      }

  for (size_t i = 0; i < pinCount; i++) {
    char square[3];
    if (axis == Axis::ROWS) {
      square[0] = 'a';
      square[1] = LibreChess::Game::rankChar(static_cast<int>(i));
    } else {
      square[0] = LibreChess::Game::fileChar(static_cast<int>(i));
      square[1] = '1';
    }
    square[2] = '\0';

    Serial.printf("Place a piece on %s (%s calibration)\n", square, axisToChessRankFile(axis));
    int row = 0;
    int col = 0;
    waitForSingleRawPress(row, col);
    Serial.printf("  Detected: row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", row, rowPins[row], col, shiftRegOutput(col), shiftRegPin(col));

    if (axis == Axis::COLS && expectedRawPin != -1) {
      int actualPin = useRow ? row : col;
      if (actualPin != expectedRawPin) {
        if (useRow)
          Serial.printf("[ERROR] Expected piece on rank 1 = row %d (GPIO %d) but detected on row %d (GPIO %d) which is not rank 1. Place piece on %s.\n", expectedRawPin, rowPins[expectedRawPin], actualPin, rowPins[actualPin], square);
        else
          Serial.printf("[ERROR] Expected piece on rank 1 = col %d (74HC595 Q%c, pin %d) but detected on col %d (74HC595 Q%c, pin %d) which is not rank 1. Place piece on %s.\n", expectedRawPin, shiftRegOutput(expectedRawPin), shiftRegPin(expectedRawPin), actualPin, shiftRegOutput(actualPin), shiftRegPin(actualPin), square);
        showCalibrationError();
        i--;
        continue;
      }
    }

    if (i == 0) {
      firstRow = row;
      firstCol = col;
      Serial.println("Remove the piece");
      waitForBoardEmpty();
      continue;
    }

    if (detectedAxis == Axis::UNKNOWN && i == 1) {
      if (row == firstRow && col != firstCol) {
        detectedAxis = Axis::COLS;
        axisPinsOrder[firstCol] = static_cast<uint8_t>(i - 1);
        counts[firstCol]++;
        Serial.printf("%s calibration using cols %s\n", axisToChessRankFile(axis), axis != detectedAxis ? "(axis swap)" : "(no axis swap)");
      } else if (col == firstCol && row != firstRow) {
        detectedAxis = Axis::ROWS;
        axisPinsOrder[firstRow] = static_cast<uint8_t>(i - 1);
        counts[firstRow]++;
        Serial.printf("%s calibration using rows %s\n", axisToChessRankFile(axis), axis != detectedAxis ? "(axis swap)" : "(no axis swap)");
      } else {
        Serial.printf("\n============== AMBIGUOUS %s CALIBRATION ==============\n", axisToChessRankFile(axis));
        Serial.printf("First press:  row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", firstRow, rowPins[firstRow], firstCol, shiftRegOutput(firstCol), shiftRegPin(firstCol));
        Serial.printf("Second press: row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", row, rowPins[row], col, shiftRegOutput(col), shiftRegPin(col));
        Serial.printf("PROBLEM: %s\n", (row == firstRow && col == firstCol) ? "Both presses detected by the SAME sensor" : "Both row AND column changed between presses");
        Serial.println("==========================================================\n");
        showCalibrationError();
        i = static_cast<size_t>(-1);
        continue;
      }
    }

    if (detectedAxis == Axis::UNKNOWN) {
      Serial.printf("Ambiguous %s calibration (no orientation detected). Retry.\n", axisToChessRankFile(axis));
      showCalibrationError();
      i = static_cast<size_t>(-1);
      continue;
    }

    int pin = (detectedAxis == Axis::ROWS) ? row : col;
    if (counts[pin] > 0) {
      int assignedIndex = axisPinsOrder[pin];
      char assignedRankFile[8];
      if (axis == Axis::ROWS)
        snprintf(assignedRankFile, sizeof(assignedRankFile), "rank %c", LibreChess::Game::rankChar(assignedIndex));
      else
        snprintf(assignedRankFile, sizeof(assignedRankFile), "file %c", LibreChess::Game::fileChar(assignedIndex));
      if (detectedAxis == Axis::ROWS)
        Serial.printf("[ERROR] Row %d (GPIO %d) already has %s assigned. Retry %s.\n", pin, rowPins[pin], assignedRankFile, square);
      else
        Serial.printf("[ERROR] Col %d (74HC595 Q%c, pin %d) already has %s assigned. Retry %s.\n", pin, shiftRegOutput(pin), shiftRegPin(pin), assignedRankFile, square);
      showCalibrationError();
      i--;
      continue;
    }

    axisPinsOrder[pin] = static_cast<uint8_t>(i);
    counts[pin]++;

    Serial.println("Remove the piece");
    waitForBoardEmpty();
  }

  return axis != detectedAxis;
}

const char* BoardCalibration::axisToChessRankFile(Axis axis) const {
  return (axis == Axis::ROWS) ? "Rank" : ((axis == Axis::COLS) ? "File" : "Unknown");
}
