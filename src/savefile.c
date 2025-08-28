#define _POSIX_C_SOURCE 200809L
#include "savefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <limits.h>

#ifndef PATH_MAX
#define PATH_MAX 4096  // Fallback, falls das System keinen PATH_MAX definiert
#endif


/* Feldtabelle: Offsets relativ zum Stats-Block.
   ONLY Felder <= 0x0118 (liegen sicher im Block). "Base carry weight @ 0x200"
   lassen wir vorerst weg; das liegt außerhalb unseres aktuellen Blocks. */
static const SaveFieldSpec g_fields[] = {
    /* Base-werte (Auszug aus deiner Liste) */
    { "sequence_base",            0x3C,  0, 100 },
    { "heal_rate_base",           0x40,  0, 100 },
    { "crit_chance_base",         0x44,  0, 100 },
    { "crit_table_mod_base",      0x48, -100, 100 },

    { "dt_normal_base",           0x4C,  0, 1000 },
    { "dt_laser_base",            0x50,  0, 1000 },
    { "dt_fire_base",             0x54,  0, 1000 },
    { "dt_plasma_base",           0x58,  0, 1000 },
    { "dt_electrical_base",       0x5C,  0, 1000 },
    { "dt_emp_base",              0x60,  0, 1000 },
    { "dt_explosive_base",        0x64,  0, 1000 },

    { "dr_normal_base",           0x68,  0, 100 },
    { "dr_laser_base",            0x6C,  0, 100 },
    { "dr_fire_base",             0x70,  0, 100 },
    { "dr_plasma_base",           0x74,  0, 100 },
    { "dr_electrical_base",       0x78,  0, 100 },
    { "dr_emp_base",              0x7C,  0, 100 },
    { "dr_explosive_base",        0x80,  0, 100 },

    { "rad_res_base",             0x84,  0, 100 },
    { "poison_res_base",          0x88,  0, 100 },
    { "start_age",                0x8C,  0, 200 },
    { "gender",                   0x90,  0,   1 },  /* 0=male, 1=female */

    /* Bonus auf SPECIAL */
    { "str_bonus",                0x94, -10, 10 },
    { "per_bonus",                0x98, -10, 10 },
    { "end_bonus",                0x9C, -10, 10 },
    { "cha_bonus",                0xA0, -10, 10 },
    { "int_bonus",                0xA4, -10, 10 },
    { "agi_bonus",                0xA8, -10, 10 },
    { "lck_bonus",                0xAC, -10, 10 },

    /* Diverse Boni */
    { "hp_max_bonus",             0xB0, -999, 999 },
    { "ap_bonus",                 0xB4, -10, 10 },
    { "ac_bonus",                 0xB8, -100, 100 },
    /* 0xBC unused */

    { "melee_dmg_bonus",          0xC0, -100, 100 },
    { "carry_weight_bonus",       0xC4, -999, 999 },
    { "sequence_bonus",           0xC8, -100, 100 },
    { "heal_rate_bonus",          0xCC, -100, 100 },
    { "crit_chance_bonus",        0xD0, -100, 100 },
    { "crit_table_mod_bonus",     0xD4, -100, 100 },

    { "dt_normal_bonus",          0xD8, -1000, 1000 },
    { "dt_laser_bonus",           0xDC, -1000, 1000 },
    { "dt_fire_bonus",            0xE0, -1000, 1000 },
    { "dt_plasma_bonus",          0xE4, -1000, 1000 },
    { "dt_electrical_bonus",      0xE8, -1000, 1000 },
    { "dt_emp_bonus",             0xEC, -1000, 1000 },
    { "dt_explosive_bonus",       0xF0, -1000, 1000 },

    { "dr_normal_bonus",          0xF4, -100, 100 },
    { "dr_laser_bonus",           0xF8, -100, 100 },
    { "dr_fire_bonus",            0xFC, -100, 100 },
    { "dr_plasma_bonus",          0x100, -100, 100 },
    { "dr_electrical_bonus",      0x104, -100, 100 },
    { "dr_emp_bonus",             0x108, -100, 100 },
    { "dr_explosive_bonus",       0x10C, -100, 100 },

    { "rad_res_bonus",            0x110, -100, 100 },
    { "poison_res_bonus",         0x114, -100, 100 },
    { "age_bonus",                0x118, -50, 50 },  /* so in deiner Liste */
};

static const size_t g_fields_count = sizeof(g_fields)/sizeof(g_fields[0]);

static const SaveFieldSpec* find_field(const char *key) {
    if (!key) return NULL;
    for (size_t i=0; i<g_fields_count; ++i) {
        if (strcmp(g_fields[i].key, key) == 0) return &g_fields[i];
    }
    return NULL;
}



static void wr_be32(unsigned char *p, uint32_t v) {
	p[0] = (unsigned char)((v >> 24) & 0xFF);
	p[1] = (unsigned char)((v >> 16) & 0xFF);
	p[2] = (unsigned char)((v >>  8) & 0xFF);
	p[3] = (unsigned char)( v        & 0xFF);
}

/* v0.2: Big-Endian 32-bit lesen */
static uint32_t be32(const unsigned char *p) {
	return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | (uint32_t)p[3];
}

/* Big-Endian lesen haben wir schon: be32(...) */

/* Variante des Item-Skips: parametrisierbar (ITEM_SIZE, contained_off) */
static int skip_items_var(const unsigned char *buf, size_t n, size_t *ioff,
		uint32_t count, size_t ITEM_SIZE, size_t contained_off) {
	for (uint32_t i = 0; i < count; ++i) {
		if (*ioff + ITEM_SIZE > n) return 0;
		size_t contained_pos = *ioff + contained_off;
		uint32_t inner = 0;
		if (contained_pos + 4 <= n) inner = be32(buf + contained_pos);
		*ioff += ITEM_SIZE;
		if (inner > 0 && inner < 100000) {
			if (!skip_items_var(buf, n, ioff, inner, ITEM_SIZE, contained_off)) return 0;
		}
	}
	return 1;
}

/* Plausibilität: 7 x 4-Byte (BE) bei base+0x08 – ALLE müssen 1..10 sein */
static int is_plausible_stats_block(const unsigned char *buf, size_t n, size_t base) {
	if (base + 0x08 + 7*4 > n) return 0;
	for (int i = 0; i < 7; ++i) {
		uint32_t v = ((uint32_t)buf[base+0x08+i*4] << 24)
			| ((uint32_t)buf[base+0x09+i*4] << 16)
			| ((uint32_t)buf[base+0x0A+i*4] << 8)
			|  (uint32_t)buf[base+0x0B+i*4];
		if (v < 1 || v > 10) return 0;   // <- strenger: keine 0 oder >10
	}
	return 1;
}

/* ---- Datei laden/schreiben ---- */

int save_load(const char *path, Save *out) {
	memset(out, 0, sizeof(*out));
	FILE *fp = fopen(path, "rb");
	if (!fp) {
		perror("fopen");
		return 0;
	}
	if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return 0; }
	long sz = ftell(fp);
	if (sz < 0) { fclose(fp); return 0; }
	if (fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return 0; }

	out->size = (size_t)sz;
	out->buf  = (uint8_t*)malloc(out->size);
	if (!out->buf) { fclose(fp); return 0; }

	size_t rd = fread(out->buf, 1, out->size, fp);
	fclose(fp);
	if (rd != out->size) {
		free(out->buf);
		memset(out, 0, sizeof(*out));
		return 0;
	}
	return 1;
}

int save_write(const char *path, const Save *s) {
	FILE *fp = fopen(path, "wb");
	if (!fp) {
		perror("fopen");
		return 0;
	}
	size_t wr = fwrite(s->buf, 1, s->size, fp);
	fclose(fp); 
	return wr == s->size;
}

void save_free(Save *s) {
	if (s && s->buf) {
		free(s->buf);
		s->buf = NULL;
		s->size = 0;
	}
}

/* ---- Convenience ---- */

static int contains_seq(const unsigned char *buf, size_t n, const char *pat) {
	size_t m = strlen(pat);
	if (m == 0 || n < m) return 0;
	for (size_t i = 0; i + m <= n; ++i) {
		if (memcmp(buf + i, pat, m) == 0) return 1;
	}
	return 0;
}

int save_check_signature(const Save *s) {
	if (!s || !s->buf) return 0;
	/* Im ersten Header-Bereich (z.B. erste 64 Bytes) nach typischen Mustern suchen. */
	size_t scan = s->size < 64 ? s->size : 64;
	if (contains_seq(s->buf, scan, "FALLOUT SAVE FILE ")) return 1; // mit Leerzeichen
	if (contains_seq(s->buf, scan, "FALLOUT SAVE FILE"))  return 1; // ohne Leerzeichen
	return 0;
}

void save_get_player_name(const Save *s, char out[32]) {
	if (!s || !s->buf || s->size < PLAYER_NAME_OFFSET + PLAYER_NAME_FIELD_LEN) {
		out[0] = '\0';
		return;
	}
	/* Feld kopieren und sicher terminieren */
	memcpy(out, s->buf + PLAYER_NAME_OFFSET, PLAYER_NAME_FIELD_LEN);
	out[PLAYER_NAME_FIELD_LEN - 1] = '\0';

	/* Defensive: falls im Feld keine 0 enthalten ist, ist out jetzt nullterminiert.
Optional: trailing garbage nach erster 0 ignorieren (out ist bereits ok). */
}

int save_set_player_name(Save *s, const char *name) {
	if (!s || !s->buf || s->size < PLAYER_NAME_OFFSET + PLAYER_NAME_FIELD_LEN)
		return 0;
	/* Feld mit 0 füllen, dann max 31 Zeichen schreiben */
	memset(s->buf + PLAYER_NAME_OFFSET, 0, PLAYER_NAME_FIELD_LEN);
	size_t n = strlen(name);
	if (n > PLAYER_NAME_FIELD_LEN - 1) n = PLAYER_NAME_FIELD_LEN - 1;
	memcpy(s->buf + PLAYER_NAME_OFFSET, name, n);
	return 1;
}

int save_find_stats_offset(const Save *s, size_t *out_offset) {
	if (!s || !s->buf || !out_offset) return 0;
	const unsigned char *buf = s->buf;
	const size_t n = s->size;

	/* 1) Alle 'FP' (0x46,0x50) prüfen */
	for (size_t fp=0; fp+1<n; ++fp) {
		if (buf[fp] != 0x46 || buf[fp+1] != 0x50) continue; /* 'F','P' */
		if (fp + 0x80 > n) continue;

		/* item_count kann an +0x44, +0x48 oder +0x4C stehen (Big-Endian) */
		const size_t cnt_offs[] = {0x44, 0x48, 0x4C};
		const size_t item_sizes[] = {0x5C, 0x60, 0x64};
		const size_t cont_offs[]  = {0x48, 0x4C, 0x50};

		for (int ci=0; ci<3; ++ci) {
			size_t cntpos = fp + cnt_offs[ci];
			if (cntpos + 4 > n) continue;
			uint32_t item_count = be32(buf + cntpos);
			if (item_count > 20000) continue; /* unplausibel */

			for (int si=0; si<3; ++si) {
				for (int co=0; co<3; ++co) {
					size_t off = fp + 0x80; /* Inventarliste beginnt hier */
					if (!skip_items_var(buf, n, &off, item_count, item_sizes[si], cont_offs[co]))
						continue;
					if (off + 376 > n) continue;
					if (!is_plausible_stats_block(buf, n, off)) continue;

					*out_offset = off;
					return 1;
				}
			}
		}
	}

	/* 2) Fallback: Vollscan nach plausibler 376-Byte-Stats-Struktur */
	for (size_t off=0; off + 376 <= n; ++off) {
		if (is_plausible_stats_block(buf, n, off)) {
			*out_offset = off;
			return 1;
		}
	}
	return 0;
}

int save_read_stat(const Save *s, int stat_index, int *out_value) {
	if (!s || !s->buf || !out_value) return 0;
	if (stat_index < 0 || stat_index >= STAT_COUNT) return 0;

	size_t stats_off;
	if (!save_find_stats_offset(s, &stats_off)) return 0;

	size_t off = stats_off + 0x08 + (size_t)stat_index * 4; /* FIRST stat @ +0x08 */
	if (off + 4 > s->size) return 0;

	*out_value = (int)be32(s->buf + off);
	return 1;
}

int save_write_stat(Save *s, int stat_index, int value) {
	if (!s || !s->buf) return 0;
	if (stat_index < 0 || stat_index >= STAT_COUNT) return 0;

	/* Guardrails: klassische SPECIAL-Spanne */
	if (value < 1) value = 1;
	if (value > 10) value = 10;

	size_t stats;
	if (!save_find_stats_offset(s, &stats)) return 0;

	size_t off = stats + 0x08 + (size_t)stat_index * 4;
	if (off + 4 > s->size) return 0;

	wr_be32(s->buf + off, (uint32_t)value);
	return 1;
}

/* ------------------ atomisches In-Place-Schreiben ------------------ */
int save_write_inplace_atomic(const char *path, const Save *s, int make_backup) {
    if (!path || !s || !s->buf) return 0;

    char tmp[PATH_MAX];
    char bak[PATH_MAX];

    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return 0;
    if (snprintf(bak, sizeof(bak), "%s.bak", path) >= (int)sizeof(bak)) return 0;

    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        fprintf(stderr, "open (write tmp) '%s': %s\n", tmp, strerror(errno));
        return 0;
    }
    size_t wr = fwrite(s->buf, 1, s->size, fp);
    fflush(fp);
    int fd = fileno(fp);
    if (fd != -1) fsync(fd);
    fclose(fp);
    if (wr != s->size) {
        remove(tmp);
        return 0;
    }

    if (make_backup) {
        /* wenn es fehlschlägt (z. B. ENOENT), ignorieren */
        (void)rename(path, bak);
    }

    if (rename(tmp, path) != 0) {
        fprintf(stderr, "rename('%s'->'%s'): %s\n", tmp, path, strerror(errno));
        remove(tmp);
        return 0;
    }
    return 1;
}  /* <= WICHTIG: diese schließende Klammer muss da sein */


/* ------------------ Debug: Kandidaten scannen ------------------ */
int save_scan_stats_candidates(const Save *s) {
    if (!s || !s->buf) return 0;
    const unsigned char *buf = s->buf;
    const size_t n = s->size;
    int found = 0;

    for (size_t off = 0; off + 376 <= n; ++off) {
        if (!is_plausible_stats_block(buf, n, off)) continue;

        int vals[7];
        for (int i=0;i<7;i++) {
            vals[i] = (int)be32(buf + off + 0x08 + i*4);
        }
        printf("Kandidat stats_off=0x%zx -> STR=%d PER=%d END=%d CHA=%d INT=%d AGI=%d LCK=%d\n",
               off, vals[0],vals[1],vals[2],vals[3],vals[4],vals[5],vals[6]);
        if (++found >= 10) break;
    }
    if (!found) {
        printf("Keine plausiblen Kandidaten gefunden.\n");
    }
    return found;
}

int save_stats_get(const Save *s, const char *key, int *out_value) {
    if (!s || !s->buf || !out_value) return 0;
    const SaveFieldSpec *f = find_field(key);
    if (!f) return 0;

    size_t stats_off = 0;
    if (!save_find_stats_offset(s, &stats_off)) return 0;

    size_t off = stats_off + f->off;
    if (off + 4 > s->size) return 0;

    *out_value = (int)be32(s->buf + off);
    return 1;
}

int save_stats_set(Save *s, const char *key, int value) {
    if (!s || !s->buf) return 0;
    const SaveFieldSpec *f = find_field(key);
    if (!f) return 0;

    /* einfache Klammerung */
    if (value < f->minv) value = f->minv;
    if (value > f->maxv) value = f->maxv;

    size_t stats_off = 0;
    if (!save_find_stats_offset(s, &stats_off)) return 0;

    size_t off = stats_off + f->off;
    if (off + 4 > s->size) return 0;

    wr_be32(s->buf + off, (uint32_t)value);
    return 1;
}

void save_stats_list_fields(void) {
    for (size_t i=0; i<g_fields_count; ++i) {
        printf("%-22s @ +0x%zx (range %d..%d)\n",
               g_fields[i].key, g_fields[i].off, g_fields[i].minv, g_fields[i].maxv);
    }
}

