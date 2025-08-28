#ifndef SAVEFILE_H
#define SAVEFILE_H

#include <stddef.h>
#include <stdint.h>

/* Minimaler Save-Container */
typedef struct {
    uint8_t *buf;
    size_t   size;
} Save;

typedef struct{
	const char 	*key;
	size_t		off;
	int		minv;
	int		maxv;
} SaveFieldSpec;

/* Konstanten für Header-Felder (bekannt & stabil) */
enum {
    SAVE_SIGNATURE_OFFSET = 0x00,
    SAVE_SIGNATURE_LEN    = 18,           /* "FALLOUT SAVE FILE " */
    PLAYER_NAME_OFFSET    = 0x1D,         /* Beginn Spielername */
    PLAYER_NAME_FIELD_LEN = 32            /* 31 Zeichen + '\0' */
};
enum {
    STAT_ST = 0, STAT_PE, STAT_EN, STAT_CH, STAT_IN, STAT_AG, STAT_LK, STAT_COUNT
};

int save_stats_get(const Save *s, const char *key, int *out_value);
int save_stats_set(Save *s, const char *key, int value);

/* Findet den Beginn des Stats-Blocks (Function 6) und liefert ihn via out_offset. */
int  save_find_stats_offset(const Save *s, size_t *out_offset);
/* Liest einen SPECIAL-Basiswert (1..10) aus dem Stats-Block. */
int  save_read_stat(const Save *s, int stat_index, int *out_value);
/* API */
int  save_load(const char *path, Save *out);
int  save_write(const char *path, const Save *s);
int  save_write_stat(Save *s, int stat_index, int value);
int  save_write_inplace_atomic(const char *path, const Save *s, int make_backup);
void save_free(Save *s);
int  save_scan_stats_candidates(const Save *s);
void save_stats_list_fields(void);
/* Convenience */
int  save_check_signature(const Save *s);                  /* 1=OK, 0=nein */
void save_get_player_name(const Save *s, char out[32]);    /* Null-terminiert */
int  save_set_player_name(Save *s, const char *name);      /* 1=OK */

#endif

