#ifndef CORE_H
#define CORE_H

#include <fstream>

#include "defines.h"
#include "time_log.h"
#include "utils.h"

template<typename M>
class Synchronizer {
protected:
    Synchronizer(M const& m, typename M::MatrixT& matrix) : _m(m), _matrix(matrix) {
        M::init_matrix_from(_matrix, m.get_matrix());
        M::assert_matrix_equals(_matrix, m.get_matrix());
    }
    
public:
    void assert_okay() {
        M::assert_matrix_equals(_matrix, _m.get_expected());
    }

protected:
    M const& _m;
    typename M::MatrixT& _matrix;
};

template<typename F, typename... Args>
static uint64 measure_time(F&& f, Args&&... args) {
    struct timespec begin, end;

    clock_gettime(CLOCK_MONOTONIC, &begin);
    f(std::forward<Args>(args)...);
    clock_gettime(CLOCK_MONOTONIC, &end);

    uint64 diff = clock_diff(&end, &begin);
    return diff;
}

template<class Synchronizer, class F, class... Args>
static uint64 measure_synchronizer_time(Synchronizer& synchronizer, F&& f, Args&&... args) {
    uint64 diff = measure_time([&]() {
        synchronizer.run(f, args...);
    });

    synchronizer.assert_okay();

    return diff;
}

class TimeCollector {
public:
    void add_time(TimeLog& log, unsigned int iteration, uint64 time) {
        log.add_time(iteration, double(time) / BILLION);
    }

    void add_time(TimeLog& log, unsigned int iteration, uint64 time, json debug_data) {
        log.add_time(iteration, double(time) / BILLION, debug_data);
    }

protected:
    std::vector<TimeLog> _times;
};

namespace JSON {
    namespace Top {
        static const std::string iterations("iterations");
        static const std::string runs("runs");
    }

    namespace Run {
        static const std::string synchronizer("synchronizer");
        static const std::string extras("extras");

        namespace Synchronizers {
            static const std::string sequential("sequential");
            static const std::string static_step_promise_plus("static_step_plus");
            static const std::string array_of_promises("array_of_promises");
            static const std::string promise_of_array("promise_of_array");
            static const std::string dsp_prod_only("dsp_prod_only");
            static const std::string dsp_cons_only("dsp_cons_only");
            static const std::string dsp_both("dsp_both");
            static const std::string dsp_prod_unblocks("dsp_prod_unblocks");
            static const std::string dsp_cons_unblocks("dsp_cons_unblocks");
            static const std::string dsp_both_unblocks("dsp_both_unblocks");
            static const std::string dsp_prod_timer("dsp_prod_timer");
            static const std::string dsp_prod_timer_unblocks("dsp_prod_timer_unblocks");
            static const std::string dsp_monitor("dsp_monitor");
            static const std::string dsp_never("dsp_never");
        }

        namespace Extras {
            static const std::string step("step");
        }
    }
}

#define DSP_ALL Synchronizers::dsp_prod_only, \
                Synchronizers::dsp_cons_only, \
                Synchronizers::dsp_both, \
                Synchronizers::dsp_prod_unblocks, \
                Synchronizers::dsp_cons_unblocks, \
                Synchronizers::dsp_both_unblocks, \
                Synchronizers::dsp_prod_timer, \
                Synchronizers::dsp_prod_timer_unblocks, \
                Synchronizers::dsp_monitor, \
                Synchronizers::dsp_never

class Runner {
public:
    Runner(std::string const& filename) {
        init_from_file(filename);
        validate();
    }

    void run() {
        unsigned int iterations = _data[JSON::Top::iterations].get<unsigned int>();
        json runs = _data[JSON::Top::runs];

        for (json run: runs) {
            process_run(iterations, run);
        }
    }

    
private:
    void init_from_file(std::string const& filename) {
        std::ifstream stream(filename);
        stream >> _data;
    }

    void validate() {
        validate_top();
        validate_runs();
    }

    void validate_top() {
        throw_if_not_present(JSON::Top::iterations);
        throw_if_not_present(JSON::Top::runs);
    }

    void validate_runs() {
        json runs = _data[JSON::Top::runs];

        for (json run: runs) {
            validate_run(run);
        }
    }

    void validate_run(json run) {
        throw_if_not_present_subobject(run, JSON::Run::synchronizer);

        validate_synchronizer(run[JSON::Run::synchronizer]);
    }

    virtual void validate_synchronizer(std::string const& synchronizer) = 0; 
    

    void throw_if_not_present(std::string const& key) {
        throw_if_not_present_subobject(_data, key);
    }

    void throw_if_not_present_subobject(json const& subobject, std::string const& key) {
        if (!subobject.contains(key)) {
            std::ostringstream stream;
            stream << "Key " << key << " not found in JSON" << std::endl;
            throw std::runtime_error(stream.str());
        }
    }

    virtual void process_run(unsigned int iterations, json run) = 0;
     
    json _data;
};


#endif
