# How to Play

A guide to each game mode in LibreChess — from game selection to move flow and mode-specific behavior.

## Game Selection

When the board powers on (or returns from a completed game), the game selection menu appears. Four squares in the center of the board light up:

| Square | Color | Mode |
|--------|-------|------|
| d5 | Blue | Human vs Human |
| e5 | Green | Human vs Bot |
| d4 | Yellow | Lichess |
| e4 | Red | Sensor Test |

Place any piece on a lit square, then lift it to select that mode. The square blinks after release to confirm your selection before proceeding.

For detailed menu navigation mechanics, see the [menus guide](menus.md).

## Board Setup

After selecting a mode, the board waits for you to place all pieces in the standard starting position. LEDs indicate what's needed:

- **White LED** — a white piece should be placed here
- **Blue LED** — a black piece should be placed here
- **Red LED** — there's a piece on this square that shouldn't be there

Once all 64 squares match the expected position, the board flashes a brief animation and the game begins. Take your time — the board waits indefinitely.

## Human vs Human

The most straightforward mode. Two players alternate turns on the same board.

**Playing a turn:**
1. Pick up one of your pieces — the board highlights all legal destinations (see [board interactions](board-interactions.md) for the full LED reference)
2. Place the piece on a valid square — a green blink confirms the move
3. If you pick up the wrong color piece, the square blinks red twice — put it back
4. Captures require removing the opponent's piece first, then placing yours
5. The opponent's turn begins automatically

**Special moves** (castling, en passant, promotion) are handled with LED guidance. See [board interactions](board-interactions.md) for details.

**Game ends** when checkmate, stalemate, or a draw condition is detected. A firework animation plays in the winner's color (white or blue), or cyan for draws.

**Resign** at any time using the physical king gesture (see [features](features.md)) or the web UI resign button.

## Human vs Bot

Play against the Stockfish chess engine. Requires a WiFi connection — the board communicates with the Stockfish API over the internet.

**Setup:**
1. After selecting bot mode, choose a difficulty level (8 levels from beginner to master). See [menus](menus.md) for the difficulty menu layout.
2. Choose your color — white, black, or random.
3. Set up the starting position as guided by the LEDs.

**Your turn:** identical to human vs human — pick up a piece, see valid moves, place it.

**Bot's turn:**
1. The four corner squares breathe with a blue pulse — this means the bot is thinking
2. Once the bot decides, its move is displayed on the board: the source square lights cyan, the destination lights white (or red for captures)
3. Physically execute the bot's move — pick up the piece from the cyan square and place it on the destination
4. For bot castling, the board guides you through both the king and rook movements
5. For bot en passant, the captured pawn's square lights purple — remove that pawn after placing the moving piece

If the WiFi connection is unavailable when bot mode is selected, the board flashes red three times and returns to the game selection menu.

## Lichess

Play live games on your physical board through the Lichess platform. Requires WiFi and a Lichess API token.

**Prerequisites:**
1. Connect the board to WiFi via the web UI
2. Save your Lichess API token in the web UI settings page (generate one at lichess.org under API Access)

**Starting a game:**
1. Select the Lichess mode — the board shows a white perimeter chase animation while waiting
2. Start a game on the Lichess website or app (or accept a challenge)
3. The board detects the game automatically, syncs the starting position, and the chase animation stops

**Your turn:** make your move on the physical board as in any other mode. The move is automatically submitted to Lichess. Pawn promotion always results in a queen (promotion selection is a [planned feature](../roadmap.md)).

**Opponent's turn:** the corner squares pulse blue while waiting. When the opponent moves on Lichess, the board displays the move for you to execute physically (same cyan/white/red guidance as bot mode).

**Game end:** detected from the Lichess game stream. The board plays the appropriate firework animation.

**Resign:** the physical king gesture or web UI resign button also submits a resignation to Lichess.

If the token is missing or invalid, or WiFi is unavailable, the board flashes red three times and returns to game selection.

## Sensor Test

A diagnostic mode for verifying hardware — not a game mode.

1. The board starts with all LEDs off
2. Every square where a sensor detects a magnet lights up in white
3. Move a piece across the board to test each sensor
4. The mode tracks which squares have been "visited" — once all 64 have been detected at least once, a firework animation plays and the board returns to game selection

This mode is useful for verifying sensor functionality after assembly or when troubleshooting detection issues. For more debugging tools, see [troubleshooting](../troubleshooting.md).

## Web Interface

The web interface extends the physical board experience with features accessible from a browser. It is served directly from the ESP32 and available at `http://librechess.local` (or the board's IP address).

### Home & Settings Page
The main page provides configuration for WiFi networks (add, remove, connect), Lichess token management, LED brightness and dark square dimming adjustments, board recalibration trigger, OTA firmware updates, and OTA password security.

### Board Page
A live view of the current board state that updates in real time. Features include:
- Evaluation bar showing the Stockfish position assessment
- Move history with navigation (forward, back, first, last) and keyboard arrow support
- Move sounds for piece movements and captures
- Drag-and-drop board editor for modifying positions (FEN import/export, turn/castling/en passant controls, validation)
- Game history browser showing all completed games with date, mode, result, and move count
- Full game review with clickable PGN-style move list
- Settings popup for board appearance (piece theme, square colors, notation toggle, sound toggle)
- Focus mode for a fullscreen board view
- Resign button (available during active games)

### Game Selection Page
Select a game mode from the browser instead of the physical board. Includes bot configuration (color, difficulty) with the same options as the physical menu.
