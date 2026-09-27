#include "utintro.h"
#include <stdint.h>
#include <fileioc.h>

#define UTINTRO_HEADER_SIZE 0

unsigned char *UTINTRO_appvar[15] =
{
    (unsigned char*)0,
    (unsigned char*)76,
    (unsigned char*)1257,
    (unsigned char*)2829,
    (unsigned char*)5136,
    (unsigned char*)7677,
    (unsigned char*)7685,
    (unsigned char*)9009,
    (unsigned char*)12061,
    (unsigned char*)15424,
    (unsigned char*)17810,
    (unsigned char*)20198,
    (unsigned char*)20206,
    (unsigned char*)25661,
    (unsigned char*)30966,
};

unsigned char UTINTRO_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTINTRO", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTINTRO_appvar[0] + UTINTRO_HEADER_SIZE;
    for (i = 0; i < 15; i++)
    {
        UTINTRO_appvar[i] += data;
    }

    ti_Close(appvar);

    return 1;
}

