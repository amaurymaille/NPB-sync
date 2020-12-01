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

void heat_cpu_naive(Matrix&, size_t);
void heat_cpu(Matrix&, size_t);

void heat_cpu_switch_loops(Matrix&, size_t);

void heat_cpu_point_promise(Matrix&, size_t, PointPromiseStore&, const PointPromiseStore&);

void heat_cpu_block_promise(Matrix&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_block_promise_switch_loops(Matrix&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_increasing_point_promise(Matrix&, size_t, IncreasingPointPromiseStore&, const IncreasingPointPromiseStore&);

void heat_cpu_jline_promise(Matrix&, size_t, JLinePromiseStore&, const JLinePromiseStore&);
void heat_cpu_kline_promise(Matrix&, size_t, KLinePromiseStore&, const KLinePromiseStore&);

void heat_cpu_increasing_jline_promise(Matrix&, size_t, IncreasingJLinePromiseStore&, const IncreasingJLinePromiseStore&);
void heat_cpu_increasing_kline_promise(Matrix&, size_t, IncreasingKLinePromiseStore&, const IncreasingKLinePromiseStore&);

/* void heat_cpu_block_promise_plus(Matrix&, size_t, BlockPromisePlusStore&, const BlockPromisePlusStore&);

void heat_cpu_jline_promise_plus(Matrix&, size_t, JLinePromisePlusStore&, const JLinePromisePlusStore&);
void heat_cpu_kline_promise_plus(Matrix&, size_t, KLinePromisePlusStore&, const KLinePromisePlusStore&);

void heat_cpu_increasing_jline_promise_plus(Matrix&, size_t, IncreasingJLinePromisePlusStore&, const IncreasingJLinePromisePlusStore&);
void heat_cpu_increasing_kline_promise_plus(Matrix&, size_t, IncreasingKLinePromisePlusStore&, const IncreasingKLinePromisePlusStore&); */

void heat_cpu_promise_plus(Matrix&, size_t, PromisePlusStore&, const PromisePlusStore&);
void heat_cpu_array_of_promises(Matrix&, size_t, ArrayOfPromisesStore&, ArrayOfPromisesStore&);
void heat_cpu_promise_of_array(Matrix&, size_t, PromiseOfArrayStore&, PromiseOfArrayStore&);

#endif /* FUNCTIONS_H */
