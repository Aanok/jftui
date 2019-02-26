#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <yajl/yajl_parse.h>
#include <yajl/yajl_tree.h>
#include <yajl/yajl_gen.h>

#include "shared.h"
#include "json_parser.h"


#define GEN_BAD_JUMP_OUT(gen) { if ((gen) != yajl_gen_status_ok) goto out; }


static char *g_header_pathname;
static char *g_records_pathname;
static char *g_json_pathname;
static yajl_handle g_parser = NULL;
static char g_buffer[PARSER_BUF_SIZE];


size_t jf_sax_init(const char *header_pathname, const char *records_pathname, const char *json_pathname)
{
	if (!(g_header_pathname = strdup(header_pathname))) {
		strcpy(g_buffer, "strdup header_pathname failed: ");
		strerror_r(errno, g_buffer + strlen(g_buffer), PARSER_BUF_SIZE - strlen(g_buffer));
		return 0;
	}
	if (!(g_records_pathname = strdup(records_pathname))) {
		strcpy(g_buffer, "strdup records_pathname failed: ");
		strerror_r(errno, g_buffer + strlen(g_buffer), PARSER_BUF_SIZE - strlen(g_buffer));
		free(g_header_pathname);
		return 0;
	}
	if (!(g_json_pathname = strdup(json_pathname))) {
		strcpy(g_buffer, "strdup json_pathname failed: ");
		strerror_r(errno, g_buffer + strlen(g_buffer), PARSER_BUF_SIZE - strlen(g_buffer));
		free(g_header_pathname);
		free(g_records_pathname);
		return 0;
	}
	// TODO all the callbacks...
	return 1;
}


void jf_sax_cleanup(void)
{
	free(g_header_pathname);
	free(g_records_pathname);
	free(g_json_pathname);
	yajl_free(g_parser);
}


char *jf_parser_error_string(void)
{
	return g_buffer;
}


// TODO make parser incremental instead of expecting the complete payload in json_file
size_t jf_sax_parse(void)
{
	size_t read_bytes;
	size_t retval = 1;
	yajl_status status;
	FILE *header_file, *records_file, *json_file;
	if (!(header_file = fopen(g_header_pathname, "w+"))) {
		strcpy(g_buffer, "fopen header_path failed: ");
		strerror_r(errno, g_buffer + strlen(g_buffer), PARSER_BUF_SIZE - strlen(g_buffer));
		return 0;
	}
	if (!(records_file = fopen(g_records_pathname, "w+"))) {
		strcpy(g_buffer, "fopen records_path failed: ");
		fclose(header_file);
		return 0;
	}
	if (!(json_file = fopen(g_json_pathname, "r"))) {
		strcpy(g_buffer, "fopen json_path failed: ");
		strerror_r(errno, g_buffer + strlen(g_buffer), PARSER_BUF_SIZE - strlen(g_buffer));
		fclose(header_file);
		fclose(records_file);
		return 0;
	}

	while ((read_bytes = fread(g_buffer, 1, PARSER_BUF_SIZE - 1, json_file))) {
		g_buffer[read_bytes] = '\0';
		if ((status = yajl_parse(g_parser, (unsigned char*)g_buffer, read_bytes)) != yajl_status_ok) {
			char *error_str = (char *)yajl_get_error(g_parser, 1, (unsigned char*)g_buffer, read_bytes);
			strcpy(g_buffer, "yajl_parse error: ");
			strncat(g_buffer, error_str, PARSER_BUF_SIZE - strlen(g_buffer));
			yajl_free_error(g_parser, (unsigned char*)error_str);
			retval = 0;
		}
	}	
	if (!feof(json_file)) {
		strcpy(g_buffer, "error on json_file read");
		retval = 0;
	}
	fclose(header_file);
	fclose(records_file);
	fclose(json_file);
	return retval;
}


size_t jf_parse_login_reply(const char *payload, jf_options *options)
{
	yajl_val parsed;
	const char *userid_selector[3] = { "User", "Id", NULL };
	const char *token_selector[3] = { "AccessToken", NULL };
	char *userid;
	char *token;

	g_buffer[0] = '\0';
	if ((parsed = yajl_tree_parse(payload, g_buffer, PARSER_BUF_SIZE)) == NULL) {
		if (g_buffer[0] == '\0') {
			strcpy(g_buffer, "yajl_tree_parse unkown error");
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


char * jf_generate_login_request(const char *username, const char *password)
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
