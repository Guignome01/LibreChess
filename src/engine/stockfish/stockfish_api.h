#ifndef STOCKFISH_API_H
#define STOCKFISH_API_H

#include <ArduinoJson.h>

// Stockfish API Endpoint
#define STOCKFISH_API_URL "stockfish.online"
#define STOCKFISH_API_PATH "/api/s/v2.php"
#define STOCKFISH_API_PORT 443

// Struct to hold parsed Stockfish API response
struct StockfishResponse {
  bool success;
  float evaluation;    // Evaluation in pawns (or null if mate exists)
  int mateInMoves;     // Number of moves until mate (null if no forced mate)
  bool hasMate;        // true if mateInMoves is valid
  String bestMove;     // Best move in UCI format (e.g., "b1c3")
  String ponderMove;   // Expected opponent response (e.g., "h7h6")
  String continuation; // Top engine line (e.g., "b1c3 h7h6 c3e2")
  String errorMessage; // Error message if success is false
};

class StockfishAPI {
 public:
  // Parse JSON response from Stockfish API
  // Returns true if parsing was successful
  static bool parseResponse(const String& jsonString, StockfishResponse& response);

  // Build the API request URL
  static String buildRequestURL(const String& fen, int depth);
};

#endif // STOCKFISH_API_H
