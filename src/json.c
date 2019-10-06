#include "json.h"

////////// GLOBALS //////////
extern jf_options g_options;
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC VARIABLES //////////
static char s_error_buffer[JF_PARSER_ERROR_BUFFER_SIZE];
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static int jf_sax_items_start_map(void *ctx);
static int jf_sax_items_end_map(void *ctx);
static int jf_sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len);
static int jf_sax_items_start_array(void *ctx);
static int jf_sax_items_end_array(void *ctx);
static int jf_sax_items_string(void *ctx, const unsigned char *string, size_t strins_len);
static int jf_sax_items_number(void *ctx, const char *string, size_t strins_len);

// Allocates a new yajl parser instance, registering callbacks and context and
// setting yajl_allow_multiple_values to let it digest multiple JSON messages
// in a row.
// Failures cause SIGABRT.
//
// Parameters:
// 	- callbacks: Pointer to callbacks struct to register.
// 	- context: Pointer to json parser context to register.
//
// Returns:
// 	The yajl_handle of the new parser.
static JF_FORCE_INLINE yajl_handle jf_sax_yajl_parser_new(yajl_callbacks *callbacks, jf_sax_context *context);

static JF_FORCE_INLINE void jf_sax_current_item_make_and_print_name(jf_sax_context *context);
static JF_FORCE_INLINE void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb);
static JF_FORCE_INLINE void jf_sax_context_current_item_clear(jf_sax_context *context);
static JF_FORCE_INLINE void jf_sax_context_current_item_copy(jf_sax_context *context);
//////////////////////////////////////


////////// SAX PARSER CALLBACKS //////////
static int jf_sax_items_start_map(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IDLE:
			context->tb->item_count = 0;
			jf_sax_context_current_item_clear(context);
			jf_disk_refresh();
			context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_LATEST_ARRAY:
			context->latest_array = true;
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEMS_ARRAY:
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_USERDATA_VALUE:
			context->parser_state = JF_SAX_IN_USERDATA_MAP;
			break;
		case JF_SAX_IN_ITEM_MAP:
			context->state_to_resume = JF_SAX_IN_ITEM_MAP;
			context->parser_state = JF_SAX_IGNORE;
			context->maps_ignoring = 1;
			break;
		case JF_SAX_IGNORE:
			context->maps_ignoring++;
			break;
		default:
			JF_SAX_BAD_STATE();
	}
	return 1;
}


static int jf_sax_items_end_map(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_QUERYRESULT_MAP:
			context->parser_state = JF_SAX_IDLE;
			break;
		case JF_SAX_IN_ITEMS_VALUE:
			context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_USERDATA_MAP:
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_MAP:
			context->tb->item_count++;
			jf_sax_current_item_make_and_print_name(context);

			jf_menu_item *item = jf_menu_item_new(context->current_item_type,
					NULL,
					(const char*)context->id,
					context->current_item_display_name->buf,
					context->runtime_ticks,
					context->playback_ticks);
			jf_disk_payload_add_item(item);
			jf_menu_item_free(item);
			jf_sax_context_current_item_clear(context);

			if (context->latest_array) {
				context->parser_state = JF_SAX_IN_LATEST_ARRAY;
				context->latest_array = false;
			} else {
				context->parser_state = JF_SAX_IN_ITEMS_ARRAY;
			}
			break;
		case JF_SAX_IGNORE:
			context->maps_ignoring--;
			if (context->maps_ignoring == 0 && context->arrays_ignoring == 0) {
				context->parser_state = context->state_to_resume;
				context->state_to_resume = JF_SAX_NO_STATE;
			}
		default:
			break;
	}
	return 1;
}


static int jf_sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_QUERYRESULT_MAP:
			if (JF_SAX_KEY_IS("Items")) {
				context->parser_state = JF_SAX_IN_ITEMS_VALUE;
			}
			break;
		case JF_SAX_IN_ITEM_MAP:
			if (JF_SAX_KEY_IS("Name")) {
				context->parser_state = JF_SAX_IN_ITEM_NAME_VALUE;
			} else if (JF_SAX_KEY_IS("Type")) {
				context->parser_state = JF_SAX_IN_ITEM_TYPE_VALUE;
			} else if (JF_SAX_KEY_IS("CollectionType")) {
				context->parser_state = JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE;
			} else if (JF_SAX_KEY_IS("Id")) {
				context->parser_state = JF_SAX_IN_ITEM_ID_VALUE;
			} else if (JF_SAX_KEY_IS("Artists")) {
				context->parser_state = JF_SAX_IN_ITEM_ARTISTS_VALUE;
			} else if (JF_SAX_KEY_IS("Album")) {
				context->parser_state = JF_SAX_IN_ITEM_ALBUM_VALUE;
			} else if (JF_SAX_KEY_IS("SeriesName")) {
				context->parser_state = JF_SAX_IN_ITEM_SERIES_VALUE;
			} else if (JF_SAX_KEY_IS("ProductionYear")) {
				context->parser_state = JF_SAX_IN_ITEM_YEAR_VALUE;
			} else if (JF_SAX_KEY_IS("IndexNumber")) {
				context->parser_state = JF_SAX_IN_ITEM_INDEX_VALUE;
			} else if (JF_SAX_KEY_IS("ParentIndexNumber")) {
				context->parser_state = JF_SAX_IN_ITEM_PARENT_INDEX_VALUE;
			} else if (JF_SAX_KEY_IS("RunTimeTicks")) {
				context->parser_state = JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE;
			} else if (JF_SAX_KEY_IS("UserData")) {
				context->parser_state = JF_SAX_IN_USERDATA_VALUE;
			}
			break;
		case JF_SAX_IN_USERDATA_MAP:
			if (JF_SAX_KEY_IS("PlaybackPositionTicks")) {
				context->parser_state = JF_SAX_IN_USERDATA_TICKS_VALUE;
			}
		default:
			break;
	}
	return 1;
}

static int jf_sax_items_start_array(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IDLE:
			context->parser_state = JF_SAX_IN_LATEST_ARRAY;
			context->tb->item_count = 0;
			jf_sax_context_current_item_clear(context);
			break;
		case JF_SAX_IN_ITEMS_VALUE:
			context->parser_state = JF_SAX_IN_ITEMS_ARRAY;
			break;
		case JF_SAX_IN_ITEM_ARTISTS_VALUE:
			context->parser_state = JF_SAX_IN_ITEM_ARTISTS_ARRAY;
			break;
		case JF_SAX_IN_ITEM_MAP:
			context->parser_state = JF_SAX_IGNORE;
			context->state_to_resume = JF_SAX_IN_ITEM_MAP;
			context->arrays_ignoring = 1;
			break;
		case JF_SAX_IGNORE:
			context->arrays_ignoring++;
			break;
		default:
			JF_SAX_BAD_STATE();
	}
	return 1;
}


static int jf_sax_items_end_array(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_LATEST_ARRAY:
			context->parser_state = JF_SAX_IDLE;
			break;
		case JF_SAX_IN_ITEMS_ARRAY:
			context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_ITEM_ARTISTS_ARRAY:
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IGNORE:
			context->arrays_ignoring--;
			if (context->arrays_ignoring == 0 && context->maps_ignoring == 0) {
				context->parser_state = context->state_to_resume;
				context->state_to_resume = JF_SAX_NO_STATE;
			}
			break;
		default:
			JF_SAX_BAD_STATE();
	}
	return 1;
}


static int jf_sax_items_string(void *ctx, const unsigned char *string, size_t string_len)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_ITEM_NAME_VALUE:
			JF_SAX_ITEM_FILL(name);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_TYPE_VALUE:
			if (JF_SAX_STRING_IS("CollectionFolder")
					&& context->current_item_type == JF_ITEM_TYPE_NONE) {
				// don't overwrite if we already got more specific information
				context->current_item_type = JF_ITEM_TYPE_COLLECTION;
			} else if (JF_SAX_STRING_IS("Folder") || JF_SAX_STRING_IS("UserView")
					|| JF_SAX_STRING_IS("Playlist") || JF_SAX_STRING_IS("PlaylistsFolder")) {
				context->current_item_type = JF_ITEM_TYPE_FOLDER;
			} else if (JF_SAX_STRING_IS("Audio")) {
				context->current_item_type = JF_ITEM_TYPE_AUDIO;
			} else if (JF_SAX_STRING_IS("Artist") || JF_SAX_STRING_IS("MusicArtist")) {
				context->current_item_type = JF_ITEM_TYPE_ARTIST;
			} else if (JF_SAX_STRING_IS("MusicAlbum")) {
				context->current_item_type = JF_ITEM_TYPE_ALBUM;
			} else if (JF_SAX_STRING_IS("Episode")) {
				context->current_item_type = JF_ITEM_TYPE_EPISODE;
			} else if (JF_SAX_STRING_IS("Season")) {
				context->current_item_type = JF_ITEM_TYPE_SEASON;
			} else if (JF_SAX_STRING_IS("Series")) {
				context->current_item_type = JF_ITEM_TYPE_SERIES;
			} else if (JF_SAX_STRING_IS("Movie")) {
				context->current_item_type = JF_ITEM_TYPE_MOVIE;
			} else if (JF_SAX_STRING_IS("AudioBook")) {
				context->current_item_type = JF_ITEM_TYPE_AUDIOBOOK;
			}
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_COLLECTION_TYPE_VALUE:
			if (JF_SAX_STRING_IS("music")) {
				context->current_item_type = JF_ITEM_TYPE_COLLECTION_MUSIC;
			} else if (JF_SAX_STRING_IS("tvshows")) {
				context->current_item_type = JF_ITEM_TYPE_COLLECTION_SERIES;
			} else if (JF_SAX_STRING_IS("movies") || JF_SAX_STRING_IS("homevideos")
					|| JF_SAX_STRING_IS("musicvideos")) {
				context->current_item_type = JF_ITEM_TYPE_COLLECTION_MOVIES;
			} else if (JF_SAX_STRING_IS("folders")) {
				context->current_item_type = JF_ITEM_TYPE_FOLDER;
			}
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_ID_VALUE:
			JF_SAX_ITEM_FILL(id);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_ARTISTS_ARRAY:
			// TODO we're effectively keeping only the last one of the list: review how reasonable this is
			JF_SAX_ITEM_FILL(artist);
			break;
		case JF_SAX_IN_ITEM_ALBUM_VALUE:
			JF_SAX_ITEM_FILL(album);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_SERIES_VALUE:
			JF_SAX_ITEM_FILL(series);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		default:
			break;
	}
	return 1;
}


static int jf_sax_items_number(void *ctx, const char *string, size_t string_len)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_ITEM_RUNTIME_TICKS_VALUE:
			context->runtime_ticks = strtoll(string, NULL, 10);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_USERDATA_TICKS_VALUE:
			context->playback_ticks = strtoll(string, NULL, 10);
			context->parser_state = JF_SAX_IN_USERDATA_MAP;
			break;
		case JF_SAX_IN_ITEM_YEAR_VALUE:
			JF_SAX_ITEM_FILL(year);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_INDEX_VALUE:
			JF_SAX_ITEM_FILL(index);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_PARENT_INDEX_VALUE:
			JF_SAX_ITEM_FILL(parent_index);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		default:
			// ignore everything else
			break;
	}
	return 1;
}
//////////////////////////////////////////


////////// SAX PARSER //////////
static JF_FORCE_INLINE void jf_sax_current_item_make_and_print_name(jf_sax_context *context)
{
	jf_growing_buffer_empty(context->current_item_display_name);
	switch (context->current_item_type) {
		case JF_ITEM_TYPE_AUDIO:
		case JF_ITEM_TYPE_AUDIOBOOK:
			JF_SAX_PRINT_LEADER("T");
			if (context->tb->promiscuous_context) {
				JF_SAX_TRY_APPEND_NAME("", artist, " - ");
				JF_SAX_TRY_APPEND_NAME("", album, " - ");
			}
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			break;
		case JF_ITEM_TYPE_ALBUM:
			JF_SAX_PRINT_LEADER("D");
			if (context->tb->promiscuous_context) {
				JF_SAX_TRY_APPEND_NAME("", artist, " - ");
			}
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			JF_SAX_TRY_APPEND_NAME(" (", year, ")");
			break;
		case JF_ITEM_TYPE_EPISODE:
			JF_SAX_PRINT_LEADER("V");
			if (context->tb->promiscuous_context) {
				JF_SAX_TRY_APPEND_NAME("", series, " - ");
				JF_SAX_TRY_APPEND_NAME("S", parent_index, "");
				JF_SAX_TRY_APPEND_NAME("E", index, " ");
			}
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			break;
		case JF_ITEM_TYPE_SEASON:
			JF_SAX_PRINT_LEADER("D");
			if (context->tb->promiscuous_context) {
				JF_SAX_TRY_APPEND_NAME("", series, " - ");
			}
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			break;
		case JF_ITEM_TYPE_MOVIE:
			JF_SAX_PRINT_LEADER("V");
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			JF_SAX_TRY_APPEND_NAME(" (", year, ")");
			break;
		case JF_ITEM_TYPE_ARTIST:
		case JF_ITEM_TYPE_SERIES:
		case JF_ITEM_TYPE_PLAYLIST:
		case JF_ITEM_TYPE_FOLDER:
		case JF_ITEM_TYPE_COLLECTION:
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
		case JF_ITEM_TYPE_COLLECTION_SERIES:
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
		case JF_ITEM_TYPE_USER_VIEW:
			JF_SAX_PRINT_LEADER("D");
			jf_growing_buffer_append(context->current_item_display_name,
					context->name, context->name_len);
			break;
		default:
			fprintf(stderr, "Warning: jf_sax_items_end_map: unexpected jf_item_type. This is a bug.\n");
	}

	jf_growing_buffer_append(context->current_item_display_name, "", 1);
	printf("%s\n", context->current_item_display_name->buf);
}


static JF_FORCE_INLINE yajl_handle jf_sax_yajl_parser_new(yajl_callbacks *callbacks, jf_sax_context *context)
{
	yajl_handle parser;
	assert((parser = yajl_alloc(callbacks, NULL, (void *)(context))) != NULL);
	// allow persistent parser to digest many JSON objects
	assert(yajl_config(parser, yajl_allow_multiple_values, 1) != 0);
	return parser;
}


static JF_FORCE_INLINE void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb)
{
	*context = (jf_sax_context){ 0 };
	context->parser_state = JF_SAX_IDLE;
	context->state_to_resume = JF_SAX_NO_STATE;
	context->latest_array = false;
	context->tb = tb;
	context->current_item_type = JF_ITEM_TYPE_NONE;
	context->current_item_display_name = jf_growing_buffer_new(0);
}


static JF_FORCE_INLINE void jf_sax_context_current_item_clear(jf_sax_context *context)
{
	context->current_item_type = JF_ITEM_TYPE_NONE;
	context->name_len = 0;
	context->id_len = 0;
	context->artist_len = 0;
	context->album_len = 0;
	context->series_len = 0;
	context->year_len = 0;
	context->index_len = 0;
	context->parent_index_len = 0;
	context->runtime_ticks = 0;
	context->playback_ticks = 0;

	free(context->copy_buffer);
	context->copy_buffer = NULL;
}


static JF_FORCE_INLINE void jf_sax_context_current_item_copy(jf_sax_context *context)
{
	// allocate a contiguous buffer containing the copied values
	// then update the context pointers to point within it
	size_t item_size;
	size_t used = 0;
	item_size = (size_t)(context->name_len + context->id_len
			+ context->artist_len + context->album_len + context->series_len
			+ context->year_len + context->index_len + context->parent_index_len);
	assert((context->copy_buffer = malloc(item_size)) != NULL);
	JF_SAX_CONTEXT_COPY(name);
	JF_SAX_CONTEXT_COPY(id);
	JF_SAX_CONTEXT_COPY(artist);
	JF_SAX_CONTEXT_COPY(album);
	JF_SAX_CONTEXT_COPY(series);
	JF_SAX_CONTEXT_COPY(year);
	JF_SAX_CONTEXT_COPY(index);
	JF_SAX_CONTEXT_COPY(parent_index);
}


void *jf_json_sax_thread(void *arg)
{
	jf_sax_context context;
	yajl_status status;
	yajl_handle parser;
	yajl_callbacks callbacks = {
		.yajl_null = NULL,
		.yajl_boolean = NULL,
		.yajl_integer = NULL,
		.yajl_double = NULL,
		.yajl_number = jf_sax_items_number,
		.yajl_string = jf_sax_items_string,
		.yajl_start_map = jf_sax_items_start_map,
		.yajl_map_key = jf_sax_items_map_key,
		.yajl_end_map = jf_sax_items_end_map,
		.yajl_start_array = jf_sax_items_start_array,
		.yajl_end_array = jf_sax_items_end_array
	};
	unsigned char *error_str;

	jf_sax_context_init(&context, (jf_thread_buffer *)arg);

	assert((parser = jf_sax_yajl_parser_new(&callbacks, &context)) != NULL);

	pthread_mutex_lock(&context.tb->mut);
	while (true) {
		while (context.tb->state != JF_THREAD_BUFFER_STATE_PENDING_DATA) {
			pthread_cond_wait(&context.tb->cv_no_data, &context.tb->mut);
		}
		if ((status = yajl_parse(parser, (unsigned char*)context.tb->data, context.tb->used)) != yajl_status_ok) {
			error_str = yajl_get_error(parser, 1, (unsigned char*)context.tb->data, context.tb->used);
			strcpy(context.tb->data, "yajl_parse error: ");
			strncat(context.tb->data, (char *)error_str, JF_PARSER_ERROR_BUFFER_SIZE - strlen(context.tb->data));
			context.tb->state = JF_THREAD_BUFFER_STATE_PARSER_ERROR;
			pthread_mutex_unlock(&context.tb->mut);
			yajl_free_error(parser, error_str);
			// the parser never recovers after an error; we must free and reallocate it
			yajl_free(parser);
			parser = jf_sax_yajl_parser_new(&callbacks, &context);
		} else if (context.parser_state == JF_SAX_IDLE) {
			// JSON fully parsed
			yajl_complete_parse(parser);
			context.tb->state = JF_THREAD_BUFFER_STATE_CLEAR;
		} else if (context.copy_buffer == NULL) {
			// we've still more to go, so we populate the copy buffer to not lose data
			// but if it is already filled from last time, filling it again would be unnecessary
			// and lead to a memory leak
			context.tb->state = JF_THREAD_BUFFER_STATE_AWAITING_DATA;
			jf_sax_context_current_item_copy(&context);
		} else {
			context.tb->state = JF_THREAD_BUFFER_STATE_AWAITING_DATA;
		}
		
		context.tb->used = 0;
		pthread_cond_signal(&context.tb->cv_has_data);
	}
}
////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
char *jf_json_error_string()
{
	return s_error_buffer;
}


void jf_json_parse_login_response(const char *payload)
{
	yajl_val parsed;
	const char *userid_selector[3] = { "User", "Id", NULL };
	const char *token_selector[2] = { "AccessToken", NULL };
	char *userid;
	char *token;

	s_error_buffer[0] = '\0';
	if ((parsed = yajl_tree_parse(payload, s_error_buffer, JF_PARSER_ERROR_BUFFER_SIZE)) == NULL) {
		fprintf(stderr, "FATAL: jf_json_parse_login_response: %s\n",
				s_error_buffer[0] == '\0' ? "yajl_tree_parse unknown error" : s_error_buffer);
		jf_exit(JF_EXIT_FAILURE);
	}
	// NB macros propagate NULL
	userid = YAJL_GET_STRING(yajl_tree_get(parsed, userid_selector, yajl_t_string));
	token = YAJL_GET_STRING(yajl_tree_get(parsed, token_selector, yajl_t_string));
	assert(userid != NULL);
	assert(token != NULL);
	free(g_options.userid);
	g_options.userid = strdup(userid);
	free(g_options.token);
	g_options.token = strdup(token);
	yajl_tree_free(parsed);
}


char *jf_json_generate_login_request(const char *username, const char *password)
{
	yajl_gen gen;
	char *json = NULL;
	size_t json_len;

	assert((gen = yajl_gen_alloc(NULL)) != NULL);
	assert(yajl_gen_map_open(gen) == yajl_status_ok);
	assert(yajl_gen_string(gen, (const unsigned char *)"Username", JF_STATIC_STRLEN("Username")) == yajl_status_ok);
	assert(yajl_gen_string(gen, (const unsigned char *)username, strlen(username)) == yajl_status_ok);
	assert(yajl_gen_string(gen, (const unsigned char *)"Pw", JF_STATIC_STRLEN("Pw")) == yajl_status_ok);
	assert(yajl_gen_string(gen, (const unsigned char *)password, strlen(password)) == yajl_status_ok);
	assert(yajl_gen_map_close(gen) == yajl_status_ok);
	assert(yajl_gen_get_buf(gen, (const unsigned char **)&json, &json_len) == yajl_status_ok);
	assert((json = strndup(json, json_len)) != NULL);

	yajl_gen_free(gen);
	return json;
}


void jf_json_parse_server_info_response(const char *payload)
{
	yajl_val parsed;
	const char *server_name_selector[2] = { "ServerName", NULL };

	s_error_buffer[0] = '\0';
	if ((parsed = yajl_tree_parse(payload, s_error_buffer, JF_PARSER_ERROR_BUFFER_SIZE)) == NULL) {
		fprintf(stderr, "FATAL: jf_json_parse_login_response: %s\n",
				s_error_buffer[0] == '\0' ? "yajl_tree_parse unknown error" : s_error_buffer);
		jf_exit(JF_EXIT_FAILURE);
	}
	// NB macros propagate NULL
	assert((g_state.server_name = YAJL_GET_STRING(yajl_tree_get(parsed, server_name_selector, yajl_t_string))) != NULL);
	g_state.server_name = strdup(g_state.server_name);
	yajl_tree_free(parsed);
}


char *jf_json_generate_progress_post(const char *id, const long long ticks)
{
	yajl_gen gen;
	char *json = NULL;
	size_t json_len;

	assert((gen = yajl_gen_alloc(NULL)) != NULL);
	assert(yajl_gen_map_open(gen) == yajl_status_ok);
	assert(yajl_gen_string(gen,
				(const unsigned char *)"ItemId",
				JF_STATIC_STRLEN("ItemId")) == yajl_status_ok);
	assert(yajl_gen_string(gen,
				(const unsigned char *)id,
				JF_ID_LENGTH) == yajl_status_ok);
	assert(yajl_gen_string(gen,
				(const unsigned char *)"PositionTicks",
				JF_STATIC_STRLEN("PositionTicks")) == yajl_status_ok);
	assert(yajl_gen_integer(gen, ticks) == yajl_status_ok);
	assert(yajl_gen_map_close(gen) == yajl_status_ok);
	assert(yajl_gen_get_buf(gen,
				(const unsigned char **)&json,
				&json_len) == yajl_status_ok);
	assert((json = strndup(json, json_len)) != NULL);

	yajl_gen_free(gen);
	return json;
}
///////////////////////////////////////////
