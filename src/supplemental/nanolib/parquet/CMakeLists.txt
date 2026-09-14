if(NNG_ENABLE_PARQUET)
    nng_sources(parquet.cc parquet_file_queue.cc)
    find_package(Arrow CONFIG REQUIRED)
    find_package(Parquet CONFIG REQUIRED)
    if(ENABLE_PARQUET_SHARED)
        set(_nng_parquet_arrow_tgt arrow_shared)
        set(_nng_parquet_parquet_tgt parquet_shared)
    else(ENABLE_PARQUET_SHARED)
        set(_nng_parquet_arrow_tgt arrow_static)
        set(_nng_parquet_parquet_tgt parquet_static)
    endif(ENABLE_PARQUET_SHARED)

    # Regular INTERFACE targets are globally visible; Arrow imported
    # targets / aliases are not. Wrap them so nanomq can link after nng.
    add_library(nng_parquet_deps INTERFACE)
    target_link_libraries(nng_parquet_deps INTERFACE
        ${_nng_parquet_arrow_tgt} ${_nng_parquet_parquet_tgt})

    # Compile parquet.cc against Arrow headers/defs only. Do not
    # nng_link_libraries(arrow_*): static nng would propagate
    # OpenSSL::Crypto (libcrypto.so.1.1) to every consumer, including
    # nanomq_cli. The broker links nng_parquet_deps after nng.
    foreach(_nng_parquet_dep ${_nng_parquet_arrow_tgt} ${_nng_parquet_parquet_tgt})
        get_target_property(_nng_parquet_incs ${_nng_parquet_dep} INTERFACE_INCLUDE_DIRECTORIES)
        if(_nng_parquet_incs)
            nng_include_directories(${_nng_parquet_incs})
        endif()
        get_target_property(_nng_parquet_defs ${_nng_parquet_dep} INTERFACE_COMPILE_DEFINITIONS)
        if(_nng_parquet_defs)
            nng_defines(${_nng_parquet_defs})
        endif()
    endforeach()
    if(TARGET nng_testing)
        target_link_libraries(nng_testing PRIVATE nng_parquet_deps)
    endif()

    # Arrow encryption key API: 14 = std::string + DeepClone (default),
    # >=22 = SecureString (Arrow 22.0.0+, no DeepClone). Override with
    # -DNNG_ARROW_VERSION_MAJOR=<major>.
    # parquet_file_queue.h uses std::optional (C++17). Linking Arrow as a
    # CMake target used to inject cxx_std_17; keep that floor here so
    # GCC 7 toolchains (Linaro / ubuntu18) still compile. >=24 needs C++20.
    set(NNG_ARROW_VERSION_MAJOR 14 CACHE STRING
        "Arrow major version for parquet encryption API (14, >=22, or >=24)")
    nng_defines(NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR})
    message(STATUS "Parquet NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR}")
    if(NNG_ARROW_VERSION_MAJOR GREATER_EQUAL 24)
        set(_nng_parquet_cxx 20)
    else()
        set(_nng_parquet_cxx 17)
    endif()
    set_property(TARGET nng PROPERTY CXX_STANDARD ${_nng_parquet_cxx})
    set_property(TARGET nng PROPERTY CXX_STANDARD_REQUIRED ON)
    if(TARGET nng_testing)
        set_property(TARGET nng_testing PROPERTY CXX_STANDARD ${_nng_parquet_cxx})
        set_property(TARGET nng_testing PROPERTY CXX_STANDARD_REQUIRED ON)
    endif()
    if(NNG_ARROW_VERSION_MAJOR GREATER_EQUAL 24)
        message(STATUS "Parquet NNG_ARROW_VERSION_MAJOR=${NNG_ARROW_VERSION_MAJOR} requires C++20")
    endif()

    # nng_test(parquet_test)
endif()
