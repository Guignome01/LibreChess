#include "board_driver.h"
#include "game_mode/player_mode.h"
#include "game_mode/bot_mode.h"
#include "engine/stockfish/stockfish_provider.h"
#include "engine/lichess/lichess_provider.h"
#include "engine/lichess/lichess_config.h"
#include "engine/librechess/librechess_provider.h"
#include "game.h"
#include "littlefs_storage.h"
#include "serial_logger.h"
#include "system_utils.h"
#include "led_colors.h"
#include "menu_config.h"
#include "sensor_test.h"
#ifdef FACTORY_RESET
#include <nvs_flash.h>
#endif
#include "wifi_manager_esp32.h"
#include <LittleFS.h>
#include <time.h>

// ---------------------------
// Game State and Configuration
// ---------------------------

enum class AppMode {
  SELECTION = 0,
  CHESS_MOVES = 1,
  BOT = 2,
  LICHESS = 3,
  SENSOR_TEST = 4
};

int botDifficultyLevel = 4;  // 1-8 difficulty level
char playerColor = 'w';
String botEngine = "stockfish";
LichessConfig lichessConfig = {""};

BoardDriver boardDriver;
SerialLogger logger;
LittleFSStorage storage(&logger);
WiFiManagerESP32 wifiManager(&boardDriver, &storage);
Game chess(&storage, &wifiManager, &logger);
GameMode* activeGame = nullptr;
SensorTest* sensorTest = nullptr;

AppMode currentMode = AppMode::SELECTION;
bool modeInitialized = false;
bool resumingGame = false;

void enterGameSelection();
void handleMenuResult(int result);
void initializeSelectedMode(AppMode mode);
void checkForResumableGame();

void setup() {
  Serial.begin(115200);
  delay(3000);
  Serial.println();
  Serial.println("================================================");
  Serial.println("         LibreChess Starting Up");
  Serial.println("================================================");
  if (!SystemUtils::ensureNvsInitialized())
    Serial.println("WARNING: NVS init failed (Preferences may not work)");

#ifdef FACTORY_RESET
  // Wipe all NVS data — add -DFACTORY_RESET to build_flags in platformio.ini,
  // flash once via USB, then remove the flag. Clears WiFi credentials, OTA
  // password, Lichess token, calibration, LED settings, and all other persisted
  // state. Requires physical USB access — cannot be triggered from the web UI.
  nvs_flash_erase();
  nvs_flash_init();
  Serial.println("*** FACTORY RESET: all NVS data erased ***");
#endif

  if (!LittleFS.begin(true))
    Serial.println("ERROR: LittleFS mount failed!");
  else
    Serial.println("LittleFS mounted successfully");
  storage.initialize();
  boardDriver.begin();
  wifiManager.setGameRef(&chess);
  wifiManager.begin();
  Serial.println();

  // Configure menu system
  initMenus(&boardDriver);

  // Kick off NTP time sync (non-blocking, will resolve in background)
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  // Check for a live game that can be resumed
  checkForResumableGame();
  if (currentMode != AppMode::SELECTION)
    return; // Resuming a game — skip showing game selection

  enterGameSelection();
}

void checkForResumableGame() {
  uint8_t resumePlayerColor = 0;
  uint8_t metaRaw[GAME_META_SIZE] = {};
  if (!chess.hasActiveGame() || !chess.getActiveGameInfo(resumePlayerColor, metaRaw))
    return;

  GameMeta meta = readMeta(metaRaw);
  GameModeId resumeMode = static_cast<GameModeId>(meta.mode);

  Serial.println("========== Live game found on flash ==========");

  LedRGB indicatorColor = LedColors::White;
  const char* modeName = "Unknown";
  bool flipped = false;

  switch (resumeMode) {
    case GameModeId::PLAYER:
      indicatorColor = LedColors::Blue;
      modeName = "Chess Moves";
      break;
    case GameModeId::BOT:
      indicatorColor = LedColors::Green;
      modeName = "Bot";
      flipped = (resumePlayerColor == 'b');
      Serial.printf("  Mode: Bot (player=%c, difficulty=%d, engineId=%d)\n",
                     (char)resumePlayerColor, meta.difficulty, meta.engineId);
      break;
    case GameModeId::LICHESS:
      Serial.println("  Lichess game found — cannot resume locally, discarding");
      chess.discardRecording();
      Serial.println("================================================");
      return;
    default:
      Serial.println("Unknown live game mode, discarding");
      chess.discardRecording();
      Serial.println("================================================");
      return;
  }

  Serial.printf("  Found: %s game — confirm resume?\n", modeName);
  Serial.println("  Green = Resume, Red = Discard");
  boardDriver.blinkSquare(3, 3, indicatorColor, 2);
  boardDriver.waitForAnimationQueueDrain();

  if (boardConfirm(&boardDriver, flipped)) {
    Serial.println("  -> Player chose to RESUME");
    switch (resumeMode) {
      case GameModeId::PLAYER:
        currentMode = AppMode::CHESS_MOVES;
        resumingGame = true;
        break;
      case GameModeId::BOT:
        currentMode = AppMode::BOT;
        resumingGame = true;
        playerColor = (char)resumePlayerColor;
        botDifficultyLevel = meta.difficulty;  // 1-8 level stored in game header
        botEngine = (meta.engineId == LibreChessProvider::ENGINE_ID) ? "librechess" : "stockfish";
        break;
    }
  } else {
    Serial.println("  -> Player chose to DISCARD");
    chess.discardRecording();
  }

  Serial.println("================================================");
}

void loop() {
  // WiFi reconnection state machine
  wifiManager.update();

  // Check for pending board edits from WiFi (FEN-based)
  String editFen;
  if (wifiManager.getPendingBoardEdit(editFen)) {
    Serial.println("Applying board edit from WiFi interface...");

    if (activeGame != nullptr && modeInitialized) {
      activeGame->setBoardStateFromFEN(std::string(editFen.c_str()));
      Serial.println("Board edit applied");
    } else {
      Serial.println("Warning: Board edit received but no active game mode");
    }

    wifiManager.clearPendingEdit();
  }

  // Check for WiFi game selection
  int selectedMode = wifiManager.getSelectedGameMode();
  if (selectedMode > 0) {
    Serial.printf("WiFi game selection detected: %d\n", selectedMode);
    switch (selectedMode) {
      case 1:
        currentMode = AppMode::CHESS_MOVES;
        break;
      case 2:
        currentMode = AppMode::BOT;
        botDifficultyLevel = wifiManager.getBotDifficultyLevel();
        playerColor = wifiManager.getBotPlayerColor();
        botEngine = wifiManager.getBotEngine();
        break;
      case 3:
        currentMode = AppMode::LICHESS;
        lichessConfig = wifiManager.getLichessConfig();
        break;
      case 4:
        currentMode = AppMode::SENSOR_TEST;
        break;
      default:
        Serial.println("Invalid game mode selected via WiFi");
        selectedMode = 0;
        break;
    }
    if (selectedMode > 0) {
      modeInitialized = false;
      navigator.clear();
      wifiManager.resetGameSelection();
      boardDriver.clearAllLEDs();
    }
  }

  if (currentMode == AppMode::SELECTION) {
    boardDriver.readSensors();
    int result = navigator.poll();
    if (result != BoardMenu::RESULT_NONE)
      handleMenuResult(result);
    delay(SENSOR_READ_DELAY_MS);
    return;
  }
  // Game mode selected
  if (!modeInitialized) {
    initializeSelectedMode(currentMode);
    modeInitialized = true;
    delay(1); // HACK: Ensure any starting animations acquire the LED mutex before proceeding
  }

  switch (currentMode) {
    case AppMode::CHESS_MOVES:
    case AppMode::BOT:
    case AppMode::LICHESS:
      if (activeGame != nullptr) {
        // Update navigation blocked state for web requests
        wifiManager.setNavigationBlocked(!activeGame->isNavigationAllowed());

        // Process pending navigation from web interface
        uint8_t navAction = wifiManager.getPendingNavAction();
        if (navAction != 0) {
          if (activeGame->isNavigationAllowed()) {
            switch (static_cast<NavAction>(navAction)) {
              case NavAction::UNDO:
                chess.undoMove();
                break;
              case NavAction::REDO:
                chess.redoMove();
                break;
              case NavAction::FIRST:
                chess.beginBatch();
                while (chess.canUndo()) chess.undoMove();
                chess.endBatch();
                break;
              case NavAction::LAST:
                chess.beginBatch();
                while (chess.canRedo()) chess.redoMove();
                chess.endBatch();
                break;
              default:
                break;
            }
          }
          wifiManager.clearPendingNav();
        }

        // Relay web resign flag to the active game
        if (wifiManager.getPendingResign()) {
          activeGame->setResignPending(true);
          wifiManager.clearPendingResign();
        }
        if (activeGame->isGameOver())
          enterGameSelection();
        else
          activeGame->update();
      }
      break;
    case AppMode::SENSOR_TEST:
      if (sensorTest != nullptr) {
        if (sensorTest->isComplete())
          enterGameSelection();
        else
          sensorTest->update();
      }
      break;
    default:
      enterGameSelection();
      break;
  }

  delay(SENSOR_READ_DELAY_MS);
}

void enterGameSelection() {
  currentMode = AppMode::SELECTION;
  modeInitialized = false;
  navigator.clear();
  navigator.push(&gameMenu);
  Serial.println("=============== Game Selection Mode ===============");
  Serial.println("Four LEDs are lit in the center of the board:");
  Serial.println("  Blue:   Chess Moves (Human vs Human)");
  Serial.println("  Green:  Chess Bot (Human vs AI)");
  Serial.println("  Yellow: Lichess (Play online games)");
  Serial.println("  Red:    Sensor Test");
  Serial.println("Place any chess piece on a LED to select that mode");
  Serial.println("===================================================");
}

void handleMenuResult(int result) {
  switch (result) {
    // Game selection menu
    case MenuId::CHESS_MOVES:
      Serial.println("Mode: 'Chess Moves' selected!");
      currentMode = AppMode::CHESS_MOVES;
      modeInitialized = false;
      navigator.clear();
      break;
    case MenuId::BOT:
      Serial.println("Mode: 'Chess Bot' selected! Choose difficulty...");
      navigator.push(&botDifficultyMenu);
      break;
    case MenuId::LICHESS:
      Serial.println("Mode: 'Lichess' selected!");
      currentMode = AppMode::LICHESS;
      modeInitialized = false;
      lichessConfig = wifiManager.getLichessConfig();
      navigator.clear();
      break;
    case MenuId::SENSOR_TEST:
      Serial.println("Mode: 'Sensor Test' selected!");
      currentMode = AppMode::SENSOR_TEST;
      modeInitialized = false;
      navigator.clear();
      break;

    // Bot difficulty menu (ids 10–17 → level 1–8)
    case MenuId::DIFF_1: case MenuId::DIFF_2: case MenuId::DIFF_3: case MenuId::DIFF_4:
    case MenuId::DIFF_5: case MenuId::DIFF_6: case MenuId::DIFF_7: case MenuId::DIFF_8: {
      int level = result - MenuId::DIFF_1 + 1;
      botDifficultyLevel = level;
      Serial.printf("Difficulty: Level %d\n", level);
      navigator.push(&botColorMenu);
      break;
    }

    // Bot color menu
    case MenuId::PLAY_WHITE:
      Serial.println("Playing as White");
      playerColor = 'w';
      currentMode = AppMode::BOT;
      modeInitialized = false;
      navigator.clear();
      break;
    case MenuId::PLAY_BLACK:
      Serial.println("Playing as Black");
      playerColor = 'b';
      currentMode = AppMode::BOT;
      modeInitialized = false;
      navigator.clear();
      break;
    case MenuId::PLAY_RANDOM:
      Serial.println("Playing as Random");
      playerColor = (random(2) == 0) ? 'w' : 'b';
      Serial.printf("  -> Assigned: %s\n", playerColor == 'w' ? "White" : "Black");
      currentMode = AppMode::BOT;
      modeInitialized = false;
      navigator.clear();
      break;

    default:
      Serial.printf("Unknown menu result: %d\n", result);
      break;
  }
}

void initializeSelectedMode(AppMode mode) {
  if (resumingGame)
    resumingGame = false;
  else
    chess.discardRecording(); // Discard any incomplete live game that wasn't properly finished or resumed

  // Clean up previous game/test
  delete activeGame;
  activeGame = nullptr;
  delete sensorTest;
  sensorTest = nullptr;

  switch (mode) {
    case AppMode::CHESS_MOVES:
      Serial.println("Starting 'Chess Moves'...");
      activeGame = new PlayerMode(&boardDriver, &wifiManager, &chess, &logger);
      activeGame->begin();
      break;
    case AppMode::BOT: {
      Serial.printf("Starting 'Chess Bot' (Engine: %s, Level: %d, Player is %s)...\n", botEngine.c_str(), botDifficultyLevel, playerColor == 'w' ? "White" : "Black");
      EngineProvider* provider;
      if (botEngine == "librechess") {
        provider = new LibreChessProvider(botDifficultyLevel, playerColor, &logger);
      } else {
        provider = new StockfishProvider(botDifficultyLevel, playerColor, &logger);
      }
      activeGame = new BotMode(&boardDriver, &wifiManager, &chess, provider, &logger);
      activeGame->begin();
      break;
    }
    case AppMode::LICHESS:
      Serial.println("Starting 'Lichess Mode'...");
      activeGame = new BotMode(&boardDriver, &wifiManager, &chess, new LichessProvider(lichessConfig, &logger), &logger);
      activeGame->begin();
      break;
    case AppMode::SENSOR_TEST:
      Serial.println("Starting 'Sensor Test'...");
      sensorTest = new SensorTest(&boardDriver);
      sensorTest->begin();
      break;
    default:
      enterGameSelection();
      break;
  }
}
