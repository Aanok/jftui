#include "network.h"


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;


////////// STATIC VARIABLES //////////
static CURL *s_handle = NULL;
static struct curl_slist *s_headers = NULL;
static struct curl_slist *s_headers_POST = NULL;
static jf_thread_buffer s_tb;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static void jf_thread_buffer_wait_parsing_done(void);
static size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata);
static size_t jf_thread_buffer_callback(char *payload, size_t size, size_t nmemb, void *userdata);
static bool jf_network_make_headers(void);
//////////////////////////////////////


////////// JF_REPLY //////////
jf_reply *jf_reply_new()
{
	jf_reply *r;
	if (!(r = (jf_reply *)malloc(sizeof(jf_reply)))) {
		return (jf_reply *)NULL;
	}
	r->payload = NULL;
	r->size = 0;
	return r;
}


void jf_reply_free(jf_reply *r)
{
	if (r != NULL) {
		free(r->payload);
		free(r);
	}
}


char *jf_reply_error_string(const jf_reply *r)
{
	if (r->size >= 0) {
		return "no error";
	}
	switch (r->size) {
		case JF_REPLY_ERROR_STUB:
			return "stub functionality";
		case JF_REPLY_ERROR_UNINITIALIZED:
			return "jf_network uninitialized";
		case JF_REPLY_ERROR_HTTP_401:
			return "http request returned error 401: unauthorized; you likely need to renew your auth token";
			break;
		case JF_REPLY_ERROR_MALLOC:
			return "memory allocation failed";
		case JF_REPLY_ERROR_CONCAT:
			return "string concatenation failed";
		case JF_REPLY_ERROR_X_EMBY_AUTH:
			return "appending x-emby-authorization failed";
		case JF_REPLY_ERROR_NETWORK:
		case JF_REPLY_ERROR_HTTP_NOT_OK:
		case JF_REPLY_ERROR_PARSER:
		case JF_REPLY_ERROR_PARSER_DEAD:
			return r->payload;
		default:
			return "unknown error (this is a bug)";
	}
}


static size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	jf_reply *reply = (jf_reply *)userdata;
	char *new_buf;
	if (!(new_buf = realloc(reply->payload, (size_t)reply->size + real_size + 1))) { //NB reply->size >=0
		free(reply->payload);
		reply->size = JF_REPLY_ERROR_MALLOC;
		return 0;
	}
	reply->payload = new_buf;
	memcpy(reply->payload + reply->size, payload, real_size);
	reply->size += real_size;
	reply->payload[reply->size] = '\0';
	return real_size;
}
//////////////////////////////


////////// PARSER THREAD COMMUNICATION //////////
static void jf_thread_buffer_wait_parsing_done()
{
	pthread_mutex_lock(&s_tb.mut);
	while (true) {
		switch (s_tb.state) {
			case JF_THREAD_BUFFER_STATE_AWAITING_DATA:
				pthread_cond_wait(&s_tb.cv_no_data, &s_tb.mut);
				break;
			case JF_THREAD_BUFFER_STATE_PENDING_DATA:
				pthread_cond_wait(&s_tb.cv_has_data, &s_tb.mut);
				break;
			default:
				pthread_mutex_unlock(&s_tb.mut);
				return;
		}
	}
}


size_t jf_thread_buffer_callback(char *payload, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	size_t written_data = 0;
	size_t chunk_size;
	jf_reply *r = (jf_reply *)userdata;

	pthread_mutex_lock(&s_tb.mut);
	while (written_data < real_size) {
		// wait for parser
		while (s_tb.state == JF_THREAD_BUFFER_STATE_PENDING_DATA) {
			pthread_cond_wait(&s_tb.cv_has_data, &s_tb.mut);
		}
		// check errors
		if (s_tb.state == JF_THREAD_BUFFER_STATE_PARSER_ERROR) {
			r->payload = strndup(s_tb.data, s_tb.used);
			r->size = JF_REPLY_ERROR_PARSER;
			return 0;	
		} else if (s_tb.state == JF_THREAD_BUFFER_STATE_PARSER_DEAD) {
			r->payload = strndup(s_tb.data, s_tb.used);
			r->size = JF_REPLY_ERROR_PARSER_DEAD;
			return 0;
		}
		// send data
		chunk_size = real_size - written_data <= JF_THREAD_BUFFER_DATA_SIZE ? real_size - written_data : JF_THREAD_BUFFER_DATA_SIZE;
		memcpy(s_tb.data, payload + written_data, chunk_size);
	    written_data += chunk_size;
		s_tb.data[chunk_size] = '\0';
		s_tb.used = chunk_size;
		s_tb.state = JF_THREAD_BUFFER_STATE_PENDING_DATA;
		pthread_cond_signal(&s_tb.cv_no_data);
	}
	pthread_mutex_unlock(&s_tb.mut);

	return written_data;
}


// NB n is 1-indexed as per user interface
jf_menu_item *jf_thread_buffer_get_parsed_item(size_t n)
{
	size_t offset;
	jf_item_type item_type;
	const char *item_id;
	
	if (0 < n && n <= s_tb.item_count) {
		offset = (n - 1) * (1 + JF_ID_LENGTH);
		item_type = *(jf_item_type *)(s_tb.parsed_ids + offset);
		item_id = (const char *)s_tb.parsed_ids + offset + 1;
		return jf_menu_item_new(item_type, item_id, NULL);
	} else {
		return NULL;
	}
}


void jf_thread_buffer_clear_error()
{
	pthread_mutex_lock(&s_tb.mut);
	s_tb.data[0] = '\0';
	s_tb.used = 0;
	s_tb.state = JF_THREAD_BUFFER_STATE_CLEAR;
	pthread_mutex_unlock(&s_tb.mut);
}
/////////////////////////////////////////////////


////////// NETWORK UNIT //////////
// TODO better error handling
bool jf_network_pre_init()
{
	curl_global_init(CURL_GLOBAL_ALL | CURL_GLOBAL_SSL);
	s_handle = curl_easy_init();
	pthread_t sax_parser_thread;


	// ask for compression (all kinds supported)
	curl_easy_setopt(s_handle, CURLOPT_ACCEPT_ENCODING, "");

	// follow redirects and keep POST method if using it
	curl_easy_setopt(s_handle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(s_handle, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

	// sax parser thread
	jf_thread_buffer_init(&s_tb);
	if (pthread_create(&sax_parser_thread, NULL, jf_sax_parser_thread, (void *)&(s_tb)) == -1) {
		return false;
	}
	if (pthread_detach(sax_parser_thread) != 0) {
		return false;
	}

	return true;
}


static bool jf_network_make_headers()
{
	char *tmp;

	if (s_handle == NULL) {
		// init not complete
		return false;
	}

	if (g_options.token != NULL) {
		if ((tmp = jf_concat(2, "x-emby-token: ", g_options.token)) == NULL) {
			return false;
		}
		if ((s_headers = curl_slist_append(s_headers, tmp)) == NULL) {
			return false;
		}
		free(tmp);
	}
	if ((s_headers = curl_slist_append(s_headers, "accept: application/json; charset=utf-8")) == NULL) {
		return false;
	}

	// headers for POST: second list
	if ((s_headers_POST = curl_slist_append(s_headers, "content-type: application/json; charset=utf-8")) == NULL) {
		return false;
	}

	return true;
}


bool jf_network_refresh_config()
{
	// security bypass stuff
	if (! g_options.ssl_verifyhost) {
		curl_easy_setopt(s_handle, CURLOPT_SSL_VERIFYHOST, 0);
	}

	// headers
	curl_slist_free_all(s_headers_POST); // no-op if arg is NULL
	s_headers = NULL;
	s_headers_POST = NULL;
	return jf_network_make_headers();
}


void jf_network_cleanup()
{
	curl_slist_free_all(s_headers_POST);
	curl_easy_cleanup(s_handle);
	curl_global_cleanup();
}
//////////////////////////////////


////////// NETWORKING //////////

//TODO add error checks to all setopt's
jf_reply *jf_request(const char *resource, jf_request_type request_type, const char *POST_payload)
{
	CURLcode result;
	long status_code;
	jf_reply *reply;
	
	if ((reply = jf_reply_new()) == NULL) {
		return NULL;
	}
	
	if (s_handle == NULL) {
		reply->size = JF_REPLY_ERROR_UNINITIALIZED;
		return reply;
	}

	// url
	{
		char *url;
		if ((url = jf_concat(2, g_options.server, resource)) == NULL) {
			reply->size = JF_REPLY_ERROR_CONCAT;
			return reply;
		}
		curl_easy_setopt(s_handle, CURLOPT_URL, url);
		free(url);
	}

	// POST and headers
	if (POST_payload != NULL) {
		curl_easy_setopt(s_handle, CURLOPT_POSTFIELDS, POST_payload);
		curl_easy_setopt(s_handle, CURLOPT_HTTPHEADER, s_headers_POST);
	} else {
		curl_easy_setopt(s_handle, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(s_handle, CURLOPT_HTTPHEADER, s_headers);
	}
	
	// request
	switch (request_type) {
		case JF_REQUEST_IN_MEMORY:
			curl_easy_setopt(s_handle, CURLOPT_WRITEFUNCTION, jf_reply_callback);		
			break;
		case JF_REQUEST_SAX_PROMISCUOUS:
			s_tb.promiscuous_context = true;
			curl_easy_setopt(s_handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback);
		case JF_REQUEST_SAX:
			s_tb.promiscuous_context = false;
			curl_easy_setopt(s_handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback);
			break;
	}
	curl_easy_setopt(s_handle, CURLOPT_WRITEDATA, (void *)reply);
	if ((result = curl_easy_perform(s_handle)) != CURLE_OK) {
		// keep error messages we've already set ourselves
		if (! JF_REPLY_PTR_HAS_ERROR(reply)) {
			free(reply->payload);
			reply->payload = (char *)curl_easy_strerror(result);
			reply->size = JF_REPLY_ERROR_NETWORK;
		}
	} else {
		if (request_type == JF_REQUEST_SAX_PROMISCUOUS || request_type == JF_REQUEST_SAX) {
			jf_thread_buffer_wait_parsing_done();
		}
		// request went well but check for http error
		curl_easy_getinfo(s_handle, CURLINFO_RESPONSE_CODE, &status_code);
		switch (status_code) { 
			case 200:
				break;
			case 401:
				reply->size = JF_REPLY_ERROR_HTTP_401;
				break;
			default:
				free(reply->payload);
				if ((reply->payload = (char *)malloc(34)) == NULL) {
					reply->size = JF_REPLY_ERROR_MALLOC;
					return reply;
				}
				snprintf(reply->payload, 34, "http request returned status %ld", status_code);
				reply->size = JF_REPLY_ERROR_HTTP_NOT_OK;
				break;
		}
	}

	return reply;
}


jf_reply *jf_login_request(const char *POST_payload)
{
	char *tmp;

	// add x-emby-authorization header
	if ((tmp = jf_concat(9,
			"x-emby-authorization: mediabrowser client=\"", g_options.client,
			"\", device=\"", g_options.device, 
			"\", deviceid=\"", g_options.deviceid,
			"\", version=\"", g_options.version,
			"\"")) == NULL ) {
		return NULL;
	}
	if ((s_headers_POST = curl_slist_append(s_headers_POST, tmp)) == NULL) {
		free(tmp);
		jf_reply *reply;
		if ((reply = jf_reply_new()) == NULL) {
			return NULL;
		}
		reply->size = JF_REPLY_ERROR_X_EMBY_AUTH;
		return reply;
	}
	free(tmp);

	// send request
	return jf_request("/users/authenticatebyname", 0, POST_payload);
}
////////////////////////////////
