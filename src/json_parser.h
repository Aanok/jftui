#ifndef _JF_JSON_PARSER
#define _JF_JSON_PARSER


// CODE MACROS
#define GEN_BAD_JUMP_OUT(gen) { if ((gen) != yajl_gen_status_ok) goto out; }
#define SAX_ITEM_FILL(field) { g_sax_item.field = string; g_sax_item.field ## _len = string_len; }
#define SAX_KEY_IS(name) (strncmp(key, name, sizeof(name) > key_len ? key_len : sizeof(name)) == 0)
#define SAX_STRING_IS(name) (strncmp(string, name, sizeof(name) > string_len ? string_len : sizeof(name)) == 0)
#define SAX_PRINT_FALLBACK(field, fallback) g_sax_item.field ## _len > 0 ? g_sax_item.field ## _len : sizeof(fallback) - 1, \
											g_sax_item.field != NULL ? g_sax_item. field : fallback



// SAX PARSER STATE MACHINE
#define JF_SAX_IDLE							0
#define JF_SAX_IN_QUERYRESULT_MAP			1
#define JF_SAX_IN_ITEMS_VALUE				2
#define JF_SAX_IN_ITEMS_ARRAY				3
#define JF_SAX_IN_ITEM_MAP					4
#define JF_SAX_IN_ITEM_TYPE_VALUE			5
#define JF_SAX_IN_ITEM_NAME_VALUE			6
#define JF_SAX_IN_ITEM_ID_VALUE				7
#define JF_SAX_IN_ITEM_ARTISTS_ARRAY		8
#define JF_SAX_IN_ITEM_ABLUM_VALUE			9
#define JF_SAX_IN_ITEM_SERIES_VALUE			10
#define JF_SAX_IN_ITEM_YEAR_VALUE			11
#define JF_SAX_IN_ITEM_INDEX_VALUE			12
#define JF_SAX_IN_ITEM_PARENT_INDEX_VALUE	13
#define JF_SAX_IN_USERDATA_MAP				14
#define JF_SAX_IN_USERDATA_VALUE			15
#define JF_SAX_IN_USERDATA_TICKS_VALUE		16


// GENERIC JELLYFIN ITEM REPRESENTATION
#define JF_ITEM_TYPE_NONE		0
#define JF_ITEM_TYPE_COLLECTION	1
#define JF_ITEM_TYPE_FOLDER		2
#define JF_ITEM_TYPE_PLAYLIST	3
#define JF_ITEM_TYPE_AUDIO		4
#define JF_ITEM_TYPE_ARTIST		5
#define JF_ITEM_TYPE_ALBUM		6
#define JF_ITEM_TYPE_EPISODE	7
#define JF_ITEM_TYPE_SEASON		8
#define JF_ITEM_TYPE_SERIES		9
#define JF_ITEM_TYPE_MOVIE		10


typedef struct jf_sax_generic_item {
	size_t type;
	unsigned char *name;			size_t name_len;
	unsigned char *id;				size_t id_len;
	unsigned char *artist;			size_t artist_len;
	unsigned char *album;			size_t album_len;
	unsigned char *series;			size_t series_len;
	unsigned char *year;			size_t year_len;
	unsigned char *index;			size_t index_len;
	unsigned char *parent_index;	size_t parent_index_len;
	long long ticks;
} jf_sax_generic_item;


// SAX PARSER FUNCTION STUBS
static size_t sax_items_start_map(void *ctx);
static size_t sax_items_end_map(void *ctx);
static size_t sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len);
static size_t sax_items_start_array(void *ctx);
static size_t sax_items_end_array(void *ctx);
static size_t sax_items_string(void *ctx, const unsigned char *string, size_t string_len);
static size_t sax_items_number(void *ctx, const unsigned char *string, size_t string_len);
void *jf_sax_parser_thread(void *arg);

// MISC FUNCTION STUBS
void jf_sax_generic_item_clear(jf_sax_generic_item *item);
char *jf_parser_error_string(void);
size_t jf_parse_login_reply(const char *payload, jf_options *options);
char *jf_generate_login_request(const char *username, const char *password);

#endif
