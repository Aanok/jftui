#ifndef _JF_JSON_PARSER
#define _JF_JSON_PARSER

#define PARSER_BUF_SIZE 65536

int jf_parser_init(const char *header_pathname, const char *records_pathname, const char *json_pathname);
void jf_parser_cleanup(void);
char *jf_parser_error_string(void);
int jf_sax_parse(void);

#endif
