# Board Interactions & LED Reference

How the board communicates through LEDs and how players interact with it physically.

## LED Color Reference

Every color on the board has a consistent meaning across all game modes and menus:

| Color | RGB | Meaning |
|-------|-----|---------|
| **Cyan** | (0, 255, 255) | Piece origin — "pick up from here" |
| **White** | (255, 255, 255) | Valid move destination, menu back button, calibration indicator |
| **Red** | (255, 0, 0) | Capture square, illegal move warning, error |
| **Green** | (0, 255, 0) | Move confirmed, "yes" in confirm dialogs |
| **Yellow** | (255, 200, 0) | King in check, pawn promotion, random option |
| **Purple** | (128, 0, 255) | En passant captured pawn location |
| **Orange** | (255, 80, 0) | Resign gesture progress |
| **Blue** | (0, 0, 255) | Bot thinking, Human vs Human mode, WiFi connecting |
| `scaleColor(White, 40.0f / 255.0f)` | (40, 40, 40) | "Play as Black" option in bot color menu |

### Dark Square Dimming

LEDs on dark squares (where row + column is odd) are automatically dimmed to a configurable percentage (default 70%, adjustable from 20% to 100% in the web UI). This compensates for the increased perceived brightness on dark backgrounds, producing a visually uniform board.

## Animations

### Capture
Concentric wave rings expand outward from the capture square. Alternating red and yellow rings with quadratic intensity falloff. The center square stays red throughout. Duration: approximately 1 second.

### Promotion
A yellow waterfall cascades down the promotion column. LEDs alternate between lit and unlit in a scrolling pattern. Duration: approximately 1.6 seconds. Currently, promotion always results in a queen — piece selection is a [planned feature](../roadmap.md).

### Check Warning
When a move puts the opponent's king in check, the king's square blinks yellow three times.

### Firework
A ring of light contracts from the board edges to the center, then expands back out. Used for game-ending events:
- **Checkmate** — winner's color (white for white, blue for black)
- **Stalemate / 50-move draw / threefold repetition** — cyan
- **Resignation** — winner's color

Duration: approximately 2.4 seconds.

### Thinking
During the bot's or Lichess opponent's turn, the four corner squares pulse with a blue breathing effect (sinusoidal brightness from 8% to 100%). The hue shifts slightly toward purple at low brightness. Continues until the opponent's move arrives.

### Waiting
A white chase animation traces the 28 perimeter squares clockwise. Eight LEDs travel around the edges in two groups (diametrically opposite). Used while waiting for a Lichess game to start. Duration: continuous until a game is found.

### Connecting
The two center rows (rows 3 and 4) fill with blue from left to right, column by column, with a brief delay between each. Plays when the board attempts a WiFi connection.

### Error Flash
The entire board flashes red three times. Indicates a critical error such as no WiFi connection for a network-dependent mode, or an invalid Lichess token.

### Move Confirmation
A single green blink on the destination square after a valid move is placed.

## Board Setup

Before each game, the board guides piece placement for the starting position:

- Squares where a **white piece** is needed light up in **white**
- Squares where a **black piece** is needed light up in **blue**
- Squares with an **extra piece** (occupied but should be empty) light up in **red**
- Correctly placed squares stay **off**

The board waits until all 64 squares match the expected position, then plays a firework animation and begins the game.

## Piece Pickup & Valid Moves

When you lift one of your pieces on your turn:

1. The **origin square** lights **cyan** — this marks where the piece came from
2. All **valid move destinations** (empty squares) light **white**
3. All **capture destinations** (squares with an opponent's piece) light **red**
4. If an **en passant capture** is available, the destination lights **red** and the captured pawn's actual square lights **purple**

All legal moves are shown — moves that would leave your king in check are filtered out and not displayed.

### Wrong Piece

If you lift an opponent's piece (wrong color for the current turn), the square blinks **red twice** and no valid moves are shown. Return the piece to its square.

### Canceling a Move

Return the piece to its origin square (the cyan one) to cancel. All highlights clear and you can try a different piece.

## Captures

1. The capture square is highlighted in **red**
2. Remove the opponent's piece from the capture square — the square blinks red once to confirm removal
3. Place your piece on the now-empty capture square
4. A capture animation plays (red/yellow concentric waves)

In Best Move assistance, a capture square keeps its ranked color during the removal prompt. A best capture stays **green**, a worst capture stays **red**, and an unranked legal capture stays **white**.

If you return your piece to the origin square instead of placing it on the capture square, the move is canceled.

## Castling

### Player Castling
When you move your king two squares toward a rook (a legal castling move):
1. The king move is processed first
2. The board then shows the rook's movement: **cyan** on the rook's current square, **white** on its destination
3. Pick up the rook from the cyan square, place it on the white square

### Remote/Bot Castling
When a bot or Lichess opponent castles:
1. The **king's** source square lights **cyan**, destination lights **white** — execute the king move first
2. Then the **rook's** source lights **cyan**, destination lights **white** — execute the rook move

## En Passant

When an en passant capture is available among your valid moves:

1. The diagonal destination square lights **red** (this is where your pawn moves to)
2. The opponent's pawn (the one being captured) lights **purple** on its actual square
3. Remove the opponent's pawn from the purple square
4. Place your pawn on the destination square

For bot/Lichess en passant moves, the same purple indicator shows which pawn to remove.

## Invalid Moves

If you place a piece on a square that isn't highlighted as a valid destination, no animation plays and nothing happens. Return the piece to the origin square (cyan) or place it on a valid destination.

## Bot & Lichess Move Execution

When the bot or Lichess opponent makes a move, the board shows:

- **Cyan** on the source square (pick up from here)
- **White** on the destination (place here) — or **red** if it's a capture
- **Purple** on the en passant captured pawn (if applicable)

Execute the move physically by picking up the piece from the cyan square and placing it on the destination. For captures, remove the captured piece first.
