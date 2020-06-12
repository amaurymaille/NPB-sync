template<size_t N>
DimensionConverter<N>::DimensionConverter(std::initializer_list<size_t> const&
dimensions) {
    assert(dimensions.size() == N);
    std::copy(dimensions.begin(), dimensions.end(), _dimensions_sizes.begin());
}

template<size_t N>
size_t DimensionConverter<N>::to_1d(std::initializer_list<size_t> const& values)
{
    assert(values.size() == _dimensions_sizes.size());

    size_t result = 0;
    typename std::array<size_t, N>::const_iterator dim_size_iter = _dimensions_sizes.cbegin() + 1;
    std::initializer_list<size_t>::const_iterator val_iter = values.begin();

    for (; dim_size_iter != _dimensions_sizes.end(); ++dim_size_iter, ++val_iter) {
        auto dim_size_iter_orig = dim_size_iter;
        size_t product = 1;

        std::for_each(dim_size_iter, _dimensions_sizes.cend(), [&product](size_t dimension_size) {
            product *= dimension_size;
        });

        dim_size_iter = dim_size_iter_orig;
        result += *val_iter * product;
    }

    return result + *val_iter;
}

template<size_t N>
std::array<size_t, N> DimensionConverter<N>::from_1d(size_t pos) {
    std::array<size_t, N> result;
    typename std::array<size_t, N>::const_reverse_iterator dim_size_iter = _dimensions_sizes.crbegin();
    typename std::array<size_t, N>::reverse_iterator result_iter = result.rbegin();

    size_t denominator = 1;
    size_t numerator = pos;
    size_t product = 1;

    for (; dim_size_iter != _dimensions_sizes.crbegin() + _dimensions_sizes.size() - 1; ++dim_size_iter, ++result_iter) {
        *result_iter = (numerator / denominator) % *dim_size_iter;

        if (std::distance(_dimensions_sizes.crbegin(), dim_size_iter) >= 2) {
            product *= denominator / *(dim_size_iter - 2);
        }
        numerator -= (*result_iter * product);
        denominator *= *dim_size_iter;
    }

    result[0] = (numerator / denominator);
    return result;
}
