#ifndef MENU_H
#define MENU_H
#include "road.h"
typedef struct {
    uint8_t selected,total,focus,card,error,best,gems;
    uint8_t adc,percent,live,manual,volume,revision,mechs,level_id;
} MenuState;
void menu_draw(const MenuState *state,uint32_t ticks,const RoadSink *sink);
#endif
