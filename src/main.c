#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/can.h>
#include "sample.h"

#define PERIOD_MS     20     /* mets 20 pour le test 1 */
#define NB_SAMPLES    5000
#define STACK_SIZE    1024
#define CAN_ID_MESURE 0x123


K_MSGQ_DEFINE(sample_q, sizeof(struct sample), 8, 4); 
K_MSGQ_DEFINE(mqtt_q, sizeof(struct sample), 256, 4);   /* ~5 s de tampon à 20 ms */
uint32_t mqtt_dropped;

static uint32_t dropped;
static uint32_t can_errors;
static uint32_t sent;
static int64_t max_send_us;

static void acquisition(void *a, void *b, void *c)
{
	int64_t period = k_ms_to_ticks_ceil64(PERIOD_MS);
	int64_t next = k_uptime_ticks() + period;

	for (uint32_t i = 0; i < NB_SAMPLES; i++) {
		k_sleep(K_TIMEOUT_ABS_TICKS(next));

		struct sample s = { .seq = i, .value = 20 + (int32_t)(i % 10) };

		if (k_msgq_put(&sample_q, &s, K_NO_WAIT) != 0) {
			dropped++;
		}
		if (k_msgq_put(&mqtt_q, &s, K_NO_WAIT) != 0) {
			mqtt_dropped++;
		}
		next += period;
	}

	printk("Fin : %u perdues, %u erreurs CAN, %u envoyees, can_send max %lld us\n",
	       dropped, can_errors, sent, max_send_us);
	printk("Fin mqtt: %u perdues, %u  pubs, %u reconnects\n",
	       mqtt_dropped, mqtt_published, mqtt_reconnects);
}

static void can_thread(void *a, void *b, void *c)
{
	const struct device *can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));
	struct sample s;
	int ret;

	if (!device_is_ready(can_dev)) {
		printk("Controleur CAN non pret\n");
		return;
	}

	ret = can_start(can_dev);
	if (ret != 0 && ret != -EALREADY) {
		printk("can_start a echoue : %d\n", ret);
		return;
	}

	while (1) {
		k_msgq_get(&sample_q, &s, K_FOREVER);

		struct can_frame frame = {
			.id = CAN_ID_MESURE,
			.dlc = 3,
			.flags = 0,
		};
		frame.data[0] = s.seq & 0xFF;          /* numero, poids faible */
		frame.data[1] = (s.seq >> 8) & 0xFF;   /* numero, poids fort */
		frame.data[2] = (uint8_t)s.value;      /* valeur de la mesure */

		int64_t t0 = k_uptime_ticks();
		ret = can_send(can_dev, &frame, K_MSEC(10), NULL, NULL);
		int64_t d = k_ticks_to_us_near64(k_uptime_ticks() - t0);

		if (d > max_send_us) {
			max_send_us = d;
		}
		if (ret != 0) {
			can_errors++;

		} else {
			sent++;
		}
	}
}

K_THREAD_DEFINE(acq_tid, STACK_SIZE, acquisition, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(can_tid, STACK_SIZE, can_thread, NULL, NULL, NULL, 6, 0, 0);

int main(void)
{
	return 0;
}