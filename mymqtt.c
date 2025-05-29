#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "errors.h"
#include "main.h"

/* Dual-tuner MQTT globals */
bool dual_tuner_mqtt_enabled = false;

/* Forward declarations */
void mqtt_process_dual_command(const char *topic, const char *payload);

/* Callback called when the client receives a CONNACK message from the broker. */
void on_connect(struct mosquitto *mosq, void *obj, int reason_code)
{
	int rc;
	/* Print out the connection result. mosquitto_connack_string() produces an
	 * appropriate string for MQTT v3.x clients, the equivalent for MQTT v5.0
	 * clients is mosquitto_reason_string().
	 */
	// fprintf(stderr, "on_connect: %s\n", mosquitto_connack_string(reason_code));
	if (reason_code != 0)
	{
		/* If the connection fails for any reason, we don't want to keep on
		 * retrying in this example, so disconnect. Without this, the client
		 * will attempt to reconnect. */
		mosquitto_disconnect(mosq);
	}

	/* Making subscriptions in the on_connect() callback means that if the
	 * connection drops and is automatically resumed by the client, then the
	 * subscriptions will be recreated when the client reconnects. */

	rc = mosquitto_subscribe(mosq, NULL, "cmd/longmynd/#", 1);
	if (rc != MOSQ_ERR_SUCCESS)
	{
		fprintf(stderr, "Error subscribing: %s\n", mosquitto_strerror(rc));
		mosquitto_disconnect(mosq);
	}

	/* Subscribe to dual-tuner specific topics if enabled */
	if (dual_tuner_mqtt_enabled) {
		rc = mosquitto_subscribe(mosq, NULL, "cmd/longmynd/tuner1/#", 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			fprintf(stderr, "Error subscribing to tuner1 topics: %s\n", mosquitto_strerror(rc));
		}

		rc = mosquitto_subscribe(mosq, NULL, "cmd/longmynd/tuner2/#", 1);
		if (rc != MOSQ_ERR_SUCCESS) {
			fprintf(stderr, "Error subscribing to tuner2 topics: %s\n", mosquitto_strerror(rc));
		}
	}
}

/* Callback called when the broker sends a SUBACK in response to a SUBSCRIBE. */
void on_subscribe(struct mosquitto *mosq, void *obj, int mid, int qos_count, const int *granted_qos)
{
	int i;
	bool have_subscription = false;

	/* In this example we only subscribe to a single topic at once, but a
	 * SUBSCRIBE can contain many topics at once, so this is one way to check
	 * them all. */
	for (i = 0; i < qos_count; i++)
	{
		// fprintf(stderr,"on_subscribe: %d:granted qos = %d\n", i, granted_qos[i]);
		if (granted_qos[i] <= 2)
		{
			have_subscription = true;
		}
	}
	if (have_subscription == false)
	{
		/* The broker rejected all of our subscriptions, we know we only sent
		 * the one SUBSCRIBE, so there is no point remaining connected. */
		fprintf(stderr, "Error: All subscriptions rejected.\n");
		mosquitto_disconnect(mosq);
	}
}

uint32_t Symbolrate = 0;
uint32_t Frequency = 0;
bool sport;
char stsip[255];

/* Dual-tuner specific globals */
uint32_t Symbolrate_tuner2 = 0;
uint32_t Frequency_tuner2 = 0;
bool sport_tuner2;
char stsip_tuner2[255];

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *key = msg->topic;
	char *svalue = (char *)msg->payload;

	/* Handle dual-tuner commands first */
	if (dual_tuner_mqtt_enabled) {
		mqtt_process_dual_command(key, svalue);
	}

	/* Backward compatibility: existing commands control tuner 1 */
	if (strcmp(key, "cmd/longmynd/sr") == 0)
	{
		Symbolrate = atol(svalue);
		config_set_symbolrate(Symbolrate);
	}
	if (strcmp(key, "cmd/longmynd/frequency") == 0)
	{
		Frequency = atol(svalue);
		config_set_frequency(Frequency);
	}
	if (strcmp(key, "cmd/longmynd/swport") == 0)
	{
		sport = atoi(svalue);
		config_set_swport(sport);
	}

	if (strcmp(key, "cmd/longmynd/tsip") == 0)
	{
		strcpy(stsip, svalue);
		config_set_tsip(svalue);
	}

	if (strcmp(key, "cmd/longmynd/polar") == 0)
	{
		if (strcmp(svalue, "h") == 0)
			config_set_lnbv(true, true);
		if (strcmp(svalue, "v") == 0)
			config_set_lnbv(true, false);
		if (strcmp(svalue, "n") == 0)
			config_set_lnbv(false, false);
	}
}

/* Callback called when the client receives a message. */
// extern void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg);

static struct mosquitto *mosq;

int mqttinit(char *MqttBroker)
{

	int rc;

	/* Required before calling other mosquitto functions */
	mosquitto_lib_init();

	/* Create a new client instance.
	 * id = NULL -> ask the broker to generate a client id for us
	 * clean session = true -> the broker should remove old sessions when we connect
	 * obj = NULL -> we aren't passing any of our private data for callbacks
	 */
	mosq = mosquitto_new(NULL, true, NULL);
	if (mosq == NULL)
	{
		fprintf(stderr, "Error: Out of memory.\n");
		return 1;
	}

	/* Configure callbacks. This should be done before connecting ideally. */
	mosquitto_connect_callback_set(mosq, on_connect);
	mosquitto_subscribe_callback_set(mosq, on_subscribe);
	mosquitto_message_callback_set(mosq, on_message);

	/* Connect to test.mosquitto.org on port 1883, with a keepalive of 60 seconds.
	 * This call makes the socket connection only, it does not complete the MQTT
	 * CONNECT/CONNACK flow, you should use mosquitto_loop_start() or
	 * mosquitto_loop_forever() for processing net traffic. */
	rc = mosquitto_connect(mosq, MqttBroker, 1883, 60);
	if (rc != MOSQ_ERR_SUCCESS)
	{
		mosquitto_destroy(mosq);
		fprintf(stderr, "Error: %s\n", mosquitto_strerror(rc));
		return 1;
	}

	/* Run the network loop in a blocking call. The only thing we do in this
	 * example is to print incoming messages, so a blocking call here is fine.
	 *
	 * This call will continue forever, carrying automatic reconnections if
	 * necessary, until the user calls mosquitto_disconnect().
	 */
	// mosquitto_loop_forever(mosq, -1, 1);
	mosquitto_loop_start(mosq);

	// mosquitto_lib_cleanup();
	return 0;
}

int mqttend()
{
	return (mosquitto_lib_cleanup());
}

const char StatusString[31][255] = {"", "rx_state", "lna_gain", "puncrate", "poweri", "powerq", "carrier_frequency", "constel_i", "constel_q",
									"symbolrate", "viterbi_error", "ber", "mer", "service_name", "provider_name", "ts_null", "es_pid", "es_type", "modcod", "short_frame", "pilots",
									"ldpc_errors", "bch_errors", "bch_uncorect", "lnb_supply", "polarisation", "agc1", "agc2", "matype1", "matype2", "rolloff"};

const char StateString[5][255] = {"Init", "Hunting", "found header", "demod_s", "demod_s2"};

uint8_t mqtt_status_write(uint8_t message, uint32_t data, bool *output_ready)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* takes a buffer and writes out the contents to udp socket                                           */
	/* *buffer: the buffer that contains the data to be sent                                              */
	/*     len: the length (number of bytes) of data to be sent                                           */
	/*  return: error code                                                                                */
	/* -------------------------------------------------------------------------------------------------- */
	(void)output_ready;
	uint8_t err = ERROR_NONE;
	char status_topic[255];
	char status_message[255];
	static int latest_modcod = 0;
	/* WARNING: This currently prints as signed integer (int32_t), even though function appears to expect unsigned (uint32_t) */
	sprintf(status_topic, "dt/longmynd/%s", StatusString[message]);
	if (message == STATUS_STATE) // state machine
	{
		sprintf(status_message, "%s", StateString[data]);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/set/sr");
		sprintf(status_message, "%d", Symbolrate);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/set/frequency");
		sprintf(status_message, "%d", Frequency);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/set/swport");
		sprintf(status_message, "%d", sport);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/set/tsip");
		sprintf(status_message, "%s", stsip);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		extern size_t video_pcrpts;
		extern size_t audio_pcrpts;
		extern long transmission_delay;

		sprintf(status_topic, "dt/longmynd/videobuffer");
		sprintf(status_message, "%d", video_pcrpts);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/audiobuffer");
		sprintf(status_message, "%d", audio_pcrpts);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		if(transmission_delay!=0)
		{
		sprintf(status_topic, "dt/longmynd/transdelay");
		sprintf(status_message, "%ld", transmission_delay);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
		}
	}
	else if (message == STATUS_SYMBOL_RATE)
	{
		data = (data + 500) / 1000; // SR EN KS
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_MODCOD)
	{
		int modcod = data;
		latest_modcod = modcod;
		char modulation[50];
		char fec[50];
		const char TabFec[][255] = {"none", "1/4", "1/3", "2/5", "1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/5", "2/3", "3/4", "5/6",
									"8/9", "9/10", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/4", "4/5", "5/6", "8/9", "9/10"};

		if (modcod < 12)
			strcpy(modulation, "QPSK");
		if (modcod == 0)
			strcpy(modulation, "none");
		if ((modcod >= 12) && (modcod <= 17))
			strcpy(modulation, "8PSK");
		if ((modcod >= 18) && (modcod <= 23))
			strcpy(modulation, "16APSK");
		if ((modcod >= 24) && (modcod <= 28))
			strcpy(modulation, "32APSK");

		strcpy(fec, TabFec[modcod]);
		mosquitto_publish(mosq, NULL, "dt/longmynd/modulation", strlen(modulation), modulation, 2, false);
		mosquitto_publish(mosq, NULL, "dt/longmynd/fec", strlen(fec), fec, 2, false);

	}
	else if (message == STATUS_MATYPE2)
	{
		sprintf(status_message, "%x", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ROLLOFF)
	{
		sprintf(status_topic, "dt/longmynd/rolloff");
		if(data==0)	sprintf(status_message, "0.35");
		if(data==1)	sprintf(status_message, "0.25");
		if(data==2)	sprintf(status_message, "0.20");
		if(data==3)	sprintf(status_message, "0.15");
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_MATYPE1)
	{
		char matype[50];
		switch ((data & 0xC0) >> 6)
		{
		case 0:
			strcpy(matype, "Generic packetized");
			break;
		case 1:
			strcpy(matype, "Generic continuous");
			break;
		case 2:
			strcpy(matype, "Generic packetized");
			break;
		case 3:
			strcpy(matype, "Transport");
			break;
		}

		mosquitto_publish(mosq, NULL, status_topic, strlen(matype), matype, 2, false);
	}
	else if (message == STATUS_MER)
	{
		int TheoricMER[] = {0, -24, -12, 0, 10, 22, 32, 40, 46, 52, 62, 65, 55, 66, 79, 94, 106, 110, 90, 102, 110, 116, 129, 131, 126, 136, 143, 157, 161};
		sprintf(status_message, "%0.1f", ((int)data) / 10.0);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
		char smargin[50];
		if (latest_modcod != 0)
		{

			int Margin = (int)data - TheoricMER[latest_modcod];
			sprintf(smargin, "%d", Margin / 10);
			mosquitto_publish(mosq, NULL, "dt/longmynd/margin_db", strlen(smargin), smargin, 2, false);
		}
		else
		{
			sprintf(smargin, "%d", 0);
			mosquitto_publish(mosq, NULL, "dt/longmynd/margin_db", strlen(smargin), smargin, 2, false);
		}
	}
	else if ((message == STATUS_CONSTELLATION_I) || (message == STATUS_CONSTELLATION_Q))
	{
		int8_t signed_data;
		signed_data = (int8_t)data;
		sprintf(status_message, "%d", data);

		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}

	return err;
}

/* -------------------------------------------------------------------------------------------------- */
uint8_t mqtt_status_string_write(uint8_t message, char *data, bool *output_ready)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* takes a buffer and writes out the contents to udp socket                                           */
	/* *buffer: the buffer that contains the data to be sent                                              */
	/*     len: the length (number of bytes) of data to be sent                                           */
	/*  return: error code                                                                                */
	/* -------------------------------------------------------------------------------------------------- */
	(void)output_ready;
	uint8_t err = ERROR_NONE;

	char status_topic[255];

	sprintf(status_topic, "dt/longmynd/%s", StatusString[message]);

	/* Debug output for service and provider names */
	if (message == STATUS_SERVICE_NAME || message == STATUS_SERVICE_PROVIDER_NAME) {
		printf("MQTT DEBUG: Single-tuner publishing %s = '%s' (length: %zu)\n",
		       StatusString[message], data, strlen(data));
	}

	mosquitto_publish(mosq, NULL, status_topic, strlen(data), data, 2, false);

	return err;
}

/* -------------------------------------------------------------------------------------------------- */
/* Dual-tuner MQTT functions                                                                          */
/* -------------------------------------------------------------------------------------------------- */

void mqtt_set_dual_tuner_mode(bool enabled)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* Enable or disable dual-tuner MQTT mode                                                            */
	/*   enabled: true to enable dual-tuner MQTT topics                                                 */
	/* -------------------------------------------------------------------------------------------------- */
	dual_tuner_mqtt_enabled = enabled;
	printf("Flow: MQTT dual-tuner mode %s\n", enabled ? "enabled" : "disabled");
}

void mqtt_init_tuner_values(uint32_t freq1, uint32_t sr1, uint32_t freq2, uint32_t sr2, const char *tsip1, const char *tsip2)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* Initialize MQTT tuner values from command line configuration                                      */
	/*   freq1: tuner 1 frequency in KHz                                                                 */
	/*   sr1: tuner 1 symbol rate in KSymbols/s                                                          */
	/*   freq2: tuner 2 frequency in KHz                                                                 */
	/*   sr2: tuner 2 symbol rate in KSymbols/s                                                          */
	/*   tsip1: tuner 1 TS IP address                                                                    */
	/*   tsip2: tuner 2 TS IP address                                                                    */
	/* -------------------------------------------------------------------------------------------------- */
	Frequency = freq1;
	Symbolrate = sr1;
	Frequency_tuner2 = freq2;
	Symbolrate_tuner2 = sr2;

	if (tsip1) {
		strncpy(stsip, tsip1, sizeof(stsip) - 1);
		stsip[sizeof(stsip) - 1] = '\0';
	}

	if (tsip2) {
		strncpy(stsip_tuner2, tsip2, sizeof(stsip_tuner2) - 1);
		stsip_tuner2[sizeof(stsip_tuner2) - 1] = '\0';
	}

	printf("Flow: MQTT tuner values initialized - T1: %d KHz/%d KS/s, T2: %d KHz/%d KS/s\n",
	       freq1, sr1, freq2, sr2);
}

void mqtt_process_dual_command(const char *topic, const char *payload)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* Process dual-tuner specific MQTT commands                                                         */
	/*   topic: MQTT topic string                                                                        */
	/*   payload: MQTT payload string                                                                    */
	/* -------------------------------------------------------------------------------------------------- */

	/* Tuner 1 commands */
	if (strcmp(topic, "cmd/longmynd/tuner1/sr") == 0) {
		uint32_t symbolrate = atol(payload);
		printf("MQTT: Tuner 1 symbol rate = %d\n", symbolrate);
		config_set_symbolrate(symbolrate);  // For now, use existing function
	}
	else if (strcmp(topic, "cmd/longmynd/tuner1/frequency") == 0) {
		uint32_t frequency = atol(payload);
		printf("MQTT: Tuner 1 frequency = %d\n", frequency);
		config_set_frequency(frequency);  // For now, use existing function
	}
	else if (strcmp(topic, "cmd/longmynd/tuner1/polar") == 0) {
		printf("MQTT: Tuner 1 polarization = %s\n", payload);
		if (strcmp(payload, "h") == 0)
			config_set_lnbv(true, true);
		else if (strcmp(payload, "v") == 0)
			config_set_lnbv(true, false);
		else if (strcmp(payload, "n") == 0)
			config_set_lnbv(false, false);
	}

	/* Tuner 2 commands */
	else if (strcmp(topic, "cmd/longmynd/tuner2/sr") == 0) {
		uint32_t symbolrate = atol(payload);
		printf("MQTT: Tuner 2 symbol rate = %d\n", symbolrate);
		if (symbolrate <= 27500 && symbolrate >= 33) {
			Symbolrate_tuner2 = symbolrate;
			config_set_symbolrate_tuner2(symbolrate);
			printf("MQTT: Tuner 2 symbol rate set to %d KSymbols/s\n", symbolrate);
		} else {
			printf("ERROR: MQTT Tuner 2 symbol rate %d out of range (33-27500 KSymbols/s)\n", symbolrate);
		}
	}
	else if (strcmp(topic, "cmd/longmynd/tuner2/frequency") == 0) {
		uint32_t frequency = atol(payload);
		printf("MQTT: Tuner 2 frequency = %d\n", frequency);
		if (frequency <= 2450000 && frequency >= 144000) {
			Frequency_tuner2 = frequency;
			config_set_frequency_tuner2(frequency);
			printf("MQTT: Tuner 2 frequency set to %d KHz\n", frequency);
		} else {
			printf("ERROR: MQTT Tuner 2 frequency %d out of range (144000-2450000 KHz)\n", frequency);
		}
	}
	else if (strcmp(topic, "cmd/longmynd/tuner2/polar") == 0) {
		printf("MQTT: Tuner 2 polarization = %s\n", payload);
		if (strcmp(payload, "h") == 0) {
			config_set_lnbv_tuner2(true, true);
			printf("MQTT: Tuner 2 polarization set to horizontal (18V)\n");
		}
		else if (strcmp(payload, "v") == 0) {
			config_set_lnbv_tuner2(true, false);
			printf("MQTT: Tuner 2 polarization set to vertical (13V)\n");
		}
		else if (strcmp(payload, "n") == 0) {
			config_set_lnbv_tuner2(false, false);
			printf("MQTT: Tuner 2 polarization supply disabled\n");
		}
		else {
			printf("ERROR: MQTT Tuner 2 invalid polarization value '%s' (use 'h', 'v', or 'n')\n", payload);
		}
	}
	/* Additional tuner 2 commands for completeness */
	else if (strcmp(topic, "cmd/longmynd/tuner2/swport") == 0) {
		bool sport_val = atoi(payload);
		printf("MQTT: Tuner 2 port swap = %s\n", sport_val ? "true" : "false");
		// Note: Port swap affects both tuners globally, so we use the existing function
		sport_tuner2 = sport_val;
		config_set_swport(sport_val);
		printf("MQTT: Port swap setting applied globally\n");
	}
	else if (strcmp(topic, "cmd/longmynd/tuner2/tsip") == 0) {
		printf("MQTT: Tuner 2 TS IP = %s\n", payload);
		strcpy(stsip_tuner2, payload);
		// Note: Tuner 2 TS IP is set via command line (-j option), not dynamically changeable
		printf("WARNING: MQTT Tuner 2 TS IP change not supported - use command line -j option\n");
	}
	/* REMOVED: Duplicate tuner1 commands - already handled above */
}

uint8_t mqtt_status_write_tuner(uint8_t tuner_id, uint8_t message, uint32_t data, bool *output_ready)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* Write status for a specific tuner to MQTT                                                         */
	/*   tuner_id: 1 for tuner 1, 2 for tuner 2                                                         */
	/*   message: status message type                                                                    */
	/*   data: status data                                                                               */
	/*   output_ready: output ready flag                                                                 */
	/*   return: error code                                                                              */
	/* -------------------------------------------------------------------------------------------------- */
	(void)output_ready;
	uint8_t err = ERROR_NONE;
	char status_topic[255];
	char status_message[255];
	static int latest_modcod_t1 = 0;
	static int latest_modcod_t2 = 0;
	const char *topic_prefix;

	if (tuner_id != 1 && tuner_id != 2) {
		printf("ERROR: Invalid tuner ID: %d\n", tuner_id);
		return ERROR_ARGS_INPUT;
	}

	/* Set topic prefix based on tuner ID */
	topic_prefix = (tuner_id == 1) ? "dt" : "dt2";

	/* Build tuner-specific topic with proper prefix */
	sprintf(status_topic, "%s/longmynd/%s", topic_prefix, StatusString[message]);

	if (message == STATUS_STATE) // state machine
	{
		sprintf(status_message, "%s", StateString[data]);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "%s/longmynd/set/sr", topic_prefix);
		if (tuner_id == 1) {
			sprintf(status_message, "%d", Symbolrate);
		} else {
			sprintf(status_message, "%d", Symbolrate_tuner2);
		}
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "%s/longmynd/set/frequency", topic_prefix);
		if (tuner_id == 1) {
			sprintf(status_message, "%d", Frequency);
		} else {
			sprintf(status_message, "%d", Frequency_tuner2);
		}
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "%s/longmynd/set/swport", topic_prefix);
		if (tuner_id == 1) {
			sprintf(status_message, "%d", sport);
		} else {
			sprintf(status_message, "%d", sport_tuner2);
		}
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "%s/longmynd/set/tsip", topic_prefix);
		if (tuner_id == 1) {
			sprintf(status_message, "%s", stsip);
		} else {
			sprintf(status_message, "%s", stsip_tuner2);
		}
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		extern size_t video_pcrpts;
		extern size_t audio_pcrpts;
		extern long transmission_delay;

		sprintf(status_topic, "%s/longmynd/videobuffer", topic_prefix);
		sprintf(status_message, "%d", video_pcrpts);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "%s/longmynd/audiobuffer", topic_prefix);
		sprintf(status_message, "%d", audio_pcrpts);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		if(transmission_delay!=0)
		{
		sprintf(status_topic, "%s/longmynd/transdelay", topic_prefix);
		sprintf(status_message, "%ld", transmission_delay);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
		}
	}
	else if (message == STATUS_SYMBOL_RATE)
	{
		data = (data + 500) / 1000; // SR EN KS
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_MODCOD)
	{
		int modcod = data;
		if (tuner_id == 1) latest_modcod_t1 = modcod;
		else latest_modcod_t2 = modcod;

		char modulation[50];
		char fec[50];
		const char TabFec[][255] = {"none", "1/4", "1/3", "2/5", "1/2", "3/5", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/5", "2/3", "3/4", "5/6",
									"8/9", "9/10", "2/3", "3/4", "4/5", "5/6", "8/9", "9/10", "3/4", "4/5", "5/6", "8/9", "9/10"};

		if (modcod < 12)
			strcpy(modulation, "QPSK");
		if (modcod == 0)
			strcpy(modulation, "none");
		if ((modcod >= 12) && (modcod <= 17))
			strcpy(modulation, "8PSK");
		if ((modcod >= 18) && (modcod <= 23))
			strcpy(modulation, "16APSK");
		if ((modcod >= 24) && (modcod <= 28))
			strcpy(modulation, "32APSK");

		strcpy(fec, TabFec[modcod]);
		sprintf(status_topic, "%s/longmynd/modulation", topic_prefix);
		mosquitto_publish(mosq, NULL, status_topic, strlen(modulation), modulation, 2, false);
		sprintf(status_topic, "%s/longmynd/fec", topic_prefix);
		mosquitto_publish(mosq, NULL, status_topic, strlen(fec), fec, 2, false);

		sprintf(status_message, "%i", data);
		sprintf(status_topic, "%s/longmynd/%s", topic_prefix, StatusString[message]);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_PUNCTURE_RATE)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_CARRIER_FREQUENCY)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_VITERBI_ERROR_RATE)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_BER)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_TS_NULL_PERCENTAGE)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ES_PID)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ES_TYPE)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_SHORT_FRAME)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_PILOTS)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ERRORS_LDPC_COUNT)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ERRORS_BCH_COUNT)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ERRORS_BCH_UNCORRECTED)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_LNB_SUPPLY)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_LNB_POLARISATION_H)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_LNA_GAIN)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_AGC1_GAIN)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_AGC2_GAIN)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_POWER_I)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_POWER_Q)
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_MATYPE2)
	{
		sprintf(status_message, "%x", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_ROLLOFF)
	{
		sprintf(status_topic, "%s/longmynd/rolloff", topic_prefix);
		if(data==0)	sprintf(status_message, "0.35");
		if(data==1)	sprintf(status_message, "0.25");
		if(data==2)	sprintf(status_message, "0.20");
		if(data==3)	sprintf(status_message, "0.15");
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else if (message == STATUS_MATYPE1)
	{
		char matype[50];
		switch ((data & 0xC0) >> 6)
		{
		case 0:
			strcpy(matype, "Generic packetized");
			break;
		case 1:
			strcpy(matype, "Generic continuous");
			break;
		case 2:
			strcpy(matype, "Generic packetized");
			break;
		case 3:
			strcpy(matype, "Transport");
			break;
		}

		mosquitto_publish(mosq, NULL, status_topic, strlen(matype), matype, 2, false);
	}
	else if (message == STATUS_MER)
	{
		int TheoricMER[] = {0, -24, -12, 0, 10, 22, 32, 40, 46, 52, 62, 65, 55, 66, 79, 94, 106, 110, 90, 102, 110, 116, 129, 131, 126, 136, 143, 157, 161};
		sprintf(status_message, "%0.1f", ((int)data) / 10.0);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
		char smargin[50];
		int latest_modcod = (tuner_id == 1) ? latest_modcod_t1 : latest_modcod_t2;
		if (latest_modcod != 0)
		{

			int Margin = (int)data - TheoricMER[latest_modcod];
			sprintf(smargin, "%d", Margin / 10);
			sprintf(status_topic, "%s/longmynd/margin_db", topic_prefix);
			mosquitto_publish(mosq, NULL, status_topic, strlen(smargin), smargin, 2, false);
		}
		else
		{
			sprintf(smargin, "%d", 0);
			sprintf(status_topic, "%s/longmynd/margin_db", topic_prefix);
			mosquitto_publish(mosq, NULL, status_topic, strlen(smargin), smargin, 2, false);
		}
	}
	else if ((message == STATUS_CONSTELLATION_I) || (message == STATUS_CONSTELLATION_Q))
	{
		int8_t signed_data;
		signed_data = (int8_t)data;
		sprintf(status_message, "%d", data);

		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}
	else
	{
		sprintf(status_message, "%i", data);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);
	}

	return err;
}

uint8_t mqtt_status_string_write_tuner(uint8_t tuner_id, uint8_t message, char *data, bool *output_ready)
{
	/* -------------------------------------------------------------------------------------------------- */
	/* Write string status for a specific tuner to MQTT                                                  */
	/*   tuner_id: 1 for tuner 1, 2 for tuner 2                                                         */
	/*   message: status message type                                                                    */
	/*   data: status string data                                                                        */
	/*   output_ready: output ready flag                                                                 */
	/*   return: error code                                                                              */
	/* -------------------------------------------------------------------------------------------------- */
	(void)output_ready;
	uint8_t err = ERROR_NONE;
	char status_topic[255];
	const char *topic_prefix;

	if (tuner_id != 1 && tuner_id != 2) {
		printf("ERROR: Invalid tuner ID: %d\n", tuner_id);
		return ERROR_ARGS_INPUT;
	}

	/* Set topic prefix based on tuner ID */
	topic_prefix = (tuner_id == 1) ? "dt" : "dt2";

	/* Build tuner-specific topic with proper prefix */
	sprintf(status_topic, "%s/longmynd/%s", topic_prefix, StatusString[message]);

	/* Debug output for service and provider names */
	if (message == STATUS_SERVICE_NAME || message == STATUS_SERVICE_PROVIDER_NAME) {
		// printf("MQTT DEBUG: Tuner %d publishing %s = '%s' (length: %zu)\n",
		//        tuner_id, StatusString[message], data, strlen(data));
	}

	mosquitto_publish(mosq, NULL, status_topic, strlen(data), data, 2, false);

	return err;
}