#ifndef FLYOVER_H
#define FLYOVER_H
#include "road.h"
#define FLYOVER_PERIOD 80000u
unsigned flyover_time(uint32_t ticks);
void flyover_prepare(unsigned time);
void flyover_line(uint16_t *pixels,unsigned y);
#endif
