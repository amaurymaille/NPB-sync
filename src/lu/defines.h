#ifndef LU_DEFINES_H
#define LU_DEFINES_H

#include <boost/multi_array.hpp>

#include "defines.h"

typedef boost::multi_array<float, 2> Matrix2D;
typedef Matrix2D::element Matrix2DValue;

typedef std::vector<float> Vector1D;
typedef Vector1D::value_type Vector1DValue;

#endif // LU_DEFINES_H
