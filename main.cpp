#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <variant>
#include <vector>

#include "args/args.hxx"
#include "hackdb.h"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/write_batch.h"
#include "leveldb/zlib_compressor.h"
#include "utils.hpp"

namespace fs = std::filesystem;
namespace ldb = leveldb;

class compression_type {
 public:
  auto make_compressor() const noexcept {
    auto compressor = make_compressor_();
    assert(!compressor || compressor->uniqueCompressionID == compression_id);
    return std::unique_ptr<ldb::Compressor>(compressor);
  }
  compression_type(const std::string name, bool is_default,
                   std::function<ldb::Compressor *(void)> &&make_compressor_)
      : make_compressor_(make_compressor_),
        compression_id(get_compression_id()),
        name(name),
        is_default(is_default) {}
  compression_type()
      : make_compressor_([]() { return nullptr; }),
        compression_id(0),
        name("no compression"),
        is_default(false) {};

 private:
  const std::function<ldb::Compressor *(void)> make_compressor_;
  hackdb::compression_id_t get_compression_id() const noexcept {
    std::unique_ptr<ldb::Compressor> compressor(make_compressor_());
    assert(compressor);
    auto id = compressor->uniqueCompressionID;
    return id;
  }

 public:
  const hackdb::compression_id_t compression_id;
  const std::string name;
  const bool is_default;
};

const auto &get_compressors() {
  static std::vector<compression_type> compressors = {
      {"zlib", false, []() { return new ldb::ZlibCompressor(); }},
      {"zlib raw", true, []() { return new ldb::ZlibCompressorRaw(); }},
      {}};
  return compressors;
}

auto make_compressors(bool only_default = false) {
  std::vector<std::unique_ptr<ldb::Compressor>> compressors = {};
  for (auto &compressor : get_compressors()) {
    if (compressor.compression_id == 0) continue;
    if (only_default) {
      if (compressor.is_default) {
        compressors.push_back(compressor.make_compressor());
        break;
      }
    } else {
      compressors.push_back(compressor.make_compressor());
    }
  }
  return compressors;
}

template <typename Deleter>
auto open_db(std::unique_ptr<ldb::Options, Deleter> &&opts,
             const std::string &name) {
  ldb::DB *db;
  auto status = ldb::DB::Open(*opts, name, &db);
  if (!status.ok()) {
    db = nullptr;
  }
  auto arena = unique_deleter_arena((ldb::DB *)nullptr, std::move(opts));
  return std::pair{std::move(std::unique_ptr<ldb::DB, decltype(arena)>(
                       db, std::move(arena))),
                   status};
}

// taken from
// https://github.com/Amulet-Team/leveldb-mcpe/blob/c446a37734d5480d4ddbc371595e7af5123c4925/mcpe_sample_setup.cpp
// https://github.com/Amulet-Team/Amulet-LevelDB/blob/47c490e8a0a79916b97aa6ad8b93e3c43b743b8c/src/leveldb/_leveldb.pyx#L191-L199
auto bedrock_default_db_options(
    std::vector<std::unique_ptr<ldb::Compressor>> &&compressors) {
  auto options = new ldb::Options();
  auto filter_policy =
      std::unique_ptr<const ldb::FilterPolicy>(ldb::NewBloomFilterPolicy(10));
  auto block_cache =
      std::unique_ptr<ldb::Cache>(ldb::NewLRUCache(8 * 1024 * 1024));

  options->filter_policy = filter_policy.get();
  options->write_buffer_size = 4 * 1024 * 1024;
  options->block_cache = block_cache.get();
  for (size_t i = 0; i < compressors.size(); i++) {
    options->compressors[i] = compressors[i].get();
  }
  options->block_size = 163840;
  options->max_open_files = 1000;

  auto arena =
      unique_deleter_arena((ldb::Options *)nullptr, std::move(compressors),
                           std::move(filter_policy), std::move(block_cache));
  return std::unique_ptr<ldb::Options, decltype(arena)>(options,
                                                        std::move(arena));
}

using db_unique_ptr_t =
    decltype(open_db(bedrock_default_db_options({}), "").first);

leveldb::Status clone_db(ldb::DB &input, ldb::DB &output,
                         const ldb::WriteOptions &wopts,
                         const ldb::ReadOptions &ropts) {
  auto buffer = ldb::WriteBatch();
  auto status = ldb::Status();

  auto input_iter = std::unique_ptr<ldb::Iterator>(input.NewIterator(ropts));
  for (input_iter->SeekToFirst(); input_iter->Valid(); input_iter->Next()) {
    auto constexpr one_meg = 1 * 1000 * 1000;
    buffer.Put(input_iter->value(), input_iter->key());
    if (buffer.ApproximateSize() < 10 * one_meg) {
      continue;
    }
    status = output.Write(wopts, &buffer);
    if (!status.ok()) {
      return status;
    }
    buffer.Clear();
  }
  status = output.Write(wopts, &buffer);
  return status;
}

class func_logger : public ldb::Logger {
 public:
  using LogFunc = std::function<void(const char *, va_list)>;
  func_logger(LogFunc &&log_func) : log_func(std::move(log_func)) {}

  void Logv(const char *format, va_list ap) { log_func(format, ap); }

 private:
  LogFunc log_func;
};

using cid_t = hackdb::compression_id_t;
template <typename DbContainer>
std::optional<std::map<cid_t, size_t>> find_compression_algo(
    std::function<DbContainer()> &&open_db, const ldb::Logger *logger) {
  assert(logger != nullptr);
  static_assert(std::numeric_limits<cid_t>::min() == 0);
  auto constexpr counts_size = std::numeric_limits<cid_t>::max() + 1;
  static_assert(counts_size < 10000);
  std::array<std::atomic<size_t>, counts_size> counts{};

  {
    hackdb::logger_entry entry(
        logger, [&counts](cid_t compression_id) { counts[compression_id]++; });
    auto maybe_db = open_db();
    if (!maybe_db) {
      return {};
    }
    ldb::DB &db = *maybe_db;
    auto ropts = ldb::ReadOptions();
    ropts.fill_cache = false;
    ropts.verify_checksums = false;

    auto iter = std::unique_ptr<ldb::Iterator>(db.NewIterator(ropts));
    iter->SeekToFirst();
    while (iter->Valid()) {
      iter->Next();
    }
  }

  std::map<cid_t, size_t> map;
  for (int i = 0; i < counts_size; i++) {
    if (counts[i] > 0) {
      map[i] = counts[i];
    }
  }
  return {map};
}

[[nodiscard]] int compress_decompress(const fs::path &input_dir,
                                      const fs::path &output_dir,
                                      const bool compress) {
  std::cout << "Input database is at: " << input_dir << std::endl;
  std::cout << "Output database is at: " << output_dir << std::endl;

  auto input_opts = bedrock_default_db_options(make_compressors(false));
  auto input_logger = func_logger([](auto format, auto args) {
    printf("leveldb intput info: ");
    vprintf(format, args);
    printf("\n");
  });
  input_opts->info_log = &input_logger;
  input_opts->create_if_missing = false;
  input_opts->error_if_exists = false;

  auto output_opts = compress
                         ? bedrock_default_db_options(make_compressors(true))
                         : bedrock_default_db_options({});
  auto output_logger = func_logger([](auto format, auto args) {
    printf("leveldb output info: ");
    vprintf(format, args);
    printf("\n");
  });
  output_opts->info_log = &output_logger;
  output_opts->create_if_missing = true;
  output_opts->error_if_exists = true;

  auto [maybe_input_db, input_status] =
      open_db(std::move(input_opts), input_dir);
  if (!maybe_input_db) {
    std::cerr << "Failed to open input DB: " << input_status.ToString()
              << std::endl;
    return 1;
  }
  auto &input_db = maybe_input_db;

  auto [maybe_output_db, output_status] =
      open_db(std::move(output_opts), output_dir);
  if (!maybe_output_db) {
    std::cerr << "Failed to open input DB: " << output_status.ToString()
              << std::endl;
    return 1;
  }
  auto &output_db = maybe_output_db;

  auto wopts = ldb::WriteOptions();
  wopts.sync = false;
  auto ropts = ldb::ReadOptions();
  ropts.fill_cache = false;
  ropts.verify_checksums = true;
  auto clone_status = clone_db(*input_db, *output_db, wopts, ropts);
  if (!clone_status.ok()) {
    std::cerr << "Failed to clone DB: " << clone_status.ToString() << std::endl;
    return 1;
  }
  output_db->CompactRange(nullptr, nullptr);
  return 0;
}

int cmd_find_compression_algos(const fs::path &db_path) {
  auto logger = func_logger([](auto format, auto args) {
    printf("leveldb info: ");
    vprintf(format, args);
    printf("\n");
  });

  ldb::Status status{};

  auto result = find_compression_algo<db_unique_ptr_t>(
      [&status, &db_path, &logger]() {
        auto opts = bedrock_default_db_options(make_compressors(false));
        opts->create_if_missing = false;
        opts->error_if_exists = false;
        opts->info_log = &logger;

        auto [maybe_db, open_status] = open_db(std::move(opts), db_path);
        status = std::move(open_status);
        static_assert(std::is_move_constructible<decltype(open_status)>::value);
        return std::move(maybe_db);
      },
      &logger);

  assert(result.has_value() == status.ok());
  if (!status.ok()) {
    std::cerr << "Failed to open DB: " << status.ToString() << std::endl;
    return 1;
  }

  for (auto &[compressor_id, occurrences] : *result) {
    const auto &compressors = get_compressors();
    const auto it = std::find_if(
        compressors.begin(), compressors.end(), [&](const auto &compressor) {
          return compressor.compression_id == compressor_id;
        });
    std::string compressor_name =
        (it != compressors.end()) ? it->name : "<unknown>";
    std::cout << "Read blocks with compressor " << compressor_name
              << " (id=" << (int)compressor_id << ")"
              << " " << occurrences << " times" << std::endl;
  }
  return 0;
}

class exit_with_code : std::runtime_error {
 public:
  const int code;
  exit_with_code(int code)
      : std::runtime_error("exit with code: " + std::to_string(code)),
        code(code) {}
  virtual ~exit_with_code() = default;
};

void cli_parse_handler(std::function<void(void)> &&parse,
                       args::ArgumentParser &parser) {
  try {
    parse();
  } catch (const args::Completion &e) {
    std::cout << e.what() << std::endl;
    throw exit_with_code(0);
  } catch (const args::Help &) {
    std::cout << parser;
    throw exit_with_code(0);
  } catch (const args::ParseError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    throw exit_with_code(1);
  } catch (const args::RequiredError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    throw exit_with_code(1);
  }
}

int main(int argc, const char **argv) {
  args::ArgumentParser parser("Compress and decompress leveldb DB");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::CompletionFlag completion(parser, {"complete"});
  // should be a positional but https://github.com/Taywee/args/issues/125
  args::ValueFlag<fs::path> input_dir(parser, "input", "Input DB directory",
                                      {'i', "input"}, args::Options::Required);
  args::Group commands(parser, "commands");
  args::Command copy(
      commands, "copy", "Copy database", [&](args::Subparser &subp) {
        auto out_dir = args::Positional<fs::path>(
            subp, "out", "Output DB directory", args::Options::Required);
        auto compress = args::Flag(subp, "compress", "Copy with compression",
                                   {"c", "compress"});

        subp.Parse();

        throw exit_with_code(
            compress_decompress(*input_dir, *out_dir, compress));
      });

  args::Command list_algos(
      commands, "list-algos", "Lists compression algorithms used in DB",
      [&](args::Subparser &subp) {
        subp.Parse();
        throw exit_with_code(cmd_find_compression_algos(*input_dir));
      });

  try {
    cli_parse_handler([&]() { parser.ParseCLI(argc, argv); }, parser);
  } catch (const exit_with_code &e) {
    return e.code;
  }
  assert(false);
}
