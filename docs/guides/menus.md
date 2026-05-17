# Menus

The board doubles as an 8×8 pixel display for menu navigation. Menus are shown as colored LEDs at specific positions, and selections are made by placing a piece on a lit square and lifting it again to confirm.

## Main Menu

The root menu displayed on boot and after a game ends. Four squares in the center of the board:

| Position | Color | Mode | ID Range |
|----------|-------|------|----------|
| d5 (row 3, col 3) | Blue | Human vs Human | 0–9 |
| e5 (row 3, col 4) | Green | Human vs Bot | 0–9 |
| d4 (row 4, col 3) | Yellow | Lichess | 0–9 |
| e4 (row 4, col 4) | Red | Sensor Test | 0–9 |

Selecting Human vs Bot opens the difficulty menu. All other selections proceed directly to the mode.

## Bot Difficulty Menu

Eight squares across row 3, representing difficulty levels from left to right:

| Position | Color | Level | Stockfish Depth |
|----------|-------|-------|----------------|
| a5 (row 3, col 0) | Green | Beginner | 3 |
| b5 (row 3, col 1) | Lime | Easy | 5 |
| c5 (row 3, col 2) | Yellow | Intermediate | 7 |
| d5 (row 3, col 3) | Orange | Medium | 9 |
| e5 (row 3, col 4) | Red | Advanced | 11 |
| f5 (row 3, col 5) | Crimson | Hard | 13 |
| g5 (row 3, col 6) | Purple | Expert | 15 |
| h5 (row 3, col 7) | Blue | Master | 17 |

A **white back button** at e4 (row 4, col 4) returns to the main menu root page.

## Bot Color Menu

Three squares in the center of row 3:

| Position | Color | Choice |
|----------|-------|--------|
| d5 (row 3, col 3) | White | Play as White |
| e5 (row 3, col 4) | `scaleColor(White, 40.0f / 255.0f)` | Play as Black |
| f5 (row 3, col 5) | Yellow | Random |

A **white back button** at e4 (row 4, col 4) returns to the difficulty menu.

## How Selection Works

Menu selection uses a three-phase debounce system to prevent accidental activations:

1. **Phase 1 (empty)** — the square must be read as empty (no piece) for 5 consecutive sensor polls (~200ms)
2. **Phase 2 (occupied)** — the square must then be read as occupied (piece placed) for another 5 consecutive polls
3. **Phase 3 (released)** — the square must be read as empty again for another 5 consecutive polls

This prevents pieces already on the board when the menu appears from triggering a selection. Only a deliberate "place a piece on an empty square, then lift it" action registers.

On confirmed selection:
- The square turns **off** once the piece is stably resting on it
- The square **blinks once** in its own color after the piece is removed
- The next menu or game starts only after the confirmation blink finishes

## Menu Navigation

Menus are managed by a stack-based navigator with a maximum depth of 8 pages inside the active typed menu:

- Selecting a tile with `NEXT` auto-advance **pushes** the next page inside the current menu
- Pressing the **back button** (white square) **pops** the current page and re-displays the parent page
- Pressing back on the bot difficulty page returns to the root mode page
- The root mode page has no back button

The navigator automatically handles re-displaying parent menus with fresh debounce state after a pop, so previously placed pieces don't immediately re-trigger selections.

## Confirm Dialog

A simple yes/no dialog used for resign confirmation and game resume:

| Position | Color | Choice |
|----------|-------|--------|
| d4 (row 4, col 3) | Green | Yes |
| e4 (row 4, col 4) | Red | No |

The dialog blocks until a selection is made. It supports orientation flipping for black-side players.

## Game Resume Dialog

When the board boots and detects a saved live game (from a previous crash or power loss):

1. A square at position d5 (row 3, col 3) **blinks twice** in the mode's color:
   - **Blue** for a Human vs Human game
   - **Green** for a Bot game
2. The confirm dialog appears (green = resume, red = discard)
3. If resumed, the game state is replayed from the saved move history and the player continues where they left off
4. If discarded, the saved game is deleted and the main menu appears

The dialog flips orientation when the saved game had the player on the black side (bot mode).

## Orientation

All menu positions described above are in white-side orientation (row 7 = rank 1 at the bottom). When `setFlipped(true)` is active, row coordinates are mirrored (`row' = 7 - row`) so that menus face a player sitting on the black side. This applies to bot games where the player chose black, and to the resign confirm dialog when it's black's turn.

## Web-Based Game Selection

Game modes can also be selected through the web interface's game selection page, bypassing the physical menu entirely. The web UI provides the same options (mode, difficulty, color) and communicates the selection to the board, which proceeds directly to board setup.
