#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_gen.h>

#include "shared.h"
#include "json_parser.h"


static char s_error_buffer[JF_PARSER_ERROR_BUFFER_SIZE];


////////// SAX PARSER CALLBACKS //////////
static int sax_items_start_map(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IDLE:
			context->tb->item_count = 0;
			jf_sax_context_current_item_clear(context);
			context->parser_state = JF_SAX_IN_QUERYRESULT_MAP;
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
	}
	return 1;
}


static int sax_items_end_map(void *ctx)
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
			if (context->current_item_type == JF_ITEM_TYPE_AUDIO
					|| context->current_item_type == JF_ITEM_TYPE_AUDIOBOOK) {
				if (context->tb->promiscuous_context) {
					printf("T %zu. %.*s - %.*s - %.*s\n",
							context->tb->item_count,
							JF_SAX_PRINT_FALLBACK(artist, "[Unknown Artist]"),
							JF_SAX_PRINT_FALLBACK(album, "[Unknown Album]"),
							context->name_len, context->name);
				} else {
					printf("T %zu. %.*s - %.*s\n",
							context->tb->item_count, 
							JF_SAX_PRINT_FALLBACK(index, "??"),
							context->name_len, context->name);
				}
			} else if (context->current_item_type == JF_ITEM_TYPE_ALBUM) {
				if (context->tb->promiscuous_context) {
					printf("D %zu. %.*s - %.*s (%.*s)\n",
							context->tb->item_count,
							JF_SAX_PRINT_FALLBACK(artist, "[Unknown Artist]"),
							context->name_len, context->name,
							JF_SAX_PRINT_FALLBACK(year, "[Unknown Year]"));
				} else {
					printf("D %zu. %.*s (%.*s)\n",
							context->tb->item_count,
							context->name_len, context->name,
							JF_SAX_PRINT_FALLBACK(year, "[Unknown Year]"));
				}
			} else if (context->current_item_type == JF_ITEM_TYPE_EPISODE) {
				if (context->tb->promiscuous_context) {
					printf("V %zu. %.*s - S%.*sE%.*s - %.*s\n",
							context->tb->item_count,
							JF_SAX_PRINT_FALLBACK(series, "[Unknown Series]"),
							JF_SAX_PRINT_FALLBACK(parent_index, "??"),
							JF_SAX_PRINT_FALLBACK(index, "??"),
							context->name_len, context->name);
				} else {
					printf("V %zu. S%.*sE%.*s - %.*s\n",
							context->tb->item_count,
							JF_SAX_PRINT_FALLBACK(parent_index, "??"),
							JF_SAX_PRINT_FALLBACK(index, "??"),
							context->name_len, context->name);
				}
			} else if (context->current_item_type == JF_ITEM_TYPE_SEASON) {
				if (context->tb->promiscuous_context) {
					printf("D %zu. %.*s - %.*s\n", // TODO check if the name contains "Season" or is just the number
							context->tb->item_count,
							JF_SAX_PRINT_FALLBACK(series, "[Unknown Series]"),
							context->name_len, context->name);
				} else {
					printf("D %zu. %.*s\n",
							context->tb->item_count,
							context->name_len, context->name);
				}
			} else if (context->current_item_type == JF_ITEM_TYPE_MOVIE) {
				printf("V %zu. %.*s (%.*s)\n",
						context->tb->item_count,
						context->name_len, context->name,
						JF_SAX_PRINT_FALLBACK(year, "[Unknown Year]"));
			} else if (context->current_item_type == JF_ITEM_TYPE_ARTIST
						|| context->current_item_type == JF_ITEM_TYPE_SERIES
						|| context->current_item_type == JF_ITEM_TYPE_PLAYLIST
						|| context->current_item_type == JF_ITEM_TYPE_FOLDER
						|| context->current_item_type == JF_ITEM_TYPE_COLLECTION) {
				printf("D %zu. %.*s\n",
						context->tb->item_count,
						context->name_len, context->name);
			}

			// SAVE ITEM ID
			if ((context->tb->item_count) * (1 + JF_ID_LENGTH) >= context->tb->parsed_ids_size) {
				// reallocate. this array only ever grows
				context->tb->parsed_ids_size = context->tb->parsed_ids_size * 2;
				if ((context->tb->parsed_ids = realloc(context->tb->parsed_ids, context->tb->parsed_ids_size)) == NULL) {
					return 0;
				}
			}
			memcpy((context->tb->parsed_ids) + ((1 + JF_ID_LENGTH)*(context->tb->item_count -1)),
					&(context->current_item_type), 1);
			memcpy((context->tb->parsed_ids) + (1 + JF_ID_LENGTH)*(context->tb->item_count -1) + 1,
					context->id, JF_ID_LENGTH);

			jf_sax_context_current_item_clear(context);
			context->parser_state = JF_SAX_IN_ITEMS_ARRAY;
			break;
		case JF_SAX_IGNORE:
			context->maps_ignoring--;
			if (context->maps_ignoring == 0 && context->arrays_ignoring == 0) {
				context->parser_state = context->state_to_resume;
				context->state_to_resume = JF_SAX_NO_STATE;
			}
	}
	return 1;
}


static int sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len)
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
			} else if (JF_SAX_KEY_IS("UserData")) {
				context->parser_state = JF_SAX_IN_USERDATA_VALUE;
			}
			break;
		case JF_SAX_IN_USERDATA_MAP:
			if (JF_SAX_KEY_IS("PlaybackPositionTicks")) {
				context->parser_state = JF_SAX_IN_USERDATA_TICKS_VALUE;
			}
	}
	return 1;
}

static int sax_items_start_array(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
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
	}
	return 1;
}


static int sax_items_end_array(void *ctx)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
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
	}
	return 1;
}


static int sax_items_string(void *ctx, const unsigned char *string, size_t string_len)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_ITEM_NAME_VALUE:
			JF_SAX_ITEM_FILL(name);
			context->parser_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_TYPE_VALUE:
			if (JF_SAX_STRING_IS("CollectionFolder")) {
				context->current_item_type = JF_ITEM_TYPE_COLLECTION;
			} else if (JF_SAX_STRING_IS("Folder")) {
				context->current_item_type = JF_ITEM_TYPE_FOLDER;
			} else if (JF_SAX_STRING_IS("Playlist")) {
				context->current_item_type = JF_ITEM_TYPE_PLAYLIST;
			} else if (JF_SAX_STRING_IS("Audio")) {
				context->current_item_type = JF_ITEM_TYPE_AUDIO;
			} else if (JF_SAX_STRING_IS("Artist")) {
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
	}
	return 1;
}


static int sax_items_number(void *ctx, const char *string, size_t string_len)
{
	jf_sax_context *context = (jf_sax_context *)(ctx);
	switch (context->parser_state) {
		case JF_SAX_IN_USERDATA_TICKS_VALUE:
			context->ticks = strtoll(string, NULL, 10);
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
	}
	return 1;
}
//////////////////////////////////////////


void jf_sax_context_init(jf_sax_context *context, jf_thread_buffer *tb)
{
	context->parser_state = JF_SAX_IDLE;
	context->state_to_resume = JF_SAX_NO_STATE;
	context->maps_ignoring = 0;
	context->arrays_ignoring = 0;
	context->tb = tb;
	context->current_item_type = JF_ITEM_TYPE_NONE;
	context->copy_buffer = NULL;
	context->name = context->id = context->artist = context->album = NULL;
	context->series = context->year = context->index = context->parent_index = NULL;
	context->name_len = context->id_len = context->artist_len = 0;
	context->album_len = context->series_len = context->year_len = 0;
	context->index_len = context->parent_index_len = 0;
	context->ticks = 0;
}


void jf_sax_context_current_item_clear(jf_sax_context *context)
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

	free(context->copy_buffer);
	context->copy_buffer = NULL;
}


void jf_sax_context_current_item_copy(jf_sax_context *context)
{
	// allocate a contiguous buffer containing the copied values
	// then update the context pointers to point within it
	size_t item_size;
	size_t used = 0;
	item_size = (size_t)(context->name_len + context->id_len + context->artist_len
		+ context->album_len + context->series_len + context->year_len +
		context->index_len + context->parent_index_len);
	if ((context->copy_buffer = malloc(item_size)) != NULL) {
		JF_SAX_CONTEXT_COPY(name);
		JF_SAX_CONTEXT_COPY(id);
		JF_SAX_CONTEXT_COPY(artist);
		JF_SAX_CONTEXT_COPY(album);
		JF_SAX_CONTEXT_COPY(series);
		JF_SAX_CONTEXT_COPY(year);
		JF_SAX_CONTEXT_COPY(index);
		JF_SAX_CONTEXT_COPY(parent_index);
	}
}



// TODO: proper error mechanism (needs signaling)
// TODO: arguments...
// NB all data created by the thread itself is allocated on the stack,
// so it is safe to detach it
void *jf_sax_parser_thread(void *arg)
{
	jf_sax_context context;
	yajl_status status;
	yajl_handle parser;
	yajl_callbacks callbacks = {
		.yajl_null = NULL,
		.yajl_boolean = NULL,
		.yajl_integer = NULL,
		.yajl_double = NULL,
		.yajl_number = sax_items_number,
		.yajl_string = sax_items_string,
		.yajl_start_map = sax_items_start_map,
		.yajl_map_key = sax_items_map_key,
		.yajl_end_map = sax_items_end_map,
		.yajl_start_array = sax_items_start_array,
		.yajl_end_array = sax_items_end_array
	};

	jf_sax_context_init(&context, (jf_thread_buffer *)arg);

	if ((parser = yajl_alloc(&callbacks, NULL, (void *)(&context))) == NULL) {
		strcpy(context.tb->data, "sax parser yajl_alloc failed");
		return NULL;
	}

	// allow persistent parser to digest many JSON objects
	if (yajl_config(parser, yajl_allow_multiple_values, 1) == 0) {
		yajl_free(parser);
		strcpy(context.tb->data, "sax parser could not allow_multiple_values");
		return NULL;
	}


	pthread_mutex_lock(&context.tb->mut);
	while (1) {
		while (context.tb->used == 0) {
			pthread_cond_wait(&context.tb->cv_no_data, &context.tb->mut);
		}
		if ((status = yajl_parse(parser, (unsigned char*)context.tb->data, context.tb->used)) != yajl_status_ok) {
			unsigned char *error_str = yajl_get_error(parser, 1, (unsigned char*)context.tb->data, context.tb->used);
			printf("%s\n", error_str);
			strcpy(context.tb->data, "yajl_parse error: ");
			strncat(context.tb->data, (char *)error_str, JF_PARSER_ERROR_BUFFER_SIZE - strlen(context.tb->data));
			pthread_mutex_unlock(&context.tb->mut);
			yajl_free_error(parser, error_str);
			return NULL;
		} else if (context.parser_state == JF_SAX_IDLE) {
			yajl_complete_parse(parser);
		} else if (context.copy_buffer == NULL) { // make sure it's not already a copy...
			jf_sax_context_current_item_copy(&context);
		}
		context.tb->used = 0;
		pthread_cond_signal(&context.tb->cv_has_data);
	}
}



char *jf_parser_error_string(void)
{
	return s_error_buffer;
}


bool jf_parse_login_reply(const char *payload, jf_options *options)
{
	yajl_val parsed;
	const char *userid_selector[3] = { "User", "Id", NULL };
	const char *token_selector[2] = { "AccessToken", NULL };
	char *userid;
	char *token;

	s_error_buffer[0] = '\0';
	if ((parsed = yajl_tree_parse(payload, s_error_buffer, JF_PARSER_ERROR_BUFFER_SIZE)) == NULL) {
		if (s_error_buffer[0] == '\0') {
			strcpy(s_error_buffer, "yajl_tree_parse unkown error");
		}
		return false;
	}
	// NB macros propagate NULL
	userid = YAJL_GET_STRING(yajl_tree_get(parsed, userid_selector, yajl_t_string));
	token = YAJL_GET_STRING(yajl_tree_get(parsed, token_selector, yajl_t_string));
	if (userid != NULL && token != NULL) {
		options->userid = strdup(userid);
		options->token = strdup(token);
		yajl_tree_free(parsed);
		return true;
	} else {
		yajl_tree_free(parsed);
		return false;
	}
}


char *jf_generate_login_request(const char *username, const char *password)
{
	yajl_gen gen;
	char *json = NULL;
	size_t json_len;

	if ((gen = yajl_gen_alloc(NULL)) == NULL) return (char *)NULL;
	JF_GEN_BAD_JUMP_OUT(yajl_gen_map_open(gen));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)"Username", JF_STATIC_STRLEN("Username")));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)username, strlen(username)));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)"pw", JF_STATIC_STRLEN("pw")));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)password, strlen(password)));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_map_close(gen));
	JF_GEN_BAD_JUMP_OUT(yajl_gen_get_buf(gen, (const unsigned char **)&json, &json_len));
	json = strndup(json, json_len);

out:
	yajl_gen_free(gen);
	return (char *)json;
}
