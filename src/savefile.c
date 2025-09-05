#define _POSIX_C_SOURCE 200809L
#include "savefile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>
#include <ctype.h>
#include <strings.h>



#ifndef PATH_MAX
#define PATH_MAX 4096  // Fallback, falls das System keinen PATH_MAX definiert
#endif
#define STATS_MIN_SPAN 0x204   // bis +0x200 lesen können
#define F_BASE(key, off, minv, maxv, desc)   { key, off, minv, maxv, "base",  desc }
#define F_BONUS(key, off, minv, maxv, desc)  { key, off, minv, maxv, "bonus", desc }
#define F_META(key, off, minv, maxv, desc)   { key, off, minv, maxv, "meta",  desc }
#define PERK_BLOCK_SIZE 0x02C8
#define FUNC13_SIZE 0x14

/* Feldtabelle: Offsets relativ zum Stats-Block.
   ONLY Felder <= 0x0118 (liegen sicher im Block). "Base carry weight @ 0x200"
   lassen wir vorerst weg; das liegt außerhalb unseres aktuellen Blocks. */
static const SaveFieldSpec g_fields[] = {
	F_META ("tabs_flags",             0x04,     0, 0x7fffffff, "Tabs flags bitmask"),

	F_BASE ("str_base",              0x08,     1, 10,   "Base strength"),
	F_BASE ("per_base",              0x0C,     1, 10,   "Base perception"),
	F_BASE ("end_base",              0x10,     1, 10,   "Base endurance"),
	F_BASE ("cha_base",              0x14,     1, 10,   "Base charisma"),
	F_BASE ("int_base",              0x18,     1, 10,   "Base intelligence"),
	F_BASE ("agi_base",              0x1C,     1, 10,   "Base agility"),
	F_BASE ("lck_base",              0x20,     1, 10,   "Base luck"),

	F_BASE ("hp_base",               0x24,  -999, 9999, "Base hit points"),
	F_BASE ("ap_base",               0x28,    -5,   20, "Base action points"),
	F_BASE ("ac_base",               0x2C,  -100,  100, "Base armor class"),

	F_BASE ("melee_dmg_base",        0x34,  -100,  200, "Base melee damage"),

	F_BASE ("sequence_base",         0x3C,     0,  100, "Base sequence"),
	F_BASE ("heal_rate_base",        0x40,     0,  100, "Base healing rate"),
	F_BASE ("crit_chance_base",      0x44,     0,  100, "Base critical chance (%)"),
	F_BASE ("crit_table_mod_base",   0x48,  -100,  100, "Base crit table roll mod"),

	F_BASE ("dt_normal_base",        0x4C,     0, 1000, "Base damage threshold (normal)"),
	F_BASE ("dt_laser_base",         0x50,     0, 1000, "Base damage threshold (laser)"),
	F_BASE ("dt_fire_base",          0x54,     0, 1000, "Base damage threshold (fire)"),
	F_BASE ("dt_plasma_base",        0x58,     0, 1000, "Base damage threshold (plasma)"),
	F_BASE ("dt_electrical_base",    0x5C,     0, 1000, "Base damage threshold (electrical)"),
	F_BASE ("dt_emp_base",           0x60,     0, 1000, "Base damage threshold (EMP)"),
	F_BASE ("dt_explosive_base",     0x64,     0, 1000, "Base damage threshold (explosive)"),

	F_BASE ("dr_normal_base",        0x68,     0,  100, "Base damage resistance (normal %)"),
	F_BASE ("dr_laser_base",         0x6C,     0,  100, "Base damage resistance (laser %)"),
	F_BASE ("dr_fire_base",          0x70,     0,  100, "Base damage resistance (fire %)"),
	F_BASE ("dr_plasma_base",        0x74,     0,  100, "Base damage resistance (plasma %)"),
	F_BASE ("dr_electrical_base",    0x78,     0,  100, "Base damage resistance (electrical %)"),
	F_BASE ("dr_emp_base",           0x7C,     0,  100, "Base damage resistance (EMP %)"),
	F_BASE ("dr_explosive_base",     0x80,     0,  100, "Base damage resistance (explosive %)"),

	F_BASE ("rad_res_base",          0x84,     0,  100, "Base radiation resistance (%)"),
	F_BASE ("poison_res_base",       0x88,     0,  100, "Base poison resistance (%)"),
	F_META ("start_age",             0x8C,     0,  200, "Player starting age"),
	F_META ("gender",                0x90,     0,    1, "0=male, 1=female"),

	F_BONUS("str_bonus",             0x94,   -10,   10, "Bonus to strength"),
	F_BONUS("per_bonus",             0x98,   -10,   10, "Bonus to perception"),
	F_BONUS("end_bonus",             0x9C,   -10,   10, "Bonus to endurance"),
	F_BONUS("cha_bonus",             0xA0,   -10,   10, "Bonus to charisma"),
	F_BONUS("int_bonus",             0xA4,   -10,   10, "Bonus to intelligence"),
	F_BONUS("agi_bonus",             0xA8,   -10,   10, "Bonus to agility"),
	F_BONUS("lck_bonus",             0xAC,   -10,   10, "Bonus to luck"),

	F_BONUS("hp_max_bonus",          0xB0,  -999,  999, "Bonus to max hit points"),
	F_BONUS("ap_bonus",              0xB4,   -10,   10, "Bonus action points"),
	F_BONUS("ac_bonus",              0xB8,  -100,  100, "Bonus armor class"),
	F_BONUS("melee_dmg_bonus",       0xC0,  -100,  100, "Bonus melee damage"),
	F_BONUS("carry_weight_bonus",    0xC4,  -999,  999, "Bonus carry weight"),
	F_BONUS("sequence_bonus",        0xC8,  -100,  100, "Bonus sequence"),
	F_BONUS("heal_rate_bonus",       0xCC,  -100,  100, "Bonus healing rate"),
	F_BONUS("crit_chance_bonus",     0xD0,  -100,  100, "Bonus critical chance (%)"),
	F_BONUS("crit_table_mod_bonus",  0xD4,  -100,  100, "Bonus crit table roll mod"),

	F_BONUS("dt_normal_bonus",       0xD8, -1000, 1000, "Bonus DT (normal)"),
	F_BONUS("dt_laser_bonus",        0xDC, -1000, 1000, "Bonus DT (laser)"),
	F_BONUS("dt_fire_bonus",         0xE0, -1000, 1000, "Bonus DT (fire)"),
	F_BONUS("dt_plasma_bonus",       0xE4, -1000, 1000, "Bonus DT (plasma)"),
	F_BONUS("dt_electrical_bonus",   0xE8, -1000, 1000, "Bonus DT (electrical)"),
	F_BONUS("dt_emp_bonus",          0xEC, -1000, 1000, "Bonus DT (EMP)"),
	F_BONUS("dt_explosive_bonus",    0xF0, -1000, 1000, "Bonus DT (explosive)"),

	F_BONUS("dr_normal_bonus",       0xF4,  -100,  100, "Bonus DR (normal %)"),
	F_BONUS("dr_laser_bonus",        0xF8,  -100,  100, "Bonus DR (laser %)"),
	F_BONUS("dr_fire_bonus",         0xFC,  -100,  100, "Bonus DR (fire %)"),
	F_BONUS("dr_plasma_bonus",       0x100, -100,  100, "Bonus DR (plasma %)"),
	F_BONUS("dr_electrical_bonus",   0x104, -100,  100, "Bonus DR (electrical %)"),
	F_BONUS("dr_emp_bonus",          0x108, -100,  100, "Bonus DR (EMP %)"),
	F_BONUS("dr_explosive_bonus",    0x10C, -100,  100, "Bonus DR (explosive %)"),

	F_BONUS("rad_res_bonus",         0x110, -100,  100, "Bonus radiation resistance (%)"),
	F_BONUS("poison_res_bonus",      0x114, -100,  100, "Bonus poison resistance (%)"),
	F_BONUS("age_bonus",             0x118,  -50,   50, "Bonus to age"),

	F_BASE ("carry_weight_base",     0x200, -999, 9999, "Base carry weight"),
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

/* Big-endian 32-bit Loader (hast du bereits), hier nur zur Erinnerung:
   static inline uint32_t be32(const unsigned char *p) {
   return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|((uint32_t)p[3]);
   }
   */

/* Prüft, ob an p (16 Byte) ein valider Tag-Block liegt:
   4 x be32, jeder Wert entweder -1 (0xFFFFFFFF) oder 0..SKILL_COUNT-1.
   Keine doppelten Non-(-1) Werte. Mind. 1 Eintrag != -1. */
static int is_valid_tag_block(const unsigned char *p) {
	int seen[SKILL_COUNT]; for (int i=0;i<SKILL_COUNT;i++) seen[i]=0;
	int nonneg = 0;
	for (int i=0;i<4;i++) {
		uint32_t v = be32(p + i*4);
		if (v == 0xFFFFFFFFu) continue;           /* unbenutzt */
		if (v >= (uint32_t)SKILL_COUNT) return 0; /* out of range */
		if (seen[v]) return 0;                    /* doppelt */
		seen[v] = 1;
		nonneg++;
	}
	return nonneg > 0; /* mind. 1 getaggter Skill */
}

typedef struct { int want; SaveInvItem *dst; const SaveInvLayout *Lsrc; SaveInvLayout *     Ldst; int found; int target; } _FindCtx;
static void _find_cb(const Save *s, const SaveInvLayout *L, const SaveInvItem *it, void      *ud) 
{    
	(void)s;
	_FindCtx *c = (_FindCtx*)ud;
	if (it->index == c->target) {
		*(c->dst) = *it;
		*(c->Ldst) = *L;
		c->found = 1;
	}
}

/* kanonische Skillnamen (Kleinschreibung, Unterstriche) */
static const char *g_skill_names[SKILL_COUNT] = {
	"small_guns", "big_guns", "energy_weapons", "unarmed", "melee_weapons", "throwing",
	"first_aid", "doctor", "sneak", "lockpick", "steal", "traps",
	"science", "repair", "speech", "barter", "gambling", "outdoorsman"
};



/* einfache Normalisierung: lower-case, Trennzeichen -> '_' */
static void norm_key(const char *in, char *out, size_t outsz) {
	size_t j=0;
	for (size_t i=0; in && in[i] && j+1<outsz; ++i) {
		unsigned char c = (unsigned char)in[i];
		if (c==' ' || c=='-' || c=='.') c = '_';
		out[j++] = (char)tolower(c);
	}
	out[j] = '\0';
}

/* Kanonische Perk-Namen für bekannte Indizes.
   Für nicht gelistete Indizes generieren wir "perk_0xNN". */
static const char *g_perk_names_known[] = {
	/* 0x00 .. 0x76 (nach deiner Liste) */
	"awareness","bonus_hth_attacks","bonus_hth_damage","bonus_move","bonus_ranged_damage",
	"bonus_rate_of_fire","earlier_sequence","faster_healing","more_criticals","night_vision",
	"presence","rad_resistance","toughness","strong_back","sharpshooter","silent_running",
	"survivalist","master_trader","educated","healer","fortune_finder","better_criticals",
	"empathy","slayer","sniper","silent_death","action_boy","mental_block","lifegiver",
	"dodger","snakeeater","mr_fixit","medic","master_thief","speaker","heave_ho",
	"friendly_foe_unused","pickpocket","ghost","cult_of_personality","scrounger",
	"explorer","flower_child","pathfinder","animal_friend","scout","mysterious_stranger",
	"ranger","quick_pockets","smooth_talker","swift_learner","tag","mutate",
	"nuka_cola_addiction","buffout_addiction","mentats_addiction","psycho_addiction",
	"radaway_addiction","weapon_long_range","weapon_accurate","weapon_penetrate",
	"weapon_knockback","powered_armor","combat_armor","weapon_scope_range",
	"weapon_fast_reload","weapon_night_sight","weapon_flameboy","armor_advanced_i",
	"armor_advanced_ii","jet_addiction","tragic_addiction","armor_charisma",
	"gecko_skinning","dermal_impact_armor","dermal_impact_assault_enh","phoenix_armor_implants",
	"phoenix_assault_enh","vault_city_inoculations","adrenaline_rush","cautious_nature",
	"comprehension","demolition_expert","gambler","gain_strength","gain_perception",
	"gain_endurance","gain_charisma","gain_intelligence","gain_agility","gain_luck",
	"harmless","here_and_now","hth_evade","kama_sutra_master","karma_beacon","light_step",
	"living_anatomy","magnetic_personality","negotiator","pack_rat","pyromaniac",
	"quick_recovery","salesman","stonewall","thief","weapon_handling","vault_city_training",
	"alcohol_raised_hp_i","alcohol_raised_hp_ii","alcohol_lowered_hp_i","alcohol_lowered_hp_ii",
	"autodoc_raised_hp_i","autodoc_raised_hp_ii","autodoc_lowered_hp_i","autodoc_lowered_hp_ii",
	"expert_excrement_expeditor","weapon_enhanced_knockout","jinxed"
};
/* Wie viele wir explizit benannt haben: */
static const int g_perk_names_known_count = (int)(sizeof(g_perk_names_known)/sizeof(g_perk_names_known[0]));


static int looks_like_perk_array(const unsigned char *p) {
	int zeros=0, big=0;
	for (int i=0;i<PERK_COUNT;i++) {
		uint32_t v = be32(p + i*4);
		if (v==0) zeros++;
		else if (v>10) big++;  /* Perks >10 Stufen sind extrem unwahrscheinlich */
	}
	return (zeros >= 150) && (big==0);
}

static int trait_ok(int32_t v) {
	return (v == -1) || (v >= 0 && v <= 32);
}
static int looks_like_traits_after_f13(const unsigned char *p) {
	int32_t t0 = (int32_t)be32(p + 0x14);
	int32_t t1 = (int32_t)be32(p + 0x18);
	return trait_ok(t0) && trait_ok(t1);
}

static int looks_like_func13(const unsigned char *p) {
	uint32_t sp  = be32(p + 0x00);
	uint32_t lvl = be32(p + 0x04);
	uint32_t xp  = be32(p + 0x08);
	uint64_t tail = ((uint64_t)be32(p + 0x0C) << 32) | (uint64_t)be32(p + 0x10);

	if (sp > 99) return 0;
	if (lvl < 1 || lvl > 99) return 0;              /* Mods können höher gehen; für False-Positives ist 1..99 gut */
	if (xp > 50000000u) return 0;                   /* sehr großzügig, aber schützt vor Zufallstreffern */
	/* tail ist in Praxis 0; tolerieren wir auch !=0, aber Null ist ein guter Hinweis */
	if (tail != 0 && xp == 0 && sp == 0) return 0;  /* schwaches Kriterium gegen reinen Nullmüll */
	return 1;
}

/* F18 liegt meist in der Nähe von F13. Wir suchen ein 4-Byte-Level gefolgt von 1 Byte (0/1). */
static int looks_like_func18(const unsigned char *p) {
	uint32_t lvl = be32(p + 0);
	unsigned char perkflag = p[4];
	if (lvl < 1 || lvl > 127) return 0;
	if (!(perkflag == 0 || perkflag == 1)) return 0;
	return 1;
}

/* ---------- Inventory (Function 5) ---------- */

typedef struct { int want; /* not used; placeholder */ } _InvDummy; /* just to keep style */

/* Bereits vorhanden:
   - be32(...), wr_be32(...)
   - save_find_func5_offset(...)
   - skip_items_var(...): rekursives Überspringen von Containern
   */

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

/* ---- Inventory: Vanilla FO1/FO2 (PC) ---- */
/* bekannte PC-Offsets/Größen: count @ +0x48, item_size=0x60, contained_off=0x4C */

static void _inv_print_rec(const unsigned char *b, size_t n, size_t *ioff,
		uint32_t count, size_t ITEM_SIZE, size_t cont_off, int *pidx) {
	for (uint32_t i = 0; i < count; ++i) {
		if (*ioff + ITEM_SIZE > n) return;
		size_t base = *ioff;

		uint32_t qty   = be32(b + base + 0x00);
		uint32_t flags = be32(b + base + 0x28);
		uint32_t objid = be32(b + base + 0x30);

		int right = !!(flags & 0x01000000u);
		int left  = !!(flags & 0x02000000u);
		int worn  = !!(flags & 0x04000000u);

		printf("[%3d] qty=%u  obj=0x%08X  flags=0x%08X  %s%s%s\n",
				*pidx, qty, objid, flags,
				right ? "[right]" : "", left ? "[left]" : "", worn ? "[worn]" : "");

		uint32_t inner = 0;
		if (base + cont_off + 4 <= n) inner = be32(b + base + cont_off);

		*ioff += ITEM_SIZE;
		(*pidx)++;

		if (inner > 0 && inner < 100000) {
			_inv_print_rec(b, n, ioff, inner, ITEM_SIZE, cont_off, pidx);
		}
	}
}

int save_list_inventory(const Save *s) {
	if (!s || !s->buf) return 0;
	const unsigned char *b = s->buf;
	const size_t n = s->size;

	size_t f5 = 0;
	if (!save_find_func5_offset(s, &f5)) return 0;

	const size_t CNT_OFF  = 0x48; /* Vanilla PC */
	const size_t ITEM_SZ  = 0x60; /* Vanilla PC */
	const size_t CONT_OFF = 0x4C; /* Vanilla PC */

	if (f5 + CNT_OFF + 4 > n) return 0;
	uint32_t count = be32(b + f5 + CNT_OFF);
	if (count > 20000u) return 0;

	size_t list_off = f5 + 0x80;

	/* Sicherheitscheck: Liste lässt sich konsistent überspringen */
	size_t tmp = list_off;
	if (!skip_items_var(b, n, &tmp, count, ITEM_SZ, CONT_OFF)) return 0;

	int idx = 0;
	_inv_print_rec(b, n, &list_off, count, ITEM_SZ, CONT_OFF, &idx);
	return 1;
}


static int _inv_try_layout(const unsigned char *b, size_t n, size_t f5,
		size_t cnt_off, size_t item_sz, size_t cont_off,
		SaveInvLayout *L) {
	if (f5 + 0x80 > n) return 0;
	if (f5 + cnt_off + 4 > n) return 0;
	uint32_t count = be32(b + f5 + cnt_off);
	if (count > 20000u) return 0; /* unplausibel */

	size_t off = f5 + 0x80;
	/* Wir prüfen nur, ob wir sauber über alle Items springen können,
	   d.h. das Layout konsistent ist. */
	size_t tmp = off;
	if (!skip_items_var(b, n, &tmp, count, item_sz, cont_off)) return 0;

	if (L) {
		L->f5_off = f5;
		L->item_count = count;
		L->list_off = off;
		L->item_size = item_sz;
		L->contained_off = cont_off;
	}
	return 1;
}

int save_inv_detect_layout(const Save *s, SaveInvLayout *out) {
	if (!s || !s->buf || !out) return 0;
	size_t f5=0;
	if (!save_find_func5_offset(s, &f5)) return 0; /* F5 muss vorhanden sein */

	static const size_t cnt_offs[3]  = {0x44, 0x48, 0x4C};
	static const size_t item_szs[3]  = {0x5C, 0x60, 0x64};
	static const size_t cont_offs[3] = {0x48, 0x4C, 0x50};

	for (int ci=0; ci<3; ++ci) {
		for (int si=0; si<3; ++si) {
			for (int co=0; co<3; ++co) {
				if (_inv_try_layout(s->buf, s->size, f5, cnt_offs[ci], item_szs[si], cont_offs[co], out))
					return 1;
			}
		}
	}
	return 0;
}

/* Rekursiv auflisten; flache Indexierung über *pidx */
static int _inv_enum_rec(const Save *s, const SaveInvLayout *L, size_t *ioff,
		uint32_t count, int *pidx, SaveInvCB cb, void *ud) {
	const unsigned char *b = s->buf;
	size_t n = s->size;

	for (uint32_t i=0; i<count; ++i) {
		if (*ioff + L->item_size > n) return 0;
		size_t item_off = *ioff;

		SaveInvItem it;
		it.index     = (*pidx);
		it.item_off  = item_off;
		it.qty_off   = item_off + 0x00; /* Menge */
		it.flags_off = item_off + 0x28; /* equip-bitfield */
		it.objid_off = item_off + 0x30; /* Object ID */

		it.quantity = be32(b + it.qty_off);
		it.obj_id   = be32(b + it.objid_off);
		it.flags    = be32(b + it.flags_off);

		if (cb) cb(s, L, &it, ud);

		/* Container? */
		size_t contained_pos = item_off + L->contained_off;
		uint32_t inner = 0;
		if (contained_pos + 4 <= n) inner = be32(b + contained_pos);

		*ioff += L->item_size;
		(*pidx)++;

		if (inner > 0 && inner < 100000) {
			if (!_inv_enum_rec(s, L, ioff, inner, pidx, cb, ud)) return 0;
		}
	}
	return 1;
}

int save_inv_enumerate(const Save *s, SaveInvCB cb, void *ud) {
	if (!s || !s->buf) return -1;
	SaveInvLayout L;
	if (!save_inv_detect_layout(s, &L)) return -1;

	size_t off = L.list_off;
	int idx = 0;
	if (!_inv_enum_rec(s, &L, &off, L.item_count, &idx, cb, ud)) return -1;
	return idx; /* Gesamtzahl der flachen Items */
}

static void _print_cb(const Save *s, const SaveInvLayout *L, const SaveInvItem *it, void *ud) {
	(void)s; (void)L; (void)ud;
	int right = !!(it->flags & 0x01000000u);
	int left  = !!(it->flags & 0x02000000u);
	int worn  = !!(it->flags & 0x04000000u);
	printf("[%3d] qty=%u  obj=0x%08X  flags=0x%08X  %s%s%s\n",
			it->index, it->quantity, it->obj_id, it->flags,
			right?"[right]":"", left?"[left]":"", worn?"[worn]":"");
}

int save_inv_print(const Save *s) {
	return save_inv_enumerate(s, _print_cb, NULL) >= 0 ? 1 : 0;
}

static int _inv_find_by_index(const Save *s, int flat_index, SaveInvLayout *L, SaveInvItem *out) {
	SaveInvLayout tmpL;
	if (!save_inv_detect_layout(s, &tmpL)) return 0;
	_FindCtx ctx; memset(&ctx, 0, sizeof(ctx));
	ctx.dst = out; ctx.Lsrc = &tmpL; ctx.Ldst = L; ctx.target = flat_index;
	int total = save_inv_enumerate(s, _find_cb, &ctx);
	(void)total;
	return ctx.found;
}

/* === Mutatoren === */
int save_inv_set_quantity(Save *s, int flat_index, uint32_t qty) {
	if (!s || !s->buf) return 0;
	SaveInvLayout L; SaveInvItem it;
	if (!_inv_find_by_index(s, flat_index, &L, &it)) return 0;
	wr_be32(s->buf + it.qty_off, qty);
	return 1;
}

/* Setze ausgerüstet-Bits exakt nach Parametern (1/0). */
int save_inv_set_equipped(Save *s, int flat_index, int right, int left, int worn) {
	if (!s || !s->buf) return 0;
	SaveInvLayout L; SaveInvItem it;
	if (!_inv_find_by_index(s, flat_index, &L, &it)) return 0;

	uint32_t f = it.flags;
	/* erst löschen */
	f &= ~(0x01000000u | 0x02000000u | 0x04000000u);
	/* dann setzen */
	if (right) f |= 0x01000000u;
	if (left)  f |= 0x02000000u;
	if (worn)  f |= 0x04000000u;

	wr_be32(s->buf + it.flags_off, f);
	return 1;
}


int save_calc_level_from_xp(int xp) {
	if (xp < 0) xp = 0;
	/* FO1/FO2 Vanilla: Schwelle für Level n ist 1000 * n*(n-1)/2.
	   Finde größtes n mit Schwelle <= xp. Clamp auf 1..127 konservativ. */
	int n = 1;
	while (n < 127) {
		long need = 1000L * n * (n - 1) / 2;
		if (need > xp) break;
		n++;
	}
	int lvl = n - 1;
	if (lvl < 1) lvl = 1;
	if (lvl > 127) lvl = 127;
	return lvl;
}


int save_find_func18_offset(const Save *s, size_t *out_off) {
	if (!s || !out_off) return 0;
	size_t f13;
	size_t n = s->size;
	const unsigned char *b = s->buf;

	/* Primär: knapp HINTER F13 suchen */
	if (save_find_func13_offset(s, &f13)) {
		size_t start = f13 + 0x10, end = (start + 0x4000 < n) ? start + 0x4000 : n;
		for (size_t off = start; off + 5 <= end; ++off) {
			if (looks_like_func18(b + off)) { *out_off = off; return 1; }
		}
	}
	/* Fallback: ganze Datei (selten nötig) */
	for (size_t off = 0; off + 5 <= n; ++off) {
		if (looks_like_func18(b + off)) { *out_off = off; return 1; }
	}
	return 0;
}

int save_get_charwin_level(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t off; if (!save_find_func18_offset(s, &off)) return 0;
	*out = (int)be32(s->buf + off);
	return 1;
}
int save_set_charwin_level(Save *s, int lvl) {
	if (!s) return 0;
	if (lvl < 1) lvl = 1; 
	if (lvl > 127) lvl = 127;
	size_t off; 
	if (!save_find_func18_offset(s, &off)) return 0;
	wr_be32(s->buf + off, (uint32_t)lvl);
	/* Perk-Flag (Byte 4) lassen wir wie es ist; das Spiel verwaltet es. */
	return 1;
}


int save_find_func13_offset(const Save *s, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b = s->buf;
	size_t n = s->size;

	// 1) Anker 1: Function 5 (Spieler-Objekt, 'FP'-Marker)
	size_t f5 = 0;
	int have_f5 = save_find_func5_offset(s, &f5);

	// 2) Anker 2: Function 9 (Perks) – relativ zum Spieler
	size_t f9 = 0;
	int have_f9 = save_find_func9_offset(s, &f9);

	// Primärfenster: hinter Function 9, begrenztes Suchfenster
	if (have_f9) {
		size_t start = f9 + 0x200;                         // ein Stück hinter Perks
		if (start > n) start = n;
		size_t end = (start + 0x3000 < n) ? start + 0x3000 : n;
		for (size_t off = start; off + FUNC13_SIZE <= end; ++off) { // Schrittweite 1!
			if (looks_like_func13(b + off) && looks_like_traits_after_f13(b + off)) {
				*out_off = off; return 1;
			}

		}
	}

	// Sekundärfenster: in der Nähe von Function 5 suchen (Spieler-Objekt)
	if (have_f5) {
		size_t start = (f5 > 0x20000) ? f5 - 0x20000 : 0;
		size_t end   = (f5 + 0x40000 < n) ? f5 + 0x40000 : n;
		for (size_t off = start; off + FUNC13_SIZE <= end; ++off) {
			if (looks_like_func13(b + off)) { *out_off = off; return 1; }
		}
	}

	// Tertiär: in der Nähe des Stats-Blocks (Function 6)
	size_t f6 = 0;
	if (save_find_stats_offset(s, &f6)) {
		size_t start = (f6 > 0x10000) ? f6 - 0x10000 : 0;
		size_t end   = (f6 + 0x20000 < n) ? f6 + 0x20000 : n;
		for (size_t off = start; off + FUNC13_SIZE <= end; ++off) {
			if (looks_like_func13(b + off)) { *out_off = off; return 1; }
		}
	}

	// Fallback: gesamtes File (langsam, aber sicher)
	for (size_t off = 0; off + FUNC13_SIZE <= n; ++off) {
		if (looks_like_func13(b + off)) { *out_off = off; return 1; }
	}
	return 0;
}

int save_find_func9_offset(const Save *s, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b=s->buf; size_t n=s->size;

	size_t stats_off=0; size_t start=0, end=n;
	if (save_find_stats_offset(s, &stats_off)) {
		start = (stats_off>0x8000)? stats_off-0x8000 : 0;
		end   = (stats_off+0x10000<n)? stats_off+0x10000 : n;
	}
	for (size_t off=start; off+PERK_BLOCK_SIZE<=end; off+=4) {
		if (looks_like_perk_array(b+off)) { *out_off=off; return 1; }
	}
	for (size_t off=0; off+PERK_BLOCK_SIZE<=n; off+=4) {
		if (looks_like_perk_array(b+off)) { *out_off=off; return 1; }
	}
	return 0;
}

int save_perk_get(const Save *s, int idx, int *out_count) {
	if (!s || !out_count || idx<0 || idx>=PERK_COUNT) return 0;
	size_t off; if (!save_find_func9_offset(s,&off)) return 0;
	*out_count = (int)be32(s->buf + off + (size_t)idx*4u);
	return 1;
}

int save_perk_set(Save *s, int idx, int count) {
	if (!s || idx<0 || idx>=PERK_COUNT) return 0;
	if (count < 0) count = 0;
	if (count > 10) count = 10; /* Safety */
	size_t off; if (!save_find_func9_offset(s,&off)) return 0;
	wr_be32(s->buf + off + (size_t)idx*4u, (uint32_t)count);
	return 1;
}

int save_perk_get_by_name(const Save *s, const char *name, int *out_count) {
	int idx = save_perk_find_index(name);
	return (idx<0) ? 0 : save_perk_get(s, idx, out_count);
}
int save_perk_set_by_name(Save *s, const char *name, int count) {
	int idx = save_perk_find_index(name);
	return (idx<0) ? 0 : save_perk_set(s, idx, count);
}

int save_perk_find_index(const char *name) {
	if (!name) return -1;
	char k[128]; norm_key(name,k,sizeof(k));

	/* Zahl erlaubt: 0..(PERK_COUNT-1), auch 0x.. */
	char *endp=NULL; long v=strtol(k,&endp,0);
	if (endp && *endp=='\0') return (v>=0 && v<PERK_COUNT) ? (int)v : -1;

	/* Sonst gegen bekannte Namen */
	for (int i=0;i<g_perk_names_known_count;i++) {
		if (strcmp(k, g_perk_names_known[i])==0) return i;
	}
	return -1;
}

const char *save_perk_name(int idx) {
	if (idx<0 || idx>=PERK_COUNT) return NULL;
	if (idx < g_perk_names_known_count) return g_perk_names_known[idx];
	return NULL; /* optional: on-demand "perk_0xNN" format in main.c */
}

int save_skill_find_index(const char *name) {
	if (!name) return -1;
	char k[64]; norm_key(name, k, sizeof(k));

	/* Zahl erlaubt: 0..17 oder 0x00..0x11 */
	char *endp=NULL;
	long v = strtol(k, &endp, 0);
	if (endp && *endp=='\0') {
		return (v>=0 && v<SKILL_COUNT) ? (int)v : -1;
	}

	for (int i=0;i<SKILL_COUNT;++i) {
		if (strcmp(k, g_skill_names[i])==0) return i;
	}
	return -1;
}

const char *save_skill_name(int idx) {
	return (idx>=0 && idx<SKILL_COUNT) ? g_skill_names[idx] : NULL;
}

int save_find_func5_offset(const Save *s, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b = s->buf;
	const size_t n = s->size;
	const uint32_t FP = 0x00004650; /* 'FP' */

	for (size_t off = 0; off + 0x28 <= n; ++off) {
		if (be32(b + off) != FP) continue;

		/* einfache Plausibilitätschecks aus dem bekannten Layout von Function 5 */
		uint32_t facing = be32(b + off + 0x1C);     /* 0..5 */

		if (facing > 5) continue;


		*out_off = off;
		return 1;
	}
	return 0;
}



int save_find_func8_offset(const Save *s, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b = s->buf;
	const size_t n = s->size;

	/* 1) bevorzugt im Umfeld des Stats-Blocks suchen (weniger False-Positives) */
	size_t stats_off = 0;
	size_t start = 0, end = n;
	if (save_find_stats_offset(s, &stats_off)) {
		/* +- ein paar KB um den Stats-Block */
		start = (stats_off > 0x4000) ? stats_off - 0x4000 : 0;
		end   = (stats_off + 0x8000 < n) ? stats_off + 0x8000 : n;
	}

	for (size_t off = start; off + 16 <= end; ++off) {
		if (is_valid_tag_block(b + off)) {
			*out_off = off;
			return 1;
		}
	}

	/* 2) Fallback: gesamtes File scannen */
	for (size_t off = 0; off + 16 <= n; ++off) {
		if (is_valid_tag_block(b + off)) {
			*out_off = off;
			return 1;
		}
	}
	return 0;
}


/* Big-Endian lesen haben wir schon: be32(...) */


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

static int looks_like_func5_at(const unsigned char *p, size_t avail) {
	if (avail < 0x20) return 0;
	/* +0x1C: Facing 0..5 */
	uint32_t facing = be32(p + 0x1C);
	if (facing > 5) return 0;
	return 1;
}

/* F5 in der Nähe suchen (um einen bekannten F13 herum) */
static int find_func5_near(const Save *s, size_t center, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b = s->buf; size_t n = s->size;
	const uint32_t FP = 0x00004650; /* 'FP' */
	size_t start = (center > 0x4000 ? center - 0x4000 : 0);
	size_t end   = (center + 0x4000 < n ? center + 0x4000 : n);
	for (size_t off = start; off + 8 <= end; ++off) {
		if (be32(b + off) != FP) continue;
		if (looks_like_func5_at(b + off, n - off)) { *out_off = off; return 1; }
	}
	return 0;
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
					if (off + STATS_MIN_SPAN > n) continue;
					if (!is_plausible_stats_block(buf, n, off)) continue;

					*out_offset = off;
					return 1;
				}
			}
		}
	}

	/* 2) Fallback: Vollscan nach plausibler 376-Byte-Stats-Struktur */
	for (size_t off=0; off + STATS_MIN_SPAN <= n; ++off) {
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
} 

int save_tabs_get_flags(const Save *s, uint32_t *out_flags) {
	int v = 0;
	if (!save_stats_get(s, "tabs_flags", &v)) return 0;
	if (out_flags) *out_flags = (uint32_t)v;
	return 1;
}

int save_tabs_update_flags(Save *s, uint32_t set_mask, uint32_t clear_mask) {
	int v = 0;
	if (!save_stats_get(s, "tabs_flags", &v)) return 0;
	uint32_t f = (uint32_t)v;
	f |= set_mask;
	f &= ~clear_mask;
	return save_stats_set(s, "tabs_flags", (int)f);
}



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


	/* Function 5 – Spezialfelder */
	if (strcmp(key, "hp_current") == 0)    return save_get_hp_current(s, out_value);
	if (strcmp(key, "rad_level")  == 0)    return save_get_rad_level(s,  out_value);
	if (strcmp(key, "poison_level")== 0)   return save_get_poison_level(s, out_value);
	if (!strcmp(key, "position") || !strcmp(key, "pos")) {
		uint32_t p; if (!save_get_position(s, &p)) return 0; *out_value = (int)p; return 1;
	}
	if (!strcmp(key, "facing"))   return save_get_facing(s, out_value);
	if (!strcmp(key, "cripple") || !strcmp(key, "cripple_flags")) {
		uint32_t m; if (!save_cripple_get(s, &m)) return 0; *out_value = (int)m; return 1;
	}
	if (strcmp(key, "skill_points")==0 || strcmp(key,"sp")==0) return save_get_skill_points(s, out_value);
	if (strcmp(key, "level")==0      || strcmp(key,"lvl")==0)  return save_get_level(s, out_value);
	if (strcmp(key, "experience")==0 || strcmp(key,"xp")==0)   return save_get_experience(s, out_value);

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

	/* Function 5 – Spezialfelder */
	if (strcmp(key, "hp_current")   == 0) return save_set_hp_current(s,   value);
	if (strcmp(key, "rad_level")    == 0) return save_set_rad_level(s,    value);
	if (strcmp(key, "poison_level") == 0) return save_set_poison_level(s, value);
	if (!strcmp(key, "position") || !strcmp(key, "pos")) return save_set_position(s, (uint32_t)value);
	if (!strcmp(key, "facing"))   return save_set_facing(s, value);
	if (!strcmp(key, "cripple") || !strcmp(key, "cripple_flags")) {
		/* hier sinnvoller: update statt absolutem Set – daher besser über separate CLI-Optionen */
		return 0;
	}
	if (strcmp(key, "skill_points")==0 || strcmp(key,"sp")==0) return save_set_skill_points(s, value);
	if (strcmp(key, "level")==0      || strcmp(key,"lvl")==0)  return save_set_level(s, value);
	if (strcmp(key, "experience")==0 || strcmp(key,"xp")==0)   return save_set_experience(s, value);



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
	printf("%-20s %-8s %-11s %-8s %s\n",
			"key", "offset", "range", "category", "description");
	printf("%-20s %-8s %-11s %-8s %s\n",
			"--------------------","--------","-----------","--------","------------------------------");
	for (size_t i=0; i<g_fields_count; ++i) {
		printf("%-20s +0x%04zx  %4d..%-4d %-8s %s\n",
				g_fields[i].key, g_fields[i].off,
				g_fields[i].minv, g_fields[i].maxv,
				g_fields[i].category ? g_fields[i].category : "",
				g_fields[i].desc ? g_fields[i].desc : "");
	}
}

/* Position @ FP + 0x04 (u32) */
int save_get_position(const Save *s, uint32_t *out_pos) {
	if (!s || !out_pos) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x04 + 4 > s->size) return 0;
	*out_pos = be32(s->buf + f5 + 0x04);
	return 1;
}
int save_set_position(Save *s, uint32_t pos) {
	if (!s) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x04 + 4 > s->size) return 0;
	wr_be32(s->buf + f5 + 0x04, pos);
	return 1;
}

/* Facing @ FP + 0x1C (0..5) */
int save_get_facing(const Save *s, int *out_dir) {
	if (!s || !out_dir) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x1C + 4 > s->size) return 0;
	*out_dir = (int)be32(s->buf + f5 + 0x1C);
	return 1;
}
int save_set_facing(Save *s, int dir) {
	if (!s) return 0;
	if (dir < 0) dir = 0;
	if (dir > 5) dir = 5;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x1C + 4 > s->size) return 0;
	wr_be32(s->buf + f5 + 0x1C, (uint32_t)dir);
	return 1;
}

/* Crippled bitfield @ FP + 0x64 (u32) */
int save_cripple_get(const Save *s, uint32_t *out_mask) {
	if (!s || !out_mask) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x64 + 4 > s->size) return 0;
	*out_mask = be32(s->buf + f5 + 0x64);
	return 1;
}
int save_cripple_update(Save *s, uint32_t set_mask, uint32_t clear_mask) {
	if (!s) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x64 + 4 > s->size) return 0;
	uint32_t cur = be32(s->buf + f5 + 0x64);
	cur |= set_mask;
	cur &= ~clear_mask;
	wr_be32(s->buf + f5 + 0x64, cur);
	return 1;
}


int save_get_hp_current(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f5 = 0;
	if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x74 + 4 > s->size) return 0;
	*out = (int)be32(s->buf + f5 + 0x74);   /* Function 5 + 0x74 */
	return 1;
}

int save_set_hp_current(Save *s, int value) {
	if (!s) return 0;
	size_t f5 = 0;
	if (!save_find_func5_offset(s, &f5)) return 0;
	if (value < 0) value = 0;               /* minimale Klammerung */
	if (f5 + 0x74 + 4 > s->size) return 0;
	wr_be32(s->buf + f5 + 0x74, (uint32_t)value);
	return 1;
}

int save_get_rad_level(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x78 + 4 > s->size) return 0;
	*out = (int)be32(s->buf + f5 + 0x78);
	return 1;
}

int save_set_rad_level(Save *s, int value) {
	if (!s) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (value < 0) value = 0;               /* einfache Klammerung */
	if (f5 + 0x78 + 4 > s->size) return 0;
	wr_be32(s->buf + f5 + 0x78, (uint32_t)value);
	return 1;
}

int save_get_poison_level(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (f5 + 0x7C + 4 > s->size) return 0;
	*out = (int)be32(s->buf + f5 + 0x7C);
	return 1;
}

int save_set_poison_level(Save *s, int value) {
	if (!s) return 0;
	size_t f5; if (!save_find_func5_offset(s, &f5)) return 0;
	if (value < 0) value = 0;               /* einfache Klammerung */
	if (f5 + 0x7C + 4 > s->size) return 0;
	wr_be32(s->buf + f5 + 0x7C, (uint32_t)value);
	return 1;
}
int save_tags_get_all(const Save *s, int out_idx[TAGS_MAX]) {
	if (!s || !out_idx) return 0;
	size_t off;
	if (!save_find_func8_offset(s, &off)) return 0;
	for (int i=0;i<TAGS_MAX;i++) {
		uint32_t v = be32(s->buf + off + i*4);
		out_idx[i] = (v == 0xFFFFFFFFu) ? -1 : (int)v;
	}
	return 1;
}

int save_tag_has(const Save *s, int skill_idx, int *out_has) {
	if (!s || !out_has) return 0;
	int tags[TAGS_MAX];
	if (!save_tags_get_all(s, tags)) return 0;
	for (int i=0;i<TAGS_MAX;i++) if (tags[i] == skill_idx) { *out_has = 1; return 1; }
	*out_has = 0; return 1;
}

int save_tag_add(Save *s, int skill_idx) {
	if (!s || !s->buf) return 0;
	if (skill_idx < 0 || skill_idx >= SKILL_COUNT) return 0;
	size_t off; if (!save_find_func8_offset(s, &off)) return 0;

	/* schon vorhanden? */
	for (int i=0;i<TAGS_MAX;i++) {
		uint32_t v = be32(s->buf + off + i*4);
		if (v == (uint32_t)skill_idx) return 1; /* bereits getaggt → OK */
	}
	/* freie Stelle? */
	for (int i=0;i<TAGS_MAX;i++) {
		uint32_t v = be32(s->buf + off + i*4);
		if (v == 0xFFFFFFFFu) {
			wr_be32(s->buf + off + i*4, (uint32_t)skill_idx);
			return 1;
		}
	}
	return 0; /* kein Slot frei */
}

int save_tag_remove(Save *s, int skill_idx) {
	if (!s || !s->buf) return 0;
	if (skill_idx < 0 || skill_idx >= SKILL_COUNT) return 0;
	size_t off; if (!save_find_func8_offset(s, &off)) return 0;

	for (int i=0;i<TAGS_MAX;i++) {
		uint32_t v = be32(s->buf + off + i*4);
		if (v == (uint32_t)skill_idx) {
			wr_be32(s->buf + off + i*4, 0xFFFFFFFFu);
			return 1;
		}
	}
	return 1; /* nicht vorhanden → gilt als erledigt */
}

int save_tags_clear(Save *s) {
	if (!s || !s->buf) return 0;
	size_t off; if (!save_find_func8_offset(s, &off)) return 0;
	for (int i=0;i<TAGS_MAX;i++) wr_be32(s->buf + off + i*4, 0xFFFFFFFFu);
	return 1;
}
int save_get_skill_points(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	*out = (int)be32(s->buf + f + 0x00);
	return 1;
}
int save_set_skill_points(Save *s, int value) {
	if (!s) return 0;
	if (value < 0) value = 0;
	if (value > 99) value = 99;
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	wr_be32(s->buf + f + 0x00, (uint32_t)value);
	return 1;
}

int save_get_level(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	*out = (int)be32(s->buf + f + 0x04);
	return 1;
}
int save_set_level(Save *s, int value) {
	if (!s) return 0;
	if (value < 1) value = 1;          /* Mindest-Level */
	if (value > 99) value = 99;        /* konservativ */
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	wr_be32(s->buf + f + 0x04, (uint32_t)value);
	return 1;
}

int save_get_experience(const Save *s, int *out) {
	if (!s || !out) return 0;
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	*out = (int)be32(s->buf + f + 0x08);
	return 1;
}
int save_set_experience(Save *s, int value) {
	if (!s) return 0;
	if (value < 0) value = 0;
	if (value > 2000000000) value = 2000000000;  /* clamp int32 */
	size_t f; if (!save_find_func13_offset(s, &f)) return 0;
	wr_be32(s->buf + f + 0x08, (uint32_t)value);
	return 1;
}


/* Liste alle F13-Blöcke. out kann NULL sein, dann nur zählen. */
int save_scan_all_func13(const Save *s, F13Hit *out, int max_out) {
	if (!s || !s->buf) return 0;
	const unsigned char *b = s->buf;
	const size_t n = s->size;
	int found = 0;
	int filled = 0;  // tatsächlich ins Array geschrieben

	for (size_t off = 0; off + 0x1C <= n; ++off) {
		if (!looks_like_func13(b + off)) continue;

		found++;
		if (out && filled < max_out) {
			F13Hit *h = &out[filled++];
			h->off    = off;
			h->sp     = be32(b + off + 0x00);
			h->lvl    = be32(b + off + 0x04);
			h->xp     = be32(b + off + 0x08);
			h->trait0 = (int32_t)be32(b + off + 0x14);
			h->trait1 = (int32_t)be32(b + off + 0x18);
		}
	}
	return found;
}

/* Finde F18 in der Nähe eines bekannten F13-Offsets (kleines Fenster). */
static int find_func18_near(const Save *s, size_t f13_off, size_t *out_off) {
	if (!s || !s->buf || !out_off) return 0;
	const unsigned char *b = s->buf;
	const size_t n = s->size;
	extern int looks_like_func18(const unsigned char *p); /* vorhanden */

	size_t start = f13_off + 0x10;
	size_t end   = (start + 0x4000 < n) ? start + 0x4000 : n;
	for (size_t off = start; off + 5 <= end; ++off) {
		if (looks_like_func18(b + off)) { *out_off = off; return 1; }
	}
	/* notfalls rückwärts ein kleines Stück probieren */
	size_t back_start = (f13_off > 0x4000) ? f13_off - 0x4000 : 0;
	for (size_t off = back_start; off + 5 <= f13_off; ++off) {
		if (looks_like_func18(b + off)) { *out_off = off; return 1; }
	}
	return 0;
}

int save_find_player_f13_auto(const Save *s, size_t *out_f13, size_t *out_f5, size_t *out_f18) {
	if (!s || !s->buf) return 0;
	F13Hit hits[512];
	int total = save_scan_all_func13(s, hits, 512);
	if (total <= 0) return 0;

	int best_i = -1, best_score = -999;
	size_t best_f5 = 0, best_f18 = 0;

	for (int i = 0; i < total && i < 512; ++i) {
		int score = 0;
		/* Traits: -1 oder „klein“ ist plausibel */
		int t0 = hits[i].trait0, t1 = hits[i].trait1;
		if ((t0 == -1 || (t0 >= 0 && t0 <= 20)) && (t1 == -1 || (t1 >= 0 && t1 <= 20))) score += 2;

		size_t f5 = 0;  if (find_func5_near(s, hits[i].off, &f5))  score += 3;
		size_t f18 = 0; if (find_func18_near(s, hits[i].off, &f18)) score += 2;

		if (score > best_score) { best_score = score; best_i = i; best_f5 = f5; best_f18 = f18; }
	}

	if (best_i < 0) return 0;
	if (out_f13) *out_f13 = hits[best_i].off;
	if (out_f5)  *out_f5  = best_f5;
	if (out_f18) *out_f18 = best_f18;
	return 1;
}

int save_set_xp_level_auto(Save *s, int xp, int *out_level) {
	if (!s || !s->buf) return 0;
	size_t f13=0, f5=0, f18=0;
	if (!save_find_player_f13_auto(s, &f13, &f5, &f18)) return 0;

	if (xp < 0) xp = 0;
	int lvl = save_calc_level_from_xp(xp);
	if (out_level) *out_level = lvl;

	if (f13 + 0x0C > s->size) return 0;
	wr_be32(s->buf + f13 + 0x08, (uint32_t)xp);   /* XP */
	wr_be32(s->buf + f13 + 0x04, (uint32_t)lvl);  /* Level */

	if (f18 && f18 + 4 <= s->size) {
		wr_be32(s->buf + f18, (uint32_t)lvl);     /* Char-Window Level */
	}
	return 1;
}


/* Setze XP+Level in ALLEN F13-Blöcken, spiegle Level in passenden F18-Caches. */
int save_set_experience_everywhere(Save *s, int xp) {
	if (!s || !s->buf) return 0;
	if (xp < 0) xp = 0;

	int num = save_scan_all_func13(s, NULL, 0);          // nur zählen
	F13Hit *hits = malloc(sizeof(*hits) * num);
	if (!hits) return 0;
	save_scan_all_func13(s, hits, num); 

	/* deine Level-Formel benutzen */
	extern int save_calc_level_from_xp(int xp);     /* vorhanden */
	int lvl = save_calc_level_from_xp(xp);

	for (int i = 0; i < num;++i) {
		size_t off = hits[i].off;
		if (off + 0x0C > s->size) continue;

		/* F13: XP/Level setzen */
		wr_be32(s->buf + off + 0x08, (uint32_t)xp);   /* Experience */
		wr_be32(s->buf + off + 0x04, (uint32_t)lvl);  /* Level      */

		/* Passenden F18 nahebei suchen und setzen (nicht fatal, wenn nicht gefunden) */
		size_t f18 = 0;
		if (find_func18_near(s, off, &f18)) {
			wr_be32(s->buf + f18, (uint32_t)lvl);
		}
	}
	free(hits);
	return 1;

}
int save_set_xp_level_where_traits(Save *s, int t0, int t1, int xp, int *out_patched) {
	if (!s || !s->buf) return 0;
	if (xp < 0) xp = 0;

	F13Hit hits[256];
	int total = save_scan_all_func13(s, hits, 256);
	int patched = 0;
	int lvl = save_calc_level_from_xp(xp);

	for (int i = 0; i < total && i < 256; ++i) {
		if (hits[i].trait0 == t0 && hits[i].trait1 == t1) {
			size_t off = hits[i].off;
			if (off + 0x0C <= s->size) {
				wr_be32(s->buf + off + 0x08, (uint32_t)xp);
				wr_be32(s->buf + off + 0x04, (uint32_t)lvl);

				size_t f18;
				if (find_func18_near(s, off, &f18)) {
					wr_be32(s->buf + f18, (uint32_t)lvl);
				}
				patched++;
			}
		}
	}
	if (out_patched) *out_patched = patched;
	return patched > 0;
}

static int trait_id_ok(int v) { return (v == -1) || (v >= 0 && v <= 15); }

int save_get_traits(const Save *s, int *out0, int *out1) {
	if (!s || !s->buf || !out0 || !out1) return 0;
	size_t f13 = 0;
	if (!save_find_func13_offset(s, &f13)) return 0;
	if (f13 + 0x1C > s->size) return 0;
	int32_t t0 = (int32_t)be32(s->buf + f13 + 0x14);
	int32_t t1 = (int32_t)be32(s->buf + f13 + 0x18);
	if (!trait_id_ok(t0) || !trait_id_ok(t1)) return 0;
	*out0 = t0; *out1 = t1;
	return 1;
}

int save_set_traits(Save *s, int t0, int t1) {

	if (!s || !s->buf) return 0;
	if (!trait_id_ok(t0) || !trait_id_ok(t1)) return 0;
	if (t0 != -1 && t1 != -1 && t0 == t1) return 0;
	size_t f13 = 0;
	if (!save_find_func13_offset(s, &f13)) return 0;
	if (f13 + 0x1C > s->size) return 0;
	/* Big-Endian schreiben */
	wr_be32(s->buf + f13 + 0x14, (uint32_t)t0);
	wr_be32(s->buf + f13 + 0x18, (uint32_t)t1);
	return 1;


}

const char *save_trait_name(int id) {
	// Statische Liste der Trait-Namen gemäß ihren IDs
	static const char *trait_names[16] = {
		"Fast Metabolism", "Bruiser", "Small Frame", "One Hander",
		"Finesse", "Kamikaze", "Heavy Handed", "Fast Shot",
		"Bloody Mess", "Jinxed", "Good Natured", "Chem Reliant",
		"Chem Resistant", "Sex Appeal", "Skilled", "Gifted"
	};
	static const char *noneStr = "None";
	if (id == -1) {
		return noneStr;  // keiner ausgewählt
	}
	if (id >= 0 && id < 16) {
		return trait_names[id];
	}
	return "(unknown)";  // ungültige ID
}

int save_trait_find_index(const char *name) {
	if (!name) {
		return -2; // -2 als Fehlercode für ungültigen Pointer
	}
	// Vergleich unabhängig von Groß-/Kleinschreibung
	if (!strcasecmp(name, "None")) {
		return -1;
	}
	// Durchsuche trait_names-Array (wie oben definiert)
	static const char *trait_names[16] = {
		"Fast Metabolism", "Bruiser", "Small Frame", "One Hander",
		"Finesse", "Kamikaze", "Heavy Handed", "Fast Shot",
		"Bloody Mess", "Jinxed", "Good Natured", "Chem Reliant",
		"Chem Resistant", "Sex Appeal", "Skilled", "Gifted"
	};
	for (int i = 0; i < 16; ++i) {
		if (!strcasecmp(name, trait_names[i])) {
			return i;
		}
	}
	return -2; // Name nicht gefunden
}
