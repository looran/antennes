#ifdef __linux__
#define _XOPEN_SOURCE /* for strptime() */
#define _DEFAULT_SOURCE /* for strsep() and strdup() */
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <sys/stat.h>
#include <err.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <ctype.h>

#include "utils.h"

/* KML colors : AABBGGRR */
#define KML_HEADER \
	"<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\n" \
	"<kml xmlns=\"http://www.opengis.net/kml/2.2\" xmlns:gx=\"http://www.google.com/kml/ext/2.2\">\n" \
	"<Folder id=\"ANFR antennes %s\">\n" \
	"\t<name>ANFR antennes %s</name>\n" \
	"\t<Snippet>KML export of french emetteurs based on ANFR data</Snippet>\n" \
	"\t<description>Generated by https://github.com/looran/antennes on %s</description>\n" \
	"\t<Style id=\"blue\">\n" \
	"\t\t<IconStyle><color>ffff0000</color></IconStyle>\n" \
	"\t</Style>\n" \
	"\t<Style id=\"orange\">\n" \
	"\t\t<IconStyle><color>ff0088ff</color></IconStyle>\n" \
	"\t</Style>\n" \
	"\t<Style id=\"red\">\n" \
	"\t\t<IconStyle><color>ff0000ff</color></IconStyle>\n" \
	"\t</Style>\n"

#define KML_DOC_START \
	"\t<Document id=\"%d\">\n" \
	"\t\t<name>%s</name>\n"

#define KML_PLACEMARK_POINT \
	"\t\t<Placemark id=\"%d\">\n" \
	"\t\t\t<name>%s</name>\n" \
	"\t\t\t<description><![CDATA[%s]]></description>\n" \
	"%s" \
	"\t\t\t<TimeSpan id=\"ts%d\">\n" \
	"\t\t\t  <begin>%s</begin>\n" \
	"\t\t\t</TimeSpan>\n" \
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

void
csv_open(struct csv *csv, char *path, int conv)
{
	int f;
	struct stat fstat;
	char *ptr;

	if (stat(path, &fstat) == -1)
		errx(1, "could not find csv: %s", path);
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
csv_float(struct csv *csv, double *val, char **orig)
{
	char *tok = csv_field(csv);

	if (tok) {
		if (orig)
			*orig = tok;
		*val = atof_fast(tok);
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
	struct stat fstat;
	char buf[1024];
	int len;

	if (stat(path, &fstat) >= 0)
		errx(1, "kml file already exists: %s", path);
	verb("creating kml file %s\n", path);
	kml->path = strdup(path);
	kml->f = fopen(path, "w");
	if (!kml->f)
		errx(1, "could not create kml file %s\n", path);
	len = snprintf(buf, sizeof(buf), KML_HEADER, name, name, conf.now_str);
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
		free(doc->name);
		free(doc);
	}

	fwrite(KML_FOOTER, sizeof(KML_FOOTER)-1, 1, kml->f);

	fclose(kml->f);
	free(kml->path);
	free(kml);
}

#define DESCRIPTION_BUF_SIZE 65536
void
kml_add_placemark_point(struct kml *kml, int doc_id, const char *doc_name, int id, char *name, char *description, float lat, float lon, float haut, const char *styleurl, const struct tm *ts_begin)
{
	char buf[DESCRIPTION_BUF_SIZE];
	char buf2[256];
	int len, idx;
	struct kml_doc *doc;
	char tsbuf[50];

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
		doc->name = strdup(doc_name);
		kml->docs[kml->docs_count] = doc;
		kml->docs_count++;
	}

	/* append to placemarks in this document */
	if (ts_begin)
		strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d", ts_begin);
	else
		tsbuf[0] = '\0';
	if (styleurl)
		snprintf(buf2, sizeof(buf2), KML_PLACEMARK_POINT_STYLE, styleurl);
	len = snprintf(buf, sizeof(buf), KML_PLACEMARK_POINT, id, name, description, buf2, id, tsbuf, lon, lat, haut);
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
		if (!isascii(buf[i]) || buf[i] == ' ')
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

/* fast atof returning float, with the following assumptions on the 's' string:
 * - comma separates integer part from fractional part, if any
 * - only digits and possibly one comma
 * - limited number of digits, before and after comma
 * - ends with '\0' */
double
atof_fast(char *s)
{
	double r;
	int c; /* comma position */
	int i; /* position in tok */

	r = 0.0;

	/* locate comma */
	for (c=0; s[c] != ',' && s[c] != '\0'; c++);

	/* calculate r */
	for (i=0; s[i] != '\0'; i++) {
		switch (c - i) {
		case 0: break; /* comma */
		case 1: r += (s[i] - '0'); break;
		case 2: r += (s[i] - '0') * 10; break;
		case 3: r += (s[i] - '0') * 100; break;
		case 4: r += (s[i] - '0') * 1000; break;
		case 5: r += (s[i] - '0') * 10000; break;
		case 6: r += (s[i] - '0') * 100000; break;
		case 7: r += (s[i] - '0') * 1000000; break;
		case -1: r += (s[i] - '0') * 0.1; break;
		case -2: r += (s[i] - '0') * 0.01; break;
		case -3: r += (s[i] - '0') * 0.001; break;
		case -4: r += (s[i] - '0') * 0.0001; break;
		default: errx(1, "atof_fast: too many digits: %s", s);
		}
	}

	return r;
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

void
strreplace(char *buf, int size, char needle, char replace)
{
	while((buf = strchr(buf, needle)) != NULL)
		*buf++ = replace;
}
