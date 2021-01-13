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
void heat_cpu_promise_plus(Matrix&, size_t, PromisePlusStore&, const PromisePlusStore&);
void heat_cpu_array_of_promises(Matrix&, size_t, ArrayOfPromisesStore&, ArrayOfPromisesStore&);
void heat_cpu_promise_of_array(Matrix&, size_t, PromiseOfArrayStore&, PromiseOfArrayStore&);

#endif /* FUNCTIONS_H */
