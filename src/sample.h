#ifndef SAMPLE_H
#define SAMPLE_H

#include <zephyr/kernel.h>

struct sample {
        uint32_t seq;
        int32_t value;
};

extern struct k_msgq mqtt_q;
extern uint32_t mqtt_dropped;
extern uint32_t mqtt_published;
extern uint32_t mqtt_reconnects;

#endif