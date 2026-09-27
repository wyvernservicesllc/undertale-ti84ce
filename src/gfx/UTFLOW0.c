#include "utflow0.h"
#include <stdint.h>
#include <fileioc.h>

#define UTFLOW0_HEADER_SIZE 0

unsigned char *UTFLOW0_appvar[2] =
{
    (unsigned char*)0,
    (unsigned char*)76,
};

unsigned char *flowey_tiles_tiles_data[50] =
{
    (unsigned char*)0,
    (unsigned char*)402,
    (unsigned char*)804,
    (unsigned char*)1206,
    (unsigned char*)1608,
    (unsigned char*)2010,
    (unsigned char*)2412,
    (unsigned char*)2814,
    (unsigned char*)3216,
    (unsigned char*)3618,
    (unsigned char*)4020,
    (unsigned char*)4422,
    (unsigned char*)4824,
    (unsigned char*)5226,
    (unsigned char*)5628,
    (unsigned char*)6030,
    (unsigned char*)6432,
    (unsigned char*)6834,
    (unsigned char*)7236,
    (unsigned char*)7638,
    (unsigned char*)8040,
    (unsigned char*)8442,
    (unsigned char*)8844,
    (unsigned char*)9246,
    (unsigned char*)9648,
    (unsigned char*)10050,
    (unsigned char*)10452,
    (unsigned char*)10854,
    (unsigned char*)11256,
    (unsigned char*)11658,
    (unsigned char*)12060,
    (unsigned char*)12462,
    (unsigned char*)12864,
    (unsigned char*)13266,
    (unsigned char*)13668,
    (unsigned char*)14070,
    (unsigned char*)14472,
    (unsigned char*)14874,
    (unsigned char*)15276,
    (unsigned char*)15678,
    (unsigned char*)16080,
    (unsigned char*)16482,
    (unsigned char*)16884,
    (unsigned char*)17286,
    (unsigned char*)17688,
    (unsigned char*)18090,
    (unsigned char*)18492,
    (unsigned char*)18894,
    (unsigned char*)19296,
    (unsigned char*)19698,
};

unsigned char UTFLOW0_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTFLOW0", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTFLOW0_appvar[0] + UTFLOW0_HEADER_SIZE;
    for (i = 0; i < 2; i++)
    {
        UTFLOW0_appvar[i] += data;
    }

    ti_Close(appvar);

    data = (unsigned int)UTFLOW0_appvar[1] - (unsigned int)flowey_tiles_tiles_data[0];
    for (i = 0; i < flowey_tiles_tiles_num; i++)
    {
        flowey_tiles_tiles_data[i] += data;
    }

    return 1;
}

