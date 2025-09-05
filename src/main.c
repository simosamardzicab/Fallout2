#include <stdio.h>
#include <string.h>
#include <getopt.h>   /* glibc: getopt_long */
#include "savefile.h"
#include <stdlib.h>
#include <strings.h>
#include <ctype.h>




/* kleine, portable strdup-Variante */
static char *xstrdup(const char *src) {
	size_t n = strlen(src) + 1;
	char *p = (char*)malloc(n);
	if (p) memcpy(p, src, n);
	return p;
}

/* trimmt führende/nachfolgende Whitespaces in-place */
static void trim_whitespace(char *s) {
	if (!s) return;
	char *start = s;
	while (*start && isspace((unsigned char)*start)) start++;
	char *end = start + strlen(start);
	while (end > start && isspace((unsigned char)end[-1])) --end;
	size_t len = (size_t)(end - start);
	memmove(s, start, len);
	s[len] = '\0';
}


static void usage(const char *argv0) {
	fprintf(stderr,
			"Usage:\n"
			"  %s --file <SAVE.DAT> [commands] [options]\n"
			"\nCommands (read-only):\n"
			"  --show-name | --show-stats | --list-fields | --get <key>\n"
			"Commands (write):\n"
			"  --set-name <name> | --set-str N | --set-per N | ... | --set-lck N | --set <key>=<val>\n"
			"\nOptions:\n"
			"  --out <path> | --in-place | --no-backup | --assume-gifted | --help\n"
			"\nExamples:\n"
			"  %s --file Slot01/SAVE.DAT --show-stats\n"
			"  %s --file Slot01/SAVE.DAT --list-fields\n"
			"  %s --file Slot01/SAVE.DAT --get dr_plasma_base\n"
			"  %s --file Slot01/SAVE.DAT --set dr_plasma_base=50 --in-place\n",
			argv0, argv0, argv0, argv0, argv0);
}

static uint32_t parse_flag_mask(const char *s) {
	if (!s || !*s) return 0;
	/* Zahl: 0x.. oder dezimal */
	if ((s[0]=='0' && (s[1]=='x'||s[1]=='X')) || (s[0]>='0'&&s[0]<='9')) {
		char *endp=NULL; unsigned long v=strtoul(s,&endp,0);
		return (endp && *endp=='\0') ? (uint32_t)v : 0;
	}
	/* Namen (case-insensitive) */
	if (!strcasecmp(s,"Sneak"))    return 0x01;
	if (!strcasecmp(s,"Radiated")) return 0x02;
	if (!strcasecmp(s,"Level"))    return 0x08;
	if (!strcasecmp(s,"Addict"))   return 0x10;
	return 0; /* unbekannt */
}

static int parse_facing(const char *s) {
	if (!s) return -1;
	if (!strcasecmp(s,"0")||!strcasecmp(s,"ne")||!strcasecmp(s,"northeast")) return 0;
	if (!strcasecmp(s,"1")||!strcasecmp(s,"e") ||!strcasecmp(s,"east"))      return 1;
	if (!strcasecmp(s,"2")||!strcasecmp(s,"se")||!strcasecmp(s,"southeast")) return 2;
	if (!strcasecmp(s,"3")||!strcasecmp(s,"sw")||!strcasecmp(s,"southwest")) return 3;
	if (!strcasecmp(s,"4")||!strcasecmp(s,"w") ||!strcasecmp(s,"west"))      return 4;
	if (!strcasecmp(s,"5")||!strcasecmp(s,"nw")||!strcasecmp(s,"northwest")) return 5;
	return -1;
}

static uint32_t parse_cripple_mask(const char *s) {
	if (!s) return 0;
	/* Zahl erlaubt */
	if ((s[0]=='0' && (s[1]=='x'||s[1]=='X')) || (s[0]>='0' && s[0]<='9')) {
		char *endp=NULL; unsigned long v=strtoul(s,&endp,0);
		if (endp && *endp=='\0') return (uint32_t)v;
	}
	/* Namen */
	if (!strcasecmp(s,"eyes"))     return 0x0001;
	if (!strcasecmp(s,"rarm")||!strcasecmp(s,"right_arm")) return 0x0002;
	if (!strcasecmp(s,"larm")||!strcasecmp(s,"left_arm"))  return 0x0004;
	if (!strcasecmp(s,"rleg")||!strcasecmp(s,"right_leg")) return 0x0008;
	if (!strcasecmp(s,"lleg")||!strcasecmp(s,"left_leg"))  return 0x0010;
	if (!strcasecmp(s,"dead"))     return 0x0080;
	return 0;
}

static const char* facing_name(int d) {
	const char* names[6] = {"NE","E","SE","SW","W","NW"};
	return (d>=0 && d<6)? names[d] : "?";
}


int main(int argc, char **argv) {
	const char *file = NULL;
	const char *outfile = NULL;
	const char *new_name = NULL;
	int show_name = 0;
	int want_show_stats = 0;
	int set_stat_flag[STAT_COUNT] = {0};
	int set_stat_value[STAT_COUNT] = {0};
	int changed = 0;
	int in_place = 1;
	int no_backup = 0;  
	int assume_gifted = 0;
	int want_list_fields = 0;
	char **set_kvs = NULL;  /* mehrere --set key=val */
	int     set_kvs_cnt = 0;
	const char *get_key = NULL;
	const char *get_flag_name = NULL;
	char **set_flag_names = NULL;   
	int set_flag_cnt = 0;
	char **clr_flag_names = NULL;   
	int clr_flag_cnt = 0;
	int want_debug_find = 0;
	int want_get_pos = 0; const char *set_pos_str = NULL;

	int want_get_facing = 0; const char *set_facing_str = NULL;

	int want_get_cripple = 0;
	char **set_cripple_ks = NULL; int set_cripple_cnt = 0;
	char **clr_cripple_ks = NULL; int clr_cripple_cnt = 0;
	int want_list_tags = 0, want_clear_tags = 0;
	char **tag_add = NULL;  int tag_add_cnt = 0;
	char **tag_del = NULL;  int tag_del_cnt = 0;
	int want_list_perks = 0;
	const char *get_perk_name = NULL;
	char **set_perk_kvs = NULL; int set_perk_cnt = 0;
	int want_show_xp = 0;
	const char *opt_set_level = NULL;
	const char *opt_set_sp    = NULL;
	const char *opt_set_xp    = NULL;
	const char *opt_add_xp    = NULL;
	int want_debug_f13 = 0;
	int want_debug_list_f13 = 0;
	const char *opt_force_xp_all = NULL;
	const char *opt_set_xp_traits = NULL;
	const char *opt_set_xp_auto = NULL;

	const char *opt_set_inv_qty = NULL;
	const char *opt_equip = NULL;
	int want_list_inv = 0;
	int         want_list_traits = 0;
	const char *opt_set_traits   = NULL;


	static struct option opts[] = {
		{"file",       required_argument, 0, 'f'},
		{"out",        required_argument, 0, 'o'},
		{"show-name",  no_argument,       0, 's'},
		{"set-name",   required_argument, 0, 'n'},
		{"show-stats", no_argument,       0, 1000},
		/* v0.3: einzelne SPECIAL setzen */
		{"set-str", required_argument, 0, 2001},
		{"set-per", required_argument, 0, 2002},
		{"set-end", required_argument, 0, 2003},
		{"set-cha", required_argument, 0, 2004},
		{"set-int", required_argument, 0, 2005},
		{"set-agi", required_argument, 0, 2006},
		{"set-lck", required_argument, 0, 2007},
		{"debug-find-stats", no_argument, 0, 3001},
		{"in-place",   no_argument,       0, 3101},  // explizit in-place (Default)
		{"no-backup",  no_argument,       0, 3102},  // kein .bak anlegen
		{"assume-gifted", no_argument,    0, 3301},
		{"help",       no_argument,       0, 'h'},
		{"list-fields", no_argument,       0, 3501},
		{"get",         required_argument, 0, 3502},  /* --get <key> */
		{"set",         required_argument, 0, 3503},  /* --set key=value (mehrfach erlaubt) */
		{"get-flag",    required_argument, 0, 3701},
		{"set-flag",    required_argument, 0, 3702},
		{"clear-flag",  required_argument, 0, 3703},
		{"get-pos",       no_argument,       0, 4101},
		{"set-pos",       required_argument, 0, 4102},
		{"get-facing",    no_argument,       0, 4111},
		{"set-facing",    required_argument, 0, 4112},
		{"get-cripple",   no_argument,       0, 4121},
		{"set-cripple",   required_argument, 0, 4122},  /* name|0xHEX (mehrfach möglich) */
		{"clear-cripple", required_argument, 0, 4123},  /* name|0xHEX (mehrfach möglich) */
		{"list-tags",   no_argument,       0, 4300},
		{"tag-skill",   required_argument, 0, 4301},  /* mehrfach erlaubt */
		{"untag-skill", required_argument, 0, 4302},  /* mehrfach erlaubt */
		{"clear-tags",  no_argument,       0, 4303},
		{"list-perks",  no_argument,       0, 4400},
		{"get-perk",    required_argument, 0, 4401},
		{"set-perk",    required_argument, 0, 4402},
		{"show-xp",           no_argument,       0, 4700},
		{"set-level",         required_argument, 0, 4701},
		{"set-skill-points",  required_argument, 0, 4702},
		{"set-xp",            required_argument, 0, 4703},
		{"add-xp",            required_argument, 0, 4704},
		{"debug-find-f13", no_argument, 0, 4801},
		{"debug-list-f13", no_argument,       0, 3401},
		{"force-xp-all",   required_argument, 0, 3402},
		{"set-xp-traits", required_argument, 0, 3403},
		{"set-xp-auto", required_argument, 0, 3410},
		{"list-inv",     no_argument,       0, 5200},
		{"list-traits", no_argument,       0, 6200},
		{"set-traits",  required_argument, 0, 6201},
		{0,0,0,0}
	};

	int c;
	while ((c = getopt_long(argc, argv, "f:o:n:sh", opts, NULL)) != -1) {
		switch (c) {
			case 'f': file = optarg; break;
			case 'o': outfile = optarg; break;
			case 'n': new_name = optarg; break;
			case 's': show_name = 1; break;
			case 1000: want_show_stats = 1; break;
			case 2001: set_stat_flag[STAT_ST] = 1; set_stat_value[STAT_ST] = atoi(optarg); break;
			case 2002: set_stat_flag[STAT_PE] = 1; set_stat_value[STAT_PE] = atoi(optarg); break;
			case 2003: set_stat_flag[STAT_EN] = 1; set_stat_value[STAT_EN] = atoi(optarg); break;
			case 2004: set_stat_flag[STAT_CH] = 1; set_stat_value[STAT_CH] = atoi(optarg); break;
			case 2005: set_stat_flag[STAT_IN] = 1; set_stat_value[STAT_IN] = atoi(optarg); break;
			case 2006: set_stat_flag[STAT_AG] = 1; set_stat_value[STAT_AG] = atoi(optarg); break;
			case 2007: set_stat_flag[STAT_LK] = 1; set_stat_value[STAT_LK] = atoi(optarg); break;
			case 3001: want_debug_find = 1; break;
			case 3101: in_place = 1; break;
			case 3102: no_backup = 1; break;
			case 3301: assume_gifted = 1; break;
			case 3401: /* --debug-list-f13 */ want_debug_list_f13 = 1; break;
			case 3402: /* --force-xp-all N */  opt_force_xp_all = optarg; break;
			case 3403: opt_set_xp_traits = optarg; break; // "6,15,105000"
			case 3410: {opt_set_xp_auto = optarg;break;}
			case 3501: want_list_fields = 1; break;
			case 3502: get_key = optarg; break;
			case 3503: {
					   /* mehrere --set sammeln */
					   set_kvs = realloc(set_kvs, (set_kvs_cnt + 1) * sizeof(char*));
					   if (!set_kvs) { fprintf(stderr,"Out of memory\n"); return 1; }
					   set_kvs[set_kvs_cnt++] = optarg;
					   break;
				   }
			case 3701: get_flag_name = optarg; break;
			case 3702:
				   set_flag_names = realloc(set_flag_names, (set_flag_cnt+1)*sizeof(char*));
				   set_flag_names[set_flag_cnt++] = optarg;
				   break;
			case 3703:
				   clr_flag_names = realloc(clr_flag_names, (clr_flag_cnt+1)*sizeof(char*));
				   clr_flag_names[clr_flag_cnt++] = optarg;
				   break;
			case 4101: want_get_pos = 1; break;
			case 4102: set_pos_str = optarg; break;

			case 4111: want_get_facing = 1; break;
			case 4112: set_facing_str = optarg; break;

			case 4121: want_get_cripple = 1; break;
			case 4122:
				   set_cripple_ks = realloc(set_cripple_ks, (set_cripple_cnt+1)*sizeof(char*));
				   set_cripple_ks[set_cripple_cnt++] = optarg;
				   break;
			case 4123:
				   clr_cripple_ks = realloc(clr_cripple_ks, (clr_cripple_cnt+1)*sizeof(char*));
				   clr_cripple_ks[clr_cripple_cnt++] = optarg;
				   break;
			case 4300: want_list_tags = 1; break;
			case 4303: want_clear_tags = 1; break;
			case 4301:
				   tag_add = realloc(tag_add, (tag_add_cnt+1)*sizeof(char*));
				   tag_add[tag_add_cnt++] = optarg;
				   break;
			case 4302:
				   tag_del = realloc(tag_del, (tag_del_cnt+1)*sizeof(char*));
				   tag_del[tag_del_cnt++] = optarg;
				   break;
			case 4400: want_list_perks = 1; break;
			case 4401: get_perk_name = optarg; break;
			case 4402:
				   set_perk_kvs = realloc(set_perk_kvs, (set_perk_cnt+1)*sizeof(char*));
				   set_perk_kvs[set_perk_cnt++] = optarg;
				   break;
			case 4700: want_show_xp = 1; break;
			case 4701: opt_set_level = optarg; break;
			case 4702: opt_set_sp    = optarg; break;
			case 4703: opt_set_xp    = optarg; break;
			case 4704: opt_add_xp    = optarg; break;
			case 4801: want_debug_f13 = 1; break;
			case 5200: want_list_inv = 1; break;
			case 5201: opt_set_inv_qty = optarg; break;
			case 5202: opt_equip = optarg; break;
			case 6200: want_list_traits = 1; break;
			case 6201: opt_set_traits   = optarg; break;   // z.B. "Gifted,Jinxed"
			case 'h': default: usage(argv[0]); return (c=='h') ? 0 : 1;
		}
	}

	if (!file) { usage(argv[0]); return 1; }


	Save s;
	if (!save_load(file, &s)) {
		fprintf(stderr, "Fehler: konnte '%s' nicht laden.\n", file);
		return 1;
	}

	if (!save_check_signature(&s)) {
		fprintf(stderr, "Fehler: Signatur nicht erkannt. Abbruch!\n");
		save_free(&s);
		return 1;
	}

	if (opt_set_xp_auto) {
		int v = atoi(opt_set_xp_auto);
		int lvl = 0;
		if (!save_set_xp_level_auto(&s, v, &lvl)) {
			fprintf(stderr, "--set-xp-auto: Spieler-Block nicht gefunden/gesetzt.\n");
			save_free(&s);
			return 1;
		}
		changed = 1;
		printf("XP=%d gesetzt, Level≈%d (auto erkannt)\n", v, lvl);
	}


	if (opt_set_xp_traits) {
		int t0=0, t1=0, xp=0;
		if (sscanf(opt_set_xp_traits, "%d,%d,%d", &t0, &t1, &xp) != 3) {
			fprintf(stderr, "--set-xp-traits erwartet: T0,T1,XP (z.B. 6,15,105000)\n");
			save_free(&s); return 1;
		}
		int patched = 0;
		if (!save_set_xp_level_where_traits(&s, t0, t1, xp, &patched) || patched == 0) {
			fprintf(stderr, "Kein F13 mit traits=[%d,%d] gefunden.\n", t0, t1);
			save_free(&s); return 1;
		}
		changed = 1;
	}

	/* ---- Debug: alle F13-Kandidaten listen ---- */
	if (want_debug_list_f13) {
		F13Hit hits[256];
		int total = save_scan_all_func13(&s, hits, 256);
		int shown = total;
		if(shown > 256) shown = 256;
		printf("F13 candidates (showing %d of %d):\n", shown, total);
		for (int i = 0; i < shown; ++i) {
			printf("  #%d @ 0x%zx  sp=%u  lvl=%u  xp=%u  traits=[%d,%d]\n",
					i, hits[i].off, hits[i].sp, hits[i].lvl, hits[i].xp,
					hits[i].trait0, hits[i].trait1);
		}

	}

	/* ---- Force: überall setzen ---- */
	if (opt_force_xp_all) {
		int v = atoi(opt_force_xp_all);
		if (!save_set_experience_everywhere(&s, v)) {
			fprintf(stderr, "--force-xp-all: kein F13 änderbar.\n");
			save_free(&s); return 1;
		}
		changed = 1;
	}


	/* ---- DEBUG: Function 13 (XP/Level) ---- */
	if (want_debug_f13) {
		size_t off;
		if (save_find_func13_offset(&s, &off)) {
			int sp = 0, lvl = 0, xp = 0;

			if (!save_get_skill_points(&s, &sp)) { fprintf(stderr, "skill_points nicht lesbar.\n"); save_free(&s); return 1; }
			if (!save_get_level(&s, &lvl))       { fprintf(stderr, "level nicht lesbar.\n");       save_free(&s); return 1; }
			if (!save_get_experience(&s, &xp))   { fprintf(stderr, "xp nicht lesbar.\n");          save_free(&s); return 1; }

			printf("Func13 @ 0x%zx  sp=%d  lvl=%d  xp=%d\n", off, sp, lvl, xp);

			/* optional: mit ausgeben, wo F5/F9 liegen (falls du die Finder hast) */
			size_t f5;
			if (save_find_func5_offset(&s, &f5)) {
				printf("Func5 (Player) @ 0x%zx\n", f5);
			}
			size_t f9;
			if (save_find_func9_offset(&s, &f9)) {
				printf("Func9 (Perks)  @ 0x%zx\n", f9);
			}
		} else {
			puts("Func13 nicht gefunden.");
		}
		save_free(&s);
		return 0;
	}

	if (want_list_inv) {
		if (!save_inv_print(&s)) { fprintf(stderr, "Inventar nicht lesbar.\n"); save_free(&s); return 1; }
	}

	/* --set-inv-qty idx=N */
	if (opt_set_inv_qty) {
		int idx=0; unsigned long n=0; char *eq = strchr(opt_set_inv_qty,'=');
		if (!eq) { fprintf(stderr,"--set-inv-qty erwartet <idx>=<N>\n"); save_free(&s); return 1; }
		*eq = '\0';
		idx = atoi(opt_set_inv_qty);
		n = strtoul(eq+1, NULL, 0);
		if (!save_inv_set_quantity(&s, idx, (uint32_t)n)) {
			fprintf(stderr,"Konnte Menge nicht setzen (idx=%d).\n", idx);
			save_free(&s); return 1;
		}
		changed = 1;
	}

	/* --equip idx=right|left|worn|none  (Mehrfachbits via Komma: right,left) */
	if (opt_equip) {
		int idx=0; char *eq = strchr(opt_equip,'=');
		if (!eq) { fprintf(stderr,"--equip erwartet <idx>=<slot(s)>\n"); save_free(&s); return 1; }
		*eq = '\0';
		idx = atoi(opt_equip);
		int r=0,l=0,w=0;
		char *p = eq+1;
		if (strcasecmp(p,"none")==0) { r=l=w=0; }
		else {
			/* mehrere durch Komma */
			char *tok = strtok(p, ",");
			while (tok) {
				if (!strcasecmp(tok,"right")) r=1;
				else if (!strcasecmp(tok,"left")) l=1;
				else if (!strcasecmp(tok,"worn")) w=1;
				tok = strtok(NULL,",");
			}
		}
		if (!save_inv_set_equipped(&s, idx, r,l,w)) {
			fprintf(stderr,"Konnte Equip-Bits nicht setzen (idx=%d).\n", idx);
			save_free(&s); return 1;
		}
		changed = 1;
	}



	if (want_get_pos) {
		uint32_t p=0;
		if (!save_get_position(&s, &p)) { fprintf(stderr,"Position nicht lesbar.\n"); save_free(&s); return 1; }
		printf("position: %u\n", p);
	}
	if (set_pos_str) {
		uint32_t p = (uint32_t)strtoul(set_pos_str, NULL, 0);
		if (!save_set_position(&s, p)) { fprintf(stderr,"Position konnte nicht gesetzt werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_facing) {
		int d=0;
		if (!save_get_facing(&s, &d)) { fprintf(stderr,"Facing nicht lesbar.\n"); save_free(&s); return 1; }
		printf("facing: %d (%s)\n", d, facing_name(d));
	}
	if (set_facing_str) {
		int d = parse_facing(set_facing_str);
		if (d < 0) { fprintf(stderr,"Ungültige Facing-Angabe. Erlaubt: 0..5 | ne,e,se,sw,w,nw\n"); save_free(&s); return 1; }
		if (!save_set_facing(&s, d)) { fprintf(stderr,"Facing konnte nicht gesetzt werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}
	uint32_t setm=0, clrm=0;
	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	/* ---- Function 8: Tag skills ---- */
	if (want_list_tags) {
		int tags[TAGS_MAX];
		if (!save_tags_get_all(&s, tags)) {
			fprintf(stderr, "Tag-Block (Function 8) nicht gefunden.\n");
			save_free(&s); return 1;
		}
		puts("Tags:");
		for (int i=0;i<TAGS_MAX;i++) {
			if (tags[i] < 0) {
				printf("  [%d] <unused>\n", i);
			} else {
				const char *nm = save_skill_name(tags[i]);
				printf("  [%d] %s (%d)\n", i, nm ? nm : "?", tags[i]);
			}
		}
	}

	/* add/remove */
	for (int i=0;i<tag_add_cnt;i++) {
		int idx = save_skill_find_index(tag_add[i]);
		if (idx < 0) { fprintf(stderr, "Unbekannter Skill: '%s'\n", tag_add[i]); save_free(&s); return 1; }
		if (!save_tag_add(&s, idx)) {
			fprintf(stderr, "Kein freier Tag-Slot oder bereits getaggt: %s\n", tag_add[i]);
			save_free(&s); return 1;
		}
		changed = 1;
	}
	for (int i=0;i<tag_del_cnt;i++) {
		int idx = save_skill_find_index(tag_del[i]);
		if (idx < 0) { fprintf(stderr, "Unbekannter Skill: '%s'\n", tag_del[i]); save_free(&s); return 1; }
		if (!save_tag_remove(&s, idx)) {
			fprintf(stderr, "Tag konnte nicht entfernt werden: %s\n", tag_del[i]);
			save_free(&s); return 1;
		}
		changed = 1;
	}
	if (want_clear_tags) {
		if (!save_tags_clear(&s)) {
			fprintf(stderr, "Tags konnten nicht geleert werden.\n");
			save_free(&s); return 1;
		}
		changed = 1;
	}


	if (want_debug_find) {
		save_scan_stats_candidates(&s);
		save_free(&s);
		return 0;
	}


	if (want_list_fields) {
		save_stats_list_fields();
		save_free(&s);
		return 0;
	}

	int handled_get = 0;


	if (get_key) {
		int v = 0;

		if (strcmp(get_key, "hp_current") == 0) {
			if (!save_get_hp_current(&s, &v)) {
				fprintf(stderr, "hp_current nicht gefunden.\n");
				save_free(&s);
				return 1;
			}
			printf("hp_current: %d\n", v);
			handled_get = 1;

		}}



	if(get_flag_name) {
		uint32_t f=0;
		if (!save_tabs_get_flags(&s, &f)) {
			fprintf(stderr,"Fehler: tabs_flags nicht lesbar.\n");
			save_free(&s); return 1;
		}
		uint32_t m = parse_flag_mask(get_flag_name);
		if (!m) {
			printf("tabs_flags = 0x%08X (Sneak=%d, Radiated=%d, Level=%d, Addict=%d)\n",
					f, !!(f&0x1), !!(f&0x2), !!(f&0x8), !!(f&0x10));
		} else {
			printf("%s: %s (mask 0x%X, flags 0x%08X)\n",
					get_flag_name, (f & m) ? "ON" : "OFF", m, f);
		}
	}

	/* SET/CLEAR-FLAG (write) */
	uint32_t set_mask=0, clr_mask=0;
	for (int i=0;i<set_flag_cnt;++i) { uint32_t m=parse_flag_mask(set_flag_names[i]); if (m) set_mask|=m; }
	for (int i=0;i<clr_flag_cnt;++i) { uint32_t m=parse_flag_mask(clr_flag_names[i]); if (m) clr_mask|=m; }
	if (set_mask || clr_mask) {
		if (!save_tabs_update_flags(&s, set_mask, clr_mask)) {
			fprintf(stderr,"Fehler: tabs_flags konnten nicht geändert werden.\n");
			save_free(&s); return 1;
		}
		changed = 1;
	}


	if (show_name) {
		char name[32];
		save_get_player_name(&s, name);
		printf("Spielername: \"%s\"\n", name);
	}



	if (want_show_stats) {
		size_t stats_off;
		if (!save_find_stats_offset(&s, &stats_off)) {
			fprintf(stderr, "Fehler: Stats-Block nicht gefunden.\n");
			save_free(&s);
			return 1;
		}
		const char *names[STAT_COUNT] = {"STR","PER","END","CHA","INT","AGI","LCK"};
		for (int i=0; i<STAT_COUNT; ++i) {
			int v=0;
			if (!save_read_stat(&s, i, &v)) {
				fprintf(stderr, "Fehler beim Lesen von %s\n", names[i]);
				save_free(&s);
				return 1;
			}
			if (assume_gifted) {
				printf("%s: %d (effektiv ~ %d)\n", names[i], v, v + 1);
			}
			else {
				printf("%s: %d\n", names[i], v);
			}

		}
	}


	if (new_name) { 
		if (!save_set_player_name(&s, new_name)) { 
			fprintf(stderr,"Fehler: Name konnte nicht gesetzt werden.\n"); 
			save_free(&s); 
			return 1; 
		} 
		changed = 1;
	}
	for (int i=0;i<STAT_COUNT;++i) {
		if (set_stat_flag[i]) {
			if (!save_write_stat(&s, i, set_stat_value[i])) { fprintf(stderr,"Fehler beim Setzen eines Stats (Index %d).\n", i); save_free(&s); return 1; }
			changed = 1;
		}
	}

	for (int i=0; i<set_kvs_cnt; ++i) {
		char *kv = set_kvs[i];
		char *eq = strchr(kv, '=');
		if (!eq || eq == kv || !eq[1]) {
			fprintf(stderr, "Fehler: --set erwartet key=value (bekommen: '%s')\n", kv);
			save_free(&s);
			return 1;
		}
		*eq = '\0';
		const char *k = kv;
		int v = atoi(eq+1);
		if (!save_stats_set(&s, k, v)) {
			fprintf(stderr, "Fehler: konnte '%s' nicht setzen.\n", k);
			save_free(&s);
			return 1;
		}
		changed = 1;
	}



	if (get_key && !handled_get) {
		int v=0;
		if (!save_stats_get(&s, get_key, &v)) {
			fprintf(stderr, "Fehler: Feld '%s' unbekannt oder nicht lesbar.\n", get_key);
			save_free(&s);
			return 1;
		}
		printf("%s: %d\n", get_key, v);
		/* kein return – du kannst get mit anderen Anzeigen kombinieren */
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}
	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}
	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}

	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}

	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}

	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}

	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	if (want_get_cripple) {
		uint32_t m=0;
		if (!save_cripple_get(&s, &m)) { fprintf(stderr,"Cripple-Flags nicht lesbar.\n"); save_free(&s); return 1; }
		printf("cripple: 0x%04X  (eyes=%d rarm=%d larm=%d rleg=%d lleg=%d dead=%d)\n",
				(unsigned)m, !!(m&0x0001), !!(m&0x0002), !!(m&0x0004), !!(m&0x0008), !!(m&0x0010), !!(m&0x0080));
	}

	for (int i=0;i<set_cripple_cnt;++i)  { setm |= parse_cripple_mask(set_cripple_ks[i]); }
	for (int i=0;i<clr_cripple_cnt;++i)  { clrm |= parse_cripple_mask(clr_cripple_ks[i]); }
	if (setm || clrm) {
		if (!save_cripple_update(&s, setm, clrm)) { fprintf(stderr,"Cripple-Flags konnten nicht geändert werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}

	/* ---- Function 9: Perks ---- */
	if (want_list_perks) {
		size_t off=0;
		if (!save_find_func9_offset(&s,&off)) {
			fprintf(stderr, "Perk-Block (Function 9) nicht gefunden.\n");
			save_free(&s); return 1;
		}
		puts("Perks (nur ≠0 gezeigt):");
		for (int i=0;i<PERK_COUNT;i++) {
			int c=0; if (!save_perk_get(&s,i,&c)) { fprintf(stderr,"Lesefehler bei perk %d\n",i); save_free(&s); return 1; }
			if (c!=0) {
				const char *nm = save_perk_name(i);
				if (!nm) { static char tmp[32]; snprintf(tmp,sizeof(tmp),"perk_0x%02X", i); nm=tmp; }
				printf("  [%3d] %-28s : %d\n", i, nm, c);
			}
		}
	}

	if (get_perk_name) {
		int c=0;
		if (!save_perk_get_by_name(&s, get_perk_name, &c)) {
			fprintf(stderr, "Perk '%s' unbekannt/nicht lesbar.\n", get_perk_name);
			save_free(&s); return 1;
		}
		printf("%s: %d\n", get_perk_name, c);
	}

	/* setzen (name=zahl), mehrfach erlaubt */
	for (int i=0;i<set_perk_cnt;i++) {
		char *kv = set_perk_kvs[i];
		char *eq = strchr(kv, '=');
		if (!eq) { fprintf(stderr, "--set-perk erwartet name=zahl\n"); save_free(&s); return 1; }
		*eq = '\0';
		const char *name = kv;
		int value = atoi(eq+1);
		if (!save_perk_set_by_name(&s, name, value)) {
			fprintf(stderr, "Perk '%s' konnte nicht gesetzt werden.\n", name);
			save_free(&s); return 1;
		}
		changed = 1;
	}
	/* ---- Function 13: XP & Level ---- */
	if (want_show_xp) {
		int sp=0, lvl=0, xp=0;
		if (!save_get_skill_points(&s, &sp) ||
				!save_get_level(&s, &lvl) ||
				!save_get_experience(&s, &xp)) {
			fprintf(stderr, "Function 13 (XP/Level) nicht gefunden/lesbar.\n");
			save_free(&s); return 1;
		}
		printf("SkillPoints: %d\nLevel: %d\nExperience: %d\n", sp, lvl, xp);
	}

	if (opt_set_level) {
		int v = atoi(opt_set_level);
		if (!save_set_level(&s, v)) { fprintf(stderr,"Level konnte nicht gesetzt werden.\n");
			/* Level konsistent in F18 spiegeln (non-fatal, falls nicht gefunden) */
			(void)save_set_charwin_level(&s, v);
			changed = 1;
			save_free(&s); 
			return 1; 

		}
		(void)save_set_charwin_level(&s, v);  // <- IMMER spiegeln
		changed = 1;
	}
	if (opt_set_sp) {
		int v = atoi(opt_set_sp);
		if (!save_set_skill_points(&s, v)) { fprintf(stderr,"SkillPoints konnten nicht gesetzt werden.\n"); save_free(&s); return 1; }
		changed = 1;
	}



	if (opt_set_xp) {
		int v = atoi(opt_set_xp);
		if (!save_set_experience(&s, v)) { /* Fehler handling ... */ }
		int lvl = save_calc_level_from_xp(v);
		(void)save_set_level(&s, lvl);          /* F13: Level */
		(void)save_set_charwin_level(&s, lvl);  /* F18: Char-Window */
		changed = 1;
	}

	if (opt_add_xp) {
		int cur=0; if (!save_get_experience(&s, &cur)) { /* ... */ }
		long add = strtol(opt_add_xp, NULL, 0);
		long long nv = (long long)cur + add;
		if (nv < 0) nv = 0; 
		if (nv > 2000000000LL) nv = 2000000000LL;
		if (!save_set_experience(&s, (int)nv)) { /* ... */ }
		int lvl = save_calc_level_from_xp((int)nv);
		(void)save_set_level(&s, lvl);
		(void)save_set_charwin_level(&s, lvl);
		changed = 1;
	}

	/* --list-traits */
	if (want_list_traits) {
		int t0 = -1, t1 = -1;
		if (!save_get_traits(&s, &t0, &t1)) {
			fprintf(stderr, "Fehler: Traits konnten nicht gelesen werden.\n");
			save_free(&s); return 1;
		}
		printf("Traits: %s, %s\n",
				save_trait_name(t0), save_trait_name(t1));
	}

	/* --set-traits "Trait1[,Trait2]" */
	if (opt_set_traits) {
		/* kopieren, da wir modifizieren – nimm deine xstrdup()/trim_whitespace falls vorhanden */
		char *input = xstrdup(opt_set_traits);
		if (!input) { fprintf(stderr, "Out of memory\n"); save_free(&s); return 1; }
		char *comma = strchr(input, ',');
		char *name0 = input, *name1 = NULL;
		if (comma) { *comma = '\0'; name1 = comma + 1; }
		trim_whitespace(name0); if (name1) trim_whitespace(name1);
		if (*name0 == '\0') { fprintf(stderr,"Fehler: erster Trait-Name fehlt.\n");
			free(input); save_free(&s); return 1; }

		int id0 = save_trait_find_index(name0);
		int id1 = (name1 && *name1) ? save_trait_find_index(name1) : -1;
		if (id0 == -2 || id1 == -2) {
			fprintf(stderr, "Fehler: unbekannter Trait-Name \"%s\".\n",
					(id0 == -2) ? name0 : name1);
			free(input); save_free(&s); return 1;
		}
		if (id0 != -1 && id1 != -1 && id0 == id1) {
			fprintf(stderr, "Fehler: derselbe Trait doppelt.\n");
			free(input); save_free(&s); return 1;
		}
		if (!save_set_traits(&s, id0, id1)) {
			fprintf(stderr, "Fehler beim Setzen der Traits.\n");
			free(input); save_free(&s); return 1;
		}
		printf("Traits erfolgreich gesetzt: %s, %s\n",
				save_trait_name(id0), save_trait_name(id1));
		free(input);
		changed = 1;
	}




	if (changed) {
		if (in_place && !outfile) {
			// atomisches In-Place mit Backup (wenn nicht abgeschaltet)
			if (!save_write_inplace_atomic(file, &s, !no_backup)) {
				fprintf(stderr, "Fehler beim In-Place-Schreiben nach '%s'.\n", file);
				save_free(&s);
				return 1;
			}
			printf("Gespeichert (in-place)%s: %s\n", no_backup ? "" : " + Backup(.bak)", file);
		} else {
			// explizite Ausgabe oder in_place=0
			if (!outfile) {
				// falls jemand in_place=0 setzt, aber kein --out angibt:
				outfile = "SAVE.out.DAT";
			}
			if (!save_write(outfile, &s)) {
				fprintf(stderr, "Fehler beim Schreiben nach '%s'.\n", outfile);
				save_free(&s);
				return 1;
			}
			printf("Gespeichert: %s\n", outfile);
		}
	}
	save_free(&s);
	return 0;
}
