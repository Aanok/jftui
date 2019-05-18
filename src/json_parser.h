#ifndef _JF_JSON_PARSER
#define _JF_JSON_PARSER


// CODE MACROS
#define JF_GEN_BAD_JUMP_OUT(gen) { if ((gen) != yajl_gen_status_ok) goto out; }

#define JF_SAX_ITEM_FILL(field)												\
{																			\
	context->field = (const unsigned char *)string;							\
	context->field ## _len = (int)string_len;								\
}
#define JF_SAX_KEY_IS(name) (strncmp((const char *)key, name, sizeof(name) > key_len ? key_len : sizeof(name)) == 0)

#define JF_SAX_STRING_IS(name) (strncmp((const char *)string, name, sizeof(name) > string_len ? string_len : sizeof(name)) == 0)

#define JF_SAX_PRINT_FALLBACK(field, fallback)										\
	context->field ## _len > 0 ? context->field ## _len : (int)sizeof(fallback) - 1,\
	context->field ## _len > 0 ? context->field : (unsigned char *)fallback

#define JF_SAX_CONTEXT_COPY(field)													\
{																					\
	if (context->field ## _len > 0) {												\
		strncpy(context->copy_buffer + used, (const char *)context->field,			\
				(size_t)context->field ## _len);									\
		context->field = (const unsigned char *)(context->copy_buffer + used);		\
		used += (size_t)context->field ## _len;										\
	}																				\
}



// SAX PARSER STATE MACHINE
#define JF_SAX_NO_STATE						0
#define JF_SAX_IDLE							1
#define JF_SAX_IN_QUERYRESULT_MAP			2
#define JF_SAX_IN_ITEMS_VALUE				3
#define JF_SAX_IN_ITEMS_ARRAY				4
#define JF_SAX_IN_ITEM_MAP					5
#define JF_SAX_IN_ITEM_TYPE_VALUE			6
#define JF_SAX_IN_ITEM_NAME_VALUE			7
#define JF_SAX_IN_ITEM_ID_VALUE				8
#define JF_SAX_IN_ITEM_ARTISTS_ARRAY		9
#define JF_SAX_IN_ITEM_ARTISTS_VALUE		10
#define JF_SAX_IN_ITEM_ALBUM_VALUE			11
#define JF_SAX_IN_ITEM_SERIES_VALUE			10
#define JF_SAX_IN_ITEM_YEAR_VALUE			11
#define JF_SAX_IN_ITEM_INDEX_VALUE			12
#define JF_SAX_IN_ITEM_PARENT_INDEX_VALUE	13
#define JF_SAX_IN_USERDATA_MAP				14
#define JF_SAX_IN_USERDATA_VALUE			15
#define JF_SAX_IN_USERDATA_TICKS_VALUE		16
#define JF_SAX_IGNORE						666


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
#define JF_ITEM_TYPE_AUDIOBOOK	11


#define JF_PARSER_ERROR_BUFFER_SIZE 1024

typedef struct jf_sax_context {
	size_t parser_state;
	size_t state_to_resume;
	size_t maps_ignoring;
	size_t arrays_ignoring;
	jf_thread_buffer *tb;
	size_t item_count;
	size_t current_item_type;
	char *copy_buffer;
	// the following _len specifiers must be int's instead of size_t's for the sake of printf precision
	const unsigned char *name;			int name_len;
	const unsigned char *id;			int id_len;
	const unsigned char *artist;		int artist_len;
	const unsigned char *album;			int album_len;
	const unsigned char *series;		int series_len;
	const unsigned char *year;			int year_len;
	const unsigned char *index;			int index_len;
	const unsigned char *parent_index;	int parent_index_len;
	long long ticks;
} jf_sax_context;


// SAX PARSER FUNCTION STUBS
static int sax_items_start_map(void *ctx) __attribute__((unused));
static int sax_items_end_map(void *ctx) __attribute__((unused));
static int sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len) __attribute__((unused));
static int sax_items_start_array(void *ctx) __attribute__((unused));
static int sax_items_end_array(void *ctx) __attribute__((unused));
static int sax_items_string(void *ctx, const unsigned char *string, size_t strins_len) __attribute__((unused));
static int sax_items_number(void *ctx, const char *string, size_t strins_len) __attribute__((unused));


void *jf_sax_parser_thread(void *arg);

// MISC FUNCTION STUBS
void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb);
void jf_sax_context_current_item_clear(jf_sax_context *context);
void jf_sax_context_current_item_copy(jf_sax_context *context);
char *jf_parser_error_string(void);
size_t jf_parse_login_reply(const char *payload, jf_options *options);
char *jf_generate_login_request(const char *username, const char *password);

#endif
