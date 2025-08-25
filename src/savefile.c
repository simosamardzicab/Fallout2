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

int save_write_inplace_atomic(const char *path, const Save *s, int make_backup) {
    if (!path || !s || !s->buf) return 0;

    char tmp[PATH_MAX];
    char bak[PATH_MAX];

    // tmp- & bak-Pfade bauen
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return 0;
    if (snprintf(bak, sizeof(bak), "%s.bak", path) >= (int)sizeof(bak)) return 0;

    // 1) in Temp schreiben
    FILE *fp = fopen(tmp, "wb");
    if (!fp) {
        fprintf(stderr, "open (write tmp) '%s': %s\n", tmp, strerror(errno));
        return 0;
    }
    size_t wr = fwrite(s->buf, 1, s->size, fp);
    fclose(fp);
    if (wr != s->size) {
        fprintf(stderr, "write (tmp) short: %zu/%zu\n", wr, s->size);
        remove(tmp);
        return 0;
    }

    // 2) optional Backup anlegen (alte Datei -> .bak)
    if (make_backup) {
        // existierendes .bak ignorieren/überschreiben
        rename(path, bak); // wenn es fehlschlägt (z. B. ENOENT), ist das ok
    }

    // 3) Temp atomisch an Ziel verschieben
    if (rename(tmp, path) != 0) {
        fprintf(stderr, "rename('%s'->'%s'): %s\n", tmp, path, strerror(errno));
        remove(tmp);
        return 0;
    }
    return 1;
}
