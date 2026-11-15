nng_test(stream_test)
nng_sources(
	stream.c
	raw_stream.c
)
nng_test(raw_stream_test)

if(NNG_ENABLE_PARQUET)
    nng_sources(someip_stream.c)
    nng_test(someip_stream_test)
endif()
