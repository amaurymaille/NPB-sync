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

void heat_cpu(MatrixReorderer&, size_t);

void heat_cpu_switch_loops(MatrixReorderer&, size_t);

void heat_cpu_point_promise(MatrixReorderer&, size_t, PointPromiseStore&, const PointPromiseStore&);

void heat_cpu_block_promise(MatrixReorderer&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_block_promise_switch_loops(MatrixReorderer&, size_t, BlockPromiseStore&, const BlockPromiseStore&);

void heat_cpu_increasing_point_promise(MatrixReorderer&, size_t, IncreasingPointPromiseStore&, const IncreasingPointPromiseStore&);

void heat_cpu_jline_promise(MatrixReorderer&, size_t, JLinePromiseStore&, const JLinePromiseStore&);
void heat_cpu_kline_promise(MatrixReorderer&, size_t, KLinePromiseStore&, const KLinePromiseStore&);

void heat_cpu_increasing_jline_promise(MatrixReorderer&, size_t, IncreasingJLinePromiseStore&, const IncreasingJLinePromiseStore&);
void heat_cpu_increasing_kline_promise(MatrixReorderer&, size_t, IncreasingKLinePromiseStore&, const IncreasingKLinePromiseStore&);

/* void heat_cpu_block_promise_plus(MatrixReorderer&, size_t, BlockPromisePlusStore&, const BlockPromisePlusStore&);

void heat_cpu_jline_promise_plus(MatrixReorderer&, size_t, JLinePromisePlusStore&, const JLinePromisePlusStore&);
void heat_cpu_kline_promise_plus(MatrixReorderer&, size_t, KLinePromisePlusStore&, const KLinePromisePlusStore&);

void heat_cpu_increasing_jline_promise_plus(MatrixReorderer&, size_t, IncreasingJLinePromisePlusStore&, const IncreasingJLinePromisePlusStore&);
void heat_cpu_increasing_kline_promise_plus(MatrixReorderer&, size_t, IncreasingKLinePromisePlusStore&, const IncreasingKLinePromisePlusStore&); */

#ifdef ACTIVE_PROMISE_TIMERS
void heat_cpu_promise_plus(MatrixReorderer&, size_t, PromisePlusStore&, const PromisePlusStore&, PromisePlusTimersByInnerIteration&);
#else
void heat_cpu_promise_plus(MatrixReorderer&, size_t, PromisePlusStore&, const PromisePlusStore&);
#endif
void heat_cpu_naive_promise_array(MatrixReorderer&, size_t, NaivePromiseArrayStore&, NaivePromiseArrayStore&);

#endif /* FUNCTIONS_H */
