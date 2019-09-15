#ifndef _JF_NET
#define _JF_NET

#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "shared.h"
#include "json.h"
#include "menu.h"
#include "config.h"


////////// JF_REPLY //////////
// size < 0 means an error occurred
typedef struct jf_reply {
	char *payload;
	int size;
} jf_reply;

#define JF_REPLY_ERROR_STUB				-1
#define JF_REPLY_ERROR_UNINITIALIZED	-2
#define JF_REPLY_ERROR_NETWORK			-3
#define JF_REPLY_ERROR_HTTP_401			-4
#define JF_REPLY_ERROR_HTTP_NOT_OK		-5
#define JF_REPLY_ERROR_MALLOC			-6
#define JF_REPLY_ERROR_CONCAT			-7
#define JF_REPLY_ERROR_X_EMBY_AUTH		-8
#define JF_REPLY_ERROR_PARSER			-9
#define JF_REPLY_ERROR_PARSER_DEAD		-10

#define JF_REPLY_PTR_HAS_ERROR(reply_ptr)	((reply_ptr)->size < 0)
#define JF_REPLY_PTR_GET_ERROR(reply_ptr)	((reply_ptr)->size)
#define JF_REPLY_PTR_ERROR_IS(reply_ptr, error_code) ((reply_ptr)->size == (error_code))

jf_reply *jf_reply_new(void);
void jf_reply_free(jf_reply *r);
char *jf_reply_error_string(const jf_reply *r);
//////////////////////////////


////////// REQUEST TYPES //////////
typedef unsigned char jf_request_type;

#define JF_REQUEST_IN_MEMORY		0
#define JF_REQUEST_SAX				1
#define JF_REQUEST_SAX_PROMISCUOUS	2
///////////////////////////////////


// function prototypes
size_t jf_thread_buffer_item_count(void);
void jf_thread_buffer_clear_error(void);

bool jf_net_pre_init(void);
bool jf_net_refresh(void);
void jf_net_clear(void);


////////// NETWORKING //////////

// Function: jf_net_request
//
// Executes a network request to the Jellyfin server. The response may be entirely put in a single jf_reply in memory or passed step by step to the JSON parser thread with constant memory usage. In the latter case, the function will wait for parsing to be complete before returning.
//
// Parameters:
// 	resource - The suffix of the resource to request. Final URL will be the result of appending this to the server's address.
// 	request_type - JF_REQUEST_IN_MEMORY will cause the response to be passed back in a jf_reply struct, JF_REQUEST_SAX will cause the response to be passed to the JSON parser and digested as a non-promiscuous context, JF_REQUEST_SAX_PROMISCUOUS likewise but digested as a promiscuous context.
//	POST_payload - If NULL, the request will be an HTTP GET. Otherwise, the argument will constitute the body of an HTTP POST.
//
// Returns:
// 	NULL on catastrophic failure (jf_reply_new failed). Otherwise a jf_reply which either:
// 	- marks an error (authentication, network, parser's), check with the JF_REPLY_PTR_HAS_ERROR macro and get an error string with jf_reply_error_string;
// 	- contains the body of the response for a JF_REQUEST_IN_MEMORY;
// 	- contains an empty body for JF_REQUEST_SAX_*.
jf_reply *jf_net_request(const char *resource, jf_request_type request_type, const char *POST_payload);

jf_reply *jf_net_login_request(const char *POST_payload);
////////////////////////////////


////////// MISCELLANEOUS GARBAGE ///////////
bool jf_net_url_is_valid(const char *url);
////////////////////////////////////////////

#endif
