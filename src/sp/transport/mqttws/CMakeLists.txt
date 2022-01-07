#
# Copyright 2020 Staysail Systems, Inc. <info@staysail.tech>
# Copyright 2018 Capitar IT Group BV <info@capitar.com>
#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

# WebSocket transport
nng_directory(mqttws)

if (NNG_TRANSPORT_MQTT_BROKER_WS OR NNG_TRANSPORT_MQTT_BROKER_WSS)
    nng_defines_if(NNG_TRANSPORT_MQTT_BROKER_WS NNG_TRANSPORT_MQTT_BROKER_WS)
    nng_defines_if(NNG_TRANSPORT_MQTT_BROKER_WSS NNG_TRANSPORT_MQTT_BROKER_WSS)
    nng_sources(nmq_websocket.c)
    nng_headers(nng/transport/mqttws/nmq_websocket.h)
endif()



