#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#define PERIOD_MS   10
#define NB_SAMPLES  500
#define STACK_SIZE  1024

struct sample {
	uint32_t seq;     /* numero de la mesure */
	int32_t value;    /* valeur simulee du capteur */
};

/* file de 8 messages maximum, chacun de la taille de struct sample */
K_MSGQ_DEFINE(sample_q, sizeof(struct sample), 8, 4);

static uint32_t dropped;

static void acquisition(void *a, void *b, void *c)
{
	int64_t period = k_ms_to_ticks_ceil64(PERIOD_MS);
	int64_t next = k_uptime_ticks() + period;

	for (uint32_t i = 0; i < NB_SAMPLES; i++) {
		k_sleep(K_TIMEOUT_ABS_TICKS(next));

		struct sample s = { .seq = i, .value = 20 + (int32_t)(i % 10) };

		/* K_NO_WAIT : si la file est pleine, on ne bloque pas */
		if (k_msgq_put(&sample_q, &s, K_NO_WAIT) != 0) {
			dropped++;
		}
		next += period;
	}
	printk("Acquisition terminee : %u mesures perdues\n", dropped);
}

static void consommateur(void *a, void *b, void *c)
{
	struct sample s;

	while (1) {
		k_msgq_get(&sample_q, &s, K_FOREVER); 
		//k_msleep(50);   test 
		if (s.seq % 100 == 0) {
			printk("recu : seq=%u valeur=%d\n", s.seq, s.value);
		}
	}
}

K_THREAD_DEFINE(acq_tid, STACK_SIZE, acquisition, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(cons_tid, STACK_SIZE, consommateur, NULL, NULL, NULL, 6, 0, 0);

int main(void)
{
	return 0;
}