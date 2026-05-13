#include "board/board.h"
#include "board/workflows/diagnostics.h"
#include "board/workflows/menu.h"
#include "game_mode/player_mode.h"
#include "game_mode/bot_mode.h"
#include "engine/stockfish/stockfish_provider.h"
#include "engine/lichess/lichess_provider.h"
#include "engine/lichess/lichess_config.h"
#include "engine/librechess/librechess_provider.h"
#include "game.h"
#include "storage/littrefs.h"
#include "shared/serial_logger.h"
#include "shared/utils.h"
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
  BOARD_DIAGNOSTICS = 4
};

int botDifficultyLevel = 4;  // 1-8 difficulty level
char playerColor = 'w';
String botEngine = "stockfish";
LichessConfig lichessConfig = {""};

Board physicalBoard;
SerialLogger logger;
LittleFSStorage storage(&logger);
WiFiManagerESP32 wifiManager(&physicalBoard, &storage);
Game chess(&storage, &wifiManager, &logger);
GameMode* activeGame = nullptr;

AppMode currentMode = AppMode::SELECTION;
bool modeInitialized = false;
bool resumingGame = false;

void enterGameSelection();
void handleGameSelection(const BoardMenu::GameSelection& selection);
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
  if (!physicalBoard.begin()) {
    Serial.println("ERROR: Board initialization failed; halting.");
    while (true) {
      delay(1000);
    }
  }
  wifiManager.setGameRef(&chess);
  wifiManager.begin();
  Serial.println();

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

  BoardMenu::GameSelectionMode confirmMode = BoardMenu::GameSelectionMode::NONE;
  const char* modeName = "Unknown";
  bool flipped = false;

  switch (resumeMode) {
    case GameModeId::PLAYER:
      confirmMode = BoardMenu::GameSelectionMode::CHESS_MOVES;
      modeName = "Chess Moves";
      break;
    case GameModeId::BOT:
      confirmMode = BoardMenu::GameSelectionMode::BOT;
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

  if (physicalBoard.menu().confirmResume(confirmMode, flipped)) {
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
        currentMode = AppMode::BOARD_DIAGNOSTICS;
        break;
      default:
        Serial.println("Invalid game mode selected via WiFi");
        selectedMode = 0;
        break;
    }
    if (selectedMode > 0) {
      modeInitialized = false;
      physicalBoard.menu().clear();
      wifiManager.resetGameSelection();
      physicalBoard.clearAllSurfaces();
    }
  }

  if (currentMode == AppMode::SELECTION) {
    BoardMenu::GameSelection selection = physicalBoard.menu().poll();
    if (selection.hasSelection())
      handleGameSelection(selection);
    delay(physicalBoard.cadenceMs());
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
    case AppMode::BOARD_DIAGNOSTICS:
      if (physicalBoard.diagnostics().isComplete())
        enterGameSelection();
      else
        physicalBoard.diagnostics().update();
      break;
    default:
      enterGameSelection();
      break;
  }

  delay(physicalBoard.cadenceMs());
}

void enterGameSelection() {
  currentMode = AppMode::SELECTION;
  modeInitialized = false;
  physicalBoard.menu().start();
  Serial.println("=============== Game Selection Mode ===============");
  Serial.println("Four LEDs are lit in the center of the board:");
  Serial.println("  Blue:   Chess Moves (Human vs Human)");
  Serial.println("  Green:  Chess Bot (Human vs AI)");
  Serial.println("  Yellow: Lichess (Play online games)");
  Serial.println("  Red:    Sensor Test");
  Serial.println("Place any chess piece on a LED to select that mode");
  Serial.println("===================================================");
}

void handleGameSelection(const BoardMenu::GameSelection& selection) {
  switch (selection.mode) {
    case BoardMenu::GameSelectionMode::CHESS_MOVES:
      Serial.println("Mode: 'Chess Moves' selected!");
      currentMode = AppMode::CHESS_MOVES;
      modeInitialized = false;
      break;
    case BoardMenu::GameSelectionMode::BOT:
      botDifficultyLevel = selection.botDifficulty;
      playerColor = selection.playerColor;
      Serial.printf("Mode: 'Chess Bot' selected! Level %d, player is %s\n",
                    botDifficultyLevel, playerColor == 'w' ? "White" : "Black");
      currentMode = AppMode::BOT;
      modeInitialized = false;
      break;
    case BoardMenu::GameSelectionMode::LICHESS:
      Serial.println("Mode: 'Lichess' selected!");
      currentMode = AppMode::LICHESS;
      modeInitialized = false;
      lichessConfig = wifiManager.getLichessConfig();
      break;
    case BoardMenu::GameSelectionMode::BOARD_DIAGNOSTICS:
      Serial.println("Mode: 'Sensor Test' selected!");
      currentMode = AppMode::BOARD_DIAGNOSTICS;
      modeInitialized = false;
      break;
    case BoardMenu::GameSelectionMode::NONE:
    default:
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

  switch (mode) {
    case AppMode::CHESS_MOVES:
      Serial.println("Starting 'Chess Moves'...");
      activeGame = new PlayerMode(&physicalBoard.gameplay(), &wifiManager, &chess, &logger);
      activeGame->begin();
      break;
    case AppMode::BOT: {
      Serial.printf("Starting 'Chess Bot' (Engine: %s, Level: %d, Player is %s)...\n", botEngine.c_str(), botDifficultyLevel, playerColor == 'w' ? "White" : "Black");
      EngineProvider* provider;
      if (botEngine == "librechess") {
        provider = new LibreChessProvider(&chess, botDifficultyLevel, playerColor, &logger);
      } else {
        provider = new StockfishProvider(botDifficultyLevel, playerColor, &logger);
      }
      activeGame = new BotMode(&physicalBoard.gameplay(), &wifiManager, &chess, provider, &logger);
      activeGame->begin();
      break;
    }
    case AppMode::LICHESS:
      Serial.println("Starting 'Lichess Mode'...");
      activeGame = new BotMode(&physicalBoard.gameplay(), &wifiManager, &chess, new LichessProvider(lichessConfig, &logger), &logger);
      activeGame->begin();
      break;
    case AppMode::BOARD_DIAGNOSTICS:
      Serial.println("Starting 'Sensor Test'...");
      physicalBoard.diagnostics().begin();
      break;
    default:
      enterGameSelection();
      break;
  }
}
