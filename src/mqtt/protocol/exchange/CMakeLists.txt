# nng_test(producer_test)
nng_test(exchange_test)
nng_test(exchange_server_test)

nng_sources(
	producer.c
	exchange.c
	exchange_server.c
)

add_subdirectory(stream)
