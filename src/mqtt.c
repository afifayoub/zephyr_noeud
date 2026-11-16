#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "sample.h"

#define BROKER_ADDR    "127.0.0.1"
#define BROKER_PORT    1883
#define TOPIC          "noeud/mesures"
#define BACKOFF_MIN_MS 500
#define BACKOFF_MAX_MS 8000

static uint8_t rx_buf[256];
static uint8_t tx_buf[256];
static struct mqtt_client client;
static struct sockaddr_storage broker;
static volatile bool connected;

uint32_t mqtt_published;
uint32_t mqtt_reconnects;

static void evt_handler(struct mqtt_client *c, const struct mqtt_evt *evt)
{
        switch (evt->type) {
        case MQTT_EVT_CONNACK:
                connected = (evt->result == 0);
                break;
        case MQTT_EVT_DISCONNECT:
                connected = false;
                break;
        default:
                break;
        }
}

static int broker_connect(void)
{
        struct sockaddr_in *b = (struct sockaddr_in *)&broker;

        memset(&broker, 0, sizeof(broker));
        b->sin_family = AF_INET;
        b->sin_port = htons(BROKER_PORT);
        zsock_inet_pton(AF_INET, BROKER_ADDR, &b->sin_addr);

        mqtt_client_init(&client);
        client.broker = &broker;
        client.evt_cb = evt_handler;
        client.client_id.utf8 = (uint8_t *)"noeud_zephyr";
        client.client_id.size = sizeof("noeud_zephyr") - 1;
        client.protocol_version = MQTT_VERSION_3_1_1;
        client.keepalive = 5;
        client.rx_buf = rx_buf;
        client.rx_buf_size = sizeof(rx_buf);
        client.tx_buf = tx_buf;
        client.tx_buf_size = sizeof(tx_buf);
        client.transport.type = MQTT_TRANSPORT_NON_SECURE;

        connected = false;
        int rc = mqtt_connect(&client);
        if (rc != 0) {
                return rc;
        }

        struct zsock_pollfd fds = {
                .fd = client.transport.tcp.sock,
                .events = ZSOCK_POLLIN,
        };
        rc = zsock_poll(&fds, 1, 2000);
        if (rc > 0) {
                mqtt_input(&client);    /* traite le CONNACK */
        }
        if (!connected) {
                mqtt_abort(&client);
                return -ECONNREFUSED;
        }
        return 0;
}

static int publish_sample(const struct sample *s)
{
        char payload[48];
        int len = snprintf(payload, sizeof(payload),
                           "{\"seq\":%u,\"value\":%d}", s->seq, s->value);

        struct mqtt_publish_param p = {
                .message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE,
                .message.topic.topic.utf8 = (uint8_t *)TOPIC,
                .message.topic.topic.size = sizeof(TOPIC) - 1,
                .message.payload.data = (uint8_t *)payload,
                .message.payload.len = len,
                .message_id = (uint16_t)s->seq,
        };
        return mqtt_publish(&client, &p);
}

/* Boucle en mode normal : sort des que la connexion tombe */
static void session(void)
{
        struct sample s;

        while (connected) {
                /* peek : l'echantillon ne quitte la file qu'apres publication reussie */
                if (k_msgq_peek(&mqtt_q, &s) == 0) {
                        if (publish_sample(&s) != 0) {
                                break;
                        }
                        k_msgq_get(&mqtt_q, &s, K_NO_WAIT);
                        mqtt_published++;
                } else {
                        k_sleep(K_MSEC(5));
                }

                struct zsock_pollfd fds = {
                        .fd = client.transport.tcp.sock,
                        .events = ZSOCK_POLLIN,
                };
                int rc = zsock_poll(&fds, 1, 0);

                if (rc > 0) {
                        if ((fds.revents & (ZSOCK_POLLHUP | ZSOCK_POLLERR |
                                            ZSOCK_POLLNVAL)) ||
                            mqtt_input(&client) != 0) {
                                break;
                        }
                }

                rc = mqtt_live(&client);
                if (rc != 0 && rc != -EAGAIN) {
                        break;
                }
        }
}

static void mqtt_thread(void *a, void *b, void *c)
{
        uint32_t backoff = BACKOFF_MIN_MS;

        while (1) {
                if (broker_connect() == 0) {
                        printk("MQTT connecte\n");
                        backoff = BACKOFF_MIN_MS;
                        session();
                        mqtt_abort(&client);
                        mqtt_reconnects++;
                        printk("MQTT deconnecte : mode degrade, %u mesures en attente\n",
                               k_msgq_num_used_get(&mqtt_q));
                } else {
                        printk("MQTT injoignable, nouvel essai dans %u ms\n", backoff);
                }
                k_sleep(K_MSEC(backoff));
                backoff = MIN(backoff * 2, BACKOFF_MAX_MS);
        }
}

K_THREAD_DEFINE(mqtt_tid, 4096, mqtt_thread, NULL, NULL, NULL, 7, 0, 0);