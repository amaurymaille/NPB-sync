#ifndef FUNCTIONS_H
#define FUNCTIONS_H

#include <array>
#include <future>
#include <optional>
#include <utility>
#include <vector>

#include "defines.h"
#include "utils.h"

namespace g = Globals;

void heat_cpu(Matrix&, size_t);
void heat_cpu_switch_loops(Matrix&, size_t);


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
void heat_cpu_point_promise(Matrix&, size_t, PointPromiseStore&, const PointPromiseStore&);

void heat_cpu_block_promise(Matrix&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_block_promise_switch_loops(Matrix&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_increasing_point_promise(Matrix&, size_t, IncreasingPointPromiseStore&, const IncreasingPointPromiseStore&);

void heat_cpu_jline_promise(Matrix&, size_t, JLinePromiseStore&, const JLinePromiseStore&);
void heat_cpu_kline_promise(Matrix&, size_t, KLinePromiseStore&, const KLinePromiseStore&);

void heat_cpu_increasing_jline_promise(Matrix&, size_t, IncreasingJLinePromiseStore&, const IncreasingJLinePromiseStore&);
void heat_cpu_increasing_kline_promise(Matrix&, size_t, IncreasingKLinePromiseStore&, const IncreasingKLinePromiseStore&);

void heat_cpu_block_promise_plus(Matrix&, size_t, BlockPromisePlusStore&, const BlockPromisePlusStore&);
void heat_cpu_jline_promise_plus(Matrix&, size_t, JLinePromisePlusStore&, const JLinePromisePlusStore&);
void heat_cpu_increasing_jline_promise_plus(Matrix&, size_t, IncreasingJLinePromisePlusStore&, const IncreasingJLinePromisePlusStore&);

#endif /* FUNCTIONS_H */