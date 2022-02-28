#
# Jaylin EMQ
# https://opensource.org/licenses/MIT.
#
nng_directory(mqtt)

if (NNG_PROTO_MQTT_BROKER)
    nng_sources_if(NNG_PROTO_MQTT_BROKER mqtt_parser.c nmq_mqtt.c)
    nng_headers_if(NNG_PROTO_MQTT_BROKER nng/protocol/mqtt/mqtt_parser.h nng/protocol/mqtt/nmq_mqtt.h)
    nng_defines_if(NNG_PROTO_MQTT_BROKER NNG_HAVE_MQTT_BROKER)
endif ()
