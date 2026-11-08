#ifndef WALLS_H
#define WALLS_H
#include "road.h"
typedef struct { int16_t left,right; uint32_t color; } WallSpan;
extern WallSpan wall_spans[89];   /* 2*FACE_CACHE+1 */
extern unsigned wall_span_count;
void walls_prepare(const Road *road,uint32_t camera);
int walls_line(unsigned y);
#endif
