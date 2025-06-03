#include <mosquitto.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "errors.h"
#include "main.h"

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

void on_message(struct mosquitto *mosq, void *obj, const struct mosquitto_message *msg)
{
	char *key = msg->topic;
	char *svalue = (char *)msg->payload;

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

const char StatusString[30][255] = {"", "rx_state", "lna_gain", "puncrate", "poweri", "powerq", "carrier_frequency", "constel_i", "constel_q",
									"symbolrate", "viterbi_error", "ber", "mer", "service_name", "provider_name", "ts_null", "es_pid", "es_type", "modcod", "short_frame", "pilots",
									"ldpc_errors", "bch_errors", "bch_uncorect", "lnb_supply", "polarisation", "agc1", "agc2", "matype1", "matype2"};

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
		sprintf(status_message, "%zu", video_pcrpts);
		mosquitto_publish(mosq, NULL, status_topic, strlen(status_message), status_message, 2, false);

		sprintf(status_topic, "dt/longmynd/audiobuffer");
		sprintf(status_message, "%zu", audio_pcrpts);
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
	mosquitto_publish(mosq, NULL, status_topic, strlen(data), data, 2, false);

	return err;
}
