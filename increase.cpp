#include <cmath>

#include "dynamic_defines.h"
#include "increase.h"

namespace Globals {
    // Base power for how many points to send when increasing the number of points
    // send with each iteration.
    static const size_t INCREASING_POINTS_BASE_POWER = 4;
    // After how many iteration we start sending full blocks and no longer sets of 
    // points.
    static const size_t INCREASING_POINTS_ITERATION_LIMIT = 4;

    static const size_t INCREASING_LINES_BASE_POWER = 4;
    static const size_t INCREASING_LINES_ITERATION_LIMIT = 4;
}

namespace g = Globals;

int nb_points_for_iteration(int iteration) {
    return iteration < g::INCREASING_POINTS_ITERATION_LIMIT ? 
           std::pow(g::INCREASING_POINTS_BASE_POWER, iteration - 1) : 
           g::NB_POINTS_PER_ITERATION;
}

int nb_jlines_for_iteration(int iteration) {
    return iteration < g::INCREASING_LINES_ITERATION_LIMIT ?
           std::pow(g::INCREASING_LINES_BASE_POWER, iteration - 1) :
           g::NB_J_LINES_PER_ITERATION;
}