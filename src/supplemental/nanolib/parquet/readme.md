# parquet
## dependencies

This section assumes you already have the Arrow C++ libraries on your system, either after [installing them using a package manager](https://arrow.apache.org/install/) or after [building them yourself](https://arrow.apache.org/docs/developers/cpp/building.html#building-arrow-cpp).

## compile
You can use parquet lib with option `NNG_ENABLE_PARQUET`
```bash
$ cmake -DNNG_ENABLE_PARQUET=ON ..
```