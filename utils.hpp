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

// taken from
// https://github.com/python/cpython/blob/f474391b26aa9208b44ca879f8635409d322f738/Objects/bytesobject.c#L1359-L1379
void python_bytes_repr(std::string &output, const std::string_view &input,
                       const bool double_quote = true, const bool wrap = true) {
  constexpr auto hexdigits = "0123456789abcdef";
  const auto quote = double_quote ? '"' : '\'';

  const auto begin = output.length();
  // worst case: all characters can only be represented as \xNN, occupying 4
  // chars each, plus b""
  output.resize(output.length() + input.length() * 4 + (wrap ? 3 : 0));

  const auto first_char = &output[begin];
  auto p = first_char;
  if (wrap) {
    *p++ = 'b';
    *p++ = quote;
  }
  for (size_t i = 0; i < input.length(); i++) {
    const auto c = input[i];
    if (c == quote || c == '\\')
      *p++ = '\\', *p++ = c;
    else if (c == '\t')
      *p++ = '\\', *p++ = 't';
    else if (c == '\n')
      *p++ = '\\', *p++ = 'n';
    else if (c == '\r')
      *p++ = '\\', *p++ = 'r';
    else if (c < ' ' || c >= 0x7f) {
      *p++ = '\\';
      *p++ = 'x';
      *p++ = hexdigits[(c & 0xf0) >> 4];
      *p++ = hexdigits[c & 0xf];
    } else
      *p++ = c;
  }
  if (wrap) {
    *p++ = quote;
  }

  // resize knowing the final size
  output.resize(begin + (p - first_char));
}

// shared_ptr in this version of clang does not require a shared_ptr deleter
// to be copy constructible, a shared_deleter_arena can be added with
// std::shared_ptr<std::tuple<Args...>> arena
