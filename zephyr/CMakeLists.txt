if(CONFIG_NANONNG)
  set(NNG_PLATFORM_POSIX ON CACHE BOOL "" FORCE)
  set(NNG_PLATFORM_ZEPHYR ON CACHE BOOL "" FORCE)
  set(NNG_STATIC_LIB ON CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(NNG_TESTS OFF CACHE BOOL "" FORCE)
  set(NNG_TOOLS OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_NNGCAT OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_COVERAGE OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_TLS OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_QUIC OFF CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_IPC OFF CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_TLS OFF CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_MQTT_TLS OFF CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_MQTT_BROKER_TLS OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_HTTP ON CACHE BOOL "" FORCE)
  set(NNG_ENABLE_SQLITE OFF CACHE BOOL "" FORCE)
  set(NNG_ENABLE_COMPAT OFF CACHE BOOL "" FORCE)

  # Honour the transport/protocol switches declared in zephyr/Kconfig.
  # Without these the NanoNNG defaults (everything ON) win and the
  # CONFIG_NANONNG_* switches have no effect on what gets compiled in.
  set(NNG_TRANSPORT_TCP ${CONFIG_NANONNG_TRANSPORT_TCP} CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_MQTT_TCP ${CONFIG_NANONNG_TRANSPORT_MQTT_TCP} CACHE BOOL "" FORCE)
  set(NNG_TRANSPORT_INPROC ${CONFIG_NANONNG_TRANSPORT_INPROC} CACHE BOOL "" FORCE)
  set(NNG_PROTO_MQTT_CLIENT ${CONFIG_NANONNG_PROTO_MQTT_CLIENT} CACHE BOOL "" FORCE)
  set(NNG_PROTO_PAIR0 ${CONFIG_NANONNG_PROTO_PAIR0} CACHE BOOL "" FORCE)
  set(NNG_PROTO_PUB0 ${CONFIG_NANONNG_PROTO_PUB0} CACHE BOOL "" FORCE)
  set(NNG_PROTO_SUB0 ${CONFIG_NANONNG_PROTO_SUB0} CACHE BOOL "" FORCE)
  set(NNG_PROTO_PUSH0 ${CONFIG_NANONNG_PROTO_PUSH0} CACHE BOOL "" FORCE)
  set(NNG_PROTO_PULL0 ${CONFIG_NANONNG_PROTO_PULL0} CACHE BOOL "" FORCE)

  # 32-bit targets other than x86 have no native 64-bit atomics, and the
  # stdatomic path would emit __sync_*_8 calls that their libatomic cannot
  # resolve on bare metal (see the note in src/platform/zephyr/CMakeLists.txt).
  # x86_32 has cmpxchg8b and 64-bit targets are unaffected, so only the rest
  # take the mutex fallback.  Deciding it here means an application that adds
  # NanoNNG as a Zephyr module does not have to know about the flag at all --
  # the demo/ ExternalProject, which drives the build by hand, still passes it.
  if(NOT CONFIG_64BIT AND NOT CONFIG_X86)
    set(NNG_ZEPHYR_NO_STDATOMIC ON CACHE BOOL "" FORCE)
  endif()

  add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/.. ${PROJECT_BINARY_DIR}/nanonng_root)

  target_include_directories(nng PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/platform/zephyr/stubs
  )
  zephyr_link_libraries(nng)
endif()
