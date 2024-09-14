#pragma once

#include <functional>
#include <map>
#include <set>
#include <shared_mutex>

#include "leveldb/env.h"
#include "leveldb/options.h"

#define NOT_COPYABLE(TypeName)         \
  TypeName(TypeName const &) = delete; \
  TypeName &operator=(TypeName const &) = delete;

#define NOT_MOVEABLE(TypeName)    \
  TypeName(TypeName &&) = delete; \
  TypeName &operator=(TypeName &&) = delete;

namespace hackdb {
namespace ldb = leveldb;
struct logger_entry;
class block_logger_entries {
  std::shared_mutex mutex;
  std::unordered_map<const ldb::Logger *, std::unordered_set<logger_entry *>>
      map;
  friend struct logger_entry;
  friend void found_block_with_compressor(unsigned char id,
                                          const ldb::Options &dbOptions);
} block_logger_map;

struct logger_entry {
  NOT_COPYABLE(logger_entry)
  NOT_MOVEABLE(logger_entry)
  logger_entry(const ldb::Logger *logger,
               std::function<void(unsigned char)> &&func)
      : logger(logger), func(func) {
    std::unique_lock lock(block_logger_map.mutex);
    block_logger_map.map[logger].insert(this);
  };

  std::function<void(unsigned char)> func;
  const ldb::Logger *logger;
  ~logger_entry() {
    std::unique_lock lock(block_logger_map.mutex);
    block_logger_map.map[logger].erase(this);
  }
};

void found_block_with_compressor(unsigned char id,
                                 const ldb::Options &dbOptions) {
  std::shared_lock lock(block_logger_map.mutex);
  auto logger = dbOptions.info_log;
  if (!block_logger_map.map.count(logger)) {
    return;
  }
  for (auto &elem : block_logger_map.map[logger]) {
    elem->func(id);
  }
}
}  // namespace hackdb
