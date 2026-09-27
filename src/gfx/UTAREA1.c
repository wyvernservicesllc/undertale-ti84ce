#include "utarea1.h"
#include <stdint.h>
#include <fileioc.h>

#define UTAREA1_HEADER_SIZE 0

unsigned char *UTAREA1_appvar[13] =
{
    (unsigned char*)0,
    (unsigned char*)22914,
    (unsigned char*)23516,
    (unsigned char*)24118,
    (unsigned char*)24720,
    (unsigned char*)25322,
    (unsigned char*)25924,
    (unsigned char*)26526,
    (unsigned char*)27128,
    (unsigned char*)27730,
    (unsigned char*)28332,
    (unsigned char*)28934,
    (unsigned char*)29536,
};

unsigned char *area1_tiles_tiles_data[57] =
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
    (unsigned char*)20100,
    (unsigned char*)20502,
    (unsigned char*)20904,
    (unsigned char*)21306,
    (unsigned char*)21708,
    (unsigned char*)22110,
    (unsigned char*)22512,
};

unsigned char UTAREA1_init(void)
{
    uintptr_t data;
    unsigned int i;
    uint8_t appvar;

    appvar = ti_Open("UTAREA1", "r");
    if (appvar == 0)
    {
        return 0;
    }

    data = (uintptr_t)ti_GetDataPtr(appvar) - (uintptr_t)UTAREA1_appvar[0] + UTAREA1_HEADER_SIZE;
    for (i = 0; i < 13; i++)
    {
        UTAREA1_appvar[i] += data;
    }

    ti_Close(appvar);

    data = (unsigned int)UTAREA1_appvar[0] - (unsigned int)area1_tiles_tiles_data[0];
    for (i = 0; i < area1_tiles_tiles_num; i++)
    {
        area1_tiles_tiles_data[i] += data;
    }

    return 1;
}

