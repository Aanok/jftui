#ifndef _JF_JSON_PARSER
#define _JF_JSON_PARSER

#define PARSER_BUF_SIZE 65536

size_t jf_parser_init(const char *header_pathname, const char *records_pathname, const char *json_pathname);
void jf_parser_cleanup(void);
char *jf_parser_error_string(void);
size_t jf_sax_parse(void);
size_t jf_parse_login_reply(const char *payload, jf_options *options);
char *jf_generate_login_request(const char *username, const char *password);

#endif
