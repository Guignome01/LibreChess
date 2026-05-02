#include "stockfish_api.h"

bool StockfishAPI::parseResponse(const String& response, StockfishResponse& stockfishResp) {
  if (response.length() == 0) {
    stockfishResp.success = false;
    stockfishResp.errorMessage = "JSON parsing failed: Empty response";
    return false;
  }

  // Separate HTTP headers from body using the blank line (CRLF CRLF)
  int headerEndPos = response.indexOf("\r\n\r\n");
  String jsonOnly;

  if (headerEndPos != -1) {
    // Headers found, extract content after the blank line
    jsonOnly = response.substring(headerEndPos + 4);
  } else {
    // Try Unix line endings (LF LF)
    headerEndPos = response.indexOf("\n\n");
    if (headerEndPos != -1)
      jsonOnly = response.substring(headerEndPos + 2);
    else
      jsonOnly = response; // No header separator found, assume entire response is JSON
  }

  // Trim whitespace
  jsonOnly.trim();

  if (jsonOnly.length() == 0) {
    stockfishResp.success = false;
    stockfishResp.errorMessage = "JSON parsing failed: No content after headers";
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonOnly);

  if (error) {
    stockfishResp.success = false;
    stockfishResp.errorMessage = "JSON parsing failed: " + String(error.f_str());
    return false;
  }

  // Check if the request was successful
  if (doc["success"].isNull()) {
    stockfishResp.success = false;
    stockfishResp.errorMessage = "Missing 'success' field";
    return false;
  }

  stockfishResp.success = doc["success"].as<bool>();

  if (!stockfishResp.success) {
    // If not successful, try to get error message
    if (!doc["error"].isNull())
      stockfishResp.errorMessage = doc["error"].as<String>();
    else if (!doc["data"].isNull())
      stockfishResp.errorMessage = doc["data"].as<String>();
    else
      stockfishResp.errorMessage = "Unknown error from API";
    return false;
  }

  // Parse evaluation (can be null)
  stockfishResp.evaluation = 0.0f;
  if (!doc["evaluation"].isNull())
    stockfishResp.evaluation = doc["evaluation"].as<float>();

  // Parse mate (can be null)
  stockfishResp.mateInMoves = 0;
  stockfishResp.hasMate = false;
  if (!doc["mate"].isNull()) {
    stockfishResp.mateInMoves = doc["mate"].as<int>();
    stockfishResp.hasMate = true;
  }

  // Parse bestmove (format: "bestmove <move> ponder <move>")
  stockfishResp.bestMove = "";
  stockfishResp.ponderMove = "";
  if (!doc["bestmove"].isNull()) {
    String bestmoveStr = doc["bestmove"].as<String>();

    // Parse the bestmove string
    // Format: "bestmove b1c3 ponder h7h6" or just "b1c3"
    int prefixPos = bestmoveStr.indexOf("bestmove ");
    int moveStart = (prefixPos != -1) ? prefixPos + 9 : 0;
    int moveEnd = bestmoveStr.indexOf(" ", moveStart);

    if (moveEnd != -1) {
      stockfishResp.bestMove = bestmoveStr.substring(moveStart, moveEnd);

      // Extract ponder move if it exists
      int ponderStart = bestmoveStr.indexOf("ponder ");
      if (ponderStart != -1) {
        ponderStart += 7; // length of "ponder "
        int ponderEnd = bestmoveStr.indexOf(" ", ponderStart);

        if (ponderEnd != -1)
          stockfishResp.ponderMove = bestmoveStr.substring(ponderStart, ponderEnd);
        else
          stockfishResp.ponderMove = bestmoveStr.substring(ponderStart);
      }
    } else {
      // No space found, use entire string after "bestmove " (normal if this move is a checkmate)
      stockfishResp.bestMove = bestmoveStr.substring(moveStart);
    }
  }

  // Parse continuation (top engine line)
  stockfishResp.continuation = !doc["continuation"].isNull() ? doc["continuation"].as<String>() : "";

  return true;
}

String StockfishAPI::buildRequestURL(const String& fen, int depth) {
  // Validate depth (min 1 max 15, matching Stockfish Online API limits)
  int validDepth = depth > 15 ? 15 : (depth < 1 ? 1 : depth);

  // Build just the path + query (no scheme/host) so callers can reuse host/port constants
  String path = String(STOCKFISH_API_PATH) + "?fen=";

  // URL encode the FEN string (space becomes %20, etc)
  for (int i = 0; i < fen.length(); i++) {
    char c = fen[i];
    if (c == ' ') {
      path += "%20";
    } else if (c == '/' || isalnum(c) || c == '-') {
      path += c;
    } else {
      // For other special characters, use percent encoding
      path += '%';
      path += String(c >> 4, HEX);
      path += String(c & 0xF, HEX);
    }
  }

  // Add depth parameter
  path += "&depth=";
  path += validDepth;

  return path;
}
