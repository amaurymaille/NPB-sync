#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include "defines.h"

namespace g = Globals;

void heat_cpu(Matrix, size_t);
void heat_cpu_switch_loops(Matrix, size_t);

typedef std::optional<std::reference_wrapper<std::array<std::vector<std::promise<MatrixValue>>, g::NB_LINES_PER_ITERATION>>> LinePromiseStore;
/* In this version of heat_cpu, loops are switched (like in heat_cpu_switch_loops),
 * and there are two additional parameters, arrays of promises. The first array
 * contains promises that are to be resolved every time a line is completely 
 * computed. The second array contains promises that are used to get the values 
 * to start computation on a line. For the first thread, the second array is useless 
 * as there are no dependencies "before". For the last thread, the first array is 
 * useless as there are no dependencies "after". For every pair of threads identified 
 * by (N, N+1), using arrays (DN, SN) and (DN+1, SN+1), DN and SN+1 are the same 
 * array, as destination values for thread N are source value for thread N+1.
 */
void heat_cpu_line_promise(Matrix, size_t, LinePromiseStore&, const LinePromiseStore&);

typedef std::vector<std::promise<std::array<MatrixValue, g::NB_VALUES_PER_BLOCK>>> BlockPromiseContainer;
typedef std::optional<std::reference_wrapper<BlockPromiseContainer>> BlockPromiseStore;
void heat_cpu_block_promise(Matrix, size_t, BlockPromiseStore&, const BlockPromiseStore&);

#endif /* FUNCTIONS_H */