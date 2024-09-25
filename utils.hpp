#pragma once
#include <optional>
#include <tuple>
#include <utility>

#define UTILS_SET_MOVE(type, value) \
  type(type &&) = value;            \
  type &operator=(type &&) = value;

#define UTILS_SET_COPY(type, value) \
  type(type const &) = value;       \
  type &operator=(type const &) = value;

#define UTILS_NOT_COPYABLE(type) UTILS_SET_COPY(type, delete)
#define UTILS_NOT_MOVEABLE(type) UTILS_SET_MOVE(type, delete)

#define UTILS_DEFAULT_COPY(type) UTILS_SET_COPY(type, default)
#define UTILS_DEFAULT_MOVE(type) UTILS_SET_MOVE(type, default)

template <typename... Args>
struct unique_deleter_arena {
  UTILS_DEFAULT_MOVE(unique_deleter_arena)
  UTILS_NOT_COPYABLE(unique_deleter_arena)

  std::optional<std::tuple<Args...>> arena = {};
  template <typename Ptr>
  void operator()(Ptr *this_) noexcept {
    assert(this_ != nullptr);
    assert(arena.has_value());
    delete this_;
    arena = {};
  }
  unique_deleter_arena(Args &&...args)
      : arena({{std::forward<Args>(args)...}}) {}
};

// shared_ptr in this version of clang does not require a shared_ptr deleter
// to be copy constructible, a shared_deleter_arena can be added with
// std::shared_ptr<std::tuple<Args...>> arena
