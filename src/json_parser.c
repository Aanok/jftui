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


static unsigned char g_error_buffer[1024];
// NB accesses to the following happen from the sax thread ONLY so it's safe for them to be a lock-free upvalue
static size_t g_sax_state = JF_SAX_IDLE;
static jf_sax_generic_item g_sax_item = {
	.type = NULL, .name = NULL, .id = NULL, .artist = NULL, .album = NULL, .series = NULL
}
static size_t g_sax_item_count;
// TODO this one requires a lock and messaging from the main
static size_t g_sax_promiscous_context;


////////// SAX PARSER CALLBACKS //////////
static size_t sax_items_start_map(void *ctx)
{
	switch (g_sax_state) {
		case JF_SAX_IDLE:
			g_sax_item_count = 0;
			g_sax_state = JF_SAX_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_ITEMS_ARRAY:
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_USERDATA_VALUE:
			g_sax_state = JF_SAX_IN_USERDATA_MAP;
			break;
	}
	return 1;
}


static size_t sax_items_end_map(void *ctx)
{
	jf_thread_buffer *tb;
	switch (g_sax_state) {
		case JF_SAX_IN_QUERYRESULT_VALUE:
			g_sax_state = JF_SAX_IDLE;
			break;
		case JF_SAX_IN_ITEMS_VALUE:
			g_sax_state = JF_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_ITEM_MAP:
			g_sax_item_count++;
			// TODO: print!
			tb = (jf_thread_buffer *)ctx;
			if (g_sax_item.type == JF_ITEM_TYPE_AUDIO) {
				if (tb->promiscuous_context) {
					printf("T %d. %.*s - %.*s - %.*s\n",
							g_sax_item_count,
							SAX_PRINT_FALLBACK(artist, "[Unknown Artist]"),
							SAX_PRINT_FALLBACK(album, "[Unknown Album]"),
							g_sax_item.name_len, g_sax_item.name);
				} else {
					printf("T %d. %.*s - %.*s\n",
							g_sax_item_count, 
							SAX_PRINT_FALLBACK(index, "??"),
							g_sax_item.name_len, g_sax_item.name);
				}
			} else if (g_sax_item.type == JF_ITEM_TYPE_ARTIST) {
				printf("D %d. %.*s\n",
						g_sax_item_count,
						g_sax_item.name_len, g_sax_item.name);
			} else if (g_sax_item.type == JF_ITEM_TYPE_ALBUM) {
				if (tb->promiscuous_context) {
					printf("D %d. %.*s - %.*s (%.*s)\n",
							g_sax_item_count,
							SAX_PRINT_FALLBACK(artist, "[Unknown Artist]"),
							g_sax_item.name_len, g_sax_item.name,
							SAX_PRINT_FALLBACK(year, "[Unknown Year]"));
				} else {
					printf("D %d. %.*s (%.*s)\n",
							g_sax_item_count,
							g_sax_item.name_len, g_sax_item.name,
							SAX_PRINT_FALLBACK(year, "[Unknown Year]"));
				}
			}
			// TODO: save id somewhere
			g_sax_state = JF_SAX_IN_ITEMS_ARRAY;
	}
	return 1;
}


static size_t sax_items_map_key(void *ctx, const unsigned char *key, size_t key_len)
{
	switch (g_sax_state) {
		case JF_SAX_IN_QUERYRESULT_MAP:
			if (SAX_KEY_IS("Items")) {
				g_sax_state = JF_SAX_IN_ITEMS_VALUE;
			}
			break;
		case JF_SAX_IN_ITEM_MAP:
			if (SAX_KEY_IS("Name")) {
				g_sax_state = JF_SAX_IN_ITEM_NAME_VALUE;
			} else if (SAX_KEY_IS("Type")) {
				g_sax_state = JF_SAX_IN_ITEM_TYPE_VALUE;
			} else if (SAX_KEY_IS("Id")) {
				g_sax_state = JF_SAX_IN_ITEM_ID_VALUE;
			} else if (SAX_KEY_IS("Artists")) {
				g_sax_state = JF_SAX_IN_ITEM_ARTISTS_VALUE;
			} else if (SAX_KEY_IS("Album")) {
				g_sax_state = JF_SAX_IN_ITEM_ALBUM_VALUE;
			} else if (SAX_KEY_IS("SeriesName")) {
				g_sax_state = JF_SAX_IN_ITEM_SERIES_VALUE;
			} else if (SAX_KEY_IS("ProductionYear")) {
				g_sax_state = JF_SAX_IN_ITEM_YEAR_VALUE;
			} else if (SAX_KEY_IS("IndexNumber")) {
				g_sax_state = JF_SAX_IN_ITEM_INDEX_VALUE;
			} else if (SAX_KEY_IS("IndexParentNumber")) {
				g_sax_state = JF_SAX_IN_ITEM_PARENT_INDEX_VALUE;
			} else if (SAX_KEY_IS("UserData")) {
				g_sax_state = JF_SAX_IN_USERDATA_VALUE;
			}
			break;
		case JF_SAX_IN_USERDATA_MAP:
			if (SAX_KEY_IS("PlaybackPositionTicks")) {
				g_sax_state = JF_SAX_IN_USERDATA_TICKS_VALUE;
			}
	}
	return 1;
}

static size_t sax_items_start_array(void *ctx)
{
	switch (g_sax_state) {
		case JF_SAX_IN_ITEMS_VALUE:
			g_sax_state = JF_SAX_IN_ITEMS_ARRAY;
			break;
		case JF_SAX_IN_ITEM_ARTISTS_VALUE:
			g_sax_state = JF_SAX_IN_ITEM_ARTISTS_ARRAY;
			break;
	}
	return 1;
}


static size_t sax_items_end_array(void *ctx)
{
	switch (g_sax_state) {
		case JF_SAX_IN_ITEMS_ARRAY:
			g_sax_state = JF_SAX_IN_QUERYRESULT_MAP;
			break;
		case JF_SAX_IN_ITEM_ARTISTS_ARRAY:
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
	}
	return 1;
}


static size_t sax_items_string(void *ctx, const unsigned char *string, size_t string_len)
{
	switch (g_sax_state) {
		case JF_SAX_IN_ITEM_NAME_VALUE:
			SAX_ITEM_FILL(name);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_TYPE_VALUE:
			if (JF_SAX_STRING_IS("CollectionFolder")) {
				g_sax_item.type = JF_ITEM_TYPE_COLLECTION;
			} else if (JF_SAX_STRING_IS("Folder")) {
				g_sax_item.type = JF_ITEM_TYPE_FOLDER;
			} else if (JF_SAX_STRING_IS("Playlist")) {
				g_sax_item.type = JF_ITEM_TYPE_PLAYLIST;
			} else if (JF_SAX_STRING_IS("Audio")) {
				g_sax_item.type = JF_ITEM_TYPE_AUDIO;
			} else if (JF_SAX_STRING_IS("Artist")) {
				g_sax_item.type = JF_ITEM_TYPE_ARTIST;
			} else if (JF_SAX_STRING_IS("Album")) {
				g_sax_item.type = JF_ITEM_TYPE_ALBUM;
			} else if (JF_SAX_STRING_IS("Episode")) {
				g_sax_item.type = JF_ITEM_TYPE_EPISODE;
			} else if (JF_SAX_STRING_IS("Season")) { 
				g_sax_item.type = JF_ITEM_TYPE_SEASON; 
			} else if (JF_SAX_STRING_IS("Series")) {
				g_sax_item.type = JF_ITEM_TYPE_SERIES;
			} else if (JF_SAX_STRING_IS("Movie")) {
				g_sax_item.type = JF_ITEM_TYPE_MOVIE;	
			}
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_ID_VALUE:
			SAX_ITEM_FILL(id);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_ARTIST_ARRAY:
			// TODO we're effectively keeping only the last one of the list: review how reasonable this is
			SAX_ITEM_FILL(artist);
			break;
		case JF_SAX_IN_ITEM_ALBUM_VALUE:
			SAX_ITEM_FILL(album);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_SERIES_VALUE:
			SAX_ITEM_FILL(series);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
	}
	return 1;
}


static size_t sax_items_number(void *ctx, const char *string, size_t string_len)
{
	switch (g_sax_state) {
		case JF_SAX_IN_USERDATA_TICKS_VALUE:
			g_sax_item.ticks = strtoll(string, NULL, 10);
			g_sax_state = JF_SAX_IN_USERDATA_MAP;
			break;
		case JF_SAX_IN_ITEM_YEAR_VALUE:
			SAX_ITEM_FILL(year);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_INDEX_VALUE:
			SAX_ITEM_FILL(index);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
		case JF_SAX_IN_ITEM_PARENT_INDEX_VALUE:
			SAX_ITEM_FILL(parent_index);
			g_sax_state = JF_SAX_IN_ITEM_MAP;
			break;
	}
	return 1;
}
//////////////////////////////////////////


// TODO: proper error mechanism (needs signaling)
// TODO: arguments...
void *jf_sax_parser_thread(void *arg)
{
	size_t read_bytes;
	jf_thread_buffer *tb = (jf_thread_buffer *)arg;
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

	/*
	FILE *header_file;
	FILE *records_file;
	char *header_pathname;
	char *records_pathname;
	*/

	/*
	if ((header_pathname = strdup(header_pathname)) == NULL) {
		strcpy(tb->data, "strdup header_pathname failed: ");
		strerror_r(errno, tb->data + strlen(tb->data), PARSER_BUF_SIZE - strlen(tb->data));
		return NULL;
	}
	if ((records_pathname = strdup(records_pathname)) == NULL) {
		strcpy(tb->data, "strdup records_pathname failed: ");
		strerror_r(errno, tb->data + strlen(tb->data), PARSER_BUF_SIZE - strlen(tb->data));
		free(header_pathname);
		return NULL;
	}
	if (!(header_file = fopen(header_pathname, "w+"))) {
		strcpy(tb->data, "fopen header_path failed: ");
		strerror_r(errno, tb->data + strlen(tb->data), PARSER_BUF_SIZE - strlen(tb->data));
		return NULL;
	}
	if (!(records_file = fopen(records_pathname, "w+"))) {
		strcpy(tb->data, "fopen records_path failed: ");
		strerror_r(errno, tb->data + strlen(tb->data), PARSER_BUF_SIZE - strlen(tb->data));
		fclose(header_file);
		return NULL;
	}
	*/
	if ((parser = yajl_alloc(&callbacks, NULL, (void *)tb)) == NULL) {
		strcpy(tb->data "sax parser yajl_alloc failed");
		return NULL;
	}


	pthread_mutex_lock(&tb->mut);
	while (1) {
		while (tb->used == 0) {
			pthread_cond_wait(&tb->cv_no_data, tb->mut);
		}
		if ((status = yajl_parse(parser, (unsigned char*)tb->data, tb->used)) != yajl_status_ok) {
			char *error_str = (char *)yajl_get_error(parser, 1, (unsigned char*)tb->data, read_bytes);
			strcpy(tb->data, "yajl_parse error: ");
			strncat(tb->data, error_str, PARSER_BUF_SIZE - strlen(tb->data));
			pthread_mutex_unlock(&tb->mut);
			yajl_free_error(parser, (unsigned char*)error_str);
// 			fclose(header_file);
// 			fclose(records_file);
			return NULL;
		}
		tb->used = 0;
		pthread_cond_signal(&tb->cv_has_data);
	}
}



unsigned char *jf_parser_error_string()
{
	return g_error_buffer;
}


size_t jf_parse_login_reply(const char *payload, jf_options *options)
{
	yajl_val parsed;
	const char *userid_selector[3] = { "User", "Id", NULL };
	const char *token_selector[3] = { "AccessToken", NULL };
	char *userid;
	char *token;

	g_error_buffer[0] = '\0';
	if ((parsed = yajl_tree_parse(payload, buf, strlen(payload))) == NULL) {
		if (g_error_buffer[0] == '\0') {
			strcpy(g_error_buffer, "yajl_tree_parse unkown error");
		}
		return 0;
	}
	// NB macros propagate NULL
	userid = YAJL_GET_STRING(yajl_tree_get(parsed, userid_selector, yajl_t_string));
	token = YAJL_GET_STRING(yajl_tree_get(parsed, token_selector, yajl_t_string));
	if (userid != NULL && token != NULL) {
		options->userid = strdup(userid);
		options->token = strdup(token);
		yajl_tree_free(parsed);
		return 1;
	} else {
		yajl_tree_free(parsed);
		return 0;
	}
}


char *jf_generate_login_request(const char *username, const char *password)
{
	yajl_gen gen;
	char *json = NULL;
	size_t json_len;

	if ((gen = yajl_gen_alloc(NULL)) == NULL) return (char *)NULL;
	GEN_BAD_JUMP_OUT(yajl_gen_map_open(gen));
	GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)"Username", STATIC_STRLEN("Username")));
	GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)username, strlen(username)));
	GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)"pw", STATIC_STRLEN("pw")));
	GEN_BAD_JUMP_OUT(yajl_gen_string(gen, (const unsigned char *)password, strlen(password)));
	GEN_BAD_JUMP_OUT(yajl_gen_map_close(gen));
	GEN_BAD_JUMP_OUT(yajl_gen_get_buf(gen, (const unsigned char **)&json, &json_len));
	json = strndup(json, json_len);

out:
	yajl_gen_free(gen);
	return (char *)json;
}
