#ifndef BOARD_ASSISTANCE_PROVIDER_H
#define BOARD_ASSISTANCE_PROVIDER_H

#include "board/types.h"

// ---------------------------------------------------------------------------
// Board assistance providers.
// ---------------------------------------------------------------------------
// Providers decide whether assistance is disabled, legal-move-only, or backed
// by an engine hint. They return data only; BoardGame owns when to service
// them and BoardAssistance owns all LED presentation.
// ---------------------------------------------------------------------------

class BoardAssistanceProvider {
 public:
  virtual ~BoardAssistanceProvider() = default;

  virtual BoardAssistanceLevel level() const = 0;
  virtual bool service(BoardBestMoveHint& hint) { return false; }
  virtual void cancel() {}
};

class BoardFixedAssistanceProvider : public BoardAssistanceProvider {
 public:
  explicit BoardFixedAssistanceProvider(BoardAssistanceLevel level) : level_(level) {}

  BoardAssistanceLevel level() const override { return level_; }

 private:
  BoardAssistanceLevel level_;
};

class BoardNoAssistanceProvider final : public BoardFixedAssistanceProvider {
 public:
  BoardNoAssistanceProvider() : BoardFixedAssistanceProvider(BoardAssistanceLevel::NONE) {}
};

class BoardLegalMoveAssistanceProvider final : public BoardFixedAssistanceProvider {
 public:
  BoardLegalMoveAssistanceProvider()
      : BoardFixedAssistanceProvider(BoardAssistanceLevel::LEGAL_MOVES) {}
};

#endif  // BOARD_ASSISTANCE_PROVIDER_H
