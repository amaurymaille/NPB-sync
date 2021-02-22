#include <cstdlib>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <argv.h>
#include <core.h>
#include <dynamic_config.h>
#include <nlohmann/json.hpp>
#include <promises/dynamic_step_promise.h>

#include "dynamic_defines.h"
#include "lu.h"
#include "matrix_core.h"

namespace g = Globals;

template<typename MatrixValue, DynamicStepPromiseMode mode>
class LUSynchronizer : public Synchronizer<LUMatrix> {
public:
    LUSynchronizer(LUMatrix const& m, Matrix2D& matrix, int nb_threads, DynamicStepPromiseBuilder<MatrixValue, mode> const& builder) : Synchronizer(m, matrix), _n_threads(nb_threads), _builder(builder) {

    }

    template<typename F, typename... Args>
    void run(F&& f, Args&&... args) {
        (void)f;
        kernel_lu_combine_n_pp(_matrix, _bs, _xs, _builder);
    }

private:
    int _n_threads;
    DynamicStepPromiseBuilder<MatrixValue, mode> const& _builder;
    std::vector<Vector1D> _xs;
    std::vector<Vector1D> _bs;
};

class LUTimeCollector : public TimeCollector {
public:
    void print_times() {

    }

    void print_iterations_times() {

    }    

    void run_sequential(unsigned int iterations) {

    }

    void run_array_of_promises(unsigned int iterations) {

    }

    void run_promise_of_array(unsigned int iterations) {

    }

    template<DynamicStepPromiseMode mode>
    void run_dsp(unsigned int iterations, unsigned int step, 
                 const std::string& synchronizer_name,
                 const std::string& function) {
        unsigned int nb_threads = omp_nb_threads();
        TimeLog log(synchronizer_name, function);
        log.add_extra_arg("step", step);

        Matrix2D matrix(boost::extents[g::LU::DIM][g::LU::DIM]);
        DynamicStepPromiseBuilder<Matrix2D::element, mode> builder(g::LU::DIM, step, nb_threads);

        for (unsigned int i = 0; i < iterations; ++i) {
            LUSynchronizer<Matrix2D::element, mode> sync(sLU, matrix, nb_threads, builder);
            auto time = measure_synchronizer_time(sync, []{});
            add_time(log, i, time);
        }

        _times.push_back(log);
    }

    /* void run_dsp_prod_only(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_cons_only(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_both(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_prod_unblocks(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_cons_unblocks(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_both_unblocks(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_prod_timer(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_prod_timer_unblocks(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_monitor(unsigned int iterations, unsigned int step) {

    }

    void run_dsp_never(unsigned int iterations, unsigned int step) {

    } */
};

namespace JSON {
    namespace Run {
        static const std::vector<std::string> authorized_synchronizers = {
            Synchronizers::sequential,
            Synchronizers::array_of_promises,
            Synchronizers::promise_of_array,
            DSP_ALL
        };
    }
}

class LURunner : public Runner {
public:
    LURunner(std::string const& filename) : Runner(filename) { }

    void validate_synchronizer(std::string const& synchronizer) override {
        auto const& authorized = JSON::Run::authorized_synchronizers;
        if (std::find(authorized.begin(), authorized.end(), synchronizer) == authorized.end()) {
            std::ostringstream stream;
            stream << "Synchronizer " << synchronizer << " is not valid" << std::endl;
            stream << "Authorized synchronizers are: ";

            std::ostream_iterator<std::string> iter(stream, ", ");
            std::copy(authorized.begin(), authorized.end(), iter);
            stream << std::endl;

            throw std::runtime_error(stream.str());
        }
    }

    void process_run(unsigned int iterations, json run) override {
        std::string const& synchronizer = run[JSON::Run::synchronizer];

        auto get_step = [&]() -> unsigned int { 
            if (run.contains(JSON::Run::extras)) {
                const json& extras = run[JSON::Run::extras];
                if (extras.contains(JSON::Run::Extras::step)) {
                    return extras[JSON::Run::Extras::step].get<unsigned int>();
                }
            }

            return 1;
        };

        namespace Sync = JSON::Run::Synchronizers;

        std::map<std::string, std::function<void()>> synchronizer_action;
        synchronizer_action[Sync::sequential] = [&] { _collector.run_sequential(iterations); };
        synchronizer_action[Sync::array_of_promises] = [&] { _collector.run_array_of_promises(iterations); };
        synchronizer_action[Sync::promise_of_array] = [&] { _collector.run_promise_of_array(iterations); };

/*
#define ACTION_DSP(NAME) synchronizer_action[Sync::NAME] = [&] { _collector.run_##NAME(iterations, get_step()); }

        ACTION_DSP(dsp_prod_only);
        ACTION_DSP(dsp_cons_only);
        ACTION_DSP(dsp_both);
        ACTION_DSP(dsp_prod_unblocks);
        ACTION_DSP(dsp_cons_unblocks);
        ACTION_DSP(dsp_both_unblocks);
        ACTION_DSP(dsp_prod_timer);
        ACTION_DSP(dsp_prod_timer_unblocks);
        ACTION_DSP(dsp_monitor);
        ACTION_DSP(dsp_never);

#undef ACTION_DSP
*/

#define ACTION_DSP(NAME, MODE, SYNC_NAME, FN) synchronizer_action[Sync::NAME] = [&] { _collector.run_dsp<MODE>(iterations, get_step(), std::string(SYNC_NAME), std::string(FN)); }

        using dspm = DynamicStepPromiseMode;

        ACTION_DSP(dsp_prod_only, dspm::SET_STEP_PRODUCER_ONLY, "DSPProdOnly", "lu");
        ACTION_DSP(dsp_cons_only, dspm::SET_STEP_CONSUMER_ONLY, "DSPConsOnly", "lu");
        ACTION_DSP(dsp_both, dspm::SET_STEP_BOTH, "DSPBoth", "lu");
        ACTION_DSP(dsp_prod_unblocks, dspm::SET_STEP_PRODUCER_ONLY_UNBLOCK, "DSPProdUnblocks", "lu");
        ACTION_DSP(dsp_cons_unblocks, dspm::SET_STEP_CONSUMER_ONLY_UNBLOCK, "DSPConsUnblocks", "lu");
        ACTION_DSP(dsp_both_unblocks, dspm::SET_STEP_BOTH_UNBLOCK, "DSPBothUnblocks", "lu");
        ACTION_DSP(dsp_prod_timer, dspm::SET_STEP_PRODUCER_TIMER, "DSPProdTimer", "lu");
        ACTION_DSP(dsp_prod_timer_unblocks, dspm::SET_STEP_PRODUCER_TIMER_UNBLOCK, "DSPProdTimerUnblocks", "lu");
        ACTION_DSP(dsp_monitor, dspm::SET_STEP_MONITOR, "DSPMonitor", "lu");
        ACTION_DSP(dsp_never, dspm::SET_STEP_NEVER, "DSPNever", "lu");

#undef ACTION_DSP

        synchronizer_action[synchronizer]();
    }

    void dump() {
        _collector.print_times();
        _collector.print_iterations_times();
    }

private:
    LUTimeCollector _collector;
};

static void log_general_data(std::ostream& out) {
    json data;
    data["dim"] = g::LU::DIM;

    std::ifstream stream(sDynamicConfigFiles.get_simulations_filename());
    json simu;
    stream >> simu;
    stream.close();

    data["file"] = sDynamicConfigFiles.get_simulations_filename();
    data["iterations"] = simu["iterations"];
    data["threads"] = omp_nb_threads();
    data["description"] = sDynamicConfigStd._description;
    
    if (std::optional<std::string> const& opt = sDynamicConfigFiles.get_input_matrix_filename()) {
        data["input_matrix"] = *opt;
    }

    if (std::optional<std::string> const& opt = sDynamicConfigFiles.get_start_matrix_filename()) {
        data["start_matrix"] = *opt;
    }

    out << std::setw(4) << data;
}

int main(int argc, char** argv) {
    srand((unsigned)time(nullptr));
    // init_logging();
    parse_command_line(argc, argv);

    if (!getenv("OMP_NUM_THREADS")) {
        std::cerr << "OMP_NUM_THREADS not set. Aborting." << std::endl;
        exit(EXIT_FAILURE);
    }

    LURunner runner(sDynamicConfigFiles.get_simulations_filename());
    runner.run();
    runner.dump();

    log_general_data(DynamicConfig::_instance()._files.parameters_file());

    return 0;
}
