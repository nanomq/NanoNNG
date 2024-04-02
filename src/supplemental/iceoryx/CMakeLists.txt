if(NNG_ENABLE_ICEORYX)
    find_package(iceoryx_binding_c REQUIRED)
	find_package(iceoryx_platform REQUIRED)
	find_package(iceoryx_posh CONFIG REQUIRED)
	find_package(iceoryx_hoofs CONFIG REQUIRED)

    nng_sources(iceoryx_api.c)
    nng_sources(iceoryx_api.h)

    #nng_link_libraries(iceoryx_binding_c)
	nng_link_libraries(iceoryx_binding_c::iceoryx_binding_c)
	#TODO nng_test(iceryx_api_test)
endif()
