#ifndef _JF_NETWORK
#define _JF_NETWORK

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "shared.h"
#include "json_parser.h"
#include "menu.h"
#include "config.h"


// error codes
#define JF_REPLY_ERROR_STUB -1
#define JF_REPLY_ERROR_UNINITIALIZED -2
#define JF_REPLY_ERROR_NETWORK -3
#define JF_REPLY_ERROR_HTTP_401 -4
#define JF_REPLY_ERROR_HTTP_NOT_OK -5
#define JF_REPLY_ERROR_MALLOC -6
#define JF_REPLY_ERROR_X_EMBY_AUTH -7


////////// REQUEST TYPES //////////
typedef size_t jf_request_type;

#define JF_REQUEST_IN_MEMORY		0
#define JF_REQUEST_SAX_PROMISCUOUS	1
#define JF_REQUEST_SAX				2
///////////////////////////////////


// function prototypes
jf_reply *jf_reply_new(void);
void jf_reply_free(jf_reply *r);
char *jf_reply_error_string(const jf_reply *r);
size_t jf_thread_buffer_callback(char *payload, size_t size, size_t nmemb, __attribute__((unused)) void *userdata);
jf_menu_item jf_thread_buffer_get_parsed_item(size_t n);
size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata);
bool jf_network_init(const jf_options *options);
bool jf_network_make_headers(void);
bool jf_network_reload_token(void);
jf_reply *jf_request(const char *resource, jf_request_type request_type, const char *POST_payload);
jf_reply *jf_login_request(const char *POST_payload);
void jf_network_cleanup(void);

#endif
