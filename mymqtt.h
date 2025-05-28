#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>

// MQTT initialization and cleanup
int mqttinit(char *MqttBroker);
int mqttend();

// Original single-tuner functions (maintained for compatibility)
uint8_t mqtt_status_write(uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write(uint8_t message, char *data, bool *output_ready);

// Dual-tuner MQTT functions
uint8_t mqtt_status_write_tuner(uint8_t tuner, uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write_tuner(uint8_t tuner, uint8_t message, char *data, bool *output_ready);

// Tuning parameter control functions
void mqtt_set_tuning_callback(void (*callback)(uint8_t tuner, const char* param, const char* value));
uint8_t mqtt_publish_tuning_status(uint8_t tuner);

// Configuration publishing
uint8_t mqtt_publish_config_status(uint8_t tuner);

// Initialization status publishing
uint8_t mqtt_publish_init_status(uint8_t tuner, bool success);

#endif
