#include "board/board.h"
#include "board/menus/confirm.h"
#include "board/menus/game_selection.h"
#include "board/programs/ids.h"
#include "engines/factory.h"
#include "game_mode/player_mode.h"
#include "game_mode/bot_mode.h"
#include "engines/lichess/config.h"
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
BoardAssistanceLevel assistanceLevel = BoardAssistanceLevel::LEGAL_MOVES;
int assistanceDifficultyLevel = 4;
String assistanceEngine = "librechess";
LichessConfig lichessConfig = {""};

Board physicalBoard;
GameSelectionMenu gameSelectionMenu;
SerialLogger logger;
LittleFSStorage storage(&logger);
WiFiManagerESP32 wifiManager(&physicalBoard, &storage);
Game chess(&storage, &wifiManager, &logger);
GameMode* activeGame = nullptr;

AppMode currentMode = AppMode::SELECTION;
bool modeInitialized = false;
bool resumingGame = false;

void enterGameSelection();
void handleGameSelection(const BoardGameSelection& selection);
void initializeSelectedMode(AppMode mode);
void checkForResumableGame();
BoardAssistanceLevel assistanceLevelFromInt(int value);
void configureBoardAssistance();
IBoardGame* startBoardGameProgram();
bool startBoardCalibration();

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
  // Wire the game-selection menu callback once; the menu invokes it from
  // `onClose` when a complete selection has been captured, removing the
  // need to poll `hasSelection()` after each board update.
  gameSelectionMenu.setOnSelected(&handleGameSelection);
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

  BoardGameSelectionMode confirmMode = BoardGameSelectionMode::NONE;
  const char* modeName = "Unknown";
  bool flipped = false;

  switch (resumeMode) {
    case GameModeId::PLAYER:
      confirmMode = BoardGameSelectionMode::CHESS_MOVES;
      modeName = "Chess Moves";
      break;
    case GameModeId::BOT:
      confirmMode = BoardGameSelectionMode::BOT;
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

  ResumeConfirmMenu resumeMenu(confirmMode);
  physicalBoard.runMenu(resumeMenu, flipped);
  if (resumeMenu.accepted()) {
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
        botEngine = (meta.engineId == Engines::LIBRECHESS_ENGINE_ID) ? "librechess" : "stockfish";
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

  // Web handlers run on the async server task; consume calibration requests
  // here so board program lifetime and active game teardown stay on the main
  // loop.
  if (wifiManager.getPendingBoardCalibration()) {
    wifiManager.clearPendingBoardCalibration();
    if (startBoardCalibration()) {
      physicalBoard.update();
    }
    delay(physicalBoard.cadenceMs());
    return;
  }

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
      if (selectedMode == 1 || selectedMode == 2 || selectedMode == 3) {
        assistanceLevel = assistanceLevelFromInt(wifiManager.getAssistanceLevel());
        assistanceDifficultyLevel = wifiManager.getAssistanceDifficultyLevel();
        assistanceEngine = wifiManager.getAssistanceEngine();
      }
      modeInitialized = false;
      physicalBoard.stopMenu();
      wifiManager.resetGameSelection();
      physicalBoard.clearAllSurfaces();
    }
  }

  Board::UpdateResult boardUpdate = physicalBoard.update();

  if (currentMode == AppMode::SELECTION) {
    // Selection finalization is delivered through the menu callback wired
    // in `setup()`; nothing to poll here.
    (void)boardUpdate;
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
      if (boardUpdate.programFinished)
        enterGameSelection();
      break;
    default:
      enterGameSelection();
      break;
  }

  delay(physicalBoard.cadenceMs());
}

BoardAssistanceLevel assistanceLevelFromInt(int value) {
  switch (value) {
    case 0:
      return BoardAssistanceLevel::NONE;
    case 2:
      return BoardAssistanceLevel::BEST_MOVE;
    case 1:
    default:
      return BoardAssistanceLevel::LEGAL_MOVES;
  }
}

void configureBoardAssistance() {
  physicalBoard.setAssistanceProvider(Engines::createAssistanceProvider(
      assistanceLevel, assistanceEngine, assistanceDifficultyLevel, &chess, &logger));
}

IBoardGame* startBoardGameProgram() {
  BoardProgram* program = physicalBoard.startProgram(BoardProgramIds::GAME);
  if (program == nullptr) {
    Serial.println("ERROR: Failed to start board game program");
    return nullptr;
  }
  // The game program is the only program that implements IBoardGame, so the
  // downcast is safe by construction (registered factory creates a BoardGame).
  return static_cast<IBoardGame*>(program);
}

bool startBoardCalibration() {
  currentMode = AppMode::SELECTION;
  modeInitialized = false;
  delete activeGame;
  activeGame = nullptr;
  physicalBoard.stopMenu();
  physicalBoard.setAssistanceProvider(nullptr);

  BoardProgram* program = physicalBoard.startProgram(BoardProgramIds::CALIBRATION);
  if (program == nullptr) {
    Serial.println("ERROR: Failed to start board calibration program");
    enterGameSelection();
    return false;
  }
  Serial.println("Board calibration requested via web interface");
  return true;
}

void enterGameSelection() {
  currentMode = AppMode::SELECTION;
  modeInitialized = false;
  delete activeGame;
  activeGame = nullptr;
  physicalBoard.stopProgram();
  physicalBoard.setAssistanceProvider(nullptr);
  physicalBoard.showMenu(gameSelectionMenu);
  Serial.println("=============== Game Selection Mode ===============");
  Serial.println("Four LEDs are lit in the center of the board:");
  Serial.println("  Blue:   Chess Moves (Human vs Human)");
  Serial.println("  Green:  Chess Bot (Human vs AI)");
  Serial.println("  Yellow: Lichess (Play online games)");
  Serial.println("  Red:    Sensor Test");
  Serial.println("Place any chess piece on a LED to select that mode");
  Serial.println("===================================================");
}

void handleGameSelection(const BoardGameSelection& selection) {
  switch (selection.mode) {
    case BoardGameSelectionMode::CHESS_MOVES:
      Serial.println("Mode: 'Chess Moves' selected!");
      currentMode = AppMode::CHESS_MOVES;
      modeInitialized = false;
      break;
    case BoardGameSelectionMode::BOT:
      botDifficultyLevel = selection.botDifficulty;
      playerColor = selection.playerColor;
      Serial.printf("Mode: 'Chess Bot' selected! Level %d, player is %s\n",
                    botDifficultyLevel, playerColor == 'w' ? "White" : "Black");
      currentMode = AppMode::BOT;
      modeInitialized = false;
      break;
    case BoardGameSelectionMode::LICHESS:
      Serial.println("Mode: 'Lichess' selected!");
      currentMode = AppMode::LICHESS;
      modeInitialized = false;
      lichessConfig = wifiManager.getLichessConfig();
      break;
    case BoardGameSelectionMode::BOARD_DIAGNOSTICS:
      Serial.println("Mode: 'Sensor Test' selected!");
      currentMode = AppMode::BOARD_DIAGNOSTICS;
      modeInitialized = false;
      break;
    case BoardGameSelectionMode::NONE:
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
  physicalBoard.stopProgram();
  physicalBoard.setAssistanceProvider(nullptr);

  switch (mode) {
    case AppMode::CHESS_MOVES: {
      Serial.println("Starting 'Chess Moves'...");
      configureBoardAssistance();
      IBoardGame* gameProgram = startBoardGameProgram();
      if (gameProgram == nullptr) {
        enterGameSelection();
        break;
      }
      activeGame = new PlayerMode(gameProgram, &wifiManager, &chess, &logger);
      activeGame->begin();
      break;
    }
    case AppMode::BOT: {
      Serial.printf("Starting 'Chess Bot' (Engine: %s, Level: %d, Player is %s)...\n", botEngine.c_str(), botDifficultyLevel, playerColor == 'w' ? "White" : "Black");
      configureBoardAssistance();
      IBoardGame* gameProgram = startBoardGameProgram();
      if (gameProgram == nullptr) {
        enterGameSelection();
        break;
      }
      EngineProvider* provider = Engines::createOpponentEngine(
          botEngine, &chess, botDifficultyLevel, playerColor, &logger);
      activeGame = new BotMode(gameProgram, &wifiManager, &chess, provider, &logger);
      activeGame->begin();
      break;
    }
    case AppMode::LICHESS: {
      Serial.println("Starting 'Lichess Mode'...");
      configureBoardAssistance();
      IBoardGame* gameProgram = startBoardGameProgram();
      if (gameProgram == nullptr) {
        enterGameSelection();
        break;
      }
      activeGame = new BotMode(gameProgram, &wifiManager, &chess,
                               Engines::createLichessEngine(lichessConfig, &logger), &logger);
      activeGame->begin();
      break;
    }
    case AppMode::BOARD_DIAGNOSTICS:
      Serial.println("Starting 'Sensor Test'...");
      physicalBoard.startProgram(BoardProgramIds::DIAGNOSTICS);
      break;
    default:
      enterGameSelection();
      break;
  }
}
