#include <stdio.h>
#include <string.h>
#include <getopt.h>   /* glibc: getopt_long */
#include "savefile.h"
#include <stdlib.h>

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
			case 3001: {
			    Save s;
    			    if (!save_load(file, &s)) { fprintf(stderr,"Fehler: konnte '%s' nicht laden.\n", file); return 1; }
    			    if (!save_check_signature(&s)) { fprintf(stderr,"Fehler: Signatur nicht erkannt.\n"); save_free(&s); return 1; }
    				save_scan_stats_candidates(&s);
    				save_free(&s);
    				return 0;
				}
			case 3101: in_place = 1; break;
			case 3102: no_backup = 1; break;
			case 3301: assume_gifted = 1; break;
			case 3501: want_list_fields = 1; break;
			case 3502: get_key = optarg; break;
			case 3503: {
    					/* mehrere --set sammeln */
    					set_kvs = realloc(set_kvs, (set_kvs_cnt + 1) * sizeof(char*));
    					if (!set_kvs) { fprintf(stderr,"Out of memory\n"); return 1; }
    					set_kvs[set_kvs_cnt++] = optarg;
   					break;
					}

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

	if (want_list_fields) {
		save_stats_list_fields();
		save_free(&s);
		return 0;
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


	if (get_key) {
    int v=0;
    if (!save_stats_get(&s, get_key, &v)) {
        fprintf(stderr, "Fehler: Feld '%s' unbekannt oder nicht lesbar.\n", get_key);
        save_free(&s);
        return 1;
    }
    printf("%s: %d\n", get_key, v);
    /* kein return – du kannst get mit anderen Anzeigen kombinieren */
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



	save_free(&s);
	return 0;
}
//Marko ein schwanz
