#include "adc_record.h"

_Static_assert(
    ADC_RECORD_SAMPLE_PERIOD_OFFSET_BYTES + UINT16_C(4) ==
        ADC_RECORD_HEADER_SIZE_BYTES,
    "sample period must occupy the final four header bytes");

_Static_assert(
    ADC_RECORD_HEADER_SIZE_BYTES + ADC_RECORD_PAYLOAD_SIZE_BYTES +
        ADC_RECORD_CRC_SIZE_BYTES == ADC_RECORD_SIZE_BYTES,
    "ADC-record regions must total 512 bytes");

_Static_assert(
    ADC_RECORD_PAYLOAD_OFFSET_BYTES == ADC_RECORD_HEADER_SIZE_BYTES,
    "ADC-record payload must immediately follow its header");

_Static_assert(
    ADC_RECORD_CRC_OFFSET_BYTES + ADC_RECORD_CRC_SIZE_BYTES ==
        ADC_RECORD_SIZE_BYTES,
    "ADC-record CRC must occupy the final four bytes");

/* Record codec implementation intentionally deferred pending test vectors. */
