#include <unistd.h>
#include <string.h>
#include <time.h>

#include <iomanip>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <sol/sol.hpp>

#include "util.h"
#include "debug.h"
#include "dedupdef.h"
#include "encoder.h"
#include "decoder.h"
#include "config.h"
#include "queue.h"

#include "lua.hpp"
#include "lua_core.h"
#include "fifo_plus.tpp"

#ifdef ENABLE_DMALLOC
#include <dmalloc.h>
#endif //ENABLE_DMALLOC

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

#ifdef ENABLE_PARSEC_HOOKS
#include <hooks.h>
#endif //ENABLE_PARSEC_HOOKS

using json = nlohmann::json;

config_t * conf;

struct hashtable* cache;

/*--------------------------------------------------------------------------*/
static void
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
}

std::chrono::time_point<steady_clock> Globals::start_time;

void start_sol(const char* file) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);

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

    sol::table roles = lua.create_table_with();
    roles["PRODUCER"] = FIFORole::PRODUCER;
    roles["CONSUMER"] = FIFORole::CONSUMER;
    lua["Roles"] = roles;

    sol::table reconfigurations = lua.create_table_with();
    reconfigurations["PHASE"] = FIFOReconfigure::PHASE;
    reconfigurations["GRADIENT"] = FIFOReconfigure::GRADIENT;
    lua["Reconfigurations"] = reconfigurations;    

    sol::usertype<DedupData> dedup_data_type = lua.new_usertype<DedupData>("DedupData");
    dedup_data_type["input_filename"] = &DedupData::_input_filename;
    dedup_data_type["output_filename"] = &DedupData::_output_filename; 
    dedup_data_type["nb_threads"] = &DedupData::_nb_threads;
    dedup_data_type["preloading"] = &DedupData::_preloading;
    dedup_data_type["add_data"] = &DedupData::push_fifo_data;
    dedup_data_type["debug_timestamps"] = &DedupData::_debug_timestamps;
    dedup_data_type["algorithm"] = &DedupData::_algorithm;
    dedup_data_type["compression"] = &DedupData::_compression;
    dedup_data_type["dump"] = &DedupData::dump;
    dedup_data_type["run_orig"] = &DedupData::run_orig;
    dedup_data_type["run_mutex"] = &DedupData::run_mutex;
    dedup_data_type["run_smart"] = &DedupData::run_smart;

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

    lua.script_file(file);
}

/*--------------------------------------------------------------------------*/
int main(int argc, char** argv) {
  Globals::start_time = steady_clock::now();
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

  int32 compress = TRUE;

  //We force the sha1 sum to be integer-aligned, check that the length of a sha1 sum is a multiple of unsigned int
  assert(SHA1_LEN % sizeof(unsigned int) == 0);

  if (argc != 2) {
    printf("Usage: ./dedup <input_lua_file>\n");
    exit(-1);
  }

  /* lua_State* L = init_lua();
  bool result = luaL_dofile(L, argv[1]);
  if (result) {
    printf("Error while running Lua file %s:\n%s\n", argv[1], lua_tolstring(L, -1, nullptr));
    lua_close(L);
    exit(-1);
  } */

  start_sol(argv[1]);

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

  return 0;
}

