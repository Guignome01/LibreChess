#ifndef LIBRECHESS_OBSERVER_H
#define LIBRECHESS_OBSERVER_H

#include <string>

// Abstract observer interface — notified by Game after every board
// mutation (makeMove, loadFEN, newGame, endGame).  Allows the core library to
// push state changes to the firmware layer (e.g. web UI) without depending on
// any concrete implementation.
//
// WiFiManagerESP32 implements this to keep the web dashboard in sync.
namespace LibreChess {

class IGameObserver {
 public:
  virtual ~IGameObserver() = default;

  // Called whenever the board state changes.
  // evaluation is in centipawns (positive = White advantage).
  virtual void onBoardStateChanged(const std::string& fen, int evaluation) = 0;
};

}  // namespace LibreChess

#endif  // CORE_OBSERVER_H
