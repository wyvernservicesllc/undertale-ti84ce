#include "utflow4.h"
#include <stdint.h>
#include <fileioc.h>

#define UTFLOW4_HEADER_SIZE 0

unsigned char *UTFLOW4_appvar[17] =
{
    (unsigned char*)0,
    (unsigned char*)76,
    (unsigned char*)6318,
    (unsigned char*)6384,
    (unsigned char*)6450,
    (unsigned char*)6488,
    (unsigned char*)6526,
    (unsigned char*)6672,
    (unsigned char*)14018,
    (unsigned char*)21364,
    (unsigned char*)28710,
    (unsigned char*)36056,
    (unsigned char*)37082,
    (unsigned char*)38108,
    (unsigned char*)39134,
    (unsigned char*)40160,
    (unsigned char*)41186,
};

unsigned char UTFLOW4_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTFLOW4", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTFLOW4_appvar[0] + UTFLOW4_HEADER_SIZE;
    for (i = 0; i < 17; i++)
    {
        UTFLOW4_appvar[i] += data;
    }

    ti_Close(appvar);

    return 1;
}

