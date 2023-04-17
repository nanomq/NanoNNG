//
// Copyright 2022 Staysail Systems, Inc. <info@staysail.tech>
// Copyright 2018 Capitar IT Group BV <info@capitar.com>
// Copyright 2018 Devolutions <info@devolutions.net>
//
// This software is supplied under the terms of the MIT License, a
// copy of which should be located in the distribution where this
// file was obtained (LICENSE.txt).  A copy of the license may also be
// found online at https://opensource.org/licenses/MIT.
//

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#include <nng/nng.h>
#include <nng/protocol/pair1/pair.h>

#include "convey.h"
#include "stubs.h"
#include "trantest.h"

TestMain("Broker-MQTT-TCP Transport", {
	mqtt_broker_trantest_test("nmq-tcp://127.0.0.1:");
})
