---
applyTo: "src/wifi_manager_esp32.*, src/web/scripts/provider.js"
description: "HTTP API contract between ESP32 backend (WiFiManagerESP32) and web frontend (provider.js). Endpoint definitions, request/response formats, parameter tables. Use when adding, modifying, or debugging any API endpoint on either side."
---

# API Reference

REST-like HTTP API served by `WiFiManagerESP32` (`AsyncWebServer` on port 80). Frontend accesses endpoints through the `Api` object in `provider.js`.

## Quick Reference

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `/health` | Health check |
| `GET` | `/board-update` | Current board FEN, evaluation, and navigation state |
| `POST` | `/board-update` | Submit a FEN-based board edit |
| `GET` | `/board-settings` | LED brightness and dimming settings |
| `POST` | `/board-settings` | Save LED settings (persisted to NVS) |
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
| `GET` | `/lichess` | Get Lichess token status (masked) |
| `POST` | `/lichess` | Save Lichess API token (persisted to NVS) |
| `GET` | `/ota/status` | Check if OTA password is set |
| `POST` | `/ota/verify` | Verify OTA password before upload |
| `POST` | `/ota/password` | Set, change, or remove OTA password |
| `POST` | `/ota` | Upload firmware binary (multipart) |

## Frontend Provider Mapping

The `Api` object in `provider.js` maps 1:1 to backend endpoints:

```
Api.getNetworks()          → GET  /wifi/networks
Api.addNetwork(s, p)       → POST /wifi/networks
Api.deleteNetwork(i)       → DELETE /wifi/networks
Api.connectNetwork(i)      → POST /wifi/connect
Api.scanNetworks()         → GET  /wifi/scan
Api.getBoardUpdate()       → GET  /board-update
Api.submitBoardEdit(fen)   → POST /board-update
Api.getBoardSettings()     → GET  /board-settings
Api.saveBoardSettings(b,d) → POST /board-settings
Api.calibrate()            → POST /board-calibrate
Api.getLichessInfo()       → GET  /lichess
Api.saveLichessToken(t)    → POST /lichess
Api.selectGame(m, c, d, e, aL, aE, aD) → POST /gameselect
Api.resign()               → POST /resign
Api.nav(action)            → POST /nav
Api.getGames()             → GET  /games
Api.getGame(id)            → GET  /games?id=<id>
Api.deleteGame(id)         → DELETE /games?id=<id>
Api.getOtaStatus()         → GET  /ota/status
Api.verifyOtaPassword(p)   → POST /ota/verify
Api.setOtaPassword(n,c,p)  → POST /ota/password
```

Low-level utilities in `api.js` (included by all pages):
- `getApi(url)` — `fetch(url)`
- `postApi(url, body)` — `fetch` with `POST`, `application/x-www-form-urlencoded`
- `deleteApi(url, body)` — same with `DELETE` method
- `pollHealth(timeoutMs)` — `fetch('/health')` with `AbortSignal.timeout`

## Request/Response Conventions

- **Content type**: All POST/DELETE bodies use `application/x-www-form-urlencoded`
- **JSON responses**: Most endpoints return `{ "status": "ok" }` or `{ "ok": true }` on success
- **Error responses**: HTTP status codes (400, 404, 409) with plain text or JSON error message
- **Navigation blocking**: `POST /nav` returns `409 Conflict` during engine's turn in bot mode

## Key Endpoint Details

### `GET /board-update`
Returns cached board state (populated by `onBoardStateChanged()`):
```json
{
  "fen": "...",
  "evaluation": "0.3",
  "moveIndex": 1,
  "moveCount": 1,
  "canUndo": true,
  "canRedo": false
}
```

### `POST /gameselect`
| Parameter | Required | Description |
|-----------|----------|-------------|
| `gamemode` | Yes | `1` (HvH), `2` (Bot), `3` (Lichess), `4` (Sensor Test) |
| `playerColor` | Bot only | `"white"` or `"black"` |
| `difficulty` | Bot only | Difficulty level (1–8) |
| `engine` | Bot only | `"stockfish"` (default) or `"librechess"` |
| `assistanceLevel` | Play modes only | `"none"`, `"legal"` (default), or `"best"` |
| `assistanceEngine` | Optional | Engine for BEST_MOVE assistance. `"librechess"` is currently the only implemented value. |
| `assistanceDifficulty` | Optional | Difficulty level (1–8) for engine-backed assistance hints. |

`NONE` and `LEGAL_MOVES` assistance must not trigger an engine request. If
`assistanceLevel=best`, the backend currently rejects any `assistanceEngine`
other than `librechess` with `400 Bad Request`.

### `POST /nav`
| Parameter | Required | Description |
|-----------|----------|-------------|
| `action` | Yes | `undo`, `redo`, `first`, `last` |

Flag-based relay: the handler sets `pendingNavAction_`, main loop consumes it.

### `GET /games` (single game)
With `?id=<game_id>`, returns raw binary (`application/octet-stream`): 16-byte packed `GameHeader` + 2-byte encoded moves.

### `POST /ota`
Multipart file upload. Validates ESP32 magic byte (`0xE9`). Requires `X-OTA-Password` header if password is set. Auto-reboots on success.

## Static File Serving

| Path | Source | Behavior |
|------|--------|----------|
| `/sounds/` | LittleFS `/sounds/` | Raw MP3 files (`setTryGzipFirst(false)`) |
| `/pieces/` | LittleFS `/pieces/` | Immutable cache (`max-age=31536000`) |
| `/` | LittleFS `/` | Default: `index.html`. Auto-detects `.gz` with `Content-Encoding: gzip` |

For full endpoint documentation with all request/response examples, see `docs/development/api.md`.

## Related Instruction Files

| File | Relationship |
|------|--------------|
| `wifi-manager.instructions.md` | Backend implementation of these endpoints |
| `frontend.instructions.md` | Frontend consumer (provider.js calls these endpoints) |
| `game-mode.instructions.md` | `POST /gameselect` maps to mode selection logic |
