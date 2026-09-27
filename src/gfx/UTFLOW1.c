#include "utflow1.h"
#include <stdint.h>
#include <fileioc.h>

#define UTFLOW1_HEADER_SIZE 0

unsigned char *UTFLOW1_appvar[16] =
{
    (unsigned char*)0,
    (unsigned char*)76,
    (unsigned char*)703,
    (unsigned char*)1330,
    (unsigned char*)4932,
    (unsigned char*)8534,
    (unsigned char*)12136,
    (unsigned char*)15738,
    (unsigned char*)19340,
    (unsigned char*)22942,
    (unsigned char*)26544,
    (unsigned char*)30146,
    (unsigned char*)33748,
    (unsigned char*)35606,
    (unsigned char*)37464,
    (unsigned char*)39322,
};

unsigned char UTFLOW1_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTFLOW1", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTFLOW1_appvar[0] + UTFLOW1_HEADER_SIZE;
    for (i = 0; i < 16; i++)
    {
        UTFLOW1_appvar[i] += data;
    }

    ti_Close(appvar);

    return 1;
}

