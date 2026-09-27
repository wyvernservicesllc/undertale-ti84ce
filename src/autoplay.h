/* Scripted input for `make AUTOPLAY=1` (testing in CEmu, which cannot
   press keys from the command line): skips the intro, starts a new game and
   names the human "Chara", then walks around room_area1. Each entry waits
   `wait` frames, then holds `keys` for `hold` frames (1 if left out). */
#ifndef AUTOPLAY_H
#define AUTOPLAY_H

static const struct { uint8_t wait, keys, hold; } autoplay[] = {
    {60, K_CONFIRM},  /* skip the intro */
    {90, K_CONFIRM},  /* title */
    {45, K_CONFIRM},  /* Begin Game */
    {20, K_RIGHT}, {6, K_RIGHT}, {10, K_CONFIRM},                 /* C */
    {6, K_DOWN}, {6, K_DOWN}, {6, K_DOWN}, {6, K_DOWN}, {6, K_DOWN},
    {6, K_LEFT}, {6, K_LEFT}, {10, K_CONFIRM},                    /* h */
    {6, K_UP}, {10, K_CONFIRM},                                   /* a */
    {6, K_DOWN}, {6, K_DOWN}, {6, K_RIGHT}, {6, K_RIGHT}, {6, K_RIGHT},
    {10, K_CONFIRM},                                              /* r */
    {6, K_UP}, {6, K_UP}, {6, K_LEFT}, {6, K_LEFT}, {6, K_LEFT},
    {10, K_CONFIRM},                                              /* a */
    {6, K_DOWN}, {6, K_DOWN}, {6, K_DOWN}, {6, K_DOWN},
    {6, K_RIGHT}, {6, K_RIGHT}, {30, K_CONFIRM},                  /* Done */
    {90, K_RIGHT}, {30, K_CONFIRM},                               /* Yes */
    {250, K_LEFT, 40},                   /* room_area1: to the left wall */
    {30, K_DOWN, 30},                    /* slide down the diagonal */
    {30, K_RIGHT, 60},
    {10, K_DOWN, 10},
    {10, K_RIGHT, 200},                  /* along the corridor to the wall */
    {10, K_LEFT, 8},                     /* back under the door */
    {10, K_UP, 30},                      /* into the doorway */
};

#endif
