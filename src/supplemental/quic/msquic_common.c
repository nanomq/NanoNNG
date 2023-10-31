#include "quic_private.h"
#include "core/nng_impl.h"

static int is_msquic_inited = 0;

void
msquic_close()
{
	if (MsQuic != NULL) {
		if (configuration != NULL) {
			MsQuic->ConfigurationClose(configuration);
		}
		if (registration != NULL) {
			// This will block until all outstanding child objects
			// have been closed.
			MsQuic->RegistrationClose(registration);
		}
		MsQuicClose(MsQuic);
		is_msquic_inited = 0;
	}
}

int
msquic_open()
{
	if (is_msquic_inited == 1)
		return 0;

	QUIC_STATUS rv = QUIC_STATUS_SUCCESS;
	// only Open MsQUIC lib once, otherwise cause memleak
	if (MsQuic == NULL)
		if (QUIC_FAILED(rv = MsQuicOpen2(&MsQuic))) {
			log_error("MsQuicOpen2 failed, 0x%x!\n", rv);
			goto error;
		}

	// Create a registration for the app's connections.
	rv = MsQuic->RegistrationOpen(&quic_reg_config, &registration);
	if (QUIC_FAILED(rv)) {
		log_error("RegistrationOpen failed, 0x%x!\n", rv);
		goto error;
	}

	is_msquic_inited = 1;
	log_info("Msquic is enabled");
	return 0;

error:
	msquic_close();
	return -1;
}

