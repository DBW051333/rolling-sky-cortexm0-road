#ifndef FRAME_CLOCK_H
#define FRAME_CLOCK_H
/* The 1 ms SysTick represents 30 Hz with repeating 33,33,34 ms budgets.
 * Keep the same phase across menu/game transitions. No division on M0. */
static inline unsigned frame_period_ms(unsigned *phase) {
    if(++*phase==3u) { *phase=0; return 34u; }
    return 33u;
}
#endif
