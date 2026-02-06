if(NNG_ENABLE_PARQUET)
    nng_sources(parquet.cc parquet_file_queue.cc)
    target_link_libraries(nng PRIVATE
        thrift
        brotlienc
        brotlidec
        brotlicommon
        bz2
        lz4
        snappy
        zstd
        ssl
        crypto
        dl
        z 
    )
    find_package(Arrow CONFIG REQUIRED)
    find_package(Parquet CONFIG REQUIRED)
    if(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_shared parquet_shared)
    else(ENABLE_PARQUET_SHARED)
        nng_link_libraries(arrow_static parquet_static)
    endif(ENABLE_PARQUET_SHARED)
    # nng_test(parquet_test)
endif()
