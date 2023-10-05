struct conf {
	struct tm now;
	char now_str[64];
	int no_color;
	int verbose;
	int warn_incoherent_data;
};

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

#define KML_STYLE_DISABLED 0
#define KML_STYLE_1_BLUE 1
#define KML_STYLE_2_ORANGE 2
#define KML_STYLE_3_RED 3
static const char *KML_STYLES[] = {
	NULL,
	"blue",
	"orange",
	"red",
};


struct kml_doc {
	int id;
	char *name;
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

/* csv */
void		 csv_open(struct csv *, char *, int);
void		 csv_close(struct csv *);
int		 csv_line(struct csv *);
char		*csv_field(struct csv *);
void		 csv_int(struct csv *, int *, char **);
void		 csv_int16(struct csv *, uint32_t *, char **);
void		 csv_float(struct csv *, double *, char **);
void		 csv_str(struct csv *, char **);
void		 csv_date(struct csv *, struct tm *, char **);
/* kml */
struct kml	*kml_open(const char *, const char *);
void		 kml_close(struct kml *);
void		 kml_add_placemark_point(struct kml *, int, const char *, int, char *, char *, float, float, float, const char *, const struct tm *);
/* utils */
void		 coord_dms_to_dd(int [3], char *, int [3], char *, float *, float *);
const char *pathable(const char *);
int		 	 append_not_empty(char *, char *);
void		*xmalloc_zero(size_t);
int			 tm_diff(struct tm *, struct tm *);
int			 next_smallest_positive_int(int *, int, int, int, int *);
int			 atoi_fast(const char *);
uint64_t	 atoi16_fast(const char *);
double		 atof_fast(char *);
char		*itoa_u32(uint32_t, char *);
char		*itoa_i32(int32_t, char *);
void		 utf8_to_iso8859(char *);
void		 strreplace(char *, int, char, char);
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
#define warn_incoherent_data(...) do { \
	conf.warn_incoherent_data += 1; \
	warnx("incoherent data: " __VA_ARGS__); \
} while (0)
