#include "utflow2.h"
#include <stdint.h>
#include <fileioc.h>

#define UTFLOW2_HEADER_SIZE 0

unsigned char *UTFLOW2_appvar[19] =
{
    (unsigned char*)0,
    (unsigned char*)76,
    (unsigned char*)3678,
    (unsigned char*)7280,
    (unsigned char*)10882,
    (unsigned char*)14484,
    (unsigned char*)18086,
    (unsigned char*)21688,
    (unsigned char*)25290,
    (unsigned char*)28892,
    (unsigned char*)32494,
    (unsigned char*)33121,
    (unsigned char*)33748,
    (unsigned char*)34375,
    (unsigned char*)35002,
    (unsigned char*)35629,
    (unsigned char*)36256,
    (unsigned char*)39858,
    (unsigned char*)43460,
};

unsigned char UTFLOW2_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTFLOW2", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTFLOW2_appvar[0] + UTFLOW2_HEADER_SIZE;
    for (i = 0; i < 19; i++)
    {
        UTFLOW2_appvar[i] += data;
    }

    ti_Close(appvar);

    return 1;
}

