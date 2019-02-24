#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <yajl/yajl_parse.h>

#include "json_parser.h"


static char *g_header_pathname;
static char *g_records_pathname;
static char *g_json_pathname;
static yajl_handle g_parser = NULL;
static char g_buffer[PARSER_BUF_SIZE];


int jf_parser_init(const char *header_pathname, const char *records_pathname, const char *json_pathname)
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


void jf_parser_cleanup(void)
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
int jf_sax_parse(void)
{
	size_t read_bytes;
	int retval = 1;
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
