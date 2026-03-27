if(NNG_ENABLE_PARQUET)
    nng_sources(parquet.cc parquet_file_queue.cc)
    # Arrow's exported CMake targets may reference CURL::libcurl in their
    # INTERFACE_LINK_LIBRARIES. Ensure the CURL imported target exists before
    # including ArrowTargets.cmake via find_package(Arrow).
    find_package(CURL REQUIRED)
    find_package(Arrow CONFIG REQUIRED)
    find_package(Parquet CONFIG REQUIRED)
    if(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_shared parquet_shared)
    else(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_static parquet_static)
    endif(ENABLE_PARQUET_SHARED)
    # nng_test(parquet_test)
endif()
