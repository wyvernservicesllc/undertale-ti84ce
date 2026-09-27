#include "utflow3.h"
#include <stdint.h>
#include <fileioc.h>

#define UTFLOW3_HEADER_SIZE 0

unsigned char *UTFLOW3_appvar[21] =
{
    (unsigned char*)0,
    (unsigned char*)76,
    (unsigned char*)1926,
    (unsigned char*)3776,
    (unsigned char*)5626,
    (unsigned char*)7476,
    (unsigned char*)9326,
    (unsigned char*)11176,
    (unsigned char*)13026,
    (unsigned char*)14876,
    (unsigned char*)16726,
    (unsigned char*)18576,
    (unsigned char*)20426,
    (unsigned char*)22276,
    (unsigned char*)24126,
    (unsigned char*)25976,
    (unsigned char*)27826,
    (unsigned char*)29676,
    (unsigned char*)31526,
    (unsigned char*)33376,
    (unsigned char*)35226,
};

unsigned char UTFLOW3_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTFLOW3", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTFLOW3_appvar[0] + UTFLOW3_HEADER_SIZE;
    for (i = 0; i < 21; i++)
    {
        UTFLOW3_appvar[i] += data;
    }

    ti_Close(appvar);

    return 1;
}

