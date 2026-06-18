#
# Copyright 2023 Staysail Systems, Inc. <info@staystail.tech>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

#  Transports.
nng_directory(transport)

add_subdirectory(socket)
add_subdirectory(inproc)
if (NNG_TRANSPORT_IPC)
    add_subdirectory(ipc)
endif()
add_subdirectory(tcp)
if (NNG_TRANSPORT_TLS)
    add_subdirectory(tls)
endif()
if (NNG_TRANSPORT_WS)
    add_subdirectory(ws)
endif()
if (NNG_TRANSPORT_MQTT_TLS)
    add_subdirectory(mqtts)
endif()
if (NNG_TRANSPORT_ZEROTIER)
    add_subdirectory(zerotier)
endif()
add_subdirectory(mqtt)
if (NNG_TRANSPORT_MQTT_WS)
    add_subdirectory(mqttws)
endif()
