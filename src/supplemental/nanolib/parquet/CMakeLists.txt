if(NNG_ENABLE_PARQUET)
    nng_sources(parquet.cc parquet_file_queue.cc)
    find_package(Arrow CONFIG REQUIRED)
    find_package(Parquet CONFIG REQUIRED)
    if(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_shared parquet_shared)
    else(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_static parquet_static)
    endif(ENABLE_PARQUET_SHARED)

    # Arrow encryption key API: 14 = std::string + DeepClone (default),
    # >=15 = SecureString (no DeepClone). Override with
    # -DNNG_ARROW_VERSION_MAJOR=<major>.
    set(NNG_ARROW_VERSION_MAJOR 14 CACHE STRING
        "Arrow major version for parquet encryption API (14 or >=15)")
    nng_defines(NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR})
    message(STATUS "Parquet NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR}")

    # nng_test(parquet_test)
endif()
