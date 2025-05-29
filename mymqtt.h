#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>

int mqttinit(char *MqttBroker);
uint8_t mqtt_status_write(uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write(uint8_t message, char *data, bool *output_ready);

/* Dual-tuner MQTT functions */
uint8_t mqtt_status_write_tuner(uint8_t tuner_id, uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write_tuner(uint8_t tuner_id, uint8_t message, char *data, bool *output_ready);
void mqtt_set_dual_tuner_mode(bool enabled);
void mqtt_init_tuner_values(uint32_t freq1, uint32_t sr1, uint32_t freq2, uint32_t sr2, const char *tsip1, const char *tsip2);
void mqtt_process_dual_command(const char *topic, const char *payload);

/* Dual-tuner MQTT global variable */
extern bool dual_tuner_mqtt_enabled;

#endif
