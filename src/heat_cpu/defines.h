#ifndef HEAT_CPU_DEFINES_H
#define HEAT_CPU_DEFINES_H

#include <boost/multi_array.hpp>

#include <defines.h>

typedef ThreadStore<PromisePlus<void>*> PromisePlusContainer;
typedef std::optional<PromisePlusContainer> PromisePlusStore;

template<typename T>
class NaivePromise;

template<>
class NaivePromise<void>;

typedef ThreadStore<NaivePromise<void>*> ArrayOfPromisesContainer;
typedef std::optional<ArrayOfPromisesContainer> ArrayOfPromisesStore;

typedef ThreadStore<NaivePromise<void>*> PromiseOfArrayContainer;
typedef std::optional<PromiseOfArrayContainer> PromiseOfArrayStore;


#endif // HEAT_CPU_DEFINES_H
