#include "adc_record.h"

_Static_assert(
    ADC_RECORD_HEADER_SIZE_BYTES + ADC_RECORD_PAYLOAD_SIZE_BYTES +
        ADC_RECORD_CRC_SIZE_BYTES == ADC_RECORD_SIZE_BYTES,
    "proposed ADC-record regions must total 512 bytes");

_Static_assert(
    ADC_RECORD_PAYLOAD_OFFSET_BYTES == ADC_RECORD_HEADER_SIZE_BYTES,
    "proposed ADC-record payload must immediately follow its header");

_Static_assert(
    ADC_RECORD_CRC_OFFSET_BYTES + ADC_RECORD_CRC_SIZE_BYTES ==
        ADC_RECORD_SIZE_BYTES,
    "proposed ADC-record CRC must occupy the final four bytes");

/* Record codec implementation intentionally deferred pending approval. */
