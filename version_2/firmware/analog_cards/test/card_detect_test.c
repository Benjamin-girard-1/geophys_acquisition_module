#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include "card_detect.h"

typedef struct {
    const uint32_t *samples_mv;
    size_t sample_count;
    size_t next_sample;
    size_t fail_at;
    uint32_t delay_calls;
    uint32_t delayed_us;
} fake_source_t;

static fw_status_t fake_read_mv(void *context,
                                uint32_t *millivolts,
                                fw_error_context_t *error)
{
    fake_source_t *source = context;
    if (source->next_sample == source->fail_at) {
        if (error != NULL) {
            *error = (fw_error_context_t) {
                .status = FW_STATUS_IO,
                .resource = FW_ERROR_RESOURCE_ADC,
                .operation = FW_ERROR_OPERATION_READ,
                .instance = 7U,
                .detail = (uint32_t)source->next_sample,
            };
        }
        return FW_STATUS_IO;
    }

    assert(source->next_sample < source->sample_count);
    *millivolts = source->samples_mv[source->next_sample++];
    return FW_STATUS_OK;
}

static void fake_delay_us(void *context, uint32_t duration_us)
{
    fake_source_t *source = context;
    source->delay_calls++;
    source->delayed_us += duration_us;
}

static card_detect_measure_config_t make_config(fake_source_t *source,
                                                 uint16_t sample_count)
{
    return (card_detect_measure_config_t) {
        .read_mv = fake_read_mv,
        .delay_us = fake_delay_us,
        .read_context = source,
        .delay_context = source,
        .sample_count = sample_count,
        .sample_interval_us = 10000U,
        .instance = 1U,
    };
}

static void test_average_and_robust_median(void)
{
    const uint32_t samples_mv[] = {2100U, 2200U, 2300U, 100U, 4000U};
    fake_source_t source = {
        .samples_mv = samples_mv,
        .sample_count = 5U,
        .fail_at = SIZE_MAX,
    };
    const card_detect_measure_config_t config = make_config(&source, 5U);
    card_detect_measurement_t measurement;
    fw_error_context_t error;

    assert(card_detect_measure(&config, &measurement, &error) ==
           FW_STATUS_OK);
    assert(measurement.average_mv == 2140U);
    assert(measurement.median_mv == 2200U);
    assert(measurement.minimum_mv == 100U);
    assert(measurement.maximum_mv == 4000U);
    assert(measurement.sample_count == 5U);
    assert(source.delay_calls == 4U);
    assert(source.delayed_us == 40000U);
    assert(error.status == FW_STATUS_OK);
}

static void test_even_sample_median_is_rounded(void)
{
    const uint32_t samples_mv[] = {4001U, 1000U, 3000U, 2000U};
    fake_source_t source = {
        .samples_mv = samples_mv,
        .sample_count = 4U,
        .fail_at = SIZE_MAX,
    };
    const card_detect_measure_config_t config = make_config(&source, 4U);
    card_detect_measurement_t measurement;

    assert(card_detect_measure(&config, &measurement, NULL) ==
           FW_STATUS_OK);
    assert(measurement.average_mv == 2500U);
    assert(measurement.median_mv == 2500U);
}

static void test_read_error_is_preserved(void)
{
    const uint32_t samples_mv[] = {2200U, 2201U, 2202U};
    fake_source_t source = {
        .samples_mv = samples_mv,
        .sample_count = 3U,
        .fail_at = 1U,
    };
    const card_detect_measure_config_t config = make_config(&source, 3U);
    card_detect_measurement_t measurement = {
        .average_mv = 999U,
    };
    fw_error_context_t error;

    assert(card_detect_measure(&config, &measurement, &error) ==
           FW_STATUS_IO);
    assert(measurement.sample_count == 0U);
    assert(measurement.average_mv == 0U);
    assert(error.status == FW_STATUS_IO);
    assert(error.instance == 7U);
    assert(error.detail == 1U);
}

static void test_invalid_sample_count_is_rejected(void)
{
    const uint32_t sample_mv = 2200U;
    fake_source_t source = {
        .samples_mv = &sample_mv,
        .sample_count = 1U,
        .fail_at = SIZE_MAX,
    };
    card_detect_measure_config_t config = make_config(&source, 0U);
    card_detect_measurement_t measurement;
    fw_error_context_t error;

    assert(card_detect_measure(&config, &measurement, &error) ==
           FW_STATUS_INVALID_ARGUMENT);
    assert(error.instance == 1U);

    config.sample_count = CARD_DETECT_MAX_SAMPLE_COUNT + 1U;
    assert(card_detect_measure(&config, &measurement, &error) ==
           FW_STATUS_INVALID_ARGUMENT);
}

int main(void)
{
    test_average_and_robust_median();
    test_even_sample_median_is_rounded();
    test_read_error_is_preserved();
    test_invalid_sample_count_is_rejected();
    return 0;
}
