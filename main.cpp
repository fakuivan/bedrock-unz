#include <filesystem>
#include <functional>
#include <memory>
#include <variant>
#include <vector>

#include "args/args.hxx"
#include "leveldb/cache.h"
#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/filter_policy.h"
#include "leveldb/zlib_compressor.h"
#include "leveldb/write_batch.h"

namespace fs = std::filesystem;
namespace ldb = leveldb;

template<typename Deleter>
auto open_db(std::unique_ptr<ldb::Options, Deleter> &&opts, const std::string &name) {
  ldb::DB *db;
  auto status = ldb::DB::Open(*opts, name, &db);
  if (!status.ok()) {
    db = nullptr;
  }
  auto deleter = [opts{std::move(opts)}](ldb::DB *db) noexcept {
    if (db != nullptr) {
      delete db;
    }
  };
  return std::pair{std::unique_ptr<ldb::DB, decltype(deleter)>(db, std::move(deleter)), status};
}

// taken from
// https://github.com/Amulet-Team/leveldb-mcpe/blob/c446a37734d5480d4ddbc371595e7af5123c4925/mcpe_sample_setup.cpp
// https://github.com/Amulet-Team/Amulet-LevelDB/blob/47c490e8a0a79916b97aa6ad8b93e3c43b743b8c/src/leveldb/_leveldb.pyx#L191-L199
auto bedrock_default_db_options(bool compression = true) {
  std::vector<ldb::Compressor *> compressors(2);
  auto options = new ldb::Options();

  options->filter_policy = ldb::NewBloomFilterPolicy(10);
  options->write_buffer_size = 4 * 1024 * 1024;
  options->block_cache = ldb::NewLRUCache(8 * 1024 * 1024);
  if (compression) {
    compressors.push_back(new ldb::ZlibCompressor());
    compressors.push_back(new ldb::ZlibCompressorRaw());
    for (int i = 0; i < compressors.size(); i++) {
      options->compressors[i] = compressors[i];
    }
  }
  options->block_size = 163840;
  options->max_open_files = 1000;

  auto deleter = [compressors](ldb::Options *options) noexcept {
    delete options;
    for (auto &compressor : compressors) {
      delete compressor;
    }
  };
  return  std::unique_ptr<ldb::Options, decltype(deleter)>(options, std::move(deleter));
}

leveldb::Status clone_db(ldb::DB &input, ldb::DB &output, const ldb::WriteOptions &wopts, const ldb::ReadOptions &ropts) {
  auto buffer = ldb::WriteBatch();
  auto status = ldb::Status();

  auto input_iter = std::unique_ptr<ldb::Iterator>(input.NewIterator(ropts));
  for (input_iter->SeekToFirst(); input_iter->Valid(); input_iter->Next()) {
    auto constexpr one_meg = 1 * 1000 * 1000;
    buffer.Put(input_iter->value(), input_iter->key());
    if (buffer.ApproximateSize() < 10*one_meg) {
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

int main(int argc, const char **argv) {
  args::ArgumentParser parser("Compress and decompress leveldb DB");
  args::HelpFlag help(parser, "help", "Display this help menu", {'h', "help"});
  args::CompletionFlag completion(parser, {"completion"});
  // should be a positional but https://github.com/Taywee/args/issues/125
  args::ValueFlag<fs::path> input_dir(parser, "input", "Input DB directory", {'i', "input"},
                                   args::Options::Required);
  args::ValueFlag<fs::path> output_dir(parser, "output", "Ouput DB directory", {'o', "output"},
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

  std::cout << "Input database is at: " << *input_dir << std::endl;
  std::cout << "Output database is at: " << *output_dir << std::endl;

  auto input_opts = bedrock_default_db_options(true);
  auto input_logger = func_logger([](auto format, auto args) {
    printf("leveldb intput info: ");
    vprintf(format, args);
    printf("\n");
  });
  input_opts->info_log = &input_logger;
  input_opts->create_if_missing = false;
  input_opts->error_if_exists = false;

  auto output_opts = bedrock_default_db_options(compress);
  auto output_logger = func_logger([](auto format, auto args) {
    printf("leveldb output info: ");
    vprintf(format, args);
    printf("\n");
  });
  output_opts->info_log = &output_logger;
  output_opts->create_if_missing = true;
  output_opts->error_if_exists = true;

  auto [maybe_input_db, input_status] = open_db(std::move(input_opts), *input_dir);
  if (!maybe_input_db) {
    std::cerr << "Failed to open input DB: " << input_status.ToString() << std::endl;
    return 1;
  }
  auto &input_db = maybe_input_db;

  auto [maybe_output_db, output_status] = open_db(std::move(output_opts), *output_dir);
  if (!maybe_output_db) {
    std::cerr << "Failed to open input DB: " << output_status.ToString() << std::endl;
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
