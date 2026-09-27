#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stdint.h>

#define FPS 30

/* Palette: the first sizeof_global_palette / 2 entries are the picture
   colors from UTINTRO (0 is black); the text colors sit at the top. */
#define COL_YELLOW 250 /* c_yellow */
#define COL_BLUE 248
#define COL_RED 249
#define COL_LTGRAY 251 /* c_ltgray */
#define COL_GRAY 253   /* c_gray */
#define COL_WHITE 254  /* c_white */

/* Buttons, as GameMaker's control_check_pressed sees them. */
#define K_CONFIRM 0x01 /* Z / Enter: 2nd, enter */
#define K_CANCEL 0x02  /* X / Shift: alpha, del */
#define K_UP 0x04
#define K_DOWN 0x08
#define K_LEFT 0x10
#define K_RIGHT 0x20

extern uint8_t keys_pressed; /* went down this frame */
extern uint8_t keys_held;

typedef struct font font_t;
extern const font_t font_main;  /* fnt_maintext */
extern const font_t font_small; /* fnt_small */

/* GameMaker alarm: set to n, fires on the nth step after. */
bool alarm_tick(int16_t *a);

/* Brightness 0-256 of the picture colors and of everything, then how far
   (0-256) everything is washed toward white. */
void set_palette(unsigned pic, unsigned all, unsigned white);

/* draw_text: '#' starts a new line. Draws into gfx_vbuffer, no clipping. */
void draw_text(const font_t *f, const char *s, int x, int y, uint8_t color);
void draw_text_centered(const font_t *f, const char *s, int x, int y, uint8_t color);
/* GameMaker-style transformed text. Scale and rotation (radians) are 8.8
   fixed point; rotation is around the text origin (x, y). */
void draw_text_transformed(const font_t *f, const char *s, int x, int y,
                           unsigned scale, int rotation, uint8_t color);
int text_width(const font_t *f, const char *s);
/* One letter at x, y (used by the intro writer). */
void draw_char(const font_t *f, char c, int x, int y, uint8_t color);

/* room_introstory, then room_introimage (title). */
void intro_begin(void);
bool intro_step(void); /* false when the room ends */

enum { TITLE_STAY, TITLE_TIMEOUT, TITLE_PROCEED };
void title_begin(void);
uint8_t title_step(void);

/* room_intromenu: instructions, name entry, confirmation. */
enum { MENU_STAY, MENU_RESTART, MENU_DONE };
void menu_begin(void);
uint8_t menu_step(void);

/* room_area1, the first room of the Ruins. */
void area1_begin(void);
bool area1_step(void); /* false when the player leaves through the door */

/* room_area1_2 and the opening Flowey encounter. */
void flowey_begin(void);
bool flowey_step(void); /* false when the player follows Toriel out */

#endif
