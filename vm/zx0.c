/* ZX0 decompressor (Einar Saukas' format, as written by convbin -c zx0),
   used by the host build; the CE build uses the toolchain's zx0_Decompress. */
#include <stdint.h>

static const uint8_t *zin;
static uint8_t zbits, zbit;
static int zbacktrack;
static uint8_t zlast;

static int read_bit(void)
{
    if (zbacktrack)
    {
        zbacktrack = 0;
        return zlast & 1;
    }
    zbit >>= 1;
    if (!zbit)
    {
        zbit = 128;
        zbits = *zin++;
    }
    return (zbits & zbit) ? 1 : 0;
}

static int read_gamma(int inverted)
{
    int value = 1;
    while (!read_bit())
    {
        value = value << 1 | (read_bit() ^ inverted);
    }
    return value;
}

void zx0_decompress_c(uint8_t *out, const uint8_t *in)
{
    int last_offset = 1, len;
    zin = in;
    zbit = 0;
    zbacktrack = 0;
    for (;;)
    {
        /* literals */
        len = read_gamma(0);
        while (len--)
        {
            *out++ = *zin++;
        }
        if (read_bit())
        {
            goto new_offset;
        }
        /* copy from last offset */
        len = read_gamma(0);
        while (len--)
        {
            *out = out[-last_offset];
            out++;
        }
        if (!read_bit())
        {
            continue;
        }
    new_offset:
        {
            int msb = read_gamma(1);
            if (msb == 256)
            {
                return;
            }
            zlast = *zin++;
            last_offset = msb * 128 - (zlast >> 1);
            zbacktrack = 1;
            len = read_gamma(0) + 1;
            while (len--)
            {
                *out = out[-last_offset];
                out++;
            }
            if (read_bit())
            {
                goto new_offset;
            }
        }
    }
}
