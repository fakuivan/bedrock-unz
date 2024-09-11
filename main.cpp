#include <filesystem>
#include <functional>
#include <memory>
#include <variant>

#include "args/args.hxx"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"

namespace fs = std::filesystem;
namespace ldb = leveldb;

template <typename Result, typename Error>
std::variant<std::unique_ptr<Result>, Error> make_or_error(
    const std::function<Error(Result *&)> &func) {
  Result *result;
  Error error = func(result);
  if (result == nullptr) {
    return {error};
  }
  return std::unique_ptr<Result>(result);
}  // namespace std::variant

// taken from
// https://github.com/Amulet-Team/leveldb-mcpe/blob/c446a37734d5480d4ddbc371595e7af5123c4925/mcpe_sample_setup.cpp
// https://github.com/Amulet-Team/Amulet-LevelDB/blob/47c490e8a0a79916b97aa6ad8b93e3c43b743b8c/src/leveldb/_leveldb.pyx#L191-L199
ldb::Options bedrock_default_db_options() {
  ldb::Options options;
  options.filter_policy = ldb::NewBloomFilterPolicy(10);
  options.write_buffer_size = 4 * 1024 * 1024;
  options.block_cache = ldb::NewLRUCache(8 * 1024 * 1024);
  // do we need to free these?
  options.block_size = 163840;
  options.max_open_files = 1000;
  return options;
}

class func_logger : public ldb::Logger {
 public:
  using LogFunc = std::function<void(const char *, std::va_list)>;
  func_logger(LogFunc &&log_func) : log_func(std::move(log_func)) {}

  void Logv(const char *format, std::va_list ap) { log_func(format, ap); }

 private:
  LogFunc log_func;
};

int main(int argc, const char **argv) {
  args::ArgumentParser parser("Compress and decompress leveldb DB");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::CompletionFlag completion(parser, {"completion"});
  // should be a positional but https://github.com/Taywee/args/issues/125
  args::ValueFlag<fs::path> db_dir(parser, "dir", "DB directory", {'d', "dir"},
                                   args::Options::Required);
  args::Group commands(parser, "commands");
  args::Command compress(commands, "compress",
                         "Enables compression on a database");
  args::Command decompress(commands, "decompress",
                           "Remove compression on a database");
  try {
    parser.ParseCLI(argc, argv);
  } catch (const args::Completion &e) {
    std::cout << e.what();
    return 0;
  } catch (const args::Help &) {
    std::cout << parser;
    return 0;
  } catch (const args::ParseError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  } catch (const args::RequiredError &e) {
    std::cerr << e.what() << std::endl;
    std::cerr << parser;
    return 1;
  }
  if (!fs::is_directory(args::get(db_dir))) {
    std::cerr << "DB directory path does not point to a valid directory: "
              << args::get(db_dir) << std::endl;
    return 1;
  }

  ldb::Options opts = bedrock_default_db_options();
  auto logger = func_logger([](auto format, auto args) {
    printf("leveldb info: ");
    vprintf(format, args);
    printf("\n");
  });
  opts.info_log = &logger;
  std::cout << "Input database is at: " << *db_dir << std::endl;
  auto maybe_db = make_or_error<ldb::DB, leveldb::Status>([&](auto *&db) {
    auto status = ldb::DB::Open(opts, db_dir->c_str(), &db);
    if (!status.ok()) {
      db = nullptr;
    }
    return status;
  });

  if (auto status = std::get_if<ldb::Status>(&maybe_db)) {
    std::cerr << "Failed to open DB: " << status->ToString() << std::endl;
    return 1;
  }
  auto db = std::move(std::get<std::unique_ptr<ldb::DB>>(maybe_db));

  if (compress) {
    std::cout << "Compressing database" << std::endl;
    opts.compression = ldb::kZLibRawCompression;
  }
  if (decompress) {
    std::cout << "Decompressing database" << std::endl;
    opts.compression = ldb::kNoCompression;
  }
  db->CompactRange(nullptr, nullptr);
  return 0;
}
