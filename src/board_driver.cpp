#include "board_driver.h"
#include "game.h"
#include "system_utils.h"
#include "led_colors.h"
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>


// 74HC595 shift register pin mapping: bits are sent MSB first, so bit 7 shifts to QH, bit 0 stays at QA
// col 0 -> QA (pin 15), col 1 -> QB (pin 1), ..., col 7 -> QH (pin 7)
static int shiftRegPin(int col) {
  const int pins[] = {15, 1, 2, 3, 4, 5, 6, 7}; // QA=15, QB=1, QC=2, QD=3, QE=4, QF=5, QG=6, QH=7
  return (col >= 0 && col < 8) ? pins[col] : -1;
}
static char shiftRegOutput(int col) {
  return (col >= 0 && col < 8) ? (char)('A' + col) : '?'; // col 0 -> 'A' (QA), col 7 -> 'H' (QH)
}

// Static members for animation queue system
QueueHandle_t BoardDriver::animationQueue = nullptr;
TaskHandle_t BoardDriver::animationTaskHandle = nullptr;
SemaphoreHandle_t BoardDriver::ledMutex = nullptr;
SemaphoreHandle_t BoardDriver::animationDoneSemaphore = nullptr;
BoardDriver* BoardDriver::instance = nullptr;

static constexpr int rowPins[NUM_ROWS] = {ROW_PIN_0, ROW_PIN_1, ROW_PIN_2, ROW_PIN_3, ROW_PIN_4, ROW_PIN_5, ROW_PIN_6, ROW_PIN_7};
// ---------------------------
// LED Strip Col/Row to Pixel index mapping (default)
// ---------------------------
static constexpr int DefaultRowColToLEDindexMap[NUM_ROWS][NUM_COLS] = {
    {0, 1, 2, 3, 4, 5, 6, 7},
    {15, 14, 13, 12, 11, 10, 9, 8},
    {16, 17, 18, 19, 20, 21, 22, 23},
    {31, 30, 29, 28, 27, 26, 25, 24},
    {32, 33, 34, 35, 36, 37, 38, 39},
    {47, 46, 45, 44, 43, 42, 41, 40},
    {48, 49, 50, 51, 52, 53, 54, 55},
    {63, 62, 61, 60, 59, 58, 57, 56},
};

BoardDriver::BoardDriver() : strip(LED_COUNT, LED_PIN), lastEnabledCol(-2), brightness(BRIGHTNESS), dimMultiplier(70), swapAxes(0), calibrationLoaded(false) {
  for (int i = 0; i < NUM_ROWS; i++)
    toLogicalRow[i] = i;
  for (int i = 0; i < NUM_COLS; i++)
    toLogicalCol[i] = i;
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++) {
      ledIndexMap[row][col] = DefaultRowColToLEDindexMap[row][col];
      currentColors[row][col] = LedColors::Off;
    }
}

void BoardDriver::begin() {
  // Initialize NeoPixel strip
  strip.Begin();
  showLEDs();        // turn off all LEDs
  loadLedSettings(); // Load LED settings from NVS (brightness, dim multiplier)
  strip.SetLuminance(brightness);
  // Shift register pins as outputs
  pinMode(SR_SER_DATA_PIN, OUTPUT);
  pinMode(SR_CLK_PIN, OUTPUT);
  pinMode(SR_LATCH_PIN, OUTPUT);
  disableAllCols();
  // Row pins as inputs
  for (int c = 0; c < NUM_ROWS; c++)
    pinMode(rowPins[c], INPUT);
  // Initialize sensor arrays
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++) {
      sensorState[row][col] = false;
      sensorPrev[row][col] = false;
      sensorRaw[row][col] = false;
      sensorDebounceTime[row][col] = 0;
    }

  // Initialize animation queue system
  instance = this;
  ledMutex = xSemaphoreCreateMutex();
  animationDoneSemaphore = xSemaphoreCreateBinary();
  animationQueue = xQueueCreate(8, sizeof(AnimationJob));
  xTaskCreatePinnedToCore(animationWorkerTask, "AnimWorker", 4096, nullptr, 1, &animationTaskHandle, 1);

  // Load calibration or run first-time calibration
  if (!loadCalibration()) {
    bool wasSkipped = runCalibration();
    if (!wasSkipped) {
      saveCalibration();
    }
  }
}

// Animation worker task - processes jobs from queue
void BoardDriver::animationWorkerTask(void* param) {
  AnimationJob job;
  while (true) {
    if (xQueueReceive(animationQueue, &job, portMAX_DELAY) == pdTRUE) {
      xSemaphoreTake(ledMutex, portMAX_DELAY);
      instance->executeAnimation(job);
      xSemaphoreGive(ledMutex);
      // Signal completion for cancellable/sync animations (after mutex release)
      if (job.type == AnimationType::THINKING || job.type == AnimationType::WAITING || job.type == AnimationType::SYNC)
        xSemaphoreGive(animationDoneSemaphore);
    }
  }
}

void BoardDriver::executeAnimation(const AnimationJob& job) {
  switch (job.type) {
    case AnimationType::CAPTURE:
      doCapture(job.params.capture.row, job.params.capture.col);
      break;
    case AnimationType::PROMOTION:
      doPromotion(job.params.promotion.col);
      break;
    case AnimationType::BLINK:
      doBlink(job.params.blink.row, job.params.blink.col, job.params.blink.color, job.params.blink.times, job.params.blink.clearAfter, job.params.blink.clearBefore);
      break;
    case AnimationType::WAITING:
      doWaiting(job.stopFlag);
      break;
    case AnimationType::THINKING:
      doThinking(job.stopFlag);
      break;
    case AnimationType::FIREWORK:
      doFirework(job.params.firework.color);
      break;
    case AnimationType::FLASH:
      doFlash(job.params.flash.color, job.params.flash.times);
      break;
    case AnimationType::SYNC:
      break; // No-op — worker signals completion in animationWorkerTask
  }
}

bool BoardDriver::loadCalibration() {
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

  // Verify pin configuration matches
  size_t rowPinsLen = prefs.getBytesLength("rowPins");
  if (rowPinsLen != NUM_ROWS) {
    prefs.end();
    return false;
  }
  uint8_t savedRowPins[NUM_ROWS];
  prefs.getBytes("rowPins", savedRowPins, sizeof(savedRowPins));
  for (int i = 0; i < NUM_ROWS; i++)
    if (savedRowPins[i] != (uint8_t)rowPins[i]) {
      prefs.end();
      return false;
    }
  uint8_t savedSRPins[3];
  prefs.getBytes("srPins", savedSRPins, sizeof(savedSRPins));
  if (savedSRPins[0] != (uint8_t)SR_CLK_PIN || savedSRPins[1] != (uint8_t)SR_LATCH_PIN || savedSRPins[2] != (uint8_t)SR_SER_DATA_PIN) {
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
  swapAxes = prefs.getUChar("swap", 0);
  prefs.getBytes("row", toLogicalRow, NUM_ROWS);
  prefs.getBytes("col", toLogicalCol, NUM_COLS);
  uint8_t ledFlat[LED_COUNT];
  prefs.getBytes("led", ledFlat, LED_COUNT);
  int idx = 0;
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      ledIndexMap[row][col] = ledFlat[idx++];
  prefs.end();
  calibrationLoaded = true;
  Serial.println("Board calibration loaded from NVS");
  return true;
}

void BoardDriver::saveCalibration() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - calibration not saved");
    return;
  }
  Preferences prefs;
  prefs.begin("boardCal", false);
  prefs.putUChar("ver", 1);
  uint8_t rowPinsU8[NUM_ROWS];
  for (int i = 0; i < NUM_ROWS; i++)
    rowPinsU8[i] = (uint8_t)rowPins[i];
  prefs.putBytes("rowPins", rowPinsU8, sizeof(rowPinsU8));
  uint8_t srPins[3] = {(uint8_t)SR_CLK_PIN, (uint8_t)SR_LATCH_PIN, (uint8_t)SR_SER_DATA_PIN};
  prefs.putBytes("srPins", srPins, sizeof(srPins));
  prefs.putUChar("swap", swapAxes);
  prefs.putBytes("row", toLogicalRow, NUM_ROWS);
  prefs.putBytes("col", toLogicalCol, NUM_COLS);
  uint8_t ledFlat[LED_COUNT];
  int idx = 0;
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      ledFlat[idx++] = ledIndexMap[row][col];
  prefs.putBytes("led", ledFlat, LED_COUNT);
  prefs.end();
  calibrationLoaded = true;
  Serial.println("Board calibration saved to NVS");
}

void BoardDriver::readRawSensors(bool rawState[NUM_ROWS][NUM_COLS]) {
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

bool BoardDriver::waitForBoardEmpty(unsigned long stableMs) {
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

bool BoardDriver::waitForSingleRawPress(int& rawRow, int& rawCol, unsigned long stableMs) {
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
        // Position changed - warn about unstable detection
        if (lastRow >= 0 && lastCol >= 0) {
          Serial.println("Sensor reading unstable - detected square changed. Hold piece steady on one square.");
          Serial.printf("  Previous: GPIO %d + 74HC595 Q%c (pin %d), Current: GPIO %d + 74HC595 Q%c (pin %d)\n", rowPins[lastRow], shiftRegOutput(lastCol), shiftRegPin(lastCol), rowPins[foundRow], shiftRegOutput(foundCol), shiftRegPin(foundCol));
        }
        lastRow = foundRow;
        lastCol = foundCol;
        stableStart = 0;
      }
    } else {
      // Multiple presses or no presses detected
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

void BoardDriver::showCalibrationError() {
  for (int i = 0; i < LED_COUNT; i++)
    strip.SetPixelColor(i, RgbColor(LedColors::Red.r, LedColors::Red.g, LedColors::Red.b));
  showLEDs();
  delay(500);
  waitForBoardEmpty();
  clearAllLEDs();
}

bool BoardDriver::calibrateAxis(Axis axis, uint8_t* axisPinsOrder, size_t NUM_PINS, bool firstAxisSwapped) {
  if ((NUM_ROWS != NUM_COLS) || (NUM_ROWS != NUM_PINS)) {
    Serial.println("Non-square boards not supported for calibration");
    return false;
  }

  Axis detectedAxis = Axis::UNKNOWN;
  int firstRow = -1;
  int firstCol = -1;
  uint8_t counts[NUM_PINS] = {0};
  for (int i = 0; i < NUM_PINS; i++)
    axisPinsOrder[i] = -1;

  // If this is column calibration and we already have row mapping, find expected raw row/col for rank 1
  int expectedRawPin = -1;
  bool useRow = true; // Whether to check row or col during column calibration
  if (axis == Axis::COLS)
    for (int i = 0; i < NUM_ROWS; i++)
      if (toLogicalRow[i] == 7) {
        expectedRawPin = i;
        // If first axis swapped, toLogicalRow is indexed by raw cols, so we check col
        useRow = !firstAxisSwapped;
        break;
      }

  for (int i = 0; i < NUM_PINS; i++) {
    char square[3];
    if (axis == Axis::ROWS) {
      square[0] = 'a';
      square[1] = Game::rankChar(i);
    } else {
      square[0] = Game::fileChar(i);
      square[1] = '1';
    }
    square[2] = '\0';

    Serial.printf("Place a piece on %s (%s calibration)\n", square, axisToChessRankFile(axis).c_str());
    int row = 0;
    int col = 0;
    waitForSingleRawPress(row, col);
    Serial.printf("  Detected: row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", row, rowPins[row], col, shiftRegOutput(col), shiftRegPin(col));

    // Verify pin consistency for column calibration
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
        axisPinsOrder[firstCol] = i - 1;
        counts[firstCol]++;
        Serial.printf("%s calibration using cols %s\n", axisToChessRankFile(axis).c_str(), axis != detectedAxis ? "(axis swap)" : "(no axis swap)");
      } else if (col == firstCol && row != firstRow) {
        detectedAxis = Axis::ROWS;
        axisPinsOrder[firstRow] = i - 1;
        counts[firstRow]++;
        Serial.printf("%s calibration using rows %s\n", axisToChessRankFile(axis).c_str(), axis != detectedAxis ? "(axis swap)" : "(no axis swap)");
      } else {
        Serial.printf("\n============== AMBIGUOUS %s CALIBRATION ==============\n", axisToChessRankFile(axis).c_str());
        Serial.printf("First press:  row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", firstRow, rowPins[firstRow], firstCol, shiftRegOutput(firstCol), shiftRegPin(firstCol));
        Serial.printf("Second press: row=%d (GPIO %d), col=%d (74HC595 Q%c, pin %d)\n", row, rowPins[row], col, shiftRegOutput(col), shiftRegPin(col));
        Serial.printf("PROBLEM: %s\n", (row == firstRow && col == firstCol) ? "Both presses detected by the SAME sensor" : "Both row AND column changed between presses");
        Serial.println("==========================================================\n");
        showCalibrationError();
        i = -1;
        continue;
      }
    }

    if (detectedAxis == Axis::UNKNOWN) {
      // Will never happen due to above logic, but just in case
      Serial.printf("Ambiguous %s calibration (no orientation detected). Retry.\n", axisToChessRankFile(axis).c_str());
      showCalibrationError();
      i = -1;
      continue;
    }

    int pin = (detectedAxis == Axis::ROWS) ? row : col;
    if (counts[pin] > 0) {
      // Find what rank/file was already assigned to this pin
      int assignedIndex = axisPinsOrder[pin];
      char assignedRankFile[8];
      if (axis == Axis::ROWS)
        snprintf(assignedRankFile, sizeof(assignedRankFile), "rank %c", Game::rankChar(assignedIndex));
      else
        snprintf(assignedRankFile, sizeof(assignedRankFile), "file %c", Game::fileChar(assignedIndex));
      if (detectedAxis == Axis::ROWS)
        Serial.printf("[ERROR] Row %d (GPIO %d) already has %s assigned. Retry %s.\n", pin, rowPins[pin], assignedRankFile, square);
      else
        Serial.printf("[ERROR] Col %d (74HC595 Q%c, pin %d) already has %s assigned. Retry %s.\n", pin, shiftRegOutput(pin), shiftRegPin(pin), assignedRankFile, square);
      showCalibrationError();
      i--;
      continue;
    }

    axisPinsOrder[pin] = i;
    counts[pin]++;

    Serial.println("Remove the piece");
    waitForBoardEmpty();
  }

  return axis != detectedAxis;
}

bool BoardDriver::runCalibration() {
  // Calibration animation - light up each pixel sequentially
  for (int i = 0; i < LED_COUNT; i++) {
    strip.SetPixelColor(i, RgbColor(LedColors::White.r, LedColors::White.g, LedColors::White.b));
    showLEDs();
    delay(50);
  }
  delay(500);
  clearAllLEDs();

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
        Serial.println("[SKIP] Calibration skipped - using default mapping");
        Serial.println("[SKIP] Sensors/LEDs will NOT work correctly!");
        Serial.println("[SKIP] You will be asked to calibrate again on next reboot");
        // Set up identity mapping (no calibration)
        swapAxes = 0;
        for (int i = 0; i < NUM_ROWS; i++) toLogicalRow[i] = i;
        for (int i = 0; i < NUM_COLS; i++) toLogicalCol[i] = i;
        for (int row = 0; row < NUM_ROWS; row++)
          for (int col = 0; col < NUM_COLS; col++)
            ledIndexMap[row][col] = row * NUM_COLS + col;
        calibrationLoaded = true;
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

  bool swapAxes1 = calibrateAxis(Axis::ROWS, toLogicalRow, NUM_ROWS, false);
  bool swapAxes2 = calibrateAxis(Axis::COLS, toLogicalCol, NUM_COLS, swapAxes1);
  if (swapAxes1 != swapAxes2) {
    Serial.println("Inconsistent axis orientation detected during calibration. Restarting calibration.");
    showCalibrationError();
    return runCalibration();
  }
  swapAxes = swapAxes1 ? 1 : 0;

  // LED mapping calibration
  Serial.println("LED mapping calibration:");

  bool logicalUsed[NUM_ROWS][NUM_COLS] = {false};

  auto displayCalibrationLEDs = [&](int currentPixel) {
    for (int i = 0; i < LED_COUNT; i++)
      strip.SetPixelColor(i, RgbColor(0));
    for (int r = 0; r < NUM_ROWS; r++)
      for (int c = 0; c < NUM_COLS; c++)
        if (logicalUsed[r][c])
          strip.SetPixelColor(ledIndexMap[r][c], RgbColor(LedColors::Green.r, LedColors::Green.g, LedColors::Green.b));
    if (currentPixel < LED_COUNT)
      strip.SetPixelColor(currentPixel, RgbColor(LedColors::White.r, LedColors::White.g, LedColors::White.b));
    showLEDs();
  };

  for (int pixelIndex = 0; pixelIndex < LED_COUNT; pixelIndex++) {
    int row = 0;
    int col = 0;
    displayCalibrationLEDs(pixelIndex);
    Serial.println("Place a piece on the white LED");
    waitForSingleRawPress(row, col);

    uint8_t logicalRow = toLogicalRow[swapAxes ? col : row];
    uint8_t logicalCol = toLogicalCol[swapAxes ? row : col];
    if (logicalUsed[logicalRow][logicalCol]) {
      Serial.printf("Duplicate square %c%c detected. Retry LED %d.\n", (char)('a' + logicalCol), (char)('8' - logicalRow), pixelIndex);
      showCalibrationError();
      pixelIndex--;
      continue;
    }
    logicalUsed[logicalRow][logicalCol] = true;
    ledIndexMap[logicalRow][logicalCol] = pixelIndex;
    Serial.printf("  LED %d -> %c%c\n", pixelIndex, (char)('a' + logicalCol), (char)('8' - logicalRow));

    displayCalibrationLEDs(pixelIndex + 1);

    Serial.println("Remove the piece");
    waitForBoardEmpty(100);
  }

  clearAllLEDs();
  Serial.println("Calibration complete");
  return false;
}

void BoardDriver::loadShiftRegister(byte data, int bits) {
#if defined(SR_INVERT_OUTPUTS) && SR_INVERT_OUTPUTS != 0
  data = ~data;
#endif
  // Make sure latch is low before shifting data
  digitalWrite(SR_LATCH_PIN, LOW);
  // Shift bits MSB first
  for (int i = bits - 1; i >= 0; i--) {
    digitalWrite(SR_SER_DATA_PIN, !!(data & (1 << i)));
    delayMicroseconds(10);
    digitalWrite(SR_CLK_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(SR_CLK_PIN, LOW);
    delayMicroseconds(10);
  }
  // Latch the data to output pins
  digitalWrite(SR_LATCH_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(SR_LATCH_PIN, LOW);
}

void BoardDriver::disableAllCols() {
  if (lastEnabledCol == 7) {
    // Sequential wrap-around: shift in a single 0 to push the 1 out of QH
    loadShiftRegister(0x00, 1);
  } else {
    // Non-sequential or startup: load full byte of zeros
    loadShiftRegister(0);
  }
  lastEnabledCol = -1; // Make next enableCol(0) call use optimized 1-bit shift because register is now all zeros
}

void BoardDriver::enableCol(int col) {
  if (col == lastEnabledCol + 1) {
    if (col == 0)
      loadShiftRegister(0x01, 1); // Sequential wrap-around: register should be all zeros already, shift in a single 1 bit into QA
    else
      loadShiftRegister(0x00, 1); // Sequential access: shift in a single 0 bit to move the 1 we shifted earlier to the next column position (towards QH)
  } else {
    // Due to above logic, this condition should never occur, but just in case...
    loadShiftRegister((byte)(1 << col));
  }
  lastEnabledCol = col;
  delayMicroseconds(100); // Allow time for the column to stabilize, otherwise random readings might occur
}

void BoardDriver::readSensors() {
  unsigned long currentTime = millis();

  for (int col = 0; col < NUM_COLS; col++) {
    enableCol(col);
    for (int row = 0; row < NUM_ROWS; row++) {
      bool newReading = digitalRead(rowPins[row]) == LOW;
      uint8_t logicalRow = toLogicalRow[swapAxes ? col : row];
      uint8_t logicalCol = toLogicalCol[swapAxes ? row : col];
      // Debounce logic
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

bool BoardDriver::getSensorState(int row, int col) {
  return sensorState[row][col];
}

bool BoardDriver::getSensorPrev(int row, int col) {
  return sensorPrev[row][col];
}

void BoardDriver::updateSensorPrev() {
  for (int row = 0; row < NUM_ROWS; row++)
    for (int col = 0; col < NUM_COLS; col++)
      sensorPrev[row][col] = sensorState[row][col];
}

int BoardDriver::getPixelIndex(int row, int col) {
  return ledIndexMap[row][col];
}

void BoardDriver::acquireLEDs() {
  xSemaphoreTake(ledMutex, portMAX_DELAY);
}

void BoardDriver::releaseLEDs() {
  xSemaphoreGive(ledMutex);
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
  currentColors[row][col] = color; // Track the intended color
  float multiplier = 1.0f;
  if ((row + col) % 2 == 1)
    multiplier = dimMultiplier / 100.0f; // Dim dark squares based on user setting
  strip.SetPixelColor(getPixelIndex(row, col), RgbColor(color.r * multiplier, color.g * multiplier, color.b * multiplier));
}

void BoardDriver::showLEDs() {
  strip.Show();
}

void BoardDriver::showConnectingAnimation() {
  LedGuard guard(this);
  for (int i = 0; i < 8; i++) {
    setSquareLED(3, i, LedColors::Blue);
    setSquareLED(4, i, LedColors::Blue);
    showLEDs();
    delay(100);
  }
  clearAllLEDs();
}

void BoardDriver::blinkSquare(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  AnimationJob job = {AnimationType::BLINK, nullptr, {}};
  job.params.blink = {row, col, color, times, clearAfter, clearBefore};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
}

void BoardDriver::doBlink(int row, int col, LedRGB color, int times, bool clearAfter, bool clearBefore) {
  if (clearBefore)
    clearAllLEDs(false);
  for (int i = 0; i < times; i++) {
    setSquareLED(row, col, color);
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    setSquareLED(row, col, LedColors::Off);
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  if (!clearAfter) {
    setSquareLED(row, col, color);
    showLEDs();
  }
}

void BoardDriver::fireworkAnimation(LedRGB color) {
  AnimationJob job = {AnimationType::FIREWORK, nullptr, {}};
  job.params.firework = {color};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
}

void BoardDriver::doFirework(LedRGB color) {
  clearAllLEDs(false);
  float centerX = 3.5;
  float centerY = 3.5;

  // Contraction phase:
  for (float radius = 6; radius > 0; radius -= 0.5) {
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        if (fabs(dist - radius) < 0.5)
          setSquareLED(row, col, color);
        else
          setSquareLED(row, col, LedColors::Off);
      }
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  // Expansion phase:
  for (float radius = 0; radius < 6; radius += 0.5) {
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);
        if (fabs(dist - radius) < 0.5)
          setSquareLED(row, col, color);
        else
          setSquareLED(row, col, LedColors::Off);
      }
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  clearAllLEDs();
}

void BoardDriver::captureAnimation(int row, int col) {
  AnimationJob job = {AnimationType::CAPTURE, nullptr, {}};
  job.params.capture = {row, col};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
}

void BoardDriver::doCapture(int centerRow, int centerCol) {
  float centerX = centerCol + 0.5f;
  float centerY = centerRow + 0.5f;

  // Wave animation with multiple expanding rings in 2 colors
  const int numWaves = 3;       // Number of concurrent wave rings
  const int totalFrames = 20;   // Total animation frames
  const float waveSpeed = 0.4f; // How fast waves expand per frame
  const float waveWidth = 1.2f; // Thickness of each wave ring

  clearAllLEDs(false);
  for (int frame = 0; frame < totalFrames; frame++) {
    for (int row = 0; row < 8; row++) {
      for (int col = 0; col < 8; col++) {
        float dx = col - centerX;
        float dy = row - centerY;
        float dist = sqrt(dx * dx + dy * dy);

        uint8_t finalR = 0, finalG = 0, finalB = 0;

        // Check each wave ring
        for (int w = 0; w < numWaves; w++) {
          // Stagger wave starts so they trail each other
          float waveRadius = (frame - w * 4) * waveSpeed;
          if (waveRadius < 0) continue;

          // Distance from this pixel to the wave ring
          float distToWave = fabs(dist - waveRadius);

          if (distToWave < waveWidth) {
            // Intensity based on how close to wave center (smooth falloff)
            float intensity = 1.0f - (distToWave / waveWidth);
            intensity = intensity * intensity; // Quadratic falloff for smoother look

            // Fade out as wave expands
            float fadeOut = 1.0f - (waveRadius / 6.0f);
            if (fadeOut < 0) fadeOut = 0;
            intensity *= fadeOut;

            // Alternate colors between waves
            if (w % 2 == 0) {
              finalR = max(finalR, (uint8_t)(LedColors::Red.r * intensity));
              finalG = max(finalG, (uint8_t)(LedColors::Red.g * intensity));
              finalB = max(finalB, (uint8_t)(LedColors::Red.b * intensity));
            } else {
              finalR = max(finalR, (uint8_t)(LedColors::Yellow.r * intensity));
              finalG = max(finalG, (uint8_t)(LedColors::Yellow.g * intensity));
              finalB = max(finalB, (uint8_t)(LedColors::Yellow.b * intensity));
            }
          }
        }
        setSquareLED(row, col, LedRGB{finalR, finalG, finalB});
      }
    }
    setSquareLED(centerRow, centerCol, LedColors::Red);
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  clearAllLEDs();
}

void BoardDriver::promotionAnimation(int col) {
  AnimationJob job = {AnimationType::PROMOTION, nullptr, {}};
  job.params.promotion.col = col;
  xQueueSend(animationQueue, &job, portMAX_DELAY);
}

void BoardDriver::doPromotion(int col) {
  clearAllLEDs(false);
  // Column-based waterfall animation
  for (int step = 0; step < 16; step++) {
    for (int row = 0; row < 8; row++) {
      // Create a golden wave moving up and down the column
      if ((step + row) % 8 < 4)
        setSquareLED(row, col, LedColors::Yellow);
      else
        setSquareLED(row, col, LedColors::Off);
    }
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  clearAllLEDs();
}

void BoardDriver::flashBoardAnimation(LedRGB color, int times) {
  AnimationJob job = {AnimationType::FLASH, nullptr, {}};
  job.params.flash = {color, times};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
}

void BoardDriver::doFlash(LedRGB color, int times) {
  for (int i = 0; i < times; i++) {
    clearAllLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
    // Light up entire board with specified color
    for (int row = 0; row < 8; row++)
      for (int col = 0; col < 8; col++)
        setSquareLED(row, col, color);
    showLEDs();
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  clearAllLEDs();
}

std::atomic<bool>* BoardDriver::startThinkingAnimation() {
  auto* stopFlag = new std::atomic<bool>(false);
  AnimationJob job = {AnimationType::THINKING, stopFlag, {}};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
  return stopFlag;
}

void BoardDriver::doThinking(std::atomic<bool>* stopFlag) {
  static const int corners[][2] = {{0, 0}, {0, 7}, {7, 0}, {7, 7}};

  // Color configuration - center hue shown at peak brightness
  static const float HUE_CENTER = 240.0f; // Blue hue
  static const float HUE_RANGE = 10.0f;   // Shift toward purple when dim
  static const float BRIGHTNESS_MIN = 0.08f;
  static const float BRIGHTNESS_MAX = 1.0f;

  float phase = 0.0f;            // 0 to 2*PI for smooth sine wave
  const float phaseStep = 0.04f; // Controls breathing speed

  clearAllLEDs(false);
  while (!stopFlag || !stopFlag->load()) {
    // Smooth sine wave for breathing (0 to 1)
    float breathe = (sinf(phase) + 1.0f) * 0.5f;

    // Brightness follows the breathing curve
    float brightness = BRIGHTNESS_MIN + breathe * (BRIGHTNESS_MAX - BRIGHTNESS_MIN);

    // Hue synced: center hue at peak, shifts toward purple as it dims
    // Using cosine so hue is at center when brightness peaks
    float hue = HUE_CENTER + HUE_RANGE * (1.0f - breathe);

    // HSV to RGB conversion (saturation = 1.0)
    float h = fmod(hue, 360.0f) / 60.0f;
    int hi = (int)h;
    float f = h - hi;
    float v = brightness;
    float q = v * (1.0f - f);
    float t = v * f;

    uint8_t r = 0, g = 0, b = 0;
    switch (hi) {
      case 0:
        r = v * 255;
        g = t * 255;
        b = 0;
        break;
      case 1:
        r = q * 255;
        g = v * 255;
        b = 0;
        break;
      case 2:
        r = 0;
        g = v * 255;
        b = t * 255;
        break;
      case 3:
        r = 0;
        g = q * 255;
        b = v * 255;
        break;
      case 4:
        r = t * 255;
        g = 0;
        b = v * 255;
        break;
      default:
        r = v * 255;
        g = 0;
        b = q * 255;
        break;
    }

    for (auto& corner : corners)
      setSquareLED(corner[0], corner[1], LedRGB{r, g, b});
    showLEDs();

    phase += phaseStep;
    if (phase >= 2.0f * M_PI)
      phase -= 2.0f * M_PI;

    vTaskDelay(pdMS_TO_TICKS(30));
  }
  clearAllLEDs();
}

void BoardDriver::stopAndWaitForAnimation(std::atomic<bool>*& stopFlag) {
  if (!stopFlag) return;
  stopFlag->store(true);
  // Block until the animation worker finishes and signals completion.
  // The semaphore is given after the worker releases the LED mutex, so the caller is
  // guaranteed that both the animation and its cleanup (clearAllLEDs) are done.
  xSemaphoreTake(animationDoneSemaphore, portMAX_DELAY);
  delete stopFlag; // Caller owns the flag — safe to free now that the worker is done with it
  stopFlag = nullptr;
}

void BoardDriver::waitForAnimationQueueDrain() {
  // Enqueue a no-op SYNC job. When the worker reaches it, all preceding animations
  // have finished. The worker signals the completion semaphore after processing SYNC.
  AnimationJob job = {AnimationType::SYNC, nullptr, {}};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
  xSemaphoreTake(animationDoneSemaphore, portMAX_DELAY);
}

std::atomic<bool>* BoardDriver::startWaitingAnimation() {
  auto* stopFlag = new std::atomic<bool>(false);
  AnimationJob job = {AnimationType::WAITING, stopFlag, {}};
  xQueueSend(animationQueue, &job, portMAX_DELAY);
  return stopFlag;
}

void BoardDriver::doWaiting(std::atomic<bool>* stopFlag) {
  static const int positions[][2] = {{0, 0}, {0, 1}, {0, 2}, {0, 3}, {0, 4}, {0, 5}, {0, 6}, {0, 7}, {1, 7}, {2, 7}, {3, 7}, {4, 7}, {5, 7}, {6, 7}, {7, 7}, {7, 6}, {7, 5}, {7, 4}, {7, 3}, {7, 2}, {7, 1}, {7, 0}, {6, 0}, {5, 0}, {4, 0}, {3, 0}, {2, 0}, {1, 0}};
  static const int numPositions = sizeof(positions) / sizeof(positions[0]);

  int frame = 0;
  while (!stopFlag || !stopFlag->load()) {
    clearAllLEDs(false);
    // Light up consecutive LEDs moving around the board
    for (int i = 0; i < 4; i++) {
      for (int j = 0; j < 2; j++) {
        int idx = (frame + i + (j * 14)) % numPositions;
        setSquareLED(positions[idx][0], positions[idx][1], LedColors::White);
      }
    }
    showLEDs();
    frame = (frame + 1) % numPositions;
    vTaskDelay(pdMS_TO_TICKS(200));
  }
  clearAllLEDs();
}

// LED settings methods
void BoardDriver::setBrightness(uint8_t value) {
  LedGuard guard(this);
  brightness = value > 255 ? 255 : (value < 10 ? 10 : value);
  strip.SetLuminance(brightness);
  showLEDs();
}

void BoardDriver::setDimMultiplier(uint8_t value) {
  LedGuard guard(this);
  dimMultiplier = value > 100 ? 100 : (value < 20 ? 20 : value);
  // Re-apply all current colors with new dim multiplier
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
  prefs.begin("ledSettings", true);
  brightness = prefs.getUChar("brightness", BRIGHTNESS);
  dimMultiplier = prefs.getUChar("dimMult", 70);
  prefs.end();
  Serial.printf("LED settings loaded: brightness=%d, dimMultiplier=%d\n", brightness, dimMultiplier);
}

void BoardDriver::saveLedSettings() {
  if (!SystemUtils::ensureNvsInitialized()) {
    Serial.println("NVS init failed - LED settings not saved");
    return;
  }
  Preferences prefs;
  prefs.begin("ledSettings", false);
  prefs.putUChar("brightness", brightness);
  prefs.putUChar("dimMult", dimMultiplier);
  prefs.end();
  Serial.printf("LED settings saved: brightness=%d, dimMultiplier=%d\n", brightness, dimMultiplier);
}

void BoardDriver::triggerCalibration() {
  // Mark calibration as needed by clearing the saved calibration
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
