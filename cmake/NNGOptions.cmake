#
# Copyright 2024 Staysail Systems, Inc. <info@staysail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

# NNG Options.  These are user configurable knobs.

include(CMakeDependentOption)

if (CMAKE_CROSSCOMPILING OR BUILD_STATIC)
    set(NNG_NATIVE_BUILD OFF)
else ()
    set(NNG_NATIVE_BUILD ON)
endif ()

# Global options.
option(BUILD_SHARED_LIBS "Build shared library" ${BUILD_SHARED_LIBS})

# We only build command line tools and tests if we are not in a
# cross-compile situation.  Cross-compiling users who still want to
# build these must enable them explicitly.  Some of these switches
# must be enabled rather early as we use their values later.
option(NNG_TESTS "Build and run tests." ${NNG_NATIVE_BUILD})
option(NNG_TOOLS "Build extra tools." ${NNG_NATIVE_BUILD})
option(NNG_ENABLE_NNGCAT "Enable building nngcat utility." ${NNG_TOOLS})
option(NNG_ENABLE_COVERAGE "Enable coverage reporting." OFF)

message(NNG_TESTS = "${NNG_TESTS}")
message(NNG_TOOLS = "${NNG_TOOLS}")

# Eliding deprecated functionality can be used to build a slimmed down
# version of the library, or alternatively to test for application
# preparedness for expected feature removals (in the next major release.)
# Applications can also set the NNG_ELIDE_DEPRECATED preprocessor symbol
# before including <nng/nng.h> -- this will prevent declarations from
# being exposed to applications, but it will not affect their ABI
# availability for existing compiled applications.
# Note: Currently this breaks the test suite, so we only do it
# for the public library.
option(NNG_ELIDE_DEPRECATED "Elide deprecated functionality." OFF)

# Turning off the compatibility layer can save some space, and
# compilation time, but may break legacy applications  It should
# be left enabled when building a shared library.
option(NNG_ENABLE_COMPAT "Enable legacy nanomsg API." ON)

option(NNG_ENABLE_STATS "Enable statistics." ON)
mark_as_advanced(NNG_ENABLE_STATS)

# SQLITE API support.
option (NNG_ENABLE_SQLITE "Enable SQLITE API." OFF)
if (NNG_ENABLE_SQLITE)
    set(NNG_SUPP_SQLITE ON)
endif()
mark_as_advanced(NNG_ENABLE_SQLITE)

# Protocols.
option (NNG_PROTO_BUS0 "Enable BUSv0 protocol." ON)
mark_as_advanced(NNG_PROTO_BUS0)

option (NNG_PROTO_PAIR0 "Enable PAIRv0 protocol." ON)
mark_as_advanced(NNG_PROTO_PAIR0)

option (NNG_PROTO_PAIR1 "Enable PAIRv1 protocol." ON)
mark_as_advanced(NNG_PROTO_PAIR1)

option (NNG_PROTO_PUSH0 "Enable PUSHv0 protocol." ON)
mark_as_advanced(NNG_PROTO_PUSH0)

option (NNG_PROTO_PULL0 "Enable PULLv0 protocol." ON)
mark_as_advanced(NNG_PROTO_PULL0)

option (NNG_PROTO_PUB0 "Enable PUBv0 protocol." ON)
mark_as_advanced(NNG_PROTO_PUB0)

option (NNG_PROTO_SUB0 "Enable SUBv0 protocol." ON)
mark_as_advanced(NNG_PROTO_SUB0)

option(NNG_PROTO_REQ0 "Enable REQv0 protocol." ON)
mark_as_advanced(NNG_PROTO_REQ0)

option(NNG_PROTO_REP0 "Enable REPv0 protocol." ON)
mark_as_advanced(NNG_PROTO_REP0)

option (NNG_PROTO_RESPONDENT0 "Enable RESPONDENTv0 protocol." ON)
mark_as_advanced(NNG_PROTO_RESPONDENT0)

option (NNG_PROTO_SURVEYOR0 "Enable SURVEYORv0 protocol." ON)
mark_as_advanced(NNG_PROTO_SURVEYOR0)

option (NNG_PROTO_MQTT_CLIENT "Enable MQTT Client protocol." ON)
mark_as_advanced(NNG_PROTO_MQTT_CLIENT)

option (NNG_PROTO_MQTT_BROKER "Enable MQTT Broker protocol." ON)
mark_as_advanced(NNG_PROTO_MQTT_BROKER)

option(NNG_ENABLE_QUIC "Enable Quic support." OFF)

if (NNG_ENABLE_QUIC)
    option (NNG_PROTO_MQTT_QUIC_CLIENT "Enable MQTT over msQuic Client protocol." ON)
    mark_as_advanced(NNG_PROTO_MQTT_QUIC_CLIENT)

    set(NNG_SUPP_QUIC ON)
    # For now we only accept msQuic as the quic lib
endif ()

if (NNG_ENABLE_QUIC)
    set(NNG_QUIC_LIBS msquic none)
    # We assume MSQUIC only for now.  (Someday replaced perhaps with ngtcp.)
    set(NNG_QUIC_LIB msquic CACHE STRING "Quic lib to use.")
    set_property(CACHE NNG_QUIC_LIB PROPERTY STRINGS ${NNG_QUIC_LIBS})
else ()
    set(NNG_QUIC_LIB none)
endif ()

# TLS support.

# Enabling TLS is required to enable support for the TLS transport
# and WSS.  It does require a 3rd party TLS engine to be selected.
option(NNG_ENABLE_TLS "Enable TLS support." OFF)
if (NNG_ENABLE_TLS)
    set(NNG_SUPP_TLS ON)
endif ()

if (NNG_ENABLE_TLS)
    set(NNG_TLS_ENGINES mbed wolf open none)
    # We assume Mbed for now.  (Someday replaced perhaps with Bear.)
    set(NNG_TLS_ENGINE mbed CACHE STRING "TLS engine to use.")
    set_property(CACHE NNG_TLS_ENGINE PROPERTY STRINGS ${NNG_TLS_ENGINES})
else ()
    set(NNG_TLS_ENGINE none)
endif ()

# HTTP API support.
option (NNG_ENABLE_HTTP "Enable HTTP API." ON)
if (NNG_ENABLE_HTTP)
    set(NNG_SUPP_HTTP ON)
endif()
mark_as_advanced(NNG_ENABLE_HTTP)

# Some sites or kernels lack IPv6 support.  This override allows us
# to prevent the use of IPv6 in environments where it isn't supported.
option (NNG_ENABLE_IPV6 "Enable IPv6." ON)
mark_as_advanced(NNG_ENABLE_IPV6)

#
# Transport Options.
#

option (NNG_TRANSPORT_INPROC "Enable inproc transport." ON)
mark_as_advanced(NNG_TRANSPORT_INPROC)

option (NNG_TRANSPORT_IPC "Enable IPC transport." ON)
mark_as_advanced(NNG_TRANSPORT_IPC)

# TCP transport
option (NNG_TRANSPORT_TCP "Enable TCP transport." ON)
mark_as_advanced(NNG_TRANSPORT_TCP)

if (NNG_ENABLE_QUIC)
    # MQTT QUIC transport
    option (NNG_TRANSPORT_MQTT_QUIC "Enable MQTT QUIC transport." ON)
    mark_as_advanced(NNG_TRANSPORT_MQTT_QUIC)
endif ()

# MQTT TCP transport
option (NNG_TRANSPORT_MQTT_TCP "Enable MQTT TCP transport." ON)
mark_as_advanced(NNG_TRANSPORT_MQTT_TCP)

# TLS transport
option (NNG_TRANSPORT_TLS "Enable TLS transport." ON)
mark_as_advanced(NNG_TRANSPORT_TLS)

# MQTT TLS transport
option (NNG_TRANSPORT_MQTT_TLS "Enable MQTT TLS transport." ON )
mark_as_advanced(NNG_TRANSPORT_MQTT_TLS)

# WebSocket
option (NNG_TRANSPORT_WS "Enable WebSocket transport." ON)
mark_as_advanced(NNG_TRANSPORT_WS)

# MQTT Client
option (NNG_TRANSPORT_MQTT_TCP "Enable MQTT TCP transport." ON)
mark_as_advanced(NNG_TRANSPORT_MQTT_TCP)

#MQTT Broker TCP
option (NNG_TRANSPORT_MQTT_BROKER_TCP "Enable MQTT BROKER TCP transport." ON)
mark_as_advanced(NNG_TRANSPORT_MQTT_BROKER_TCP)

#MQTT Broker TLS
option (NNG_TRANSPORT_MQTT_BROKER_TLS "Enable MQTT BROKER TLS transport." ON)
mark_as_advanced(NNG_TRANSPORT_MQTT_BROKER_TLS)

#MQTT Broker Websocket
option (NNG_TRANSPORT_MQTT_BROKER_WS "Enable MQTT BROKER WS transport." ON)
mark_as_advanced(NNG_TRANSPORT_MQTT_BROKER_WS)

#MQTT Broker Websocket TLS
option (NNG_TRANSPORT_MQTT_BROKER_WSS "Enable MQTT BROKER WSS transport." OFF)
mark_as_advanced(NNG_TRANSPORT_MQTT_BROKER_WSS)

CMAKE_DEPENDENT_OPTION(NNG_TRANSPORT_WSS "Enable WSS transport." ON
        "NNG_ENABLE_TLS" OFF)
mark_as_advanced(NNG_TRANSPORT_WSS)

option (NNG_TRANSPORT_FDC "Enable File Descriptor transport (EXPERIMENTAL)" ON)
mark_as_advanced(NNG_TRANSPORT_FDC)

# ZeroTier
option (NNG_TRANSPORT_ZEROTIER "Enable ZeroTier transport (requires libzerotiercore)." OFF)
mark_as_advanced(NNG_TRANSPORT_ZEROTIER)

if (NNG_TRANSPORT_WS OR NNG_TRANSPORT_WSS)
    # Make sure things we *MUST* have are enabled.
    set(NNG_SUPP_WEBSOCKET ON)
    set(NNG_SUPP_HTTP ON)
    set(NNG_SUPP_BASE64 ON)
    set(NNG_SUPP_SHA1 ON)
endif()
