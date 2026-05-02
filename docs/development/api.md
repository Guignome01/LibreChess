# API Reference

LibreChess exposes a REST-like HTTP API from the ESP32's async web server. All endpoints are defined in `wifi_manager_esp32.cpp`. The frontend accesses them through the `Api` object in `provider.js`.

## Quick Reference

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/health` | Health check |
| `GET` | `/board-update` | Current board FEN and evaluation |
| `POST` | `/board-update` | Submit a FEN-based board edit |
| `GET` | `/board-settings` | LED brightness and dimming settings |
| `POST` | `/board-settings` | Save LED settings |
| `POST` | `/board-calibrate` | Trigger recalibration on next reboot |
| `POST` | `/gameselect` | Select a game mode |
| `POST` | `/resign` | Submit a resign request |
| `POST` | `/nav` | Navigate move history (undo/redo/first/last) |
| `GET` | `/games` | List completed games (JSON) or fetch game data (binary) |
| `DELETE` | `/games` | Delete a completed game |
| `GET` | `/wifi/networks` | List saved networks and connection state |
| `POST` | `/wifi/networks` | Add a new WiFi network |
| `DELETE` | `/wifi/networks` | Remove a saved network |
| `POST` | `/wifi/connect` | Connect to a saved network |
| `GET` | `/wifi/scan` | Trigger or retrieve WiFi scan results |
| `GET` | `/lichess` | Get Lichess token status |
| `POST` | `/lichess` | Save Lichess API token |
| `GET` | `/ota/status` | Check if OTA password is set |
| `POST` | `/ota/verify` | Verify OTA password before upload |
| `POST` | `/ota/password` | Set, change, or remove OTA password |
| `POST` | `/ota` | Upload firmware binary (multipart) |

## Board

### `GET /board-update`

Returns the current board position and evaluation.

**Response** (JSON):
```json
{
  "fen": "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1",
  "evaluation": "0.3",
  "moveIndex": 1,
  "moveCount": 1,
  "canUndo": true,
  "canRedo": false
}
```

| Field | Type | Description |
|-------|------|-------------|
| `fen` | string | Current board position in FEN notation |
| `evaluation` | string | Position evaluation from Stockfish (bot mode only) |
| `moveIndex` | int | Current move cursor position (0 = start, n = after nth move) |
| `moveCount` | int | Total number of moves in the history log |
| `canUndo` | bool | Whether backward navigation is available |
| `canRedo` | bool | Whether forward navigation is available |

### `POST /board-update`

Submit a FEN string to edit the board position from the web UI.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `fen` | Yes | Valid FEN string to apply to the board |

**Response**: `200 OK` or `400 Bad Request` with error message.

### `GET /board-settings`

Returns current LED settings.

**Response** (JSON):
```json
{
  "brightness": 200,
  "dimMultiplier": 70
}
```

| Field | Type | Description |
|-------|------|-------------|
| `brightness` | int | LED brightness (10–255) |
| `dimMultiplier` | int | Dark square dimming percentage (20–100) |

### `POST /board-settings`

Save LED brightness and dimming settings. Persisted to NVS.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `brightness` | Yes | LED brightness value (10–255) |
| `dimMultiplier` | Yes | Dark square dimming percentage (20–100) |

**Response** (JSON): `{ "status": "ok" }`

### `POST /board-calibrate`

Triggers board recalibration on the next reboot. Sets an NVS flag that the firmware checks at startup.

**Response** (JSON): `{ "status": "ok" }`

## Game

### `POST /gameselect`

Select a game mode from the web UI.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `gamemode` | Yes | Mode ID: `1` (Human vs Human), `2` (Bot), `3` (Lichess), `4` (Sensor Test) |
| `playerColor` | Bot only | `1` (White) or `2` (Black) |
| `difficulty` | Bot only | Difficulty level (1–8) |

**Response** (JSON): `{ "status": "ok" }` or error message.

### `POST /resign`

Submit a resignation request. The board processes it on the next update cycle and shows a confirm dialog on the physical board.

**Response** (JSON): `{ "status": "ok" }`

### `POST /nav`

Navigate the in-game move history. The server applies the action to `Game`'s undo/redo system and the next `GET /board-update` reflects the updated position.

In bot mode, navigation is blocked during the engine's turn (returns `409 Conflict`).

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `action` | Yes | Navigation action: `undo`, `redo`, `first`, `last` |

**Response** (JSON): `{ "ok": true }` or `409` / `400` with error message.

### `GET /games`

Without query parameters, returns a JSON array of completed game summaries. With `?id=<game_id>`, returns the raw binary game file.

**Response (list)** (JSON):
```json
[
  {
    "id": 1,
    "mode": 2,
    "result": 1,
    "winner": "w",
    "moves": 42,
    "timestamp": 1708000000
  }
]
```

| Field | Type | Description |
|-------|------|-------------|
| `id` | int | Game identifier |
| `mode` | int | Game mode code (1 = HvH, 2 = Bot) |
| `result` | int | Result code (1 = checkmate, 2 = stalemate, 3 = 50-move, 4 = threefold, 5 = resignation) |
| `winner` | string | `"w"`, `"b"`, or `"d"` (draw) |
| `moves` | int | Total number of moves |
| `timestamp` | int | Unix timestamp |

**Response (single game)**: Raw binary data (`application/octet-stream`). Format: 16-byte packed header + 2-byte UCI-encoded moves.

### `DELETE /games`

Delete a completed game.

**Query parameter**:
| Parameter | Required | Description |
|-----------|----------|-------------|
| `id` | Yes | Game ID to delete |

**Response**: `200 OK` or `404 Not Found`.

## WiFi

### `GET /wifi/networks`

Returns saved networks, current connection state, and AP status.

**Response** (JSON):
```json
{
  "networks": [
    { "ssid": "HomeNetwork", "connected": true }
  ],
  "ap": true,
  "hostname": "librechess",
  "ip": "192.168.1.100"
}
```

### `POST /wifi/networks`

Add a new WiFi network to the saved list (maximum 3).

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `ssid` | Yes | Network SSID |
| `password` | Yes | Network password |

**Response** (JSON): `{ "status": "ok" }` or error if limit reached.

### `DELETE /wifi/networks`

Remove a saved network by index.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `index` | Yes | Network index (0-based) |

**Response**: `200 OK`.

### `POST /wifi/connect`

Attempt to connect to a saved network by index.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `index` | Yes | Network index (0-based) |

**Response**: `200 OK`. Connection happens asynchronously.

### `GET /wifi/scan`

Triggers a WiFi scan or returns cached results if a scan was recently completed.

**Response** (JSON): Array of visible networks with SSID, signal strength, and encryption type.

## Lichess

### `GET /lichess`

Returns Lichess token status. The actual token is never exposed.

**Response** (JSON):
```json
{
  "hasToken": true,
  "maskedToken": "lip_****...****abcd"
}
```

### `POST /lichess`

Save a Lichess API token. Persisted to NVS.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `token` | Yes | Lichess personal API token |

**Response** (JSON): `{ "status": "ok" }`

## OTA (Over-the-Air Updates)

### `GET /ota/status`

Check whether an OTA password is configured.

**Response** (JSON):
```json
{
  "hasPassword": "true"
}
```

### `POST /ota/verify`

Verify an OTA password before attempting an upload.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `password` | Yes | OTA password to verify |

**Response** (JSON): `{ "valid": true }` or `{ "valid": false }`

### `POST /ota/password`

Set, change, or remove the OTA password.

**Body** (`application/x-www-form-urlencoded`):
| Parameter | Required | Description |
|-----------|----------|-------------|
| `newPassword` | Yes | New password (empty string to remove) |
| `confirmPassword` | For setting/changing | Must match `newPassword` |
| `currentPassword` | For changing/removing | Current password for verification |

**Response** (JSON): `{ "status": "ok" }` or error message.

### `POST /ota`

Upload a firmware binary. Multipart file upload with optional password authentication.

**Headers**:
| Header | Required | Description |
|--------|----------|-------------|
| `X-OTA-Password` | If password is set | OTA password for authentication |

**Body**: Multipart form data with the firmware `.bin` file.

The firmware validates the upload by checking the ESP32 magic byte (`0xE9`) at offset 0. After successful upload, the ESP32 reboots automatically.

## Health

### `GET /health`

Simple health check endpoint. Used by the frontend to detect when the board has rebooted after an OTA update.

**Response** (JSON): `{ "ok": true }`

## Static File Serving

The web server serves static files from LittleFS using three routes:

| Path | Source | Behavior |
|------|--------|----------|
| `/sounds/` | LittleFS `/sounds/` | `setTryGzipFirst(false)` — serves raw MP3 files |
| `/pieces/` | LittleFS `/pieces/` | Cached with `max-age=31536000, immutable` |
| `/` | LittleFS `/` | Default file: `index.html`. Auto-detects `.gz` files and serves with `Content-Encoding: gzip` |

Unmatched paths return `404 Not Found` as plain text.

## Frontend Provider

The `Api` object in `provider.js` maps to backend endpoints:

| Method | Endpoint | Parameters |
|--------|----------|------------|
| `Api.getNetworks()` | `GET /wifi/networks` | — |
| `Api.addNetwork(ssid, password)` | `POST /wifi/networks` | SSID, password |
| `Api.deleteNetwork(index)` | `DELETE /wifi/networks` | Network index |
| `Api.connectNetwork(index)` | `POST /wifi/connect` | Network index |
| `Api.scanNetworks()` | `GET /wifi/scan` | — |
| `Api.getBoardUpdate()` | `GET /board-update` | — |
| `Api.submitBoardEdit(fen)` | `POST /board-update` | FEN string |
| `Api.getBoardSettings()` | `GET /board-settings` | — |
| `Api.saveBoardSettings(brightness, dim)` | `POST /board-settings` | Brightness, dimming |
| `Api.calibrate()` | `POST /board-calibrate` | — |
| `Api.getLichessInfo()` | `GET /lichess` | — |
| `Api.saveLichessToken(token)` | `POST /lichess` | Token |
| `Api.selectGame(mode, color, difficulty)` | `POST /gameselect` | Mode, player color, difficulty |
| `Api.resign()` | `POST /resign` | — |
| `Api.getGames()` | `GET /games` | — |
| `Api.getGame(id)` | `GET /games?id=` | Game ID |
| `Api.deleteGame(id)` | `DELETE /games?id=` | Game ID |
| `Api.getOtaStatus()` | `GET /ota/status` | — |
| `Api.verifyOtaPassword(password)` | `POST /ota/verify` | Password |
| `Api.setOtaPassword(new, confirm, current)` | `POST /ota/password` | Passwords |

Low-level utilities in `api.js`:
- `getApi(url)` — `fetch(url)`
- `postApi(url, body)` — `fetch(url, { method: 'POST', body, headers })` with `application/x-www-form-urlencoded`
- `deleteApi(url, body)` — same as POST but with `DELETE` method
- `pollHealth(timeoutMs)` — `fetch('/health')` with abort signal timeout
