#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

if (NNG_ENABLE_QUIC)
    set(NNG_QUIC_LIBS msquic none)

    # We assume msquic for now.
    set(NNG_QUIC_LIB msquic CACHE STRING "QUIC lib to use.")
    add_subdirectory(msquic)

    nng_sources(quic_api.c)
    nng_sources(quic_api.h)

    find_path(INTERNAL_MSQUIC_INCLUDE_DIR
    NAMES msquic.h
    HINTS ${_MSQUIC_ROOT_HINTS}
    PATHS ${PROJECT_SOURCE_DIR}/extern/msquic/src
    PATH_SUFFIXES inc)

    if(NNG_PLATFORM_POSIX)
        nng_link_libraries(dl)
    elseif(NNG_PLATFORM_WINDOWS)
        nng_link_libraries(ws2_32)
    endif()

	find_library(OPENSSLQUIC_LIBRARIES
	   NAMES openssl ssl libssl libopenssl
	   HINTS "${CMAKE_SOURCE_DIR}/build/_deps/opensslquic-build/openssl"
	   PATHS "${CMAKE_SOURCE_DIR}/build/_deps/opensslquic-build/openssl"
	   PATH_SUFFIXES lib)


	if(OPENSSLQUIC_LIBRARIES_NOTFOUND)
		message("!!!!!!!!!!!!!!!!!!!! No openssl for MsQuic be found")
	else()
		nng_include_directories(${CMAKE_SOURCE_DIR}/build/_deps/opensslquic-build/openssl/include)
		set(SSLQUIC_LIBRARIES ${OPENSSLQUIC_LIBRARIES})
		mark_as_advanced(SSLQUIC_LIBRARIES)
		nng_link_libraries(${SSLQUIC_LIBRARIES})
		message(${CMAKE_SOURCE_DIR})
		message(${SSLQUIC_LIBRARIES})
		message("---------------------------------")
	endif()

	#if(OPENSSL_DIR)
	#include_directories(${OPENSSL_DIR}/include)
	#nng_include_directories(${OPENSSL_DIR}/include)

	#   message("YES!!!!!!!!!!!!!!!!!!!!!!!!!!!")
		#else()
		#message("NO OPENSSL DIR!!!!!!!!!!!!!!!!!!!!!!!!!!!111")
		#endif()
    #include_directories(${PROJECT_SOURCE_DIR}/extern/msquic/submodules/openssl/include)
    #message(STATUS "OpenSSL Found!")

    if(NNG_PROTO_MQTT_BROKER)
        nng_include_directories(${INTERNAL_MSQUIC_INCLUDE_DIR})
    endif()
    # set_property(CACHE NNG_QUIC_LIB PROPERTY STRINGS ${NNG_TLS_ENGINES})
else ()
    set(NNG_QUIC_LIB none)
endif ()

# if (NOT NNG_QUIC_LIB STREQUAL "none")
#     nng_test(quic_test)
# endif ()
