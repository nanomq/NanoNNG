#ifndef NNG_SUPP_QUIC_PRIVATE_H
#define NNG_SUPP_QUIC_PRIVATE_H

#include "msquic.h"

// Config for msquic
static const QUIC_REGISTRATION_CONFIG quic_reg_config = {
	"mqtt",
	QUIC_EXECUTION_PROFILE_LOW_LATENCY
};

static const QUIC_BUFFER quic_alpn = {
	sizeof("mqtt") - 1,
	(uint8_t *) "mqtt"
};

static const QUIC_API_TABLE *MsQuic = NULL;

static HQUIC registration;
static HQUIC configuration;

int  msquic_open();
void msquic_close();

#endif
