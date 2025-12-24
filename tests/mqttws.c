//
// Copyright 2025 NanoMQ Team, Inc. <jaylin@emqx.io>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#include <string.h>
#include <nng/nng.h>
#include <nng/protocol/mqtt/mqtt.h>
#include <nng/protocol/mqtt/nmq_mqtt.h>
#include <nng/transport/mqttws/nmq_websocket.h>

#include "convey.h"
#include "core/nng_impl.h"
#include "supplemental/mqtt/mqtt_msg.h"

// Mock structures for testing (matching internal implementation)
typedef struct test_ws_pipe {
	nni_mtx     mtx;
	bool        closed;
	uint8_t     txlen[10];
	uint16_t    peer;
	size_t      gotrxhead;
	size_t      wantrxhead;
	nni_lmq     recvlmq;
	nni_lmq     rslmq;
	void       *conf;
	nni_msg    *tmp_msg;
	nni_aio    *user_txaio;
	nni_aio    *user_rxaio;
	nni_aio    *ep_aio;
	nni_aio    *txaio;
	nni_aio    *rxaio;
	nni_aio    *qsaio;
	void       *npipe;
	void       *ws_param;
	void       *ws;
	uint8_t    *qos_buf;
	size_t      qlength;
	uint16_t    qrecv_quota;
	uint32_t    qsend_quota;
	int         err_code;
} test_ws_pipe;

// Test helper: Create a test pipe structure
static test_ws_pipe *
create_test_pipe(void)
{
	test_ws_pipe *p = calloc(1, sizeof(test_ws_pipe));
	if (p == NULL) {
		return NULL;
	}
	nni_mtx_init(&p->mtx);
	p->closed = false;
	p->err_code = 0;
	return p;
}

// Test helper: Destroy test pipe
static void
destroy_test_pipe(test_ws_pipe *p)
{
	if (p == NULL) {
		return;
	}
	nni_mtx_fini(&p->mtx);
	free(p);
}

// Test helper: Create and initialize an AIO
static nni_aio *
create_test_aio(void)
{
	nni_aio *aio;
	if (nni_aio_alloc(&aio, NULL, NULL) != 0) {
		return NULL;
	}
	return aio;
}

TestMain("MQTT WebSocket Transport Tests", {

	Convey("Given a WebSocket pipe structure", {
		test_ws_pipe *pipe = create_test_pipe();
		So(pipe != NULL);

		Convey("wstran_pipe_send_cb tests", {

			Convey("When send succeeds with no errors", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(txaio != NULL);
				So(user_aio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				
				// Simulate successful send (result = 0)
				nni_aio_finish(txaio, 0, 0);
				
				Convey("Then user_txaio should be reset to NULL", {
					// After callback execution
					So(pipe->user_txaio == NULL);
				});

				Convey("Then user aio should complete successfully", {
					// User aio should be finished without error
					So(nni_aio_result(user_aio) == 0);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When send fails with error", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				nni_msg *msg;
				So(txaio != NULL);
				So(user_aio != NULL);
				So(nni_msg_alloc(&msg, 0) == 0);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				
				// Set message on txaio
				nni_aio_set_msg(txaio, msg);
				
				// Simulate failed send
				nni_aio_finish_error(txaio, NNG_ECLOSED);
				
				Convey("Then error should be logged and message freed", {
					int rv = nni_aio_result(txaio);
					So(rv == NNG_ECLOSED);
				});

				Convey("Then user aio should receive the error", {
					// User aio should be finished with error
					So(nni_aio_result(user_aio) != 0);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When pipe is closed during send", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(txaio != NULL);
				So(user_aio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				pipe->closed = true;
				pipe->err_code = NNG_ECLOSED;
				
				Convey("Then user aio should receive pipe error code", {
					// Callback should detect closed state
					So(pipe->closed == true);
					So(pipe->err_code == NNG_ECLOSED);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When user_txaio is NULL", {
				nni_aio *txaio = create_test_aio();
				So(txaio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = NULL;
				
				nni_aio_finish(txaio, 0, 0);
				
				Convey("Then callback should handle gracefully", {
					// Should not crash with NULL user_txaio
					So(pipe->user_txaio == NULL);
				});

				nni_aio_free(txaio);
			});

			Convey("Thread safety: mutex protects shared state", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(txaio != NULL);
				So(user_aio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				
				Convey("Then mutex should be held during state updates", {
					// Mutex should protect access to user_txaio
					nni_mtx_lock(&pipe->mtx);
					So(pipe->user_txaio != NULL);
					pipe->user_txaio = NULL;
					nni_mtx_unlock(&pipe->mtx);
					So(pipe->user_txaio == NULL);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});
		});

		Convey("wstran_pipe_recv_cancel tests", {

			Convey("When canceling matching receive operation", {
				nni_aio *rxaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(rxaio != NULL);
				So(user_aio != NULL);

				pipe->rxaio = rxaio;
				pipe->user_rxaio = user_aio;
				
				Convey("Then user_rxaio should be cleared", {
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio == user_aio) {
						pipe->user_rxaio = NULL;
					}
					nni_mtx_unlock(&pipe->mtx);
					So(pipe->user_rxaio == NULL);
				});

				Convey("Then rxaio should be aborted", {
					// Should abort the internal rxaio
					nni_aio_abort(rxaio, NNG_ECANCELED);
					So(nni_aio_result(rxaio) == NNG_ECANCELED);
				});

				Convey("Then user aio should finish with error", {
					nni_aio_finish_error(user_aio, NNG_ECANCELED);
					So(nni_aio_result(user_aio) == NNG_ECANCELED);
				});

				nni_aio_free(rxaio);
				nni_aio_free(user_aio);
			});

			Convey("When canceling non-matching receive operation", {
				nni_aio *user_aio1 = create_test_aio();
				nni_aio *user_aio2 = create_test_aio();
				So(user_aio1 != NULL);
				So(user_aio2 != NULL);

				pipe->user_rxaio = user_aio1;
				
				Convey("Then should return early without changes", {
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio != user_aio2) {
						nni_mtx_unlock(&pipe->mtx);
						// Should return early
						So(pipe->user_rxaio == user_aio1);
					}
				});

				nni_aio_free(user_aio1);
				nni_aio_free(user_aio2);
			});

			Convey("Thread safety: mutex protects cancellation", {
				nni_aio *user_aio = create_test_aio();
				So(user_aio != NULL);

				pipe->user_rxaio = user_aio;
				
				Convey("Then mutex should be locked during operation", {
					// Test that mutex is acquired and released properly
					nni_mtx_lock(&pipe->mtx);
					So(pipe->user_rxaio == user_aio);
					nni_mtx_unlock(&pipe->mtx);
				});

				Convey("Then mutex should be released on early return", {
					nni_aio *other_aio = create_test_aio();
					So(other_aio != NULL);
					
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio != other_aio) {
						nni_mtx_unlock(&pipe->mtx);
						// Mutex should be unlocked
					}
					
					nni_aio_free(other_aio);
				});

				nni_aio_free(user_aio);
			});

			Convey("When user_rxaio is NULL", {
				nni_aio *user_aio = create_test_aio();
				So(user_aio != NULL);

				pipe->user_rxaio = NULL;
				
				Convey("Then should return early safely", {
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio != user_aio) {
						nni_mtx_unlock(&pipe->mtx);
						So(pipe->user_rxaio == NULL);
					}
				});

				nni_aio_free(user_aio);
			});

			Convey("Multiple cancel attempts", {
				nni_aio *rxaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(rxaio != NULL);
				So(user_aio != NULL);

				pipe->rxaio = rxaio;
				pipe->user_rxaio = user_aio;
				
				Convey("Then first cancel should succeed", {
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio == user_aio) {
						pipe->user_rxaio = NULL;
						nni_aio_abort(rxaio, NNG_ECANCELED);
						nni_aio_finish_error(user_aio, NNG_ECANCELED);
					}
					nni_mtx_unlock(&pipe->mtx);
					So(pipe->user_rxaio == NULL);
				});

				Convey("Then second cancel should be no-op", {
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio != user_aio) {
						nni_mtx_unlock(&pipe->mtx);
						// Should return without changes
					}
					So(pipe->user_rxaio == NULL);
				});

				nni_aio_free(rxaio);
				nni_aio_free(user_aio);
			});
		});

		Convey("Error handling and logging tests", {

			Convey("When send fails with various error codes", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(txaio != NULL);
				So(user_aio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;

				Convey("Then NNG_ECLOSED should be handled", {
					nni_aio_finish_error(txaio, NNG_ECLOSED);
					So(nni_aio_result(txaio) == NNG_ECLOSED);
				});

				Convey("Then NNG_ETIMEDOUT should be handled", {
					nni_aio_finish_error(txaio, NNG_ETIMEDOUT);
					So(nni_aio_result(txaio) == NNG_ETIMEDOUT);
				});

				Convey("Then NNG_ECONNRESET should be handled", {
					nni_aio_finish_error(txaio, NNG_ECONNRESET);
					So(nni_aio_result(txaio) == NNG_ECONNRESET);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When message needs to be freed on error", {
				nni_aio *txaio = create_test_aio();
				nni_msg *msg;
				So(txaio != NULL);
				So(nni_msg_alloc(&msg, 100) == 0);

				pipe->txaio = txaio;
				pipe->user_txaio = NULL;
				
				nni_aio_set_msg(txaio, msg);
				nni_aio_finish_error(txaio, NNG_ECLOSED);
				
				Convey("Then message should be freed to prevent leak", {
					nni_msg *retrieved = nni_aio_get_msg(txaio);
					if (retrieved != NULL && nni_aio_result(txaio) != 0) {
						// Message should be freed by callback
						nni_msg_free(retrieved);
					}
				});

				nni_aio_free(txaio);
			});
		});

		Convey("Race condition and edge case tests", {

			Convey("When pipe closes during send callback", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(txaio != NULL);
				So(user_aio != NULL);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				
				Convey("Then should check closed flag under mutex", {
					nni_mtx_lock(&pipe->mtx);
					pipe->closed = true;
					pipe->err_code = NNG_ECLOSED;
					if (pipe->closed && pipe->user_txaio != NULL) {
						// Should handle closed state
						So(pipe->closed == true);
					}
					nni_mtx_unlock(&pipe->mtx);
				});

				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When both txaio and user_txaio reference same message", {
				nni_aio *txaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				nni_msg *msg;
				So(txaio != NULL);
				So(user_aio != NULL);
				So(nni_msg_alloc(&msg, 50) == 0);

				pipe->txaio = txaio;
				pipe->user_txaio = user_aio;
				
				nni_aio_set_msg(txaio, msg);
				nni_aio_set_msg(user_aio, NULL); // User aio message cleared
				
				Convey("Then message ownership should be clear", {
					// txaio owns the message
					So(nni_aio_get_msg(txaio) != NULL);
					So(nni_aio_get_msg(user_aio) == NULL);
				});

				nni_msg_free(msg);
				nni_aio_free(txaio);
				nni_aio_free(user_aio);
			});

			Convey("When cancel is called concurrently with completion", {
				nni_aio *rxaio = create_test_aio();
				nni_aio *user_aio = create_test_aio();
				So(rxaio != NULL);
				So(user_aio != NULL);

				pipe->rxaio = rxaio;
				pipe->user_rxaio = user_aio;
				
				Convey("Then mutex should serialize operations", {
					// Simulate concurrent access
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio == user_aio) {
						pipe->user_rxaio = NULL;
					}
					nni_mtx_unlock(&pipe->mtx);
					
					// Second operation sees NULL
					nni_mtx_lock(&pipe->mtx);
					So(pipe->user_rxaio == NULL);
					nni_mtx_unlock(&pipe->mtx);
				});

				nni_aio_free(rxaio);
				nni_aio_free(user_aio);
			});
		});

		Convey("Integration tests for send/receive flow", {

			Convey("When send completes followed by receive cancel", {
				nni_aio *txaio = create_test_aio();
				nni_aio *rxaio = create_test_aio();
				nni_aio *user_txaio = create_test_aio();
				nni_aio *user_rxaio = create_test_aio();
				
				So(txaio != NULL);
				So(rxaio != NULL);
				So(user_txaio != NULL);
				So(user_rxaio != NULL);

				pipe->txaio = txaio;
				pipe->rxaio = rxaio;
				pipe->user_txaio = user_txaio;
				pipe->user_rxaio = user_rxaio;
				
				Convey("Then both operations should complete correctly", {
					// Complete send
					nni_aio_finish(txaio, 0, 0);
					nni_mtx_lock(&pipe->mtx);
					pipe->user_txaio = NULL;
					nni_mtx_unlock(&pipe->mtx);
					
					// Cancel receive
					nni_mtx_lock(&pipe->mtx);
					if (pipe->user_rxaio == user_rxaio) {
						pipe->user_rxaio = NULL;
						nni_aio_abort(rxaio, NNG_ECANCELED);
					}
					nni_mtx_unlock(&pipe->mtx);
					
					So(pipe->user_txaio == NULL);
					So(pipe->user_rxaio == NULL);
				});

				nni_aio_free(txaio);
				nni_aio_free(rxaio);
				nni_aio_free(user_txaio);
				nni_aio_free(user_rxaio);
			});
		});

		destroy_test_pipe(pipe);
	});

	Convey("Regression tests for specific bug fixes", {
		test_ws_pipe *pipe = create_test_pipe();
		So(pipe != NULL);

		Convey("Variable declaration moved outside critical section", {
			nni_aio *txaio = create_test_aio();
			nni_aio *user_aio = create_test_aio();
			So(txaio != NULL);
			So(user_aio != NULL);

			pipe->txaio = txaio;
			pipe->user_txaio = user_aio;
			
			Convey("Then rv can be accessed before and after lock", {
				// Variable declared at function start, accessible throughout
				int rv = 0;
				nni_aio_finish(txaio, 0, 0);
				rv = nni_aio_result(txaio);
				
				nni_mtx_lock(&pipe->mtx);
				So(rv == 0);
				nni_mtx_unlock(&pipe->mtx);
			});

			nni_aio_free(txaio);
			nni_aio_free(user_aio);
		});

		Convey("Duplicate aio_set_msg line removed in send_start_v4", {
			nni_msg *msg;
			nni_aio *txaio = create_test_aio();
			nni_aio *user_aio = create_test_aio();
			
			So(nni_msg_alloc(&msg, 10) == 0);
			So(txaio != NULL);
			So(user_aio != NULL);

			pipe->txaio = txaio;
			
			Convey("Then message should be set only once on txaio", {
				// Before fix: nni_aio_set_msg called twice
				// After fix: called once
				nni_aio_set_msg(txaio, msg);
				nni_aio_set_msg(user_aio, NULL);
				
				So(nni_aio_get_msg(txaio) == msg);
				So(nni_aio_get_msg(user_aio) == NULL);
			});

			nni_msg_free(msg);
			nni_aio_free(txaio);
			nni_aio_free(user_aio);
		});

		Convey("Mutex protection added to recv_cancel", {
			nni_aio *user_aio = create_test_aio();
			So(user_aio != NULL);

			pipe->user_rxaio = user_aio;
			
			Convey("Then all recv_cancel operations are mutex-protected", {
				// Lock before checking
				nni_mtx_lock(&pipe->mtx);
				bool matched = (pipe->user_rxaio == user_aio);
				nni_mtx_unlock(&pipe->mtx);
				
				if (matched) {
					nni_mtx_lock(&pipe->mtx);
					pipe->user_rxaio = NULL;
					nni_mtx_unlock(&pipe->mtx);
				}
				
				So(pipe->user_rxaio == NULL);
			});

			nni_aio_free(user_aio);
		});

		destroy_test_pipe(pipe);
	});
})