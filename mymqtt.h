#ifndef MQTT_H
#define MQTT_H

#include <stdint.h>
#include <stdbool.h>

int mqttinit(char *MqttBroker);
uint8_t mqtt_status_write(uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write(uint8_t message, char *data, bool *output_ready);

/* Tuner 2 MQTT status functions (publishes to dt2/longmynd/ prefix) */
uint8_t mqtt_status_write_tuner2(uint8_t message, uint32_t data, bool *output_ready);
uint8_t mqtt_status_string_write_tuner2(uint8_t message, char *data, bool *output_ready);

#endif
