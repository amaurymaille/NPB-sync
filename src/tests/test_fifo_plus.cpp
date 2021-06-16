#include <cstring>

#include <iostream>

#include <dlfcn.h>
#include <unistd.h>

#include "test_fifo_plus.h"

#include "lua.hpp"

namespace Basic {
    template<typename T>
    void launch_test(FIFOPlusPopPolicy pop_policy,
                     /* unsigned int n,
                     unsigned int no_work,
                     unsigned int with_work,
                     unsigned int work_amount, */
                     std::vector<ThreadCreateData>& threads,
                     void* (*prod_fn)(void*), 
                     void* (*cons_fn)(void*)) {
        PThreadThreadIdentifier* identifier = new PThreadThreadIdentifier();
        // identifier->register_thread();

        FIFOPlus<T>* fifo = new FIFOPlus<T>(pop_policy, identifier, threads.size());
        /* fifo->set_role(FIFORole::PRODUCER);
        fifo->set_n(n);
        fifo->set_thresholds(no_work, with_work, work_amount); */

        for (ThreadCreateData& data: threads) {
            ThreadInitData<T>* init_data = new ThreadInitData<T>;
            init_data->_tss = data._tss;
            init_data->_fifo = fifo;

            identifier->pthread_create(&data._thread_id, NULL, data._tss._role == FIFORole::CONSUMER ? cons_fn : prod_fn, init_data);
        }

        for (ThreadCreateData const& data: threads) {
            pthread_join(data._thread_id, NULL);
        }

        delete fifo;
        // return std::unique_ptr<FIFOPlus<T>>(fifo);
    }
}

namespace Lua {

struct TestData {
    FIFOPlusPopPolicy _policy;
    Basic::ThreadSpecificData _main_thread;
    char _consumer_function[100];
    char _producer_function[100];
    char _functions_library[100];
    std::vector<Basic::ThreadSpecificData>* _threads;
};

static int test_data_new(lua_State* L) {
    TestData* data = (TestData*)lua_newuserdata(L, sizeof(TestData));
    data->_threads = new std::vector<Basic::ThreadSpecificData>();
    luaL_getmetatable(L, "LuaBook.TestData");

    lua_setmetatable(L, -2);

    return 1;
}

static TestData* check_test_data(lua_State* L) {
    TestData* data = (TestData*)luaL_checkudata(L, 1, "LuaBook.TestData");
    luaL_argcheck(L, data != nullptr, 1, "TestData expected");
    return data;
}

static int test_data_destroy(lua_State* L) {
    TestData* data = check_test_data(L);
    delete data->_threads;
    return 0;
}

static void populate_thread_data(lua_State* L, Basic::ThreadSpecificData& data) {
    int n = luaL_checkinteger(L, 2);
    int no_work = luaL_checkinteger(L, 3);
    int with_work = luaL_checkinteger(L, 4);
    int work_amount = luaL_checkinteger(L, 5);
    FIFORole role = (FIFORole)luaL_checkinteger(L, 6);

    luaL_argcheck(L, n >= 0, 2, "n must be positive");    
    luaL_argcheck(L, no_work >= 0, 3, "no_work must be positive");    
    luaL_argcheck(L, with_work >= 0, 4, "with_work must be positive");    
    luaL_argcheck(L, work_amount >= 0, 5, "work_amount must be positive");    

    data._n = n;
    data._no_work = no_work;
    data._with_work = with_work;
    data._work_amount = work_amount;
    data._role = role;
}

static int test_data_set_policy(lua_State* L) {
    TestData* data = check_test_data(L);
    int policy = luaL_checkinteger(L, 2);
    data->_policy = (FIFOPlusPopPolicy)policy;
    return 0;
}

static int test_data_register_thread_data(lua_State* L) {
    TestData* data = check_test_data(L);
    Basic::ThreadSpecificData thread_data;
    populate_thread_data(L, thread_data);
    data->_threads->push_back(thread_data);
    return 0;
}

static int test_data_register_main_thread_data(lua_State* L) {
    TestData* data = check_test_data(L);
    populate_thread_data(L, data->_main_thread);
    return 0;
}

static int test_data_register_producer_function(lua_State* L) {
    TestData* data = check_test_data(L);
    const char* producer_function = luaL_checkstring(L, 2);
    strncpy(data->_producer_function, producer_function, 100);
    return 0;
}

static int test_data_register_consumer_function(lua_State* L) {
    TestData* data = check_test_data(L);
    const char* consumer_function = luaL_checkstring(L, 2);
    strncpy(data->_consumer_function, consumer_function, 100);
    return 0;
}

static int test_data_register_functions_library(lua_State* L) {
    TestData* data = check_test_data(L);
    const char* functions_library = luaL_checkstring(L, 2);
    strncpy(data->_functions_library, functions_library, 100);
    return 0;
}

static void dump_test_data(TestData const* data) {
    std::cout << "policy = " << (unsigned int)data->_policy << std::endl
              << "main_thread = {" << std::endl
              << "\tn = " << data->_main_thread._n << std::endl
              << "\tno_work = " << data->_main_thread._no_work << std::endl
              << "\twith_work = " << data->_main_thread._with_work << std::endl
              << "\twork_amount = " << data->_main_thread._work_amount << std::endl
              << "\trole = " << (unsigned int)data->_main_thread._role << std::endl
              << "}" << std::endl;

    for (Basic::ThreadSpecificData const& thread_data: *(data->_threads)) {
        std::cout << "thread = {" << std::endl
                  << "\tn = " << thread_data._n << std::endl
                  << "\tno_work = " << thread_data._no_work << std::endl
                  << "\twith_work = " << thread_data._with_work << std::endl
                  << "\twork_amount = " << thread_data._work_amount << std::endl
                  << "\trole = " << (unsigned int)thread_data._role << std::endl
                  << "}" << std::endl;
    }


}

static int test_data_run(lua_State* L) {
    TestData* data = check_test_data(L);

    void* library = dlopen(data->_functions_library, RTLD_LAZY);
    void* (*consumer_function)(void*) = reinterpret_cast<void*(*)(void*)>(dlsym(library, data->_consumer_function));
    void* (*producer_function)(void*) = reinterpret_cast<void*(*)(void*)>(dlsym(library, data->_producer_function));

    std::vector<Basic::ThreadCreateData> thread_data;
    for (Basic::ThreadSpecificData const& tsd: *(data->_threads)) {
        Basic::ThreadCreateData create_data;
        create_data._tss = tsd;
        thread_data.push_back(create_data);
    }

    Basic::launch_test<int>(data->_policy,
        /* data->_main_thread._n,
        data->_main_thread._no_work,
        data->_main_thread._with_work,
        data->_main_thread._work_amount, */
        thread_data,
        producer_function,
        consumer_function
    );

    dlclose(library);
    return 0;
}

static luaL_Reg test_data_functions[] = {
    { "SetPolicy", test_data_set_policy },
    // { "RegisterMainThreadData", test_data_register_main_thread_data },
    { "RegisterThreadData", test_data_register_thread_data },
    { "RegisterProducerFunction", test_data_register_producer_function },
    { "RegisterConsumerFunction", test_data_register_consumer_function },
    { "RegisterFunctionsLibrary", test_data_register_functions_library },
    { "Run", test_data_run },
    { nullptr, nullptr }
};

lua_State* lua_init() {
    lua_State* L = luaL_newstate();
    luaL_openlibs(L);

    luaL_newmetatable(L, "LuaBook.TestData");
    lua_pushstring(L, "__index");
    lua_pushvalue(L, -2);
    lua_settable(L, -3);

    lua_register(L, "TestDataDestroy", test_data_destroy);

    lua_pushstring(L, "__gc");
    lua_getglobal(L, "TestDataDestroy");
    lua_settable(L, -3);

    luaL_setfuncs(L, test_data_functions, 0);
    lua_pop(L, 1);

    lua_register(L, "TestData", test_data_new);

    lua_newtable(L);
    lua_pushstring(L, "POP_WAIT");
    lua_pushinteger(L, (lua_Integer)FIFOPlusPopPolicy::POP_WAIT);
    lua_settable(L, -3);

    lua_pushstring(L, "POP_NO_WAIT");
    lua_pushinteger(L, (lua_Integer)FIFOPlusPopPolicy::POP_NO_WAIT);
    lua_settable(L, -3);

    lua_setglobal(L, "PopPolicy");

    lua_newtable(L);
    lua_pushstring(L, "CONSUMER");
    lua_pushinteger(L, (lua_Integer)FIFORole::CONSUMER);
    lua_pushstring(L, "PRODUCER");
    lua_pushinteger(L, (lua_Integer)FIFORole::PRODUCER);
    lua_settable(L, -5);
    lua_settable(L, -3);

    lua_setglobal(L, "Role");

    return L;
}

}

int main(int argc, char** argv) {
    std::string filename;
    if (argc == 1) {
        filename = "default.lua";
    } else {
        filename = std::string(argv[1]);
    }

    lua_State* L = Lua::lua_init();
    luaL_dofile(L, filename.c_str());
    lua_close(L);
    return 0;
}
