#ifndef GEOPHYS_APP_H
#define GEOPHYS_APP_H

#include <stdint.h>

#include "fw_error.h"

/* Zero identifies the current unreleased development firmware. */
#define APP_FIRMWARE_VERSION UINT32_C(0x00000000)

fw_status_t app_start(fw_error_context_t *error);

#endif /* GEOPHYS_APP_H */
