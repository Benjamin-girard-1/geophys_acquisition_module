#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "card_magnetic.h"

typedef enum {
    EVENT_OUTPUTS_SAFE = 1,
    EVENT_RAIL_ON,
    EVENT_RAIL_OFF,
    EVENT_DELAY_SETTLING,
    EVENT_OUTPUT_ON,
    EVENT_DELAY_PULSE,
    EVENT_OUTPUT_OFF,
} event_t;

typedef struct {
    event_t events[16];
    size_t event_count;
    fw_status_t output_on_status;
} observation_t;

static void observe(observation_t *observation, event_t event)
{
    assert(observation->event_count <
           sizeof(observation->events) / sizeof(observation->events[0]));
    observation->events[observation->event_count++] = event;
}

static fw_status_t set_rail(void *context, bool enabled,
                            fw_error_context_t *error)
{
    (void)error;
    observation_t *observation = context;
    observe(observation, enabled ? EVENT_RAIL_ON : EVENT_RAIL_OFF);
    return FW_STATUS_OK;
}

static fw_status_t set_output(void *context,
                              card_magnetic_slot_t slot,
                              card_magnetic_operation_t operation,
                              bool enabled,
                              fw_error_context_t *error)
{
    (void)error;
    observation_t *observation = context;
    assert(slot == CARD_MAGNETIC_SLOT_2);
    assert(operation == CARD_MAGNETIC_OPERATION_RESET);
    observe(observation, enabled ? EVENT_OUTPUT_ON : EVENT_OUTPUT_OFF);
    return enabled ? observation->output_on_status : FW_STATUS_OK;
}

static fw_status_t outputs_safe(void *context, fw_error_context_t *error)
{
    (void)error;
    observation_t *observation = context;
    observe(observation, EVENT_OUTPUTS_SAFE);
    return FW_STATUS_OK;
}

static void delay_us(void *context, uint32_t duration_us)
{
    observation_t *observation = context;
    if (duration_us == CARD_MAGNETIC_RAIL_SETTLING_US) {
        observe(observation, EVENT_DELAY_SETTLING);
    } else {
        assert(duration_us == CARD_MAGNETIC_CONTROL_PULSE_US);
        observe(observation, EVENT_DELAY_PULSE);
    }
}

static card_magnetic_config_t make_config(observation_t *observation)
{
    return (card_magnetic_config_t) {
        .set_18v_rail = set_rail,
        .set_output = set_output,
        .outputs_safe = outputs_safe,
        .delay_us = delay_us,
        .context = observation,
        .rail_settling_us = CARD_MAGNETIC_RAIL_SETTLING_US,
        .control_pulse_us = CARD_MAGNETIC_CONTROL_PULSE_US,
    };
}

static void assert_events(const observation_t *observation,
                          const event_t *expected,
                          size_t expected_count)
{
    assert(observation->event_count == expected_count);
    for (size_t index = 0U; index < expected_count; index++) {
        assert(observation->events[index] == expected[index]);
    }
}

static void test_success_sequence(void)
{
    observation_t observation = {.output_on_status = FW_STATUS_OK};
    const card_magnetic_config_t config = make_config(&observation);
    fw_error_context_t error;

    assert(card_magnetic_pulse(
               &config, CARD_MAGNETIC_SLOT_2,
               CARD_MAGNETIC_OPERATION_RESET, &error) == FW_STATUS_OK);
    const event_t expected[] = {
        EVENT_OUTPUTS_SAFE,
        EVENT_RAIL_ON,
        EVENT_DELAY_SETTLING,
        EVENT_OUTPUT_ON,
        EVENT_DELAY_PULSE,
        EVENT_OUTPUT_OFF,
        EVENT_OUTPUTS_SAFE,
        EVENT_RAIL_OFF,
    };
    assert_events(&observation, expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_failure_still_restores_safe_state(void)
{
    observation_t observation = {.output_on_status = FW_STATUS_IO};
    const card_magnetic_config_t config = make_config(&observation);
    fw_error_context_t error;

    assert(card_magnetic_pulse(
               &config, CARD_MAGNETIC_SLOT_2,
               CARD_MAGNETIC_OPERATION_RESET, &error) == FW_STATUS_IO);
    const event_t expected[] = {
        EVENT_OUTPUTS_SAFE,
        EVENT_RAIL_ON,
        EVENT_DELAY_SETTLING,
        EVENT_OUTPUT_ON,
        EVENT_OUTPUTS_SAFE,
        EVENT_RAIL_OFF,
    };
    assert_events(&observation, expected,
                  sizeof(expected) / sizeof(expected[0]));
}

static void test_invalid_request_does_not_touch_hardware(void)
{
    observation_t observation = {.output_on_status = FW_STATUS_OK};
    const card_magnetic_config_t config = make_config(&observation);

    assert(card_magnetic_pulse(
               &config, CARD_MAGNETIC_SLOT_INVALID,
               CARD_MAGNETIC_OPERATION_RESET, NULL) ==
           FW_STATUS_INVALID_ARGUMENT);
    assert(observation.event_count == 0U);
}

int main(void)
{
    test_success_sequence();
    test_failure_still_restores_safe_state();
    test_invalid_request_does_not_touch_hardware();
    puts("magnetic card tests passed");
    return EXIT_SUCCESS;
}
