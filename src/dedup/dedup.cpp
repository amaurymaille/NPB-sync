#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iomanip>
#include <fstream>
#include <sstream>

#include <boost/program_options.hpp>

#include <nlohmann/json.hpp>

#include <sol/sol.hpp>

#include "smart_fifo.h"

#include "util.h"
#include "debug.h"
#include "dedupdef.h"
#include "encoder.h"
#include "decoder.h"
#include "config.h"
#include "queue.h"

#include "lua.hpp"
#include "lua_core.h"
// #include "fifo_plus.tpp"

#ifdef ENABLE_DMALLOC
#include <dmalloc.h>
#endif //ENABLE_DMALLOC

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif //ENABLE_PARSEC_HOOKS

namespace po = boost::program_options;
using json = nlohmann::json;

config_t * conf;

struct hashtable* cache;

std::map<void*, std::tuple<std::string, std::array<size_t, 2>>> _semaphore_data;

TimestampData timestamp_data[1000000];
size_t _log_n = 0;

/*--------------------------------------------------------------------------*/
/* static void
usage(char* prog)
{
  printf("usage: %s [-cusfvh] [-w gzip/bzip2/none] [-i file] [-o file] [-t number_of_threads]\n",prog);
  printf("-c \t\t\tcompress\n");
  printf("-u \t\t\tuncompress\n");
  printf("-p \t\t\tpreloading (for benchmarking purposes)\n");
  printf("-w \t\t\tcompression type: gzip/bzip2/none\n");
  printf("-i file\t\t\tthe input file\n");
  printf("-o file\t\t\tthe output file\n");
  printf("-t \t\t\tnumber of threads per stage \n");
  printf("-v \t\t\tverbose output\n");
  printf("-h \t\t\thelp\n");
} */

#ifdef FIFO_PLUS_TIMESTAMP_DATA
std::chrono::time_point<steady_clock> Globals::start_time;
#endif

Globals::SteadyTP Globals::_start_time;

struct CLIArgs {
    std::string _lua_file;
    std::string _lua_output_file;
    char _lua_output_file_mode;
    bool _orig;
    bool _smart;
    bool _auto;
    bool _numbers;
    std::optional<std::string> _output;
    std::optional<std::string> _observers;
};

void parse_args(int argc, char** argv, CLIArgs& args) {
    po::options_description options("All options");
    options.add_options()
        ("help,h", "Display this help and exit")
        ("file,f", po::value<std::string>(), "Name of the Lua file to run")
        ("orig,o",  "Run the original algorithm")
        ("smart,s", "Run the smart FIFO algorithm")
        ("auto,a", "Run the auto reconfiguration algorithm")
        ("lua-output-file", po::value<std::string>(), "Output file in which the Lua script can write its information")
        ("lua-output-file-mode", po::value<char>(), "Mode in which the output file is to be opened ('w' or 'a')")
        ("output", po::value<std::string>(), "Output file in which the program will write the compressed output")
        ("observers", po::value<std::string>(), "Output file in which the observers will write their logs")
        ("numbers", "Fragment and refine the input file and output the number of chunks");

    po::variables_map vm;
    po::command_line_parser parser(argc, argv);
    parser.options(options);
    po::store(parser.run(), vm);
    po::notify(vm);

    if (vm.count("help")) {
        std::cout << options << std::endl;
        exit(0);
    }

    if (!vm.count("file")) {
        std::cout << "-f or --file required" << std::endl;
        exit(1);
    }

    args._lua_file = vm["file"].as<std::string>();
    
    if (vm.count("orig")) {
        args._orig = true;
    } else {
        args._orig = false;
    }

    if (vm.count("smart")) {
        args._smart = true;
    } else {
        args._smart = false;
    }

    if (vm.count("auto")) {
        args._auto = true;
    } else {
        args._auto = false;
    }

    if (vm.count("numbers")) {
        args._numbers = true;
    } else {
        args._numbers = false;
    }

    if (vm.count("lua-output-file")) {
        args._lua_output_file = vm["lua-output-file"].as<std::string>();
        if (!vm.count("lua-output-file-mode")) {
            std::cerr << "[WARN] No mode specified for output file, will use 'a' by security" << std::endl;
            args._lua_output_file_mode = 'a';
        } else {
            char mode = vm["lua-output-file-mode"].as<char>();
            if (mode != 'a' && mode != 'w') {
                std::cerr << "[WARN] Unrecognized mode '" << mode << " 'for output filen will use 'a' by security" << std::endl;
                args._lua_output_file_mode = 'a';
            } else {
                args._lua_output_file_mode = mode;
            }
        }
    } else {
        std::cerr << "[WARN] No output file specified, will use /dev/null" << std::endl;
        args._lua_output_file = "/dev/null";
        args._lua_output_file_mode = 'w';
    }
    
    if (vm.count("output")) {
        args._output = std::make_optional(vm["output"].as<std::string>());
    }

    if (vm.count("observers")) {
        args._observers = std::make_optional(vm["observers"].as<std::string>());
    }
}

void start_sol(CLIArgs const& args) {
    sol::state lua;
    lua.open_libraries(sol::lib::base, sol::lib::io, sol::lib::table, sol::lib::package);

    sol::table layers = lua.create_table_with();
    layers["FRAGMENT"] = Layers::FRAGMENT;
    layers["REFINE"] = Layers::REFINE;
    layers["COMPRESS"] = Layers::COMPRESS;
    layers["DEDUPLICATE"] = Layers::DEDUPLICATE;
    layers["REORDER"] = Layers::REORDER;
    lua["Layers"] = layers;

    sol::table compressions = lua.create_table_with();
    compressions["GZIP"] = Compressions::GZIP;
    compressions["BZIP2"] = Compressions::BZIP;
    compressions["NONE"] = Compressions::NONE;
    lua["Compressions"] = compressions;

    /* sol::table roles = lua.create_table_with();
    roles["PRODUCER"] = FIFORole::PRODUCER;
    roles["CONSUMER"] = FIFORole::CONSUMER;
    lua["Roles"] = roles; */

    /* sol::table reconfigurations = lua.create_table_with();
    reconfigurations["PHASE"] = FIFOReconfigure::PHASE;
    reconfigurations["GRADIENT"] = FIFOReconfigure::GRADIENT;
    lua["Reconfigurations"] = reconfigurations; */

    sol::table modes = lua.create_table_with();
    modes["orig"] = args._orig;
    // modes["mutex"] = args._mutex;
    modes["smart"] = args._smart;
    modes["auto"] = args._auto;
    lua["Modes"] = modes;

    sol::table debug_output = lua.create_table_with();
    debug_output["file"] = args._lua_output_file;
    debug_output["mode"] = args._lua_output_file_mode;
    lua["Output"] = debug_output;

    sol::usertype<DedupData> dedup_data_type = lua.new_usertype<DedupData>("DedupData");
    dedup_data_type["input_filename"] = &DedupData::_input_filename;
    dedup_data_type["output_filename"] = &DedupData::_output_filename; 
    dedup_data_type["preloading"] = &DedupData::_preloading;
    dedup_data_type["new_fifo"] = &DedupData::new_fifo;
    dedup_data_type["debug_timestamps"] = &DedupData::_debug_timestamps;
    dedup_data_type["algorithm"] = &DedupData::_algorithm;
    dedup_data_type["compression"] = &DedupData::_compression;
    dedup_data_type["dump"] = &DedupData::dump;
    dedup_data_type["run_orig"] = &DedupData::run_orig;
    // dedup_data_type["run_mutex"] = &DedupData::run_mutex;
    // dedup_data_type["run_smart"] = &DedupData::run_smart;
    dedup_data_type["run_auto"] = &DedupData::run_auto;
    dedup_data_type["push_layer"] = &DedupData::push_layer_data;
    dedup_data_type["set_observers"] = &DedupData::set_observers;
    dedup_data_type["run_numbers"] = &DedupData::run_numbers;

    sol::usertype<LayerData> layer_datatype = lua.new_usertype<LayerData>("LayerData");
    layer_datatype["push"] = &LayerData::push;

    sol::usertype<ThreadData> thread_datatype = lua.new_usertype<ThreadData>("ThreadData");
    thread_datatype["push_input"] = &ThreadData::push_input;
    thread_datatype["push_output"] = &ThreadData::push_output;
    thread_datatype["push_extra"] = &ThreadData::push_extra;

    sol::usertype<FIFOData> fifo_data_type = lua.new_usertype<FIFOData>("FIFOData");
    fifo_data_type["min"] = &FIFOData::_min; 
    fifo_data_type["n"] = &FIFOData::_n;
    fifo_data_type["max"] = &FIFOData::_max;
    fifo_data_type["with_work_threshold"] = &FIFOData::_with_work_threshold;
    fifo_data_type["no_work_threshold"] = &FIFOData::_no_work_threshold;
    fifo_data_type["critical_threshold"] = &FIFOData::_critical_threshold;
    fifo_data_type["increase_mult"] = &FIFOData::_increase_mult;
    fifo_data_type["decrease_mult"] = &FIFOData::_decrease_mult;
    fifo_data_type["history_size"] = &FIFOData::_history_size;
    fifo_data_type["reconfigure"] = &FIFOData::_reconfigure;
    fifo_data_type["dump"] = &FIFOData::dump;
    fifo_data_type["duplicate"] = &FIFOData::duplicate;
    fifo_data_type["change_step_after"] = &FIFOData::_change_step_after;
    fifo_data_type["new_step"] = &FIFOData::_new_step;

    if (args._output) {
        lua["output"] = *args._output;
    }

    if (args._observers) {
        lua["observers_path"] = *args._observers;
    }

    std::cout << "Running lua script file" << std::endl;
    lua.script_file(args._lua_file);
    std::cout << "Done running lua script file" << std::endl;
}

/* ReorderData* reorder_data;
size_t reorder_data_n;

ReorderTreeData* reorder_tree_data;
size_t reorder_tree_data_n;

DedupCompressData* dedupcompress_data;
size_t dedupcompress_data_n;

TSLogger<CompressData> compress_logger;
TSLogger<DeduplicateData> deduplicate_logger;
TSLogger<unsigned int> deduplicate_locks_logger; */

/*--------------------------------------------------------------------------*/
int main(int argc, char** argv) {
#ifdef FIFO_PLUS_TIMESTAMP_DATA
  Globals::start_time = steady_clock::now();
#endif

  Globals::_start_time = steady_clock::now();

#ifdef PARSEC_VERSION
#define __PARSEC_STRING(x) #x
#define __PARSEC_XSTRING(x) __PARSEC_STRING(x)
  printf("PARSEC Benchmark Suite Version "__PARSEC_XSTRING(PARSEC_VERSION)"\n");
#else
  printf("PARSEC Benchmark Suite\n");
#endif //PARSEC_VERSION
#ifdef ENABLE_PARSEC_HOOKS
        __parsec_bench_begin(__parsec_dedup);
#endif //ENABLE_PARSEC_HOOKS

  // int32 compress = TRUE;

  //We force the sha1 sum to be integer-aligned, check that the length of a sha1 sum is a multiple of unsigned int
  assert(SHA1_LEN % sizeof(unsigned int) == 0);

  CLIArgs args;
  parse_args(argc, argv, args);

  /* lua_State* L = init_lua();
  bool result = luaL_dofile(L, argv[1]);
  if (result) {
    printf("Error while running Lua file %s:\n%s\n", argv[1], lua_tolstring(L, -1, nullptr));
    lua_close(L);
    exit(-1);
  } */

  start_sol(args);
  /* std::ofstream log_stream("fifo_active_wait.log", std::ios::out);
  if (!log_stream) {
      std::cout << ":( :( :( :(" << std::endl;
      return 0;
  }

  for (size_t i = 0; i < _log_n; ++i) {
      TimestampData& td = timestamp_data[i];
      log_stream << td.begin << ", " << td.end << ", " << td.diff << ", " << td.count << std::endl;
  }

  std::ofstream reorder_stream("reorder_active_wait.log", std::ios::out);
  for (size_t i = 0; i < reorder_data_n; ++i) {
    ReorderData& rd = reorder_data[i];
    reorder_stream << rd.time << ", " << rd.l1 << ", " << rd.l2 << std::endl;
  } */

  /* std::ofstream reorder_tree_stream("reorder_tree.log", std::ios::out);
  for (size_t i = 0; i < reorder_tree_data_n; ++i) {
    ReorderTreeData& rd = reorder_tree_data[i];
    reorder_tree_stream << rd.time << "," << rd.nb_elements << std::endl;
  } */

  /* std::ofstream dedupcompress_stream("dedupcompress.log", std::ios::out);
  for (int i = 0; i < dedupcompress_data_n; ++i) {
      dedupcompress_stream << dedupcompress_data[i].l1 << ", " << dedupcompress_data[i].l2 << ", ";
      if (dedupcompress_data[i].compress) {
          dedupcompress_stream << "compress";
      } else {
          dedupcompress_stream << "deduplicate";
      }

      dedupcompress_stream << std::endl;
  }

  free(dedupcompress_data);

  std::ofstream compress_data_stream("compress.log", std::ios::out);
  compress_logger.log([&compress_data_stream](CompressData const& data) -> void {
            compress_data_stream << data.l1 << ", " << data.l2 << ", " << data.in << ", " << data.out << std::endl;
          });

  std::ofstream deduplicate_data_stream("dedup_to_compress.log", std::ios::out);
  deduplicate_logger.log([&deduplicate_data_stream](DeduplicateData const& data) -> void {
            deduplicate_data_stream << data.l1 << ", " << data.l2 << ", " << data.arrived << ", " << data.push << std::endl;
        });

  std::ofstream deduplicate_locks_stream("dedup_lock.log", std::ios::out);
  deduplicate_locks_logger.log([&deduplicate_locks_stream](int const& l) -> void {
          deduplicate_locks_stream << l << std::endl;
          }); */
  /* for (auto const& [addr, data]: _semaphore_data) {
    const auto& [name, arr] = data;
    std::cout << "[End] FIFO " << name << " => " << arr[0] << ", " << arr[1] << std::endl;
  } */ 

  /*conf = (config_t *) malloc(sizeof(config_t));
  if (conf == NULL) {
    EXIT_TRACE("Memory allocation failed\n");
  }

  strcpy(conf->outfile, "");
  conf->compress_type = COMPRESS_GZIP;
  conf->preloading = 0;
  conf->nthreads = 1;
  conf->verbose = 0;

  //parse the args
  int ch;
  opterr = 0;
  optind = 1;
  while (-1 != (ch = getopt(argc, argv, "cupvo:i:w:t:h"))) {
    switch (ch) {
    case 'c':
      compress = TRUE;
      strcpy(conf->infile, "test.txt");
      strcpy(conf->outfile, "out.ddp");
      break;
    case 'u':
      compress = FALSE;
      strcpy(conf->infile, "out.ddp");
      strcpy(conf->outfile, "new.txt");
      break;
    case 'w':
      if (strcmp(optarg, "gzip") == 0)
        conf->compress_type = COMPRESS_GZIP;
      else if (strcmp(optarg, "bzip2") == 0) 
        conf->compress_type = COMPRESS_BZIP2;
      else if (strcmp(optarg, "none") == 0)
        conf->compress_type = COMPRESS_NONE;
      else {
        fprintf(stdout, "Unknown compression type `%s'.\n", optarg);
        usage(argv[0]);
        return -1;
      }
      break;
    case 'o':
      strcpy(conf->outfile, optarg);
      break;
    case 'i':
      strcpy(conf->infile, optarg);
      break;
    case 'h':
      usage(argv[0]);
      return -1;
    case 'p':
      conf->preloading = TRUE;
      break;
    case 't':
      conf->nthreads = atoi(optarg);
      break;
    case 'v':
      conf->verbose = TRUE;
      break;
    case '?':
      fprintf(stdout, "Unknown option `-%c'.\n", optopt);
      usage(argv[0]);
      return -1;
    }
  }

#ifndef ENABLE_BZIP2_COMPRESSION
 if (conf->compress_type == COMPRESS_BZIP2){
    printf("Bzip2 compression not supported\n");
    exit(1);
  }
#endif

#ifndef ENABLE_GZIP_COMPRESSION
 if (conf->compress_type == COMPRESS_GZIP){
    printf("Gzip compression not supported\n");
    exit(1);
  }
#endif

#ifndef ENABLE_STATISTICS
 if (conf->verbose){
    printf("Statistics collection not supported\n");
    exit(1);
  }
#endif

#ifndef ENABLE_PTHREADS
 if (conf->nthreads != 1){
    printf("Number of threads must be 1 (serial version)\n");
    exit(1);
  }
#endif

  if (compress) {
    Encode(conf);
  } else {
    Decode(conf);
  }*/

  free(conf);

#ifdef ENABLE_PARSEC_HOOKS
  __parsec_bench_end();
#endif

  std::cout << "End" << std::endl;
  return 0;
}

