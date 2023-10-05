/*
 * Copyright (c) 2022 Laurent Ghigonis <ooookiwi@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <strings.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <err.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <libgen.h>

#include "utils.h"

#define STA_NM_LEN 10
#define STA_NM_DEPT_LEN 3
#define STA_NM_ZONE_LEN 3
struct sta_nm {
	uint64_t nm;
	uint16_t dept; /* INSEE departement code */
	uint16_t zone;
	uint16_t id;
	const char *str;
};

#define NATURE_ID_MAX 100
struct f_nature {
	struct csv csv;
	struct nature *table[NATURE_ID_MAX];
	int count;
};

struct nature {
	int nat_id;
	char *nat_lb_nom;
};

#define SUPPORTS_ID_MAX 4 * 1000 * 1000
struct f_support {
	struct csv csv;
	struct support *table[SUPPORTS_ID_MAX];
	int count;
};

#define SUPPORT_STA_MAX 100
#define SUPPORT_DESCRIPTION_BUF_SIZE 65536
#define SUPPORT_CP_DEPT_MAX 0x99 /* departement INSEE */
struct support {
	int sup_id;
	struct sta_nm sta_nm_anfr[SUPPORT_STA_MAX];
	int nat_id;
	int lat_dms[3];
	char *lat_ns;
	int lon_dms[3];
	char *lon_ew;
	int sup_nm_haut;
	int tpo_id;
	char *adr_lb_lieu;
	char *adr_lb_add0;
	char *adr_lb_add2;
	char *adr_lb_add3;
	char *adr_nm_cp_str;
	int adr_nm_cp;
	uint32_t com_cd_insee;
	/* calculated */
	float lat;
	float lon;
	uint8_t dept;
	char dept_name[3];
	int sta_count;
};

#define PROPRIETAIRE_ID_MAX 100
struct f_proprietaire {
	struct csv csv;
	struct proprio *table[PROPRIETAIRE_ID_MAX];
	int count;
};

struct proprio {
	int tpo_id;
	char *tpo_lb;
};

#define EXPLOITANT_ID_MAX 500
struct f_exploitant {
	struct csv csv;
	struct exploitant *table[EXPLOITANT_ID_MAX];
	int count;
};

struct exploitant {
	int adm_id;
	char *adm_lb_nom;
};

/*
 * SUP_STATION.txt
 * ---------------------------
 * |        STA_NM_ANFR      |
 * |-------------------------|
 * | 0 1 2   3 4 5   6 7 8 9 |
 * |-------------------------|
 * |  dept |  zone |   id    |
 * |-------------------------|
 * |    area       |   id    |
 * ---------------------------
 * STA_NM_ANFR is mapped to sta_nm structure
 * storage is in f_station:
 * - 'dept' are indexed in a pointer table 'depts'.
 * - 'zone' are indexed in a pointer table 'zones' per 'dept'.
 * - 'id' are indexed in in a pointer table 'stations' as an array of pointers to all possible stations for a given 'dept' and 'zone'.
 */

#define STATION_EMETTEUR_MAX 500
#define STATION_ANTENNE_MAX 100
#define SYSTEMES_ID_MAX 100
struct station {
	struct sta_nm sta_nm;
	int adm_id;
	char *dem_nm_consis_str;
	struct tm dte_implemntatation;
	char *dte_implemntatation_str;
	struct tm dte_modif;
	char *dte_modif_str;
	struct tm dte_en_service;
	char *dte_en_service_str;
	/* computed */
	struct tm dte_latest; /* most recent date from the above */
	/* references */
	struct emetteur *emetteurs[STATION_EMETTEUR_MAX];
	int emetteur_count;
	int systeme_count[SYSTEMES_ID_MAX];
	struct antenne *antennes[STATION_ANTENNE_MAX];
	int antenne_count;
};

/* station id is 4 decimal digits */
#define STATION_ID_MAX 10*10*10*10
struct station_zone {
	struct station *stations[STATION_ID_MAX];
	int station_count;
};

/* zones are from sta_nm character 3 to 5, max value of 465 as of 202101 is obtained by:
 * $ cut -d';' -f1 SUP_STATION.txt |cut -c 4-6 |sort -n |tail -n1 */
#define STATION_ZONE_MAX 600
struct station_dept {
	struct station_zone *zones[STATION_ZONE_MAX];
	int zone_count;
};

/* depts are from sta_nm character 0 to 2, max value of 988 as of 20220729 is obtained by:
 * $ cut -d';' -f1 SUP_STATION.txt |cut -c 1-3 |sort -n |tail -n1 */
#define STATION_DEPT_MAX 0x999
struct f_station {
	struct csv csv;
	struct station_dept *depts[STATION_DEPT_MAX];
	int id_count;
	int station_count;
	int dept_count;
	int zone_count;
	struct tm latest; /* latest station update date */
};

/* emetteurs have an integer id, max value of 20308500 as of 20220729 obtained by:
 * $ cut -d';' -f1 SUP_EMETTEUR.txt |sort -n |tail -n1 */
#define EMETTEUR_ID_MAX 30000000
struct f_emetteur {
	struct csv csv;
	struct emetteur *table[EMETTEUR_ID_MAX];
	int count;
	char *systemes_lb[SYSTEMES_ID_MAX]; /* index for the different values of emr_lb_systeme */
	int systemes_count[SYSTEMES_ID_MAX];
	int systeme_count;
};

#define EMETTEUR_BAND_MAX 50
struct emetteur {
	int emr_id;
	char *emr_id_str;
	char *emr_lb_systeme;
	int systeme_id;
	struct sta_nm sta_nm;
	int aer_id;
	struct tm emr_dt_service;
	char *emr_dt_service_str;
	struct bande *bandes[EMETTEUR_BAND_MAX];
	int bande_count;
};

/* bandes have an integer id, max value of 45214227 as of 20220729 obtained by:
 * $ cut -d';' -f2 tmp/extract/SUP_BANDE.txt |sort -n |tail -n1 */
#define BANDE_ID_MAX 100000000
struct f_bande {
	struct csv csv;
	struct bande *table[BANDE_ID_MAX];
	int count;
};

struct bande {
	struct sta_nm sta_nm;
	int ban_id;
	int emr_id;
	uint64_t ban_nb_f_deb;
	char *ban_nb_f_deb_str;
	uint64_t ban_nb_f_fin;
	char *ban_nb_f_fin_str;
	char *ban_fg_unite;
};
/* used when computing bands per exploitant */
struct bande_tree {
	int emr_count;
	uint64_t ban_nb_f_deb;
	char *ban_nb_f_deb_str;
	uint64_t ban_nb_f_fin;
	char *ban_nb_f_fin_str;
	int systemes_count[SYSTEMES_ID_MAX];
	struct bande_tree *next; /* band with higher ban_nb_f_deb, or same ban_nb_f_deb and higher ban_nb_f_fin */
};

/* antennes have an integer id, max value of 7878184 as of 20220729 obtained by:
 * $ cut -d';' -f2 tmp/extract/SUP_ANTENNE.txt  |sort -n |tail -n1 */
#define ANTENNE_ID_MAX 10000000
#define ANTENNE_EMETTEUR_MAX 50
struct f_antenne {
	struct csv csv;
	struct antenne *table[ANTENNE_ID_MAX];
	int count;
};

struct antenne {
	struct sta_nm sta_nm;
	int aer_id;
	char *aer_id_str;
	int tae_id;
	float aer_nb_dimension;
	char *aer_nb_dimension_str;
	char *aer_fg_rayon;
	float aer_nb_azimut;
	char *aer_nb_azimut_str;
	float aer_nb_alt_bas;
	char *aer_nb_alt_bas_str;
	char *sup_id_str;
	struct emetteur *emetteurs[ANTENNE_EMETTEUR_MAX];
	int emetteur_count;
};

#define TYPE_ANTENNE_ID_MAX 150
struct f_type_antenne {
	struct csv csv;
	char *table[TYPE_ANTENNE_ID_MAX];
	int counts[TYPE_ANTENNE_ID_MAX];
	int count;
};

struct anfr_set {
	struct f_nature *natures;
	struct f_support *supports;
	struct f_proprietaire *proprietaires;
	struct f_station *stations;
	struct f_exploitant *exploitants;
	struct f_emetteur *emetteurs;
	struct f_bande *bandes;
	struct f_antenne *antennes;
	struct f_type_antenne *types_antenne;
};

#define KML_ANFR_DESCRIPTION "KML export of french emetteurs <5W based on ANFR data"

__attribute__((__noreturn__)) void usageexit(void);
/* input file processing */
struct anfr_set		*set_load(char *);
void				 set_free(struct anfr_set *);
struct f_nature		*natures_load(char *);
void				 natures_free(struct f_nature *);
const char			*nature_get_name(struct f_nature *, int);
struct f_support	*supports_load(char *);
void				 supports_free(struct f_support *);
struct f_proprietaire *proprietaires_load(char *);
void				 proprietaires_free(struct f_proprietaire *);
const char			*proprietaire_get_name(struct f_proprietaire *, int);
struct f_station	*stations_load(char *);
void				 stations_free(struct f_station *);
struct station		*station_get(struct f_station *, struct sta_nm *);
struct station		*station_get_next(struct f_station *, struct sta_nm *, int, struct station *);
int					 station_description(struct f_type_antenne *, struct station *, char *);
int					 station_systemes(struct f_emetteur *, struct station *, char *);
struct f_exploitant	*exploitants_load(char *);
void				 exploitants_free(struct f_exploitant *);
const char			*exploitant_get_name(struct f_exploitant *, int);
struct f_emetteur	*emetteurs_load(char *, struct f_station *, struct f_antenne *);
void				 emetteurs_free(struct f_emetteur *);
const char *		 emetteurs_stats(struct f_emetteur *);
struct emetteur		*emetteur_get(struct f_emetteur *, int);
struct emetteur		*emetteur_get_next(struct emetteur **, int, struct emetteur *);
struct f_bande		*bandes_load(char *, struct f_emetteur *);
void				 bandes_free(struct f_bande *);
struct f_antenne	*antennes_load(char *, struct f_station *);
void				 antennes_free(struct f_antenne *);
struct antenne		*antenne_get_next(struct antenne **, int, struct antenne *);
struct f_type_antenne *types_antenne_load(char *);
void				 types_antenne_free(struct f_type_antenne *);
char				*type_antenne_get(struct f_type_antenne *, int);
/* output file */
void				 output_kml(struct anfr_set *, const char *, const char *);
void				 output_bands(struct anfr_set *, const char *, const char *);
/* utils */
void		 csv_stanm(struct csv *, struct sta_nm *);

struct conf conf;

__attribute__((__noreturn__)) void
usageexit()
{
	printf("usage: antennes [-Cv] [-b <dir>] [-k <dir>] <data_dir>\n");
	printf("Query and export KML files from ANFR radio sites public data\n");
	printf("-b <dir> export csv bands statistics to this directory\n");
	printf("-C       do not set any kml placemark colors\n");
	printf("-k <dir> export kml files to this directory\n");
	printf("-s       display antennes statistics\n");
	printf("-v       verbose logging\n");
	printf("if neither -s or -k are specified, this program only loads the data.\n");
	printf("output kml files hierarchy:\n");
	printf("   anfr_proprietaires.kml : all supports in a single file, one section per proprietaire\n");
	printf("   anfr_departements.kml : all supports in a single file, one section per departement\n");
	printf("   anfr_departements_light.kml : all supports in a single file, one section per departement, no description\n");
	printf("   anfr_proprietaire/anfr_proprietaire_<proprietaire-id>_<proprietaire-name>.kml : one file per proprietaire\n");
	printf("   anfr_departement/anfr_departement_<dept-id>.kml : one file per departement\n");
	printf("   anfr_systeme/anfr_systeme_<sys-name>.kml : one file per systeme, one section per departement\n");
	printf("kml placemark colors:\n");
	printf("   orange for supports with stations updated in less than 3 months, red for 1 month, blue otherwise\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct anfr_set *set;
	int ch, stats = 0;
	char *kml_export = NULL, *bands_export = NULL;
	time_t now;

	bzero(&conf, sizeof(conf));
	while ((ch = getopt(argc, argv, "b:Chk:sv")) != -1) {
		switch (ch) {
			case 'b':
				bands_export = optarg;
				break;
			case 'C':
				conf.no_color = 1;
				break;
			case 'k':
				kml_export = optarg;
				break;
			case 's':
				stats = 1;
				break;
			case 'v':
				conf.verbose = 1;
				break;
			default:
				usageexit();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1)
		usageexit();

	time(&now);
	gmtime_r(&now, &conf.now);
	strftime(conf.now_str, sizeof(conf.now_str), "%Y-%m-%d", &conf.now);

	info("[+] loading files from %s\n", argv[0]);
	if (stats)
		printf("file name : %s\n\n", basename(argv[0]));
	set = set_load(argv[0]);

	if (stats) {
		info("[*] displaying statistics\n");
		printf("\nemetteurs systemes count:\n%s", emetteurs_stats(set->emetteurs));
	}

	if (kml_export) {
		info("[*] exporting kml to %s\n", kml_export);
		output_kml(set, kml_export, basename(argv[0]));
	}

	if (bands_export) {
		info("[*] exporting bands usage to %s\n", bands_export);
		output_bands(set, bands_export, basename(argv[0]));
	}

#ifdef DEBUG
	info("[*] freeing ressources\n");
	set_free(set);
#endif

	if (conf.warn_incoherent_data > 0)
		printf("incoherent data warnings: %d\n", conf.warn_incoherent_data);

	return 0;
}

struct anfr_set *
set_load(char *path)
{
	struct anfr_set *set = xmalloc_zero(sizeof(struct anfr_set));
	char dir[PATH_MAX];

	snprintf(dir, sizeof(dir), "%s/SUP_NATURE.txt", path);
	set->natures = natures_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_SUPPORT.txt", path);
	set->supports = supports_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_PROPRIETAIRE.txt", path);
	set->proprietaires = proprietaires_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_STATION.txt", path);
	set->stations = stations_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_EXPLOITANT.txt", path);
	set->exploitants = exploitants_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_ANTENNE.txt", path);
	set->antennes = antennes_load(dir, set->stations);
	snprintf(dir, sizeof(dir), "%s/SUP_TYPE_ANTENNE.txt", path);
	set->types_antenne = types_antenne_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_EMETTEUR.txt", path);
	set->emetteurs = emetteurs_load(dir, set->stations, set->antennes);
	snprintf(dir, sizeof(dir), "%s/SUP_BANDE.txt", path);
	set->bandes = bandes_load(dir, set->emetteurs);

	return set;
}

void
set_free(struct anfr_set *set)
{
	bandes_free(set->bandes);
	emetteurs_free(set->emetteurs);
	types_antenne_free(set->types_antenne);
	antennes_free(set->antennes);
	exploitants_free(set->exploitants);
	stations_free(set->stations);
	proprietaires_free(set->proprietaires);
	supports_free(set->supports);
	natures_free(set->natures);
}

struct f_nature *
natures_load(char *path)
{
	struct f_nature *natures;
	struct csv *csv;
	struct nature *nature;
	int nat_id;

	natures = xmalloc_zero(sizeof(struct f_nature));
	csv = &natures->csv;
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &nat_id, NULL);
		if (nat_id == 999999999) /* Support non décrit */
			continue;
		if (nat_id >= NATURE_ID_MAX)
			errx(1, "nature id %d too big", nat_id);

		nature = malloc(sizeof(struct nature));
		nature->nat_id = nat_id;
		csv_str(csv, &nature->nat_lb_nom);
		natures->table[nature->nat_id] = nature;
		natures->count++;
	}
	printf("%d natures of support\n", natures->count);

	return natures;
}

void
natures_free(struct f_nature *natures)
{
	int n;

	for (n=0; n<NATURE_ID_MAX; n++)
		if (natures->table[n])
			free(natures->table[n]);
	csv_close(&natures->csv);
	free(natures);
}

const char *
nature_get_name(struct f_nature *f, int id)
{
	if (id > NATURE_ID_MAX || !f->table[id])
		return "Support non décrit";
	return f->table[id]->nat_lb_nom;
}

struct f_support *
supports_load(char *path)
{
	struct f_support *supports;
	struct csv *csv;
	struct support *sup;
	int sup_id;

	supports = xmalloc_zero(sizeof(struct f_support));
	csv = &supports->csv;

	csv_open(csv, path, CSV_NORMAL, ';', 0);
	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &sup_id, NULL);
		if (sup_id >= SUPPORTS_ID_MAX)
			errx(1, "invalid support id, too big: %d", sup_id);
		if (!supports->table[sup_id]) {
			verb("new support %d\n", sup_id);
			sup = malloc(sizeof(struct support));
			sup->sup_id = sup_id;
			csv_stanm(csv, &sup->sta_nm_anfr[0]);
			csv_int(csv, &sup->nat_id, NULL);
			csv_int(csv, &sup->lat_dms[0], NULL);
			csv_int(csv, &sup->lat_dms[1], NULL);
			csv_int(csv, &sup->lat_dms[2], NULL);
			csv_str(csv, &sup->lat_ns);
			csv_int(csv, &sup->lon_dms[0], NULL);
			csv_int(csv, &sup->lon_dms[1], NULL);
			csv_int(csv, &sup->lon_dms[2], NULL);
			csv_str(csv, &sup->lon_ew);
			csv_int(csv, &sup->sup_nm_haut, NULL);
			csv_int(csv, &sup->tpo_id, NULL);
			csv_str(csv, &sup->adr_lb_lieu);
			csv_str(csv, &sup->adr_lb_add0);
			csv_str(csv, &sup->adr_lb_add2);
			csv_str(csv, &sup->adr_lb_add3);
			csv_int(csv, &sup->adr_nm_cp, &sup->adr_nm_cp_str);
			csv_int16(csv, &sup->com_cd_insee, NULL);
			coord_dms_to_dd(sup->lat_dms, sup->lat_ns, sup->lon_dms, sup->lon_ew, &sup->lat, &sup->lon);
			sup->dept = sup->com_cd_insee >> 12;
			snprintf(sup->dept_name, sizeof(sup->dept_name), "%02X", sup->dept);
			sup->sta_count = 0;
			supports->table[sup->sup_id] = sup;
			supports->count++;
		} else {
			verb("existing support %d\n", sup_id);
			sup = supports->table[sup_id];
			if (sup->sta_count == SUPPORT_STA_MAX)
				errx(1, "maximum stations %d reached for support %d", SUPPORT_STA_MAX, sup->sup_id);
			csv_stanm(csv, &sup->sta_nm_anfr[sup->sta_count]);
		}
		sup->sta_count++;
		verb("%d: tpo=%d lieu='%s' add0='%s' cp=%d insee=%x\n", sup->sup_id, sup->tpo_id, sup->adr_lb_lieu, sup->adr_lb_add0, sup->adr_nm_cp, sup->com_cd_insee);
	}
	printf("%d supports\n", supports->count);

	return supports;
}

void
supports_free(struct f_support *supports)
{
	int n;

	for (n=0; n<SUPPORTS_ID_MAX; n++)
		if (supports->table[n])
			free(supports->table[n]);
	csv_close(&supports->csv);
	free(supports);
}

struct f_proprietaire *
proprietaires_load(char *path)
{
	struct f_proprietaire *proprietaires;
	struct csv *csv;
	struct proprio *proprio;
	int tpo_id;

	proprietaires = xmalloc_zero(sizeof(struct f_proprietaire));
	csv = &proprietaires->csv;
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &tpo_id, NULL);
		if (tpo_id >= PROPRIETAIRE_ID_MAX)
			errx(1, "line %d: invalid proprietaire id %d too big", csv->line_count, tpo_id);
		if (proprietaires->table[tpo_id]) {
			warn_incoherent_data("line %d: proprietaire %d already exists, ignoring\n", csv->line_count, tpo_id);
			continue;
		}
		proprio = malloc(sizeof(struct proprio));
		proprio->tpo_id = tpo_id;
		csv_str(csv, &proprio->tpo_lb);
		verb("proprietaire id %d : %s\n", tpo_id, proprio->tpo_lb);
		proprietaires->table[proprio->tpo_id] = proprio;
		proprietaires->count++;
	}
	printf("%d proprietaires\n", proprietaires->count);

	return proprietaires;
}

void
proprietaires_free(struct f_proprietaire *proprietaires)
{
	int n;

	for (n=0; n<PROPRIETAIRE_ID_MAX; n++)
		if (proprietaires->table[n])
			free(proprietaires->table[n]);
	csv_close(&proprietaires->csv);
	free(proprietaires);
}

const char *
proprietaire_get_name(struct f_proprietaire *f, int proprio)
{
	if (proprio > PROPRIETAIRE_ID_MAX)
		return "invalid id";
	if (!f->table[proprio])
		return "unknown";
	return f->table[proprio]->tpo_lb;
}

struct f_station *
stations_load(char *path)
{
	struct f_station *stations;
	struct csv *csv;
	struct station_dept *dept;
	struct station_zone *zone;
	struct station *sta;
	struct tm *dte_latest;

	stations = xmalloc_zero(sizeof(struct f_station));
	csv = &stations->csv;
	csv_open(csv, path, CSV_NORMAL, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */

		sta = malloc(sizeof(struct station));
		csv_stanm(csv, &sta->sta_nm);
		csv_int(csv, &sta->adm_id, NULL);
		csv_int(csv, NULL, &sta->dem_nm_consis_str);
		csv_date(csv, &sta->dte_implemntatation, &sta->dte_implemntatation_str);
		csv_date(csv, &sta->dte_modif, &sta->dte_modif_str);
		csv_date(csv, &sta->dte_en_service, &sta->dte_en_service_str);
		sta->emetteur_count = 0;
		bzero(sta->systeme_count, sizeof(sta->systeme_count));
		sta->antenne_count = 0;

		/* set station most recent date */
		if (tm_diff(&sta->dte_implemntatation, &sta->dte_modif) > 0) {
			if (tm_diff(&sta->dte_implemntatation, &sta->dte_en_service) > 0)
				dte_latest = &sta->dte_implemntatation;
			else
				dte_latest = &sta->dte_en_service;
		} else {
			if (tm_diff(&sta->dte_modif, &sta->dte_en_service))
				dte_latest = &sta->dte_modif;
			else
				dte_latest = &sta->dte_en_service;
		}
		memcpy(&sta->dte_latest, dte_latest, sizeof(struct tm));

		/* update latest station date, except if date is more recent than now (incoherent data) */
		if (tm_diff(&conf.now, dte_latest) > 0 && tm_diff(dte_latest, &stations->latest) > 0)
			memcpy(&stations->latest, dte_latest, sizeof(struct tm));

		/* insert the station in maching zone of departement */
		if (!stations->depts[sta->sta_nm.dept]) {
			stations->depts[sta->sta_nm.dept] = xmalloc_zero(sizeof(struct station_dept));
			stations->dept_count++;
		}
		dept = stations->depts[sta->sta_nm.dept];
		if (!dept->zones[sta->sta_nm.zone]) {
			dept->zones[sta->sta_nm.zone] = xmalloc_zero(sizeof(struct station_zone));
			dept->zone_count++;
			stations->zone_count++;
		}
		zone = dept->zones[sta->sta_nm.zone];
		if (zone->stations[sta->sta_nm.id]) {
			warn_incoherent_data("line %d: station %s already exists, ignoring", csv->line_count, sta->sta_nm.str);
			free(sta);
			continue;
		}
		zone->stations[sta->sta_nm.id] = sta;
		stations->station_count++;
		zone->station_count++;
	}
	printf("%d stations in %d departement and %d zones\n", stations->station_count, stations->dept_count, stations->zone_count);

	return stations;
}

void
stations_free(struct f_station *stations)
{
	int ndept, nzone, id;
	struct station_dept *dept;
	struct station_zone *zone;
	struct station *sta;
	int freed_stations = 0;

	for (ndept=0; ndept < STATION_DEPT_MAX; ndept++) {
		dept = stations->depts[ndept];
		if (!dept)
			continue;
		for (nzone=0; nzone < STATION_ZONE_MAX; nzone++) {
			zone = dept->zones[nzone];
			if (!zone)
				continue;
			for (id=0; id < STATION_ID_MAX; id++) {
				sta = zone->stations[id];
				if (!sta)
					continue;
				free(sta);
				freed_stations++;
			}
			free(zone);
		}
		free(dept);
	}
	if (stations->station_count != freed_stations)
		warnx("freed_stations %d != station_count %d", freed_stations, stations->station_count);
	csv_close(&stations->csv);
	free(stations);
}

struct station *
station_get(struct f_station *stations, struct sta_nm *nm)
{
	if (!stations->depts[nm->dept])
		errx(1, "station_get: departement %x not found for station %s", nm->dept, nm->str);
	if (!stations->depts[nm->dept]->zones[nm->zone]) {
		warn_incoherent_data("zone %d not found in departement %x when looking for station %s", nm->zone, nm->dept, nm->str);
		return NULL;
	}
	return stations->depts[nm->dept]->zones[nm->zone]->stations[nm->id];
}

/* returns the next recently modified or en service station older than 'last' in 'table' sta_nm index
 * in case of equality, station number is compared */
struct station *
station_get_next(struct f_station *stations, struct sta_nm *table, int count, struct station *last)
{
	struct station *next = NULL, *sta;
	int n, tmdiff1_last, tmdiff1_next, tmdiff2_last, tmdiff2_next;
	struct tm *sta_date, *cmp_date;

	for (n=0; n<count; n++) {
		sta = station_get(stations, &table[n]);
		if (!sta) {
			warn_incoherent_data("station %s not found, ignoring", table[n].str);
			continue;
		}
		tmdiff1_last = 0;
		tmdiff1_next = 0;
		tmdiff2_last = 0;
		tmdiff2_next = 0;
		sta_date = (sta->dte_modif_str[0]) ? &sta->dte_modif : &sta->dte_en_service;

		if (last) {
			cmp_date = (last->dte_modif_str[0]) ? &last->dte_modif : &last->dte_en_service;
			tmdiff1_last = tm_diff(sta_date, cmp_date);
			tmdiff2_last = tm_diff(&sta->dte_en_service, &last->dte_en_service);
		}
		if (next) {
			cmp_date = (next->dte_modif_str[0]) ? &next->dte_modif : &next->dte_en_service;
			tmdiff1_next = tm_diff(sta_date, cmp_date);
			tmdiff2_next = tm_diff(&sta->dte_en_service, &next->dte_en_service);
		}
		if ( (!last || tmdiff1_last < 0 || (tmdiff1_last == 0 && (tmdiff2_last < 0 || (tmdiff2_last == 0 && sta->sta_nm.nm < last->sta_nm.nm))))
				&& (!next || tmdiff1_next > 0 || (tmdiff1_next == 0 && (tmdiff2_next > 0 || (tmdiff2_next == 0 && sta->sta_nm.nm > next->sta_nm.nm)))) )
			next = sta;
	}

	return next;
}

/* appends a text description of a station to 'desc'
 * we use strbuf_*() macros to avoid poor performance of sprintf, as this function loops a lot during kml file generation */
int
station_description(struct f_type_antenne *types_antenne, struct station *sta, char *desc)
{
	char *desc_start = desc;
	struct emetteur *emr;
	struct bande *ban;
	struct antenne *aer;
	int n, b, e;

	/* summary */
	strbuf_str(desc, "    implementation: ");
	strbuf_str(desc, sta->dte_implemntatation_str);
	strbuf_str(desc, "\n    modification: ");
	strbuf_str(desc, sta->dte_modif_str);
	strbuf_str(desc, "\n    en service: ");
	strbuf_str(desc, sta->dte_en_service_str);
	strbuf_chr(desc, '\n');
	strbuf_int(desc, sta->emetteur_count);
	strbuf_str(desc, " emetteur");
	if (sta->emetteur_count > 1)
		strbuf_chr(desc, 's');
	strbuf_chr(desc, '\n');

	/* emetteurs list */
	emr = NULL;
	for (n=0; n<sta->emetteur_count; n++) {
		emr = emetteur_get_next(sta->emetteurs, sta->emetteur_count, emr);
		strbuf_chr(desc, '>');
		strbuf_int(desc, n+1);
		strbuf_chr(desc, ' ');
		strbuf_str(desc, emr->emr_id_str);
		strbuf_chr(desc, ' ');
		strbuf_str(desc, emr->emr_lb_systeme);
		strbuf_chr(desc, ' ');
		strbuf_str(desc, emr->emr_dt_service_str);
		strbuf_chr(desc, ' ');
		for (b=0; b<emr->bande_count; b++) {
			ban = emr->bandes[b];
			strbuf_str(desc, ban->ban_nb_f_deb_str);
			strbuf_chr(desc, '-');
			strbuf_str(desc, ban->ban_nb_f_fin_str);
			strbuf_str(desc, ban->ban_fg_unite);
			strbuf_chr(desc, ' ');
		}
		strbuf_chr(desc, '\n');
	}

	/* antennes list */
	strbuf_int(desc, sta->antenne_count);
	strbuf_str(desc, " antenne");
	if (sta->antenne_count > 1)
		strbuf_chr(desc, 's');
	strbuf_chr(desc, '\n');
	aer = NULL;
	for (n=0; n<sta->antenne_count; n++) {
		aer = antenne_get_next(sta->antennes, sta->antenne_count, aer);
		strbuf_chr(desc, '>');
		strbuf_int(desc, n+1);
		strbuf_chr(desc, ' ');
		strbuf_str(desc, aer->aer_id_str);
		strbuf_chr(desc, ' ');
		strbuf_str(desc, type_antenne_get(types_antenne, aer->tae_id));
		strbuf_chr(desc, ' ');
		strbuf_str(desc, aer->aer_nb_dimension_str);
		strbuf_chr(desc, 'm');
		if (aer->aer_fg_rayon[0] == 'D')
			strbuf_str(desc, " Directional ");
		else if (aer->aer_fg_rayon[0] == 'N')
			strbuf_str(desc, " Omnidirectional ");
		strbuf_str(desc, aer->aer_nb_azimut_str);
		strbuf_chr(desc, 'd');
		strbuf_chr(desc, ' ');
		strbuf_chr(desc, '+');
		strbuf_str(desc, aer->aer_nb_alt_bas_str);
		strbuf_chr(desc, '\n');
		if (aer->emetteur_count > 0) {
			emr = NULL;
			strbuf_str(desc, "    ");
			for (e=0; e<aer->emetteur_count; e++) {
				if (emr) {
					strbuf_chr(desc, ',');
					strbuf_chr(desc, ' ');
				}
				emr = aer->emetteurs[e];
				strbuf_str(desc, emr->emr_id_str);
				strbuf_chr(desc, ' ');
				strbuf_str(desc, emr->emr_lb_systeme);
			}
			strbuf_chr(desc, '\n');
		}
	}

	return desc - desc_start;
}

/* appends a string of all emetteur systemes present on a station to description, sorted by systemes count */
int
station_systemes(struct f_emetteur *emetteurs, struct station *sta, char *desc)
{
	char *desc_start = desc;
	int id = 0, count = 0;
	char *lb = NULL;

	while ( (count = next_smallest_positive_int(sta->systeme_count, SYSTEMES_ID_MAX, count, id, &id)) > 0 ) {
		if (lb) {
			strbuf_chr(desc, ',');
			strbuf_chr(desc, ' ');
		}
		lb = emetteurs->systemes_lb[id];
		strbuf_str(desc, lb);
		strbuf_chr(desc, ' ');
		strbuf_chr(desc, '(');
		strbuf_int(desc, count);
		strbuf_chr(desc, ')');
	}
	strbuf_chr(desc, '\n');

	return desc - desc_start;
}

struct f_exploitant *
exploitants_load(char *path)
{
	struct f_exploitant *exploitants;
	struct csv *csv;
	struct exploitant *exploitant;
	int adm_id;

	exploitants = xmalloc_zero(sizeof(struct f_exploitant));
	csv = &exploitants->csv;
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &adm_id, NULL);
		if (adm_id >= EXPLOITANT_ID_MAX)
			errx(1, "invalid exploitant id, too big (%d) at line %d", adm_id, csv->line_count);
		if (exploitants->table[adm_id]) {
			warn_incoherent_data("line %d: exploitant %d already exists, ignoring\n", csv->line_count, adm_id);
			continue;
		}

		exploitant = malloc(sizeof(struct exploitant));
		exploitant->adm_id = adm_id;
		csv_str(csv, &exploitant->adm_lb_nom);
		verb("exploitant id %d : %s\n", adm_id, exploitant->adm_lb_nom);
		exploitants->table[exploitant->adm_id] = exploitant;
		exploitants->count++;
	}
	printf("%d exploitants\n", exploitants->count);

	return exploitants;
}

void
exploitants_free(struct f_exploitant *exploitants)
{
	int n;

	for (n=0; n<EXPLOITANT_ID_MAX; n++)
		if (exploitants->table[n])
			free(exploitants->table[n]);
	csv_close(&exploitants->csv);
	free(exploitants);
}

const char *
exploitant_get_name(struct f_exploitant *f, int adm_id)
{
	if (adm_id < 0 || adm_id > EXPLOITANT_ID_MAX)
		errx(1, "invalid exploitant id %d", adm_id);
	if (!f->table[adm_id])
		return "unknown";
	return f->table[adm_id]->adm_lb_nom;
}

struct f_emetteur *
emetteurs_load(char *path, struct f_station *stations, struct f_antenne *antennes)
{
	struct f_emetteur *emetteurs;
	struct csv *csv;
	struct emetteur *emr;
	int emr_id, sys_id;
	char *emr_id_str;
	struct station *sta;
	struct antenne *aer;

	emetteurs = xmalloc_zero(sizeof(struct f_emetteur));
	csv = &emetteurs->csv;
	csv_open(csv, path, CSV_NORMAL, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &emr_id, &emr_id_str);
		if (emr_id >= EMETTEUR_ID_MAX)
			errx(1, "emetteur id too big: %d", emr_id);
		if (emetteurs->table[emr_id]) {
			warn_incoherent_data("line %d: emetteur %d already exists, ignoring", csv->line_count, emr_id);
			continue;
		}

		emr = malloc(sizeof(struct emetteur));
		emr->emr_id = emr_id;
		emr->emr_id_str = emr_id_str;
		csv_str(csv, &emr->emr_lb_systeme);
		csv_stanm(csv, &emr->sta_nm);
		csv_int(csv, &emr->aer_id, NULL);
		csv_date(csv, NULL, &emr->emr_dt_service_str);
		emr->bande_count = 0;

		/* lookup the systeme of this emetteur and link it to emetteur */
		for (sys_id=0; sys_id<emetteurs->systeme_count; sys_id++)
			if (!strcmp(emr->emr_lb_systeme, emetteurs->systemes_lb[sys_id]))
				break;
		if (sys_id == emetteurs->systeme_count) {
			/* add a new systeme */
			if (sys_id >= SYSTEMES_ID_MAX)
				errx(1, "exceeded system id %d", sys_id);
			emetteurs->systemes_lb[emetteurs->systeme_count] = emr->emr_lb_systeme;
			emetteurs->systeme_count++;
		}
		emr->systeme_id = sys_id;
		emetteurs->systemes_count[sys_id]++;

		/* update related station */
		sta = station_get(stations, &emr->sta_nm);
		if (!sta) {
			warn_incoherent_data("station %s not found for emetteur %d, ignoring", emr->sta_nm.str, emr_id);
			free(emr);
			continue;
		}
		if (sta->emetteur_count == STATION_EMETTEUR_MAX)
			errx(1, "maximum emetteur count %d reached for station %s", STATION_EMETTEUR_MAX, sta->sta_nm.str);
		sta->emetteurs[sta->emetteur_count] = emr;
		sta->emetteur_count++;
		sta->systeme_count[sys_id]++;

		/* link to antenne */
		aer = antennes->table[emr->aer_id];
		if (aer) {
			if (aer->emetteur_count == ANTENNE_EMETTEUR_MAX)
				errx(1, "maximum number of emetteurs %d reached for antenne %d", ANTENNE_EMETTEUR_MAX, aer->aer_id);
			aer->emetteurs[aer->emetteur_count] = emr;
			aer->emetteur_count++;
		} else
			warn_incoherent_data("emetteur %d refers to non-existing antenne %d", emr_id, emr->aer_id);

		emetteurs->table[emr->emr_id] = emr;
		emetteurs->count++;
	}
	printf("%d emetteurs and %d systemes\n", emetteurs->count, emetteurs->systeme_count);

	return emetteurs;
}

void
emetteurs_free(struct f_emetteur *emetteurs)
{
	int n;

	for (n=0; n<EMETTEUR_ID_MAX; n++)
		if (emetteurs->table[n])
			free(emetteurs->table[n]);
	csv_close(&emetteurs->csv);
	free(emetteurs);
}

const char *
emetteurs_stats(struct f_emetteur *emetteurs)
{
	static char buf[2048];
	char buf2[1024];
	int sys_id = 0, count = 0;

	buf[0] = '\0';
	while ( (count = next_smallest_positive_int(emetteurs->systemes_count, SYSTEMES_ID_MAX, count, sys_id, &sys_id)) > 0 ) {
		snprintf(buf2, sizeof(buf2), "%6d %s\n", count, emetteurs->systemes_lb[sys_id]);
		strcat(buf, buf2);
	}

	return buf;
}

struct emetteur *
emetteur_get(struct f_emetteur *emetteurs, int emr_id)
{
	if (emr_id > EMETTEUR_ID_MAX)
		errx(1, "too big emetteur id %d", emr_id);

	return emetteurs->table[emr_id];
}

/* returns the next emetteur with smallest id larger than 'last' in 'table' */
struct emetteur *
emetteur_get_next(struct emetteur **table, int count, struct emetteur *last)
{
	struct emetteur *next = NULL, *emr;
	int n;

	for (n=0; n<count; n++) {
		emr = table[n];
		if ( (!last || emr->emr_id < last->emr_id)
				&& (!next || emr->emr_id > next->emr_id) )
			next = emr;
	}

	return next;
}

struct f_bande *
bandes_load(char *path, struct f_emetteur *emetteurs)
{
	struct f_bande *bandes;
	struct csv *csv;
	struct bande *ban;
	struct sta_nm sta_nm;
	struct emetteur *emr;
	int ban_id;
	double deb, fin;

	bandes = xmalloc_zero(sizeof(struct f_bande));
	csv = &bandes->csv;
	csv_open(csv, path, CSV_NORMAL, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_stanm(csv, &sta_nm);
		csv_int(csv, &ban_id, NULL);
		if (ban_id >= BANDE_ID_MAX)
			errx(1, "bande id too big: %d", ban_id);

		ban = malloc(sizeof(struct bande));
		memcpy(&ban->sta_nm, &sta_nm, sizeof(sta_nm));
		ban->ban_id = ban_id;
		csv_int(csv, &ban->emr_id, NULL);
		csv_float(csv, &deb, &ban->ban_nb_f_deb_str);
		csv_float(csv, &fin, &ban->ban_nb_f_fin_str);
		csv_str(csv, &ban->ban_fg_unite);
		if (ban->ban_fg_unite) {
			switch (ban->ban_fg_unite[0]) {
			case 'K':
				ban->ban_nb_f_deb = deb * 1000;
				ban->ban_nb_f_fin = fin * 1000;
				break;
			case 'M':
				ban->ban_nb_f_deb = deb * 1000000;
				ban->ban_nb_f_fin = fin * 1000000;
				break;
			case 'G':
				ban->ban_nb_f_deb = deb * 1000000000;
				ban->ban_nb_f_fin = fin * 1000000000;
				break;
			}
		}

		emr = emetteur_get(emetteurs, ban->emr_id);
		if (!emr) {
			warn_incoherent_data("emetteur %d not found for bande %d, ignoring", ban->emr_id, ban_id);
			free(ban);
			continue;
		}
		if (emr->bande_count == EMETTEUR_BAND_MAX)
			errx(1, "maximum band count %d reached for emetteur %d", EMETTEUR_BAND_MAX, emr->emr_id);
		emr->bandes[emr->bande_count] = ban;
		emr->bande_count++;

		bandes->table[ban_id] = ban;
		bandes->count++;
	}
	printf("%d bandes\n", bandes->count);

	return bandes;
}

void
bandes_free(struct f_bande *bandes)
{
	int n;

	for (n=0; n<BANDE_ID_MAX; n++)
		if (bandes->table[n])
			free(bandes->table[n]);
	csv_close(&bandes->csv);
	free(bandes);
}

struct f_antenne *
antennes_load(char *path, struct f_station *stations)
{
	struct f_antenne *antennes;
	struct csv *csv;
	struct antenne *aer;
	struct station *sta;
	struct sta_nm sta_nm;
	int aer_id;
	char *aer_id_str;

	antennes = xmalloc_zero(sizeof(struct f_antenne));
	csv = &antennes->csv;
	csv_open(csv, path, CSV_NORMAL, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_stanm(csv, &sta_nm);
		csv_int(csv, &aer_id, &aer_id_str);
		if (aer_id >= ANTENNE_ID_MAX)
			errx(1, "antenne id too big: %d", aer_id);

		if (antennes->table[aer_id]) {
			aer = antennes->table[aer_id];
		} else {
			aer = malloc(sizeof(struct antenne));
			memcpy(&aer->sta_nm, &sta_nm, sizeof(sta_nm));
			aer->aer_id = aer_id;
			aer->aer_id_str = aer_id_str;
			csv_int(csv, &aer->tae_id, NULL);
			csv_str(csv, &aer->aer_nb_dimension_str); // we may need csv_float() in the future
			csv_str(csv, &aer->aer_fg_rayon);
			csv_str(csv, &aer->aer_nb_azimut_str); // we may need csv_float() in the future
			csv_str(csv, &aer->aer_nb_alt_bas_str); // we may need csv_float() in the future
			csv_int(csv, NULL, &aer->sup_id_str);
			aer->emetteur_count = 0;
		}

		/* update related station counters */
		sta = station_get(stations, &sta_nm);
		if (!sta) {
			warn_incoherent_data("station %s not found for antenne %d, ignoring", sta_nm.str, aer_id);
			if (!antennes->table[aer_id])
				free(aer); /* free only if it is new antenne, not attached to other stations */
			continue;
		}
		if (sta->antenne_count == STATION_ANTENNE_MAX)
			errx(1, "maximum antenne count %d reached for station %s", STATION_ANTENNE_MAX, sta_nm.str);
		sta->antennes[sta->antenne_count] = aer;
		sta->antenne_count++;

		if (!antennes->table[aer_id]) {
			/* multiple antennes with same ID is allowed, we store it once for reference */
			antennes->table[aer_id] = aer;
			antennes->count++;
		}
	}
	printf("%d antennes\n", antennes->count);

	return antennes;
}

void
antennes_free(struct f_antenne *antennes)
{
	int n, freed_antennes = 0;

	for (n=0; n<ANTENNE_ID_MAX; n++) {
		if (antennes->table[n]) {
			free(antennes->table[n]);
			freed_antennes++;
		}
	}
	if (antennes->count != freed_antennes)
		warnx("freed_antennes %d != antennes count %d", freed_antennes, antennes->count);
	csv_close(&antennes->csv);
	free(antennes);
}

/* returns the next antenne with smallest id larger than 'last' in 'table' */
struct antenne *
antenne_get_next(struct antenne **table, int count, struct antenne *last)
{
	struct antenne *next = NULL, *aer;
	int n;

	for (n=0; n<count; n++) {
		aer = table[n];
		if ( (!last || aer->aer_id < last->aer_id)
				&& (!next || aer->aer_id > next->aer_id) )
			next = aer;
	}

	return next;
}

struct f_type_antenne *
types_antenne_load(char *path)
{
	struct f_type_antenne *types_antenne;
	struct csv *csv;
	int tae_id;

	types_antenne = xmalloc_zero(sizeof(struct f_type_antenne));
	csv = &types_antenne->csv;
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859, ';', 0);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &tae_id, NULL);
		if (tae_id == 999999999)
			tae_id = TYPE_ANTENNE_ID_MAX-1; /* Aérien issu de reprise des données électroniques */
		if (tae_id >= TYPE_ANTENNE_ID_MAX)
			errx(1, "type antenne id too big: %d", tae_id);
		csv_str(csv, &types_antenne->table[tae_id]);
		types_antenne->count++;
	}
	printf("%d types of antenne\n", types_antenne->count);

	return types_antenne;
}

void
types_antenne_free(struct f_type_antenne *types_antenne)
{
	csv_close(&types_antenne->csv);
	free(types_antenne);
}

char *
type_antenne_get(struct f_type_antenne *types, int tae_id)
{
	if (tae_id == 999999999)
		tae_id = TYPE_ANTENNE_ID_MAX-1; /* Aérien issu de reprise des données électroniques */
	if (!types->table[tae_id])
		errx(1, "type antenne %d does not exist", tae_id);
	return types->table[tae_id];
}

void
output_kml(struct anfr_set *set, const char *output_dir, const char *source_name)
{
	int idx, sup_count, n, e, len_stalist, len_desc, kml_count, style, diff;
	int sup_systeme_ids[SYSTEMES_ID_MAX];
	struct support *sup;
	char desc[SUPPORT_DESCRIPTION_BUF_SIZE], stalist[SUPPORT_DESCRIPTION_BUF_SIZE];
	char path[PATH_MAX], buf[1024], buf2[128], expllist[4096];
	struct kml *kmls_tpo[PROPRIETAIRE_ID_MAX];
	struct kml *kmls_dept[SUPPORT_CP_DEPT_MAX];
	struct kml *kmls_sys[SYSTEMES_ID_MAX];
	struct kml *ka_tpo, *ka_dept, *ka_dept_light, *k_tpo, *k_dept, *k_sys;
	const char *tpo_name, *exploitant_name;
	struct stat fstat;
	struct station *sta;
	struct emetteur *emr;
	struct tm *ts_begin;

	if (stat(output_dir, &fstat) == -1)
		mkdir(output_dir, 0755);
	snprintf(path, sizeof(path), "%s/anfr_proprietaire", output_dir);
	if (stat(path, &fstat) == -1)
		mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/anfr_departement", output_dir);
	if (stat(path, &fstat) == -1)
		mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/anfr_systeme", output_dir);
	if (stat(path, &fstat) == -1)
		mkdir(path, 0755);
	bzero(kmls_tpo, sizeof(kmls_tpo));
	bzero(kmls_dept, sizeof(kmls_dept));
	bzero(kmls_sys, sizeof(kmls_sys));

	/* open the main kml files */
	snprintf(path, sizeof(path), "%s/anfr_proprietaires.kml", output_dir);
	snprintf(buf, sizeof(buf), "ANFR antennes %s per proprietaire", source_name);
	ka_tpo = kml_open(path, buf, KML_ANFR_DESCRIPTION);
	snprintf(path, sizeof(path), "%s/anfr_departements.kml", output_dir);
	snprintf(buf, sizeof(buf), "ANFR antennes %s per departement", source_name);
	ka_dept = kml_open(path, buf, KML_ANFR_DESCRIPTION);
	snprintf(path, sizeof(path), "%s/anfr_departements_light.kml", output_dir);
	snprintf(buf, sizeof(buf), "ANFR antennes %s per departement (light)", source_name);
	ka_dept_light = kml_open(path, buf, KML_ANFR_DESCRIPTION);
	kml_count = 3;

	/* iterate over supports and append to aggregated and per-proprietaire kml files */
	for (idx=0, sup_count=0;
			idx < SUPPORTS_ID_MAX && sup_count < set->supports->count;
			idx++) {
		sup = set->supports->table[idx];
		if (!sup)
			continue;
		sup_count++;
		tpo_name = proprietaire_get_name(set->proprietaires, sup->tpo_id);

		/* find kml file matching the proprietaire */
		if (!kmls_tpo[sup->tpo_id]) {
			snprintf(path, sizeof(path), "%s/anfr_proprietaire/anfr_proprietaire_%d_%s.kml", output_dir, sup->tpo_id, pathable(tpo_name));
			snprintf(buf, sizeof(buf), "ANFR antennes %s %s (%d)", source_name, pathable(tpo_name), sup->tpo_id);
			kmls_tpo[sup->tpo_id] = kml_open(path, buf, KML_ANFR_DESCRIPTION);
			kml_count++;
		}
		k_tpo = kmls_tpo[sup->tpo_id];
		/* find kml file matching the departement */
		if (!kmls_dept[sup->dept]) {
			snprintf(path, sizeof(path), "%s/anfr_departement/anfr_departement_%02X.kml", output_dir, sup->dept);
			snprintf(buf, sizeof(buf), "ANFR antennes %s %02X", source_name, sup->dept);
			kmls_dept[sup->dept] = kml_open(path, buf, KML_ANFR_DESCRIPTION);
			kml_count++;
		}
		k_dept = kmls_dept[sup->dept];

		/* write kml files content */
		/* description summary */
		len_desc = snprintf(desc, sizeof(desc), "support %d '%s' %s\n", sup->sup_id, tpo_name, nature_get_name(set->natures, sup->nat_id));
		len_desc += append_not_empty(desc+len_desc, sup->adr_lb_add0);
		len_desc += append_not_empty(desc+len_desc, sup->adr_lb_add2);
		len_desc += append_not_empty(desc+len_desc, sup->adr_lb_add3);
		len_desc += append_not_empty(desc+len_desc, sup->adr_lb_lieu);
		len_desc += append_not_empty(desc+len_desc, sup->adr_nm_cp_str);
		/* description station list summary and full station list */
		stalist[0] = '\0';
		expllist[0] = '\0';
		len_stalist = 0;
		if (conf.no_color == 1)
			style = KML_STYLE_DISABLED;
		else
			style = KML_STYLE_1_BLUE;
		sta = NULL;
		ts_begin = NULL;
		for (n=0; n<sup->sta_count; n++) {
			sta = station_get_next(set->stations, sup->sta_nm_anfr, sup->sta_count, sta);
			if (!sta) {
				warn_incoherent_data("missing stations for support %d, ignoring", sup->sup_id);
				continue;
			}
			exploitant_name = exploitant_get_name(set->exploitants, sta->adm_id);
			if (expllist[0] != '\0')
				strcat(expllist, ", ");
			snprintf(buf, sizeof(buf), "%s (%d)", exploitant_name, sta->emetteur_count);
			strcat(expllist, buf);
			len_desc += sprintf(desc+len_desc, "#%d %s '%s' %s %s (%d)\n    ",
					n+1, sta->sta_nm.str, exploitant_name, sta->dte_modif_str, sta->dte_en_service_str, sta->emetteur_count);
			len_desc += station_systemes(set->emetteurs, sta, desc+len_desc);
			len_stalist += sprintf(stalist+len_stalist, "-------------------\nstation #%d %s '%s'\n",
					n+1, sta->sta_nm.str, exploitant_name);
			len_stalist += station_description(set->types_antenne, sta, stalist + len_stalist);
			if (len_stalist >= sizeof(stalist))
				errx(1, "output_kml: description station list output size %d exceeded buffer size %lu", len_stalist, sizeof(stalist));
			/* update support style based of station time */
			if (style != KML_STYLE_DISABLED && style < KML_STYLE_3_RED) {
				diff = tm_diff(&conf.now, &sta->dte_latest);
				if (diff < 0) {
					style = KML_STYLE_3_RED; /* if support latest date is more recent than now, mark it as recent anyway */
				} else {
					diff = tm_diff(&set->stations->latest, &sta->dte_latest);
					if (diff < 30)
						style = KML_STYLE_3_RED;
					else if (style == KML_STYLE_1_BLUE && diff < 90)
						style = KML_STYLE_2_ORANGE;
				}
			}
			/* update support timespan begin */
			if (!ts_begin || tm_diff(&sta->dte_implemntatation, ts_begin) < 0)
				ts_begin = &sta->dte_implemntatation;
		}
		memcpy(desc+len_desc, stalist, len_stalist+1);
		len_desc += len_stalist;
		if (len_desc >= sizeof(desc))
			errx(1, "output_kml: description output size %d exceeded buffer size %lu", len_desc, sizeof(desc));
		/* name */
		buf2[0] = '\0';
		if (sup->sta_count > 1)
			snprintf(buf2, sizeof(buf2), "[%d] ", sup->sta_count);
		snprintf(buf, sizeof(buf), "%s%s", buf2, expllist);
		/* append placemark to kmls */
		kml_add_placemark_point(k_tpo,  sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
		kml_add_placemark_point(ka_tpo,   sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
		kml_add_placemark_point(k_dept, sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
		kml_add_placemark_point(ka_dept, sup->dept, sup->dept_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
		kml_add_placemark_point(ka_dept_light, sup->dept, sup->dept_name, sup->sup_id, "", "", sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
		/* append placemark to systeme kmls */
		bzero(sup_systeme_ids, sizeof(sup_systeme_ids));
		for (n=0; n<sup->sta_count; n++) {
			sta = station_get(set->stations, &sup->sta_nm_anfr[n]);
			if (!sta)
				continue;
			for (e=0; e<sta->emetteur_count; e++) {
				emr = sta->emetteurs[e];
				if (sup_systeme_ids[emr->systeme_id])
					continue; // support already recorded in that systeme id
				/* find kml file matching the systeme */
				if (!kmls_sys[emr->systeme_id]) {
					strncpy(buf2, emr->emr_lb_systeme, sizeof(buf2));
					strreplace(buf2, sizeof(buf2), '/', '_');
					snprintf(path, sizeof(path), "%s/anfr_systeme/anfr_systeme_%s.kml", output_dir, buf2);
					snprintf(buf2, sizeof(buf2), "ANFR antennes %s %s", source_name, emr->emr_lb_systeme);
					kmls_sys[emr->systeme_id] = kml_open(path, buf2, KML_ANFR_DESCRIPTION);
					kml_count++;
				}
				k_sys = kmls_sys[emr->systeme_id];
				snprintf(buf2, sizeof(buf2), "%s, %s", sup->dept_name, emr->emr_lb_systeme);
				kml_add_placemark_point(k_sys, sup->dept, buf2, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, "relativeToGround", KML_STYLES[style], ts_begin);
				sup_systeme_ids[emr->systeme_id] = 1;
			}
		}
	}

	/* close all kml files */
	for (idx=0; idx<PROPRIETAIRE_ID_MAX; idx++) {
		if (!kmls_tpo[idx])
			continue;
		kml_close(kmls_tpo[idx]);
	}
	for (idx=0; idx<SUPPORT_CP_DEPT_MAX; idx++) {
		if (!kmls_dept[idx])
			continue;
		kml_close(kmls_dept[idx]);
	}
	for (idx=0; idx<SYSTEMES_ID_MAX; idx++) {
		if (!kmls_sys[idx])
			continue;
		kml_close(kmls_sys[idx]);
	}
	kml_close(ka_tpo);
	kml_close(ka_dept);
	kml_close(ka_dept_light);

	info("created %d kml files\n", kml_count);
}

/* create one csv file per exploitant containing all the bands sorted by frequency together with their emetteur count and systemes sorted by count
 * <expoitant>_bands.csv
 * freq_min;freq_max;emr_count;systeme1_name;systeme1_count;systeme2_name;systeme2_count[...] */
void
output_bands(struct anfr_set *set, const char *output_dir, const char *source_name)
{
	struct bande_tree tree[EXPLOITANT_ID_MAX];
	struct bande_tree *ce, *ne, *pe;
	int count[EXPLOITANT_ID_MAX];
	struct support *sup;
	struct station *sta;
	struct emetteur *emr;
	struct bande *ban;
	int prepend, append, s, sc, n, e, b;
	struct stat fstat;
	const char *exploitant_name;
	char *lb;
	int id;
	char path[PATH_MAX];
	FILE *csv;


	bzero(tree, sizeof(tree));
	bzero(count, sizeof(count));
	if (stat(output_dir, &fstat) == -1)
		mkdir(output_dir, 0755);

	/* for all stations, insert bands in the exploitants bands tree */
	for (s=0, sc=0;
			s < SUPPORTS_ID_MAX && sc < set->supports->count;
			s++) {
		sup = set->supports->table[s];
		if (!sup)
			continue;
		for (n=0; n<sup->sta_count; n++) {
			sta = station_get(set->stations, &sup->sta_nm_anfr[n]);
			if (!sta)
				continue;
			for (e=0; e<sta->emetteur_count; e++) {
				emr = sta->emetteurs[e];
				for (b=0; b<emr->bande_count; b++) {
					ban = emr->bandes[b];
					/* insert this station band into exploitant tree */
					pe = NULL;               /* previous entry */
					ce = &tree[sta->adm_id]; /* current entry */
					while (1) {
						prepend = 0;
						append = 0;
						if (ban->ban_nb_f_deb == ce->ban_nb_f_deb && ban->ban_nb_f_fin == ce->ban_nb_f_fin) {
							/* increment entry counters */
							ce->emr_count++;
							ce->systemes_count[emr->systeme_id]++;
							break;
						} else if ((ban->ban_nb_f_deb < ce->ban_nb_f_deb) || (ban->ban_nb_f_deb == ce->ban_nb_f_deb && ban->ban_nb_f_fin < ce->ban_nb_f_fin)) {
							prepend = 1;
						} else if (!ce->next && ((ban->ban_nb_f_deb > ce->ban_nb_f_deb) || (ban->ban_nb_f_deb == ce->ban_nb_f_deb && ban->ban_nb_f_fin > ce->ban_nb_f_fin))) {
							append = 1;
						}
						if (prepend || append) {
							/* create new entry */
							ne = malloc(sizeof(struct bande_tree));
							ne->emr_count = 1;
							ne->ban_nb_f_deb = ban->ban_nb_f_deb;
							ne->ban_nb_f_deb_str = ban->ban_nb_f_deb_str;
							ne->ban_nb_f_fin = ban->ban_nb_f_fin;
							ne->ban_nb_f_fin_str = ban->ban_nb_f_fin_str;
							bzero(ne->systemes_count, sizeof(ne->systemes_count));
							ne->systemes_count[emr->systeme_id]++;
							ne->next = NULL;
							/* insert new entry */
							if (prepend) {
								if (!pe)
									errx(1, "band bellow 0mhz ! deb=%" PRIu64 " fin=%" PRIu64, ban->ban_nb_f_deb, ban->ban_nb_f_fin);
								pe->next = ne;
								ne->next = ce;
							} else {
								ce->next = ne;
							}
							count[sta->adm_id]++;
							break;
						}
						/* search next entry */
						pe = ce;
						ce = ce->next;
					}
				}
			}
		}
	}

	/* write csv */
	for (n=0; n<EXPLOITANT_ID_MAX; n++) {
		if (count[n] == 0)
			continue;

		exploitant_name = exploitant_get_name(set->exploitants, n);
		snprintf(path, sizeof(path), "%s/%03d_%s_bands.csv", output_dir, n, pathable(exploitant_name));

		csv = fopen(path, "w");
		fprintf(csv, "# %d - %s: %d bands\n", n, exploitant_name, count[n]);
		fprintf(csv, "# freq_min;freq_max;emr_count;systeme1_name;systeme1_count[...]\n");

		for (ce=tree[n].next; ce; ce=ce->next) {
			id = 0;
			s = 0;
			fprintf(csv, "%" PRIu64 ";%" PRIu64 ";%d", ce->ban_nb_f_deb, ce->ban_nb_f_fin, ce->emr_count);
			while ( (s = next_smallest_positive_int(ce->systemes_count, SYSTEMES_ID_MAX, s, id, &id)) > 0 ) {
				lb = set->emetteurs->systemes_lb[id];
				fprintf(csv, ";%s;%d", lb, s);
			}
			fprintf(csv, "\n");
#ifdef DEBUG
			free(ce);
#endif
		}

		fclose(csv);
	}
}

void
csv_stanm(struct csv *csv, struct sta_nm *sta_nm)
{
	char val[STA_NM_LEN+1];
	char *tok = csv_field(csv);

	if (tok) {
		sta_nm->str = tok;
		sta_nm->nm = atoi16_fast(tok);
		memcpy(val, tok, STA_NM_LEN+1);
		sta_nm->id = atoi_fast(val+STA_NM_DEPT_LEN+STA_NM_ZONE_LEN);
		if (sta_nm->id < 0 || sta_nm->id > STATION_ID_MAX)
			errx(1, "invalid sta_nm id %d", sta_nm->id);
		val[STA_NM_DEPT_LEN+STA_NM_ZONE_LEN] = '\0';
		sta_nm->zone = atoi_fast(val+STA_NM_DEPT_LEN);
		if (sta_nm->zone < 0 || sta_nm->zone > STATION_ZONE_MAX)
			errx(1, "invalid sta_nm zone %d", sta_nm->zone);
		val[STA_NM_DEPT_LEN] = '\0';
		sta_nm->dept = atoi16_fast(val);
		if (sta_nm->dept < 0 || sta_nm->dept > STATION_DEPT_MAX)
			errx(1, "invalid sta_nm dept %d", sta_nm->dept);
	}
}

