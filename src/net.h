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


////////// CODE MACROS //////////
#define JF_CURL_ASSERT(_s)													\
do {																		\
	CURLcode _c = _s;														\
	if (_c != CURLE_OK) {													\
		fprintf(stderr, "%s:%d: " #_s " failed.\n", __FILE__, __LINE__);	\
		fprintf(stderr, "FATAL: libcurl error: %s: %s.\n",					\
				curl_easy_strerror(_c), s_curl_errorbuffer);				\
		exit(EXIT_FAILURE);													\
	}																		\
} while (false)

#define JF_CURL_SHARE_ASSERT(_s)											\
do {																		\
	CURLSHcode _c = _s;														\
	if (_c != CURLSHE_OK) {													\
		fprintf(stderr, "%s:%d: " #_s " failed.\n", __FILE__, __LINE__);	\
		fprintf(stderr, "FATAL: libcurl error: %s.\n",						\
				curl_share_strerror(_c));									\
		exit(EXIT_FAILURE);													\
	}																		\
} while	(false)

/////////////////////////////////


////////// JF_REPLY //////////
typedef enum __attribute__((__packed__)) jf_reply_state {
	JF_REPLY_PENDING = 0,
	JF_REPLY_SUCCESS = 1,

	JF_REPLY_ERROR_STUB = -1,
	JF_REPLY_ERROR_NETWORK = -2,
	JF_REPLY_ERROR_HTTP_401 = -3,
	JF_REPLY_ERROR_HTTP_NOT_OK = -4,
	JF_REPLY_ERROR_MALLOC = -5,
	JF_REPLY_ERROR_CONCAT = -6,
	JF_REPLY_ERROR_X_EMBY_AUTH = -7,
	JF_REPLY_ERROR_PARSER = -8,
	JF_REPLY_ERROR_BAD_LOCATION = -9
} jf_reply_state;


typedef struct jf_reply {
	char *payload;
	size_t size;
	jf_reply_state state;
} jf_reply;


#define JF_REPLY_PTR_HAS_ERROR(_p)	((_p)->state < 0)
#define JF_REPLY_PTR_GET_ERROR(_p)	((_p)->state)
#define JF_REPLY_PTR_ERROR_IS(_p, _e) ((_p)->state == (_e))

jf_reply *jf_reply_new(void);
void jf_reply_free(jf_reply *r);
char *jf_reply_error_string(const jf_reply *r);
//////////////////////////////


////////// REQUEST TYPES //////////
typedef enum jf_request_type {
	JF_REQUEST_IN_MEMORY = 0,
	JF_REQUEST_SAX = 1,
	JF_REQUEST_SAX_PROMISCUOUS = 2,

	JF_REQUEST_ASYNC_IN_MEMORY = -1,
	JF_REQUEST_ASYNC_DETACH = -2,
	JF_REQUEST_CHECK_UPDATE = -3
} jf_request_type;

#define JF_REQUEST_TYPE_IS_ASYNC(_t) ((_t) < 0)
///////////////////////////////////


////////// PARSER THREAD COMMUNICATION //////////
size_t jf_thread_buffer_item_count(void);
void jf_thread_buffer_clear_error(void);
/////////////////////////////////////////////////


////////// NETWORK UNIT //////////
void jf_net_clear(void);
//////////////////////////////////


////////// NETWORKING //////////

// Executes a network request to the Jellyfin server. The response may be
// entirely put in a single jf_reply in memory or passed step by step to the
// JSON parser thread with constant memory usage. In the latter case, the
// function will wait for parsing to be complete before returning.
//
// Parameters:
// 	resource:
// 		For JF_REQUEST_CHECK_UPDATE, this is ignored.
// 		Otherwise, it is treated as a suffix to append to the server's address
// 		to compute the full URL.
// 	request_type:
// 		- JF_REQUEST_IN_MEMORY will cause the request to be evaded blockingly
// 			and the response to be passed back in a jf_reply struct;
// 		- JF_REQUEST_SAX will cause the response to be blockingly passed to the
// 			JSON parser and digested as a non-promiscuous context;
// 		- JF_REQUEST_SAX_PROMISCUOUS likewise but digested as a promiscuous
// 			context;
// 			(note: both SAX requests will wait for the parser to be done before
// 			returning control to the caller)
// 		- JF_REQUEST_ASYNC_IN_MEMORY will cause the request to be evaded
// 			asynchronously: control will be passed back the caller immediately
// 			while a separate thread takes care of network traffic. The response
// 			will be passed back in a jf_reply struct. The caller may use
// 			jf_net_async_await to wait until the request is fully evaded.
// 		- JF_REQUEST_ASYNC_DETACH will likewise work asynchronously; however,
// 			the function will immediately return NULL and all response data
// 			will be discarded on arrival. Use for requests whose outcome you
// 			don't care about, like watch state updates.
// 		- JF_REQUEST_CHECK_UPDATE functions like JF_REQUEST_ASYNC_IN_MEMORY,
// 			except the resource parameter is ignored and internally set to the
// 			one required for the optional update check against github.com
//	POST_payload:
//		If NULL, the request will be an HTTP GET. Otherwise, the argument will
//		constitute the body of an HTTP POST.
//
// Returns:
// 	A jf_reply which either:
// 	- marks an error (authentication, network, parser's), check with the
// 		JF_REPLY_PTR_HAS_ERROR macro and get an error string with
// 		jf_reply_error_string;
// 	- contains the body of the response for a JF_REQUEST_[ASYNC_]IN_MEMORY and
// 		JF_REQUEST_CHECK_UPDATE.
// 	- contains an empty body for JF_REQUEST_SAX_*;
// 	- is NULL for JF_REQUEST_ASYNC_DETACH.
// CAN FATAL.
jf_reply *jf_net_request(const char *resource,
		jf_request_type request_type,
		const char *POST_payload);
////////////////////////////////


////////// ASYNC NETWORKING //////////
typedef struct jf_async_request {
	jf_reply *reply;
	char *resource;
	jf_request_type type;
	char *POST_payload;
} jf_async_request;


jf_reply *jf_net_await(jf_reply *r);
//////////////////////////////////////


////////// MISCELLANEOUS GARBAGE ///////////
char *jf_net_urlencode(const char *url);
bool jf_net_url_is_valid(const char *url);
////////////////////////////////////////////

#endif
