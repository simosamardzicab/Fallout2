#include <stdio.h>
#include <string.h>
#include <getopt.h>   /* glibc: getopt_long */
#include "savefile.h"
#include <stdlib.h>

static void usage(const char *argv0) {
    fprintf(stderr,
        "Usage:\n"
        "  %s --file <SAVE.DAT> [--show-name] [--set-name \"Neuer Name\"] [--out <Zieldatei>]\n"
        "\n"
        "Beispiele:\n"
        "  %s --file Slot01/SAVE.DAT --show-name\n"
        "  %s --file Slot01/SAVE.DAT --set-name \"Max\"\n"
        "  %s --file Slot01/SAVE.DAT --set-name \"Max\" --out Slot01/SAVE_modified.DAT\n",
        argv0, argv0, argv0, argv0
    );
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
	{"in-place",   no_argument,       0, 3101},  // explizit in-place (Default)
	{"no-backup",  no_argument,       0, 3102},  // kein .bak anlegen
	{"help",       no_argument,       0, 'h'},
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
	    case 3101: in_place = 1; break;
	    case 3102: no_backup = 1; break;
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
           if (save_read_stat(&s, i, &v)) printf("%s: %d\n", names[i], v);
           else fprintf(stderr, "Fehler beim Lesen von %s\n", names[i]);
    }
}


    if (new_name) {
        if (!save_set_player_name(&s, new_name)) {
            fprintf(stderr, "Fehler: Name konnte nicht gesetzt werden.\n");
            save_free(&s);
            return 1;
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




    save_free(&s);
    return 0;
}

