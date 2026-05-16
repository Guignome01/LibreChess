#ifndef BOARD_SERVICES_REGISTRY_H
#define BOARD_SERVICES_REGISTRY_H

#include <memory>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------
// BoardRegistry<Product, Context> — fixed-size named-creator registry.
// ---------------------------------------------------------------------------
// Generic mechanism used by board menu and program factories: register a
// `Creator` under a stable string id, then construct fresh `Product` instances
// from a `Context`. Keeps construction wiring out of the runner/Board owners.
// ---------------------------------------------------------------------------

template <typename Product, typename Context, uint8_t Capacity = 8>
class BoardRegistry {
 public:
  using Creator = std::unique_ptr<Product> (*)(Context& context);

  static constexpr uint8_t CAPACITY = Capacity;

  BoardRegistry() : entries_{}, count_(0) {}

  BoardRegistry(const BoardRegistry&) = delete;
  BoardRegistry& operator=(const BoardRegistry&) = delete;

  /// Register a creator under a stable string id. Returns false if the table
  /// is full, the id/creator is invalid, or the id is already registered.
  bool registerCreator(const char* id, Creator creator) {
    if (id == nullptr || id[0] == '\0' || creator == nullptr) return false;
    if (count_ >= Capacity || find(id) != nullptr) return false;
    entries_[count_++] = Entry{id, creator};
    return true;
  }

  /// Create a fresh product instance by id, or an empty pointer when unknown.
  std::unique_ptr<Product> create(const char* id, Context& context) const {
    const Entry* entry = find(id);
    return entry != nullptr ? entry->creator(context) : std::unique_ptr<Product>();
  }

  /// Return true when the id has a registered creator.
  bool has(const char* id) const { return find(id) != nullptr; }

 private:
  struct Entry {
    const char* id;
    Creator creator;
  };

  const Entry* find(const char* id) const {
    if (id == nullptr) return nullptr;
    for (uint8_t i = 0; i < count_; ++i) {
      if (strcmp(entries_[i].id, id) == 0) return &entries_[i];
    }
    return nullptr;
  }

  Entry entries_[Capacity];
  uint8_t count_;
};

#endif  // BOARD_SERVICES_REGISTRY_H
