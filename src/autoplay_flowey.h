/* CEmu smoke test: enter Flowey's trigger, reach the battle, and advance
   through the first dialogue and pellet wave. */
#ifndef AUTOPLAY_FLOWEY_H
#define AUTOPLAY_FLOWEY_H
#define TAP {2, K_CONFIRM, 1}
static const struct { uint8_t wait, keys, hold; } autoplay[] = {
    {45, K_UP, 12},
    {200, 0, 1},
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    {55, 0, 1},
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    {115, 0, 1},
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    {115, 0, 1},
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    TAP, TAP, TAP, TAP, TAP, TAP, TAP, TAP,
    {90, 0, 1},
    TAP, TAP,
    {20, K_UP, 65},
};
#undef TAP
#endif
