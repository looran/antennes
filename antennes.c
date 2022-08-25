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

#ifdef __linux__
#define _XOPEN_SOURCE /* for strptime() */
#define _DEFAULT_SOURCE /* for strsep() and strdup() */
#endif
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
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

static struct conf {
	struct tm now;
	int no_color;
	int verbose;
} conf;

#define CSV_NORMAL 0
#define CSV_CONV_UTF8_TO_ISO8859 1
struct csv {
	char *file;
	char *p;
	int size;
	char *line;
	char *save_line;
	char *save_field;
	int line_count;
	int field_count;
	int conv;
};

/* KML colors : AABBGGRR */
#define KML_HEADER \
	"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" \
	"<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n" \
	"<Folder id=\"ANFR antennes %s\">\n" \
	"\t<name>ANFR antennes %s</name>\n" \
	"\t<Style id=\"blue\">\n" \
	"\t\t<IconStyle><color>ffff0000</color></IconStyle>\n" \
	"\t</Style>\n" \
	"\t<Style id=\"orange\">\n" \
	"\t\t<IconStyle><color>ff0088ff</color></IconStyle>\n" \
	"\t</Style>\n" \
	"\t<Style id=\"red\">\n" \
	"\t\t<IconStyle><color>ff0000ff</color></IconStyle>\n" \
	"\t</Style>\n"

#define KML_STYLE_DISABLED 0
#define KML_STYLE_1_BLUE 1
#define KML_STYLE_2_ORANGE 2
#define KML_STYLE_3_RED 3
const char *KML_STYLES[] = {
	NULL,
	"blue",
	"orange",
	"red",
};

#define KML_DOC_START \
	"\t<Document id=\"%d\">\n" \
	"\t\t<name>%s</name>\n"

#define KML_PLACEMARK_POINT \
	"\t\t<Placemark id=\"%d\">\n" \
	"\t\t\t<name>%s</name>\n" \
	"\t\t\t<description><![CDATA[%s]]></description>\n" \
	"%s" \
	"\t\t\t<Point>\n" \
	"\t\t\t\t<altitudeMode>relativeToGround</altitudeMode>\n" \
	"\t\t\t\t<coordinates>%f,%f,%f</coordinates>\n" \
	"\t\t\t</Point>\n" \
	"\t\t</Placemark>\n"

#define KML_PLACEMARK_POINT_STYLE \
	"\t\t\t<styleUrl>#%s</styleUrl>\n"

#define KML_DOC_END \
	"\t</Document>\n"

#define KML_FOOTER \
	"</Folder>\n" \
	"</kml>\n"

struct kml_doc {
	int id;
	const char *name;
	char *placemarks;
	int placemarks_size;
	int placemarks_count;
};

#define KML_DOC_MAX 200
struct kml {
	char *path;
	FILE *f;
	struct kml_doc *docs[KML_DOC_MAX];
	int docs_count;
};

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
	float ban_nb_f_deb;
	char *ban_nb_f_deb_str;
	float ban_nb_f_fin;
	char *ban_nb_f_fin_str;
	char *ban_fg_unite;
};

/* antennes have an integer id, max value of 7878184 as of 20220729 obtained by:
 * $ cut -d';' -f2 tmp/extract/SUP_ANTENNE.txt  |sort -n |tail -n1 */
#define ANTENNE_ID_MAX 10000000
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

__attribute__((__noreturn__)) void usageexit(void);
/* input file processing */
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
struct f_emetteur	*emetteurs_load(char *, struct f_station *);
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
/* csv */
void		 csv_open(struct csv *, char *, int);
void		 csv_close(struct csv *);
int			 csv_line(struct csv *);
char		*csv_field(struct csv *);
void		 csv_int(struct csv *, int *, char **);
void		 csv_int16(struct csv *, uint32_t *, char **);
void		 csv_float(struct csv *, float *, char **);
void		 csv_stanm(struct csv *, struct sta_nm *);
void		 csv_str(struct csv *, char **);
void		 csv_date(struct csv *, struct tm *, char **);
/* kml */
struct kml	*kml_open(const char *, const char *);
void		 kml_close(struct kml *);
void		 kml_add_placemark_point(struct kml *, int, const char *, int, char *, char *, float, float, float, const char *);
/* utils */
void		 coord_dms_to_dd(int [3], char *, int [3], char *, float *, float *);
const char *pathable(const char *);
int		 	 append_not_empty(char *, char *);
void		*xmalloc_zero(size_t);
int			 tm_diff(struct tm *, struct tm *);
int			 next_smallest_positive_int(int *, int, int, int, int *);
int			 atoi_fast(const char *);
uint64_t	 atoi16_fast(const char *);
char		*itoa_u32(uint32_t, char *);
char		*itoa_i32(int32_t, char *);
void		 utf8_to_iso8859(char *);
#define strbuf_chr(buf, chr) do { \
	buf[0] = chr; \
	buf[1] = '\0'; \
	buf++; \
} while (0)
#define strbuf_str(buf, str) do { buf = stpcpy(buf, str); } while (0)
#define strbuf_int(buf, num) do { buf = itoa_i32(num, buf); } while (0)
#define verb(...) do { \
	if (conf.verbose) \
		fprintf(stderr, __VA_ARGS__); \
} while (0)
#define info(...) do { fprintf(stderr, __VA_ARGS__); } while (0)

__attribute__((__noreturn__)) void
usageexit()
{
	printf("usage: antennes [-Cv] [-k <dir>] <data_dir>\n");
	printf("Query and export KML files from ANFR radio sites public data\n");
	printf("-C       do not set any kml placemark colors\n");
	printf("-k <dir> export kml files to this directory\n");
	printf("-s       display antennes statistics\n");
	printf("-v       verbose logging\n");
	printf("if neither -s or -k are specified, this program only loads the data.\n");
	printf("output kml files hierarchy:\n");
	printf("   anfr.kml : all supports in a single file, one document section per proprietaire\n");
	printf("   anfr_proprietaire/anfr_proprietaire_<proprietaire-id>_<proprietaire-name>.kml : one file per proprietaire\n");
	printf("   anfr_departement/anfr_departement_<dept-id>.kml : one file per departement\n");
	printf("kml placemark colors:\n");
	printf("   orange for supports with stations updated in less than 3 months, red for 1 month, blue otherwise\n");
	exit(1);
}

int
main(int argc, char *argv[])
{
	struct anfr_set set;
	char dir[PATH_MAX];
	int ch, stats = 0;
	char *kml_export = NULL;
	time_t now;

	bzero(&conf, sizeof(conf));
	while ((ch = getopt(argc, argv, "Chk:sv")) != -1) {
		switch (ch) {
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

	info("[+] loading files from %s\n", argv[0]);
	if (stats)
		printf("file name : %s\n\n", basename(argv[0]));
	snprintf(dir, sizeof(dir), "%s/SUP_NATURE.txt", argv[0]);
	set.natures = natures_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_SUPPORT.txt", argv[0]);
	set.supports = supports_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_PROPRIETAIRE.txt", argv[0]);
	set.proprietaires = proprietaires_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_STATION.txt", argv[0]);
	set.stations = stations_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_EXPLOITANT.txt", argv[0]);
	set.exploitants = exploitants_load(dir);
	snprintf(dir, sizeof(dir), "%s/SUP_EMETTEUR.txt", argv[0]);
	set.emetteurs = emetteurs_load(dir, set.stations);
	snprintf(dir, sizeof(dir), "%s/SUP_BANDE.txt", argv[0]);
	set.bandes = bandes_load(dir, set.emetteurs);
	snprintf(dir, sizeof(dir), "%s/SUP_ANTENNE.txt", argv[0]);
	set.antennes = antennes_load(dir, set.stations);
	snprintf(dir, sizeof(dir), "%s/SUP_TYPE_ANTENNE.txt", argv[0]);
	set.types_antenne = types_antenne_load(dir);

	if (stats) {
		info("[*] displaying statistics\n");
		printf("\nemetteurs systemes count:\n%s", emetteurs_stats(set.emetteurs));
	}

	if (kml_export) {
		info("[*] exporting kml to %s\n", kml_export);
		output_kml(&set, kml_export, basename(argv[0]));
	}

#ifdef DEBUG
	info("[*] freeing ressources\n");
	types_antenne_free(set.types_antenne);
	antennes_free(set.antennes);
	bandes_free(set.bandes);
	emetteurs_free(set.emetteurs);
	exploitants_free(set.exploitants);
	stations_free(set.stations);
	proprietaires_free(set.proprietaires);
	supports_free(set.supports);
	natures_free(set.natures);
#endif

	return 0;
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
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859);

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

	csv_open(csv, path, CSV_NORMAL);
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
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &tpo_id, NULL);
		if (tpo_id >= PROPRIETAIRE_ID_MAX)
			errx(1, "line %d: invalid proprietaire id %d too big", csv->line_count, tpo_id);
		if (proprietaires->table[tpo_id]) {
			warnx("incoherent data: line %d: proprietaire %d already exists, ignoring\n", csv->line_count, tpo_id);
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
	csv_open(csv, path, CSV_NORMAL);

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
			warnx("incoherent data: line %d: station %s already exists, ignoring", csv->line_count, sta->sta_nm.str);
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
	if (!stations->depts[nm->dept]->zones[nm->zone])
		errx(1, "station_get: zone %d not found in departement %x for station %s", nm->zone, nm->dept, nm->str);
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
			warnx("incoherent data: station %s not found, ignoring", table[n].str);
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
	int n, b;

	/* summary */
	strbuf_str(desc, "   implementation: ");
	strbuf_str(desc, sta->dte_implemntatation_str);
	strbuf_str(desc, "\n   modification: ");
	strbuf_str(desc, sta->dte_modif_str);
	strbuf_str(desc, "\n   en service: ");
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
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &adm_id, NULL);
		if (adm_id >= EXPLOITANT_ID_MAX)
			errx(1, "invalid exploitant id, too big (%d) at line %d", adm_id, csv->line_count);
		if (exploitants->table[adm_id]) {
			warnx("incoherent data: line %d: exploitant %d already exists, ignoring\n", csv->line_count, adm_id);
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
emetteurs_load(char *path, struct f_station *stations)
{
	struct f_emetteur *emetteurs;
	struct csv *csv;
	struct emetteur *emr;
	int emr_id, sys_id;
	char *emr_id_str;
	struct station *sta;

	emetteurs = xmalloc_zero(sizeof(struct f_emetteur));
	csv = &emetteurs->csv;
	csv_open(csv, path, CSV_NORMAL);

	while (csv_line(csv)) {
		if (!isdigit(csv->line[0]))
			continue; /* comment or header */
		csv_int(csv, &emr_id, &emr_id_str);
		if (emr_id >= EMETTEUR_ID_MAX)
			errx(1, "emetteur id too big: %d", emr_id);
		if (emetteurs->table[emr_id]) {
			warnx("incoherent data: line %d: emetteur %d already exists, ignoring", csv->line_count, emr_id);
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
			warnx("incoherent data: station %s not found for emetteur %d, ignoring", emr->sta_nm.str, emr_id);
			free(emr);
			continue;
		}
		if (sta->emetteur_count == STATION_EMETTEUR_MAX)
			errx(1, "maximum emetteur count %d reached for station %s", STATION_EMETTEUR_MAX, sta->sta_nm.str);
		sta->emetteurs[sta->emetteur_count] = emr;
		sta->emetteur_count++;
		sta->systeme_count[sys_id]++;

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

	bandes = xmalloc_zero(sizeof(struct f_bande));
	csv = &bandes->csv;
	csv_open(csv, path, CSV_NORMAL);

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
		csv_float(csv, &ban->ban_nb_f_deb, &ban->ban_nb_f_deb_str);
		csv_float(csv, &ban->ban_nb_f_fin, &ban->ban_nb_f_fin_str);
		csv_str(csv, &ban->ban_fg_unite);

		emr = emetteur_get(emetteurs, ban->emr_id);
		if (!emr) {
			warnx("incoherent data: emetteur %d not found for bande %d, ignoring", ban->emr_id, ban_id);
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
	csv_open(csv, path, CSV_NORMAL);

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
			csv_float(csv, &aer->aer_nb_dimension, &aer->aer_nb_dimension_str);
			csv_str(csv, &aer->aer_fg_rayon);
			csv_float(csv, &aer->aer_nb_azimut, &aer->aer_nb_azimut_str);
			csv_float(csv, &aer->aer_nb_alt_bas, &aer->aer_nb_alt_bas_str);
			csv_int(csv, NULL, &aer->sup_id_str);
		}

		/* update related station counters */
		sta = station_get(stations, &sta_nm);
		if (!sta) {
			warnx("incoherent data: station %s not found for antenne %d, ignoring", sta_nm.str, aer_id);
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
	csv_open(csv, path, CSV_CONV_UTF8_TO_ISO8859);

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
	int idx, sup_count, n, len_stalist, len_desc, kml_count, departement, style, diff;
	struct support *sup;
	char desc[SUPPORT_DESCRIPTION_BUF_SIZE], stalist[SUPPORT_DESCRIPTION_BUF_SIZE];
	char path[PATH_MAX], buf[1024], buf2[128], expllist[4096];
	struct kml *kmls_tpo[PROPRIETAIRE_ID_MAX];
	struct kml *kmls_dept[SUPPORT_CP_DEPT_MAX];
	struct kml *k_all, *k_tpo, *k_dept;
	const char *tpo_name, *exploitant_name;
	struct stat fstat;
	struct station *sta;

	if (stat(output_dir, &fstat) == -1)
		mkdir(output_dir, 0755);
	snprintf(path, sizeof(path), "%s/anfr.kml", output_dir);
	k_all = kml_open(path, source_name);
	kml_count = 1;
	snprintf(path, sizeof(path), "%s/anfr_proprietaire", output_dir);
	if (stat(path, &fstat) == -1)
		mkdir(path, 0755);
	snprintf(path, sizeof(path), "%s/anfr_departement", output_dir);
	if (stat(path, &fstat) == -1)
		mkdir(path, 0755);

	bzero(kmls_tpo, sizeof(kmls_tpo));
	bzero(kmls_dept, sizeof(kmls_dept));

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
			snprintf(buf, sizeof(buf), "%s %s (%d)", source_name, pathable(tpo_name), sup->tpo_id);
			kmls_tpo[sup->tpo_id] = kml_open(path, buf);
			kml_count++;
		}
		k_tpo = kmls_tpo[sup->tpo_id];
		/* find kml file matching the departement */
		departement = sup->com_cd_insee >> 12;
		if (!kmls_dept[departement]) {
			snprintf(path, sizeof(path), "%s/anfr_departement/anfr_departement_%02X.kml", output_dir, departement);
			snprintf(buf, sizeof(buf), "%s %02X", source_name, departement);
			kmls_dept[departement] = kml_open(path, buf);
			kml_count++;
		}
		k_dept = kmls_dept[departement];

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
		for (n=0; n<sup->sta_count; n++) {
			sta = station_get_next(set->stations, sup->sta_nm_anfr, sup->sta_count, sta);
			if (!sta) {
				warnx("incoherent data: missing stations for support %d, ignoring", sup->sup_id);
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
		kml_add_placemark_point(k_tpo,  sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, KML_STYLES[style]);
		kml_add_placemark_point(k_dept, sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, KML_STYLES[style]);
		kml_add_placemark_point(k_all,   sup->tpo_id, tpo_name, sup->sup_id, buf, desc, sup->lat, sup->lon, (float)sup->sup_nm_haut, KML_STYLES[style]);
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
	kml_close(k_all);

	info("created %d kml files\n", kml_count);
}

void
csv_open(struct csv *csv, char *path, int conv)
{
	int f;
	struct stat fstat;
	char *ptr;

	if (stat(path, &fstat) == -1)
		errx(1, "could not stat csv: %s", path);
	f = open(path, O_RDONLY);
	if (!f)
		errx(1, "could not open csv: %s", path);
	ptr = mmap(0, fstat.st_size, PROT_READ, MAP_PRIVATE, f, 0);
	if (!ptr)
		errx(1, "could not mmap csv: %s", path);
	csv->file = malloc(fstat.st_size+1);
	memcpy(csv->file, ptr, fstat.st_size);
	munmap(ptr, fstat.st_size);
	csv->file[fstat.st_size] = '\0';
	csv->size = fstat.st_size;
	csv->p = csv->file;
	csv->conv = conv;
}

void
csv_close(struct csv *csv)
{
	free(csv->file);
}

int
csv_line(struct csv *csv)
{
	csv->line = strsep(&csv->p, "\n");
	if (!csv->line || csv->line[0] == '\0')
		return 0;
	if (csv->p && *(csv->p-2) == '\r')
		*(csv->p-2) = '\0'; /* remove \r */
	csv->line_count++;
	csv->field_count = 0;
	return 1;
}

char *
csv_field(struct csv *csv)
{
	char *tok;

	csv->field_count++;
	tok = strsep(&csv->line, ";");
	if (!tok)
		tok = ""; /* in case the field is not found (old csv format), set it to an empty string */
	return tok;
}

void
csv_int(struct csv *csv, int *val, char **orig)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (orig)
			*orig = tok;
		if (val)
			*val = atoi_fast(tok);
	}
}

void
csv_int16(struct csv *csv, uint32_t *val, char **orig)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (orig)
			*orig = tok;
		if (val)
			*val = atoi16_fast(tok);
	}
}

void
csv_float(struct csv *csv, float *val, char **orig)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (orig)
			*orig = tok;
		// TODO implement float parsing, not needed for now since we use only the string value
		// *val = atoi_fast(tok);
		*val = 0.0;
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

void
csv_str(struct csv *csv, char **s)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (csv->conv == CSV_CONV_UTF8_TO_ISO8859)
			utf8_to_iso8859(tok);
		*s = tok;
	}
}

void
csv_date(struct csv *csv, struct tm *val, char **orig)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (orig)
			*orig = tok;
		if (val) {
			bzero(val, sizeof(struct tm));
			strptime(tok, "%d/%m/%Y", val);
		}
	}
}

struct kml *
kml_open(const char *path, const char *name)
{
	struct kml *kml = xmalloc_zero(sizeof(struct kml));
	char buf[1024];
	int len;

	verb("creating kml file %s\n", path);
	kml->path = strdup(path);
	kml->f = fopen(path, "w");
	if (!kml->f)
		errx(1, "could not create kml file %s\n", path);
	len = snprintf(buf, sizeof(buf), KML_HEADER, name, name);
	fwrite(buf, len, 1, kml->f);

	return kml;
}

void
kml_close(struct kml *kml)
{
	struct kml_doc *doc;
	char buf[1024];
	int len, idx;

	/* write KML documents */
	for (idx=0; idx<kml->docs_count; idx++) {
		doc = kml->docs[idx];
		len = snprintf(buf, sizeof(buf), KML_DOC_START, doc->id, doc->name);
		fwrite(buf, len, 1, kml->f);
		fwrite(doc->placemarks, doc->placemarks_size, 1, kml->f);
		fwrite(KML_DOC_END, sizeof(KML_DOC_END)-1, 1, kml->f);
		free(doc->placemarks);
		free(doc);
	}

	fwrite(KML_FOOTER, sizeof(KML_FOOTER)-1, 1, kml->f);

	fclose(kml->f);
	free(kml->path);
	free(kml);
}

void
kml_add_placemark_point(struct kml *kml, int doc_id, const char *doc_name, int id, char *name, char *description, float lat, float lon, float haut, const char *styleurl)
{
	char buf[SUPPORT_DESCRIPTION_BUF_SIZE];
	char buf2[256];
	int len, idx;
	struct kml_doc *doc;

	/* get the document matching doc_id */
	doc = NULL;
	for (idx=0; idx<kml->docs_count; idx++) {
		if (kml->docs[idx]->id == doc_id) {
			doc = kml->docs[idx];
			break;
		}
	}
	if (!doc) {
        if (kml->docs_count == KML_DOC_MAX)
            errx(1, "kml reached maximum document count %d", KML_DOC_MAX);
		doc = xmalloc_zero(sizeof(struct kml_doc));
		doc->id = doc_id;
		doc->name = doc_name;
		kml->docs[kml->docs_count] = doc;
		kml->docs_count++;
	}

	/* append to placemarks in this document */
	if (styleurl) {
		snprintf(buf2, sizeof(buf2), KML_PLACEMARK_POINT_STYLE, styleurl);
	}
	len = snprintf(buf, sizeof(buf), KML_PLACEMARK_POINT, id, name, description, buf2, lon, lat, haut);
	if (len >= sizeof(buf))
		errx(1, "kml_add_placemark_point internal buffer limit reached (%d)", len);
	doc->placemarks = realloc(doc->placemarks, doc->placemarks_size + len);
	memcpy(doc->placemarks + doc->placemarks_size, buf, len);
	doc->placemarks_size += len;
	doc->placemarks_count++;
}

/* converts Degree Minute Seconds coordinates notation to Decimal Degree */
void
coord_dms_to_dd(int lat_dms[3], char *lat_ns, int lon_dms[3], char *lon_ew, float *out_lat, float *out_lon)
{
	*out_lat = (float)lat_dms[0] + ((float)lat_dms[1] + ((float)lat_dms[2] / 60.0)) / 60.0;
	if (lat_ns[0] == 'S')
		*out_lat = - *out_lat;
	*out_lon = (float)lon_dms[0] + ((float)lon_dms[1] + ((float)lon_dms[2] / 60.0)) / 60.0;
	if (lon_ew[0] == 'W')
		*out_lon = - *out_lon;
}

/* makes a string usable as a path. returns a station copy. */
const char *
pathable(const char *s)
{
	static char buf[255];
	int len, i;

	strncpy(buf, s, sizeof(buf)-1);
	len = strlen(buf);
	for (i=0; i<len; i++) {
		if (buf[i] == ' ')
			buf[i] = '_';
		if (buf[i] == '/' || buf[i] == '\'')
			buf[i] = '-';
	}

	return buf;
}

int
append_not_empty(char *text, char *append)
{
	int len = 0;

	if (append[0] != '\0') {
		len = strlen(append);
		memcpy(text, append, len);
		*(text+len) = '\n';
		*(text+len+1) = '\0';
		len++;
	}

	return len;
}

void *
xmalloc_zero(size_t size)
{
	void *p;

	p = malloc(size);
	if (!p)
		err(1, "malloc");
	bzero(p, size);
	return p;
}

/* returns a positive value if 'a' is older than 'b', negative value if 'b' older than 'a' and 0 if 'a' equals 'b'.
 * the value is the number of days of difference. only year, month and day are compared */
int
tm_diff(struct tm *a, struct tm *b)
{
	int diff = 0;

	/* XXX if (a->tm_year < b->tm_year)
		return -1;
	if (a->tm_year > b->tm_year)
		return 1;
	if (a->tm_mon < b->tm_mon)
		return -1;
	if (a->tm_mon > b->tm_mon)
		return 1;
	if (a->tm_mday < b->tm_mday)
		return -1;
	if (a->tm_mday > b->tm_mday)
		return 1; */
	diff += (a->tm_year - b->tm_year) * 365;
	diff += (a->tm_mon - b->tm_mon) * 30;
	diff += a->tm_mday - b->tm_mday;

	return diff;
}

int
next_smallest_positive_int(int *table, int size, int last, int last_index, int *next_index)
{
	int n, next = 0;

	for (n=0; n<size; n++) {
		if ( table[n] > 0
				&& (!last || (table[n] < last || (table[n] == last && n > last_index)))
				&& (table[n] > next) ) {
			next = table[n];
			*next_index = n;
		}
	}

	return next;
}

int
atoi_fast(const char *str)
{
	int val = 0;

	while ( *str && *str >= '0' && *str <= '9' )
		val = val*10 + (*str++ - '0');

	return val;
}

uint64_t
atoi16_fast(const char *str)
{
	uint64_t val = 0;
	int v;

	while ( (v = *str) ) {
		if (v >= 'A' && v <= 'F')
			v = 10 + (v - 'A');
		else
			v = v - '0';
		val = val*0x10 + v;
		str++;
	}

	return val;
}

/* from Milo Yip itoa-benchmark naive implementation */
char *
itoa_u32(uint32_t value, char *buffer)
{
	char temp[10];
	char *p = temp;

	do {
		*p++ = (char)(value % 10) + '0';
		value /= 10;
	} while (value > 0);

	do {
		*buffer++ = *--p;
	} while (p != temp);

	return buffer;
}

/* from Milo Yip itoa-benchmark naive implementation */
char *
itoa_i32(int32_t value, char *buffer)
{
	uint32_t u = (uint32_t)(value);

	if (value < 0) {
		*buffer++ = '-';
		u = ~u + 1;
	}

	return itoa_u32(u, buffer);
}

void
utf8_to_iso8859(char *s)
{
	char *d = s;
	uint8_t c;

	while ((c = *s++)) {
		if (c < 0x7F)
			*d++ = c;
		else if (c == 0xC2)
			*d++ = *s++;
		else if (c == 0xC3)
			*d++ = *s++ + 0x40;
	}
	*d = '\0';
}
