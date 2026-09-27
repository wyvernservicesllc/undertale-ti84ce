#ifndef UTINTRO_appvar_include_file
#define UTINTRO_appvar_include_file

#ifdef __cplusplus
extern "C" {
#endif

#define UTINTRO_appvar_size 31182
#define sizeof_global_palette 76
#define global_palette (UTINTRO_appvar[0])
#define panels_palette_offset 0
#define intro0_width 200
#define intro0_height 110
#define UTINTRO_panels_intro0_compressed_index 1
#define intro0_compressed UTINTRO_appvar[1]
#define intro1_width 200
#define intro1_height 110
#define UTINTRO_panels_intro1_compressed_index 2
#define intro1_compressed UTINTRO_appvar[2]
#define intro2_width 200
#define intro2_height 110
#define UTINTRO_panels_intro2_compressed_index 3
#define intro2_compressed UTINTRO_appvar[3]
#define intro3_width 200
#define intro3_height 110
#define UTINTRO_panels_intro3_compressed_index 4
#define intro3_compressed UTINTRO_appvar[4]
#define intro4_width 200
#define intro4_height 110
#define UTINTRO_panels_intro4_compressed_index 5
#define intro4_compressed UTINTRO_appvar[5]
#define intro5_width 200
#define intro5_height 110
#define UTINTRO_panels_intro5_compressed_index 6
#define intro5_compressed UTINTRO_appvar[6]
#define intro6_width 200
#define intro6_height 110
#define UTINTRO_panels_intro6_compressed_index 7
#define intro6_compressed UTINTRO_appvar[7]
#define intro7_width 200
#define intro7_height 110
#define UTINTRO_panels_intro7_compressed_index 8
#define intro7_compressed UTINTRO_appvar[8]
#define intro8_width 200
#define intro8_height 110
#define UTINTRO_panels_intro8_compressed_index 9
#define intro8_compressed UTINTRO_appvar[9]
#define intro9_width 200
#define intro9_height 110
#define UTINTRO_panels_intro9_compressed_index 10
#define intro9_compressed UTINTRO_appvar[10]
#define intro10_width 200
#define intro10_height 110
#define UTINTRO_panels_intro10_compressed_index 11
#define intro10_compressed UTINTRO_appvar[11]
#define last0_width 200
#define last0_height 175
#define UTINTRO_panels_last0_compressed_index 12
#define last0_compressed UTINTRO_appvar[12]
#define last1_width 200
#define last1_height 175
#define UTINTRO_panels_last1_compressed_index 13
#define last1_compressed UTINTRO_appvar[13]
#define title_width 282
#define title_height 27
#define UTINTRO_panels_title_compressed_index 14
#define title_compressed UTINTRO_appvar[14]
#define UTINTRO_entries_num 15
extern unsigned char *UTINTRO_appvar[15];
unsigned char UTINTRO_init(void);

#ifdef __cplusplus
}
#endif

#endif
