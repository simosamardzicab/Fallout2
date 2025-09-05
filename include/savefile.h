#ifndef SAVEFILE_H
#define SAVEFILE_H
#define TAGS_MAX    4
#define SKILL_COUNT 18
#define PERK_COUNT 0xB2

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
	const char *category;
	const char *desc;
} SaveFieldSpec;

typedef struct {
    size_t   off;       /* Offset des F13-Blocks im File */
    uint32_t sp;        /* Skill Points */
    uint32_t lvl;       /* Level */
    uint32_t xp;        /* Experience */
    int32_t  trait0;    /* Wort direkt nach F13 (+0x14) */
    int32_t  trait1;    /* Wort direkt nach F13 (+0x18) */
} F13Hit;

/* Konstanten für Header-Felder (bekannt & stabil) */
enum {
    SAVE_SIGNATURE_OFFSET = 0x00,
    SAVE_SIGNATURE_LEN    = 18,           /* "FALLOUT SAVE FILE " */
    PLAYER_NAME_OFFSET    = 0x1D,         /* Beginn Spielername */
    PLAYER_NAME_FIELD_LEN = 32            /* 31 Zeichen + '\0' */
};

/* ---- Function 5: Inventory ---- */

/* Minimal-Ansicht eines Inventaritems gemäß FO1/FO2-Mac-Layout.
   Wir führen nur die stabilen/benötigten Felder; restliche Offsets bleiben intern. */
typedef struct {
    /* absolute Datei-Offsets für schnelles Patchen */
    size_t item_off;      /* Beginn des Items im SAVE.DAT */
    size_t qty_off;       /* +0x00 */
    size_t flags_off;     /* +0x28 */
    size_t objid_off;     /* +0x30 */

    /* ausgelesene Werte */
    int      index;       /* 0..(count-1) in der flachen Reihenfolge */
    uint32_t quantity;    /* +0x00 */
    uint32_t obj_id;      /* +0x30 */
    uint32_t flags;       /* +0x28 (Bitfeld: 0x01000000 right, 0x02000000 left, 0x04000000 worn) */
} SaveInvItem;

/* Layout-Info, automatisch erkannt */
typedef struct {
    size_t f5_off;           /* Offset des F5-Blocks */
    uint32_t item_count;     /* Top-Level-Itemanzahl */
    size_t list_off;         /* = f5_off + 0x80 */
    size_t item_size;        /* 0x5C | 0x60 | 0x64 */
    size_t contained_off;    /* 0x48 | 0x4C | 0x50 (u32: Anzahl enthaltener Items) */
} SaveInvLayout;


enum {
    STAT_ST = 0, STAT_PE, STAT_EN, STAT_CH, STAT_IN, STAT_AG, STAT_LK, STAT_COUNT
};

int  save_stats_get(const Save *s, const char *key, int *out_value);
int  save_stats_set(Save *s, const char *key, int value);

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
/* Heuristik: aktuellen HP-Wert finden */
int  save_guess_current_hp(const Save *s, int *out_value, size_t *out_off);
/* Debug-Scan: mehrere Kandidaten drucken, Rückgabe = Anzahl Kandidaten */
int  save_scan_hp_current_candidates(const Save *s, int hp_max_min);
int  save_check_signature(const Save *s);                  /* 1=OK, 0=nein */
void save_get_player_name(const Save *s, char out[32]);    /* Null-terminiert */
int save_find_func9_offset(const Save *s, size_t *out_off);

int save_perk_get(const Save *s, int idx, int *out_count);
int save_perk_set(Save *s, int idx, int count);

int save_perk_get_by_name(const Save *s, const char *name, int *out_count);
int save_perk_set_by_name(Save *s, const char *name, int count);

int  save_perk_find_index(const char *name);     /* -1 bei unbekannt */
const char *save_perk_name(int idx);             /* NULL falls out-of-range */int  save_set_player_name(Save *s, const char *name);      /* 1=OK */
int  save_tabs_get_flags(const Save *s, uint32_t *out_flags);
int  save_tabs_update_flags(Save *s, uint32_t set_mask, uint32_t clear_mask);

/* Function 5 (Player & Inventory) – Current HP etc. */
int save_get_hp_current(const Save *s, int *out);
int save_set_hp_current(Save *s, int value);


int save_get_rad_level(const Save *s, int *out);
int save_set_rad_level(Save *s, int value);
int save_get_poison_level(const Save *s, int *out);
int save_set_poison_level(Save *s, int value);
int save_find_func5_offset(const Save *s, size_t *out_off);

int save_get_position(const Save *s, uint32_t *out_pos);
int save_set_position(Save *s, uint32_t pos);

int save_get_facing(const Save *s, int *out_dir);     /* 0..5 */
int save_set_facing(Save *s, int dir);                /* 0..5 */

int save_cripple_get(const Save *s, uint32_t *out_mask);
int save_cripple_update(Save *s, uint32_t set_mask, uint32_t clear_mask);

/* ---- Function 8: Tag Skills (4 x u32, -1 wenn unbenutzt) ---- */

/* ---- Skills: Name <-> Index ---- */
int  save_skill_find_index(const char *name);   /* -1 wenn unbekannt; akzeptiert 0..17/0x00..0x11 */
const char *save_skill_name(int idx);           /* NULL bei out-of-range */

/* Finder: Offset des Tag-Blocks (heuristisch) */
int save_find_func8_offset(const Save *s, size_t *out_off);
int save_tags_get_all(const Save *s, int out_idx[TAGS_MAX]);    /* -1 für unbenutzt */
int save_tag_has(const Save *s, int skill_idx, int *out_has);   /* out_has=1/0 */
int save_tag_add(Save *s, int skill_idx);                       /* fügt, wenn frei */
int save_tag_remove(Save *s, int skill_idx);                    /* entfernt falls vorhanden */
int save_tags_clear(Save *s);                                   /* alle auf -1 setzen */

/* (Hilfsfunktionen für Skills hast du bereits)
int  save_skill_find_index(const char *name);
const char *save_skill_name(int idx);
*/
int save_find_func9_offset(const Save *s, size_t *out_off);

int save_perk_get(const Save *s, int idx, int *out_count);
int save_perk_set(Save *s, int idx, int count);

int save_perk_get_by_name(const Save *s, const char *name, int *out_count);
int save_perk_set_by_name(Save *s, const char *name, int count);

int  save_perk_find_index(const char *name);     /* -1 bei unbekannt */
const char *save_perk_name(int idx);             /* NULL falls out-of-range */


/* ---- Function 13: Experience & Level (20 Bytes) ---- */
int save_find_func13_offset(const Save *s, size_t *out_off);

int save_get_skill_points(const Save *s, int *out);     /* 0..99 */
int save_set_skill_points(Save *s, int value);

int save_get_level(const Save *s, int *out);            /* typisch 1..99 */
int save_set_level(Save *s, int value);

int save_get_experience(const Save *s, int *out);       /* >=0 */
int save_set_experience(Save *s, int value);

/* Function 18: Character window (5 Bytes) */
int save_find_func18_offset(const Save *s, size_t *out_off);
int save_get_charwin_level(const Save *s, int *out);
int save_set_charwin_level(Save *s, int lvl);
/* Hilfsfunktion: Vanilla-Level aus XP berechnen (FO1/FO2-Standard) */
int save_calc_level_from_xp(int xp);
/* Liste aller plausiblen F13-Blöcke. out kann NULL sein (nur zählen). */
int save_scan_all_func13(const Save *s, F13Hit *out, int max_out);

/* Setze XP + (berechnetes) Level in ALLEN F13-Blöcken und spiegle F18. */
/* Auto-Erkennung & gezieltes Setzen */
int save_find_player_f13_auto(const Save *s, size_t *out_f13, size_t *out_f5, size_t *out_f18);
int save_set_xp_level_auto(Save *s, int xp, int *out_level);
int save_set_experience_everywhere(Save *s, int xp);
int save_set_xp_level_where_traits(Save *s, int trait0, int trait1, int xp, int *out_patched);

/* Layout erkennen (liefert 1/0). count kann 0 sein. */
int save_inv_detect_layout(const Save *s, SaveInvLayout *out);

/* Inventar über Callback auflisten (inkl. Container rekursiv, flach indiziert).
   Rückgabe: Anzahl gelisteter Items (>=0), oder -1 bei Fehler. */
typedef void (*SaveInvCB)(const Save *s, const SaveInvLayout *L, const SaveInvItem *it, void *ud);
int save_inv_enumerate(const Save *s, SaveInvCB cb, void *ud);

/* Convenience: einfache Liste in stdout */
int save_inv_print(const Save *s);

/* Mutatoren (schreiben Big-Endian) */
int save_inv_set_quantity(Save *s, int flat_index, uint32_t qty);
int save_inv_set_equipped(Save *s, int flat_index, int right, int left, int worn);

/* ---- Inventory (Vanilla FO1/FO2, PC) ---- */
/* Druckt das Inventar flach (inkl. Container-Inhalte); Rückgabe: 1=OK, 0=Fehler */
int save_list_inventory(const Save *s);

/* ---- Traits (Fallout 2, Vanilla PC) ---- */
int  save_get_traits(const Save *s, int *out0, int *out1);   /* 1=OK */
int  save_set_traits(Save *s, int t0, int t1);               /* 1=OK */
const char *save_trait_name(int id);                         /* -1 => "None" */
int  save_trait_find_index(const char *name);                /* case-insensitive; "none" => -1; -2 = not found */


#endif

