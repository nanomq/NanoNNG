# nng_test(producer_test)
nng_test(exchange_test)
nng_test(exchange_server_test)
if(NNG_TESTS AND NNG_ENABLE_PARQUET AND UNIX AND NOT APPLE)
    nng_test(exchange_query_test)
    set_property(TARGET exchange_query_test APPEND_STRING PROPERTY LINK_FLAGS
        " -Wl,--wrap=parquet_get_file_ranges,--wrap=parquet_get_data_packets_in_range_by_column,--wrap=stream_decoded_data_free")
endif()

nng_sources(
	producer.c
	exchange.c
	exchange_server.c
)

add_subdirectory(stream)
