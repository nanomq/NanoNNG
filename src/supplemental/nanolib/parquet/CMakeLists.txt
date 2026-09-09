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
    # >=22 = SecureString (Arrow 22.0.0+, no DeepClone). Override with
    # -DNNG_ARROW_VERSION_MAJOR=<major>.
    # >=24 also requires C++20 (Arrow 24 headers). Legacy ubuntu18.04
    # parquet images omit the flag and keep GCC 7 / C++14.
    set(NNG_ARROW_VERSION_MAJOR 14 CACHE STRING
        "Arrow major version for parquet encryption API (14, >=22, or >=24)")
    nng_defines(NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR})
    message(STATUS "Parquet NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR}")
    if(NNG_ARROW_VERSION_MAJOR GREATER_EQUAL 24)
        set_property(TARGET nng PROPERTY CXX_STANDARD 20)
        set_property(TARGET nng PROPERTY CXX_STANDARD_REQUIRED ON)
        if(TARGET nng_testing)
            set_property(TARGET nng_testing PROPERTY CXX_STANDARD 20)
            set_property(TARGET nng_testing PROPERTY CXX_STANDARD_REQUIRED ON)
        endif()
        message(STATUS "Parquet NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR} requires C++20")
    endif()

    # nng_test(parquet_test)
endif()
