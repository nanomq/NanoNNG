#
# This software is supplied under the terms of the MIT License, a
# copy of which should be located in the distribution where this
# file was obtained (LICENSE.txt).  A copy of the license may also be
# found online at https://opensource.org/licenses/MIT.
#

add_subdirectory(mqtt)
add_subdirectory(mqtt_async)
add_subdirectory(mqttv5)

if(NNG_ENABLE_QUIC)
    add_subdirectory(quic)
endif ()