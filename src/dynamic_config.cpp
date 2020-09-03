#include "dynamic_config.h"

namespace ExtraConfig {
    std::ostream& runs_times_file() {
        return DynamicConfig::_instance()._files.runs_times_file();
    }

    std::ostream& iterations_times_file() {
        return DynamicConfig::_instance()._files.iterations_times_file();
    }

#ifdef ACTIVE_PROMISE_TIMERS
    std::ostream& promise_plus_timers_file() {
        return DynamicConfig::_instance()._files.promise_plus_timers_file();
    }
#endif 
}
