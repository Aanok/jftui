#ifndef _JF_JSON_PARSER
#define _JF_JSON_PARSER

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_gen.h>

#include "shared.h"
#include "config.h"
#include "disk_io.h"


// CODE MACROS
#define JF_JSON_GEN_FATAL(prod)			\
do {									\
	if ((prod) != yajl_gen_status_ok) {	\
			yajl_gen_free(gen);			\
			return NULL;				\
	}									\
} while (false)

#define JF_SAX_ITEM_FILL(field)						\
do {												\
	context->field = (const unsigned char *)string;	\
	context->field ## _len = string_len;			\
} while (false)

#define JF_SAX_CONTEXT_COPY(field)													\
do {																				\
	if (context->field ## _len > 0) {												\
		strncpy(context->copy_buffer + used, (const char *)context->field,			\
				(size_t)context->field ## _len);									\
		context->field = (const unsigned char *)(context->copy_buffer + used);		\
		used += (size_t)context->field ## _len;										\
	}																				\
} while (false)

#define JF_SAX_KEY_IS(name) (strncmp((const char *)key, name, sizeof(name) > key_len ? key_len : sizeof(name)) == 0)

#define JF_SAX_STRING_IS(name) (strncmp((const char *)string, name, sizeof(name) > string_len ? string_len : sizeof(name)) == 0)

#define JF_SAX_PRINT_LEADER(tag)						\
	do {												\
		printf(tag " %zu. ", context->tb->item_count);	\
	} while (false)

#define JF_SAX_TRY_PRINT(prefix, field, suffix)					\
	do {														\
		if (context->field ## _len > 0) {						\
			JF_STATIC_PRINT(prefix);							\
			write(1, context->field, context->field ## _len);	\
			JF_STATIC_PRINT(suffix);							\
		}														\
	} while (false)

// NB THIS WILL NOT BE NULL-TERMINATED ON ITS OWN!!!
#define JF_SAX_TRY_APPEND_NAME(prefix, field, suffix)						\
	do {																	\
		if (context->field ## _len > 0) {									\
			jf_growing_buffer_append(context->current_item_display_name,	\
					prefix, JF_STATIC_STRLEN(prefix));						\
			jf_growing_buffer_append(context->current_item_display_name,	\
					context->field, context->field ## _len);				\
			jf_growing_buffer_append(context->current_item_display_name,	\
					suffix, JF_STATIC_STRLEN(suffix));						\
		}																	\
	} while (false)


////////// SAX PARSER STATE MACHINE //////////
typedef unsigned char jf_sax_parser_state;

#define JF_SAX_NO_STATE							0
#define JF_SAX_IDLE								1
#define JF_SAX_IN_LATEST_ARRAY					2
#define JF_SAX_IN_QUERYRESULT_MAP				3
#define JF_SAX_IN_ITEMS_VALUE					4
#define JF_SAX_IN_ITEMS_ARRAY					5
#define JF_SAX_IN_ITEM_MAP						6
#define JF_SAX_IN_ITEM_TYPE_VALUE				7
#define JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE	8
#define JF_SAX_IN_ITEM_NAME_VALUE				9
#define JF_SAX_IN_ITEM_ID_VALUE					10
#define JF_SAX_IN_ITEM_ARTISTS_ARRAY			11
#define JF_SAX_IN_ITEM_ARTISTS_VALUE			12
#define JF_SAX_IN_ITEM_ALBUM_VALUE				13
#define JF_SAX_IN_ITEM_SERIES_VALUE				14
#define JF_SAX_IN_ITEM_YEAR_VALUE				15
#define JF_SAX_IN_ITEM_INDEX_VALUE				16
#define JF_SAX_IN_ITEM_PARENT_INDEX_VALUE		17
#define JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE		18
#define JF_SAX_IN_USERDATA_MAP					19
#define JF_SAX_IN_USERDATA_VALUE				20
#define JF_SAX_IN_USERDATA_TICKS_VALUE			21
#define JF_SAX_IGNORE							255
//////////////////////////////////////////////


#define JF_PARSER_ERROR_BUFFER_SIZE 1024


typedef struct jf_sax_context {
	jf_sax_parser_state parser_state;
	jf_sax_parser_state state_to_resume;
	size_t maps_ignoring;
	size_t arrays_ignoring;
	bool latest_array;
	jf_thread_buffer *tb;
	jf_item_type current_item_type;
	char *copy_buffer;
	jf_growing_buffer *current_item_display_name;
	const unsigned char *name;			size_t name_len;
	const unsigned char *id;			size_t id_len;
	const unsigned char *artist;		size_t artist_len;
	const unsigned char *album;			size_t album_len;
	const unsigned char *series;		size_t series_len;
	const unsigned char *year;			size_t year_len;
	const unsigned char *index;			size_t index_len;
	const unsigned char *parent_index;	size_t parent_index_len;
	long long runtime_ticks;
	long long playback_ticks;
} jf_sax_context;


void *jf_sax_parser_thread(void *arg);

char *jf_json_error_string(void);
bool jf_json_parse_login_response(const char *payload);
char *jf_json_generate_login_request(const char *username, const char *password);
char *jf_json_generate_progress_post(const char *id, const long long ticks);

#endif
