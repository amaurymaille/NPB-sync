#include <unistd.h>
#include <string.h>
#include <time.h>

#include <sstream>

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

/* Forward decl */
static DedupData* check_dedup_data(lua_State* L);

/* Constructors, destructor */

static int dedup_data_new(lua_State* L) {
    DedupData* data = (DedupData*)lua_newuserdata(L, sizeof(DedupData));
    luaL_getmetatable(L, "LuaBook.DedupData");
    lua_setmetatable(L, -2);
    data->_fifo_data = new std::map<Layers, std::map<Layers, std::map<FIFORole, FIFOData>>>();
    data->_input_filename = new std::string();
    data->_output_filename = new std::string();
    return 1;
}

static int dedup_data_destroy(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    delete data->_fifo_data;
    delete data->_input_filename;
    delete data->_output_filename;
    return 0;
}

static FIFOData* new_fifo_data(lua_State* L) {
    FIFOData* data = (FIFOData*)lua_newuserdata(L, sizeof(FIFOData));
    luaL_getmetatable(L, "LuaBook.FIFOData");
    lua_setmetatable(L, -2);
    return data;
}

static int fifo_data_new(lua_State* L) {
    new_fifo_data(L);
    return 1;
}

/* DedupData */

DedupData* check_dedup_data(lua_State* L) {
    void* res = luaL_checkudata(L, 1, "LuaBook.DedupData");
    luaL_argcheck(L, res != nullptr, 1, "DedupData expected");
    return (DedupData*)res;
}

static int dedup_data_set_input_file(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    const char* filename = luaL_checkstring(L, 2);

    FILE* f = fopen(filename, "r");
    if (!f) {
        std::ostringstream error;
        error << "Input file " << filename << " does not exist" << std::endl;
        luaL_argerror(L, 2, error.str().c_str());
    }

    fclose(f);
    *data->_input_filename = filename;

    return 0;
}

static int dedup_data_set_output_file(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    const char* filename = luaL_checkstring(L, 2);

    FILE* f = fopen(filename, "w");
    if (!f) {
        std::ostringstream error;
        error << "Unable to open output file " << filename << std::endl;
        luaL_argerror(L, 2, error.str().c_str());
    }

    fclose(f);
    *data->_output_filename = filename;

    return 0;
}

static int dedup_data_set_layer_configuration(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    Layers source = (Layers)luaL_checkinteger(L, 2);
    Layers destination = (Layers)luaL_checkinteger(L, 3);
    FIFORole role = (FIFORole)luaL_checkinteger(L, 4);
    FIFOData* fifo = (FIFOData*)luaL_checkudata(L, 5, "LuaBook.FIFOData");

    if (source < FRAGMENT || source >= REORDER) {
        std::ostringstream error;
        error << source << " is not a valid source layer" << std::endl;
        luaL_argerror(L, 2, error.str().c_str());
    }

    if (destination <= FRAGMENT || destination > REORDER) {
        std::ostringstream error;
        error << destination << " is not a valid destination layer" << std::endl;
        luaL_argerror(L, 3, error.str().c_str());
    }

    if (role != FIFORole::PRODUCER && role != FIFORole::CONSUMER) {
        std::ostringstream error;
        error << (int)role << " is not a valid role" << std::endl;
        luaL_argerror(L, 4, error.str().c_str());
    }

    luaL_argcheck(L, fifo != nullptr, 5, "Expected FIFOData");

    if (data->_fifo_data->find(source) != data->_fifo_data->end()) {
        if ((*data->_fifo_data)[source].find(destination) != (*data->_fifo_data)[source].end()) {
            if ((*data->_fifo_data)[source][destination].find(role) != (*data->_fifo_data)[source][destination].end()) {
                std::ostringstream error;
                error << "The combination (" << source << ", " << destination << ", " << (int)role << ") has already been specified" << std::endl;
                luaL_argerror(L, 2, error.str().c_str());
            }
        }
    }

    /* auto source_data = (*data->_fifo_data)[source];
    if (source_data.find(destination) != source_data.end()) {
        std::ostringstream error;
        error << destination << " has already been specified as a destination layer" << std::endl;
        luaL_argerror(L, 3, error.str().c_str());
    } */

    (*data->_fifo_data)[source][destination][role] = *fifo;
    return 0;
}

static int dedup_data_set_nb_threads(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    int nb_threads = luaL_checkinteger(L, 2);

    luaL_argcheck(L, nb_threads >= 1, 2, "Number of threads must be greater than 1");
    data->_nb_threads = nb_threads;

    return 0;
}

static void dedup_data_run_check_layers(lua_State* L, DedupData const* data) {
    if ((*data->_fifo_data)[FRAGMENT].find(REFINE) == (*data->_fifo_data)[FRAGMENT].end()) {
        luaL_error(L, "No REFINE layer provided for FRAGMENT layer\n");
    }

    if ((*data->_fifo_data)[REFINE].find(DEDUPLICATE) == (*data->_fifo_data)[REFINE].end()) {
        luaL_error(L, "No DEDUPLICATE layer provided for REFINE layer\n");
    }

    if ((*data->_fifo_data)[DEDUPLICATE].find(COMPRESS) == (*data->_fifo_data)[DEDUPLICATE].end()) {
        luaL_error(L, "No COMPRESS layer provided for DEDUPLICATE layer\n");
    }

    if ((*data->_fifo_data)[DEDUPLICATE].find(REORDER) == (*data->_fifo_data)[DEDUPLICATE].end()) {
        luaL_error(L, "No REORDER layer provided for DEDUPLICATE layer\n");
    }

    /* if (data->_fifo_data[COMPRESS].find(REORDER) == data->_fifo_data[COMPRESS].end()) {
        luaL_error(L, "No REORDER layer provided for COMPRESS layer\n");
    } */
}

static int dedup_data_run(lua_State* L) {
    DedupData* data = check_dedup_data(L);

    // Do not consider the two paths leading to reorder separately
    if (data->_fifo_data->size() != COMPRESS) {
        luaL_error(L, "Cannot run DedupData. Expected %d source layers, only got %d\n", REORDER, data->_fifo_data->size());
    }

    dedup_data_run_check_layers(L, data);

    unsigned long long diff = Encode(*data);
    lua_pushinteger(L, diff);
    return 1;
}

static int dedup_data_set_compression_type(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    int compression_type = luaL_checkinteger(L, 2);

    if (compression_type != Compressions::NONE && compression_type != Compressions::GZIP && compression_type != Compressions::BZIP) {
        std::ostringstream error;
        error << compression_type << " is not a valid compression type" << std::endl;
        luaL_argerror(L, 2, error.str().c_str());
    }

#ifndef ENABLE_BZIP2_COMPRESSION
    if (compression_type == Compressions::BZIP) {
        luaL_argerror(L, 2, "BZIP2 compression not supported\n");
    }
#endif

#ifndef ENABLE_GZIP_COMPRESSION
    if (compression_type == Compressions::GZIP) {
        luaL_argerror(L, 2, "GZIP compression not supported\n");
    }
#endif

    data->_compression = (Compressions)compression_type;
    return 0;
}

static bool luaL_checkboolean(lua_State* L, int index) {
    if (lua_isboolean(L, index)) {
        return lua_toboolean(L, index);
    } else {
        luaL_error(L, "bad argument #%d (boolean expected, got %s\n", lua_typename(L, lua_type(L, index)));
        return false; // Not reached because luaL_error never returns. Shut up GCC.
    }
}

static int dedup_data_set_preloading(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    bool preloading = luaL_checkboolean(L, 2);
    data->_preloading = preloading;
    return 0;
}

static int dedup_data_get_input_file(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    lua_pushstring(L, data->_input_filename->c_str());
    return 1;
}

static int dedup_data_get_output_file(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    lua_pushstring(L, data->_output_filename->c_str());
    return 1;
}

static int dedup_data_get_nb_threads(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    lua_pushinteger(L, data->_nb_threads);
    return 1;
}

static int dedup_data_get_layer_configuration(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    Layers src = (Layers)luaL_checkinteger(L, 2);
    Layers dst = (Layers)luaL_checkinteger(L, 3);
    FIFORole role = (FIFORole)luaL_checkinteger(L, 4);

    if (src < FRAGMENT || src > REORDER) {
        std::ostringstream stream;
        stream << (int)src << " is not a valid source layer" << std::endl;
        luaL_argerror(L, 2, stream.str().c_str());
    }

    if (dst < FRAGMENT || dst > FRAGMENT) {
        std::ostringstream stream;
        stream << (int)src << " is not a valid destination layer" << std::endl;
        luaL_argerror(L, 3, stream.str().c_str());
    }

    if (role != FIFORole::CONSUMER && role != FIFORole::PRODUCER) {
        std::ostringstream stream;
        stream << (int)role << " is not a valid role" << std::endl;
        luaL_argerror(L, 4, stream.str().c_str());
    }

    if (data->_fifo_data->find(src) == data->_fifo_data->end()) {
        lua_pushnil(L);
    } else if ((*data->_fifo_data)[src].find(dst) == (*data->_fifo_data)[src].end()) {
        lua_pushnil(L);
    } else {
        lua_newtable(L); // Stack = table

        lua_getglobal(L, "Roles"); // Stack = table, Roles
        lua_pushstring(L, "PRODUCER"); // Stack = table, Roles, PRODUCER
        lua_gettable(L, -2); // Stack = table, Roles[PRODUCER]

        if ((*data->_fifo_data)[src][dst].find(FIFORole::PRODUCER) == (*data->_fifo_data)[src][dst].end()) {
            lua_pushnil(L); // Stack = table, Roles[PRODUCER], nil
        } else {
            FIFOData* result = new_fifo_data(L); // Stack = table, Roles[PRODUCER], userdata
            *result = (*data->_fifo_data)[src][dst][FIFORole::PRODUCER];
        }
        lua_settable(L, -3); // Stack = table

        lua_getglobal(L, "Roles"); // Stack = table, Roles
        lua_pushstring(L, "CONSUMER"); // Stack = table, Roles, CONSUMER
        lua_gettable(L, -2); // Stack, Roles[CONSUMER]

        if ((*data->_fifo_data)[src][dst].find(FIFORole::CONSUMER) == (*data->_fifo_data)[src][dst].end()) {
            lua_pushnil(L); // Stack = table, Roles[CONSUMER], nil
        } else {
            FIFOData* result = new_fifo_data(L); // Stack = table, Roles[CONSUMER], userdata
            *result = (*data->_fifo_data)[src][dst][FIFORole::CONSUMER];
        }
        lua_settable(L, -3); // Stack = table
    }

    return 1;
}

static int dedup_data_run_default(lua_State* L) {
    DedupData* data = check_dedup_data(L);
    unsigned long long diff = EncodeDefault(*data);
    lua_pushinteger(L, diff);
    return 1;
}

static luaL_Reg dedup_data_methods[] = {
    { "SetInputFile", &dedup_data_set_input_file },
    { "GetInputFile", &dedup_data_get_input_file },
    { "SetOutputFile", &dedup_data_set_output_file },
    { "GetOutputFile", &dedup_data_get_output_file },
    { "SetLayerConfiguration", &dedup_data_set_layer_configuration },
    { "SetNbThreads", &dedup_data_set_nb_threads },
    { "GetNbThreads", &dedup_data_get_nb_threads },
    { "SetCompressionType", &dedup_data_set_compression_type },
    { "SetPreloading", &dedup_data_set_preloading },
    { "GetLayersConfiguration", &dedup_data_get_layer_configuration },
    { "Run", &dedup_data_run },
    { "RunDefault", &dedup_data_run_default },
    { nullptr, nullptr }
};

/* FIFOData */

static FIFOData* check_fifo_data(lua_State* L) {
    void* ud = luaL_checkudata(L, 1, "LuaBook.FIFOData");
    luaL_argcheck(L, ud != nullptr, 1, "Expected FIFOData");
    return (FIFOData*)ud;
}

static int fifo_data_set_n(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    int n = luaL_checkinteger(L, 2);

    luaL_argcheck(L, n >= 1, 2, "n must be greater or equal to 1");
    data->_n = n;

    return 0;
}

static int fifo_data_set_work(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    int threshold = luaL_checkinteger(L, 2);

    luaL_argcheck(L, threshold >= 1, 2, "Work threshold must be greater or equal to 1");
    data->_with_work_threshold = threshold;

    return 0;
}

static int fifo_data_set_no_work(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    int threshold = luaL_checkinteger(L, 2);

    luaL_argcheck(L, threshold >= 1, 2, "No work threshold must be greater or equal to 1");
    data->_no_work_threshold = threshold;

    return 0;
}

static int fifo_data_set_critical(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    int threshold = luaL_checkinteger(L, 2);

    luaL_argcheck(L, threshold >= 1, 2, "Work amount threshold must be greater or equal to 1");
    data->_critical_threshold = threshold;

    return 0;
}

static int fifo_data_set_multipliers(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    float increase = luaL_checknumber(L, 2);
    float decrease = luaL_checknumber(L, 3);

    luaL_argcheck(L, increase >= 1.f, 2, "Increase multiplier must be greater or equal to 1");
    luaL_argcheck(L, decrease > 0.f && decrease <= 1.f, 3, "Decrease multiplier must be strictly greater than 0 and at most 1");

    data->_increase_mult = increase;
    data->_decrease_mult = decrease;

    return 0;
}

static int fifo_data_set_history_size(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    int history_size = luaL_checknumber(L, 2);

    luaL_argcheck(L, history_size > 0, 2, "History size must be strictly positive");

    data->_history_size = history_size;
    return 0;
}

static int fifo_data_allow_reconfiguration(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    if (lua_gettop(L) == 1) {
        data->_reconfigure = true;
    } else {
        data->_reconfigure = luaL_checkboolean(L, 2);
    }
    return 0;
}

static int fifo_data_is_reconfiguration_allowed(lua_State* L) {
    FIFOData* data = check_fifo_data(L);
    lua_pushboolean(L, data->_reconfigure);
    return 1;
}

static luaL_Reg fifo_data_methods[] = {
    { "SetN", &fifo_data_set_n },
    { "SetWork", &fifo_data_set_work },
    { "SetNoWork", &fifo_data_set_no_work },
    { "SetCritical", &fifo_data_set_critical },
    { "SetMultipliers", &fifo_data_set_multipliers },
    { "SetHistorySize", &fifo_data_set_history_size },
    { "AllowReconfiguration", &fifo_data_allow_reconfiguration },
    { "IsReconfigurable", &fifo_data_is_reconfiguration_allowed },
    { nullptr, nullptr }
};

static lua_State* init_lua() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    /// DedupData
    luaL_newmetatable(L, "LuaBook.DedupData");
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);

    lua_register(L, "DedupDataDestroy", dedup_data_destroy);

    lua_pushstring(L, "__gc");
    lua_getglobal(L, "DedupDataDestroy");
    lua_settable(L, -3);

    luaL_setfuncs(L, dedup_data_methods, 0);

    /// FIFOData
    luaL_newmetatable(L, "LuaBook.FIFOData");
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);

    luaL_setfuncs(L, fifo_data_methods, 0);

    lua_pop(L, 2);

    lua_register(L, "DedupData", dedup_data_new);
    lua_register(L, "FIFOData", fifo_data_new);

    /// Register layers table
    lua_newtable(L);
    lua_pushstring(L, "FRAGMENT");
    lua_pushinteger(L, FRAGMENT);
    lua_pushstring(L, "REFINE");
    lua_pushinteger(L, REFINE);
    lua_pushstring(L, "DEDUPLICATE");
    lua_pushinteger(L, DEDUPLICATE);
    lua_pushstring(L, "COMPRESS");
    lua_pushinteger(L, COMPRESS);
    lua_pushstring(L, "REORDER");
    lua_pushinteger(L, REORDER);

    for (int i = 5; i > 0; --i) {
        lua_settable(L, -2 * (i + 1) + 1);
    }

    lua_setglobal(L, "Layers");

    /// Registers compression table
    lua_newtable(L);
    lua_pushstring(L, "GZIP");
    lua_pushinteger(L, Compressions::GZIP);
    lua_pushstring(L, "BZIP2");
    lua_pushinteger(L, Compressions::BZIP);
    lua_pushstring(L, "NONE");
    lua_pushinteger(L, Compressions::NONE);

    for (int i = 3; i > 0; --i) {
        lua_settable(L, -2 * (i + 1) + 1);
    }

    lua_setglobal(L, "Compressions");

    /// Registers roles table
    lua_newtable(L);
    lua_pushstring(L, "PRODUCER");
    lua_pushinteger(L, (int)FIFORole::PRODUCER);
    lua_pushstring(L, "CONSUMER");
    lua_pushinteger(L, (int)FIFORole::CONSUMER);

    for (int i = 2; i > 0; --i) {
        lua_settable(L, -2 * (i + 1) + 1);
    }

    lua_setglobal(L, "Roles");

    return L;
}
/*--------------------------------------------------------------------------*/
int main(int argc, char** argv) {
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

  lua_State* L = init_lua();
  bool result = luaL_dofile(L, argv[1]);
  if (result) {
    printf("Error while running Lua file %s:\n%s\n", argv[1], lua_tolstring(L, -1, nullptr));
    lua_close(L);
    exit(-1);
  }

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

