#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

#include "network.h"
#include "shared.h"
#include "json_parser.h"



////////// GLOBALS //////////
static CURL *s_handle = NULL;
static struct curl_slist *s_headers = NULL;
static struct curl_slist *s_headers_POST = NULL;
static const jf_options *s_options;
static jf_thread_buffer s_tb;


////////// JF_REPLY //////////
jf_reply *jf_reply_new(void)
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
		case JF_REPLY_ERROR_X_EMBY_AUTH:
			return "appending x-emby-authorization failed";
		case JF_REPLY_ERROR_NETWORK:
		case JF_REPLY_ERROR_HTTP_NOT_OK:
			return r->payload;
		default:
			return "unknown error (this is a bug)";
	}
}


size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	jf_reply *r = (jf_reply *)userdata;
	char *new_buf;
	if (!(new_buf = realloc(r->payload, (size_t)r->size + real_size + 1))) { //NB r->size >=0
		return 0;
	}
	r->payload = new_buf;
	memcpy(r->payload + r->size, payload, real_size);
	r->size += real_size;
	r->payload[r->size] = '\0';
	return real_size;
}


////////// PARSER THREAD COMMUNICATION //////////
size_t jf_thread_buffer_callback(char *payload, size_t size, size_t nmemb, __attribute__((unused)) void *userdata)
{
	size_t real_size = size * nmemb;
	size_t written_data = 0;
	size_t chunk_size;

	pthread_mutex_lock(&s_tb.mut);
	
	while (written_data < real_size) {
		while (s_tb.used != 0) {
			pthread_cond_wait(&s_tb.cv_has_data, &s_tb.mut);
		}
		chunk_size = real_size - written_data <= JF_THREAD_BUFFER_DATA_SIZE ? real_size - written_data : JF_THREAD_BUFFER_DATA_SIZE;
		memcpy(s_tb.data, payload + written_data, chunk_size);
	    written_data += chunk_size;
		s_tb.data[chunk_size] = '\0';
		s_tb.used = chunk_size;
		pthread_cond_signal(&s_tb.cv_no_data);
	}
	pthread_mutex_unlock(&s_tb.mut);

	return written_data;
}
/////////////////////////////////////////////////


////////// NETWORKING FUNCTIONS //////////
size_t jf_network_init(const jf_options *options)
{
	curl_global_init(CURL_GLOBAL_ALL | CURL_GLOBAL_SSL);
	s_handle = curl_easy_init();
	s_options = options;
	pthread_t sax_parser_thread;

	// headers
	if (! jf_network_make_headers()) {
		return 0;
	}

	// security bypass stuff
	if (! s_options->ssl_verifyhost) {
		curl_easy_setopt(s_handle, CURLOPT_SSL_VERIFYHOST, 0);
	}

	// ask for compression (all kinds supported)
	curl_easy_setopt(s_handle, CURLOPT_ACCEPT_ENCODING, "");

	// follow redirects and keep POST method if using it
	curl_easy_setopt(s_handle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(s_handle, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

	// sax parser thread
	if (pthread_create(&sax_parser_thread, NULL, jf_sax_parser_thread, (void *)&(s_tb)) == -1) {
		return 0;
	}
	if (pthread_detach(sax_parser_thread) != 0) {
		return 0;
	}

	return 1;
}


size_t jf_network_make_headers(void)
{
	char *tmp;

	if (s_options->token != NULL) {
		if ((tmp = jf_concat(2, "x-emby-token: ", s_options->token)) == NULL) {
				return 0;
		}
		if ((s_headers = curl_slist_append(s_headers, tmp)) == NULL) {
			return 0;
		}
		free(tmp);
	}
	if ((s_headers = curl_slist_append(s_headers, "accept: application/json; charset=utf-8")) == NULL) {
		return 0;
	}

	// headers for POST: second list
	if ((s_headers_POST = curl_slist_append(s_headers, "content-type: application/json; charset=utf-8")) == NULL) {
		return 0;
	}

	return 1;
}


size_t jf_network_reload_token(void)
{
	curl_slist_free_all(s_headers_POST);
	s_headers = NULL;
	s_headers_POST = NULL;
	return jf_network_make_headers();
}


jf_reply *jf_request(const char *resource, size_t request_type, const char *POST_payload)
{
	CURLcode result;
	long status_code;
	jf_reply *reply;
	
	if ((reply = jf_reply_new()) == NULL) {
		return (jf_reply *)NULL;
	}
	
	if (s_handle == NULL) {
		reply->size = JF_REPLY_ERROR_UNINITIALIZED;
		return reply;
	}

	// url
	{
		size_t resource_len = strlen(resource);
		char *url;
		if ((url = (char *)malloc(s_options->server_len + resource_len + 1)) == NULL) {
			reply->size = JF_REPLY_ERROR_MALLOC;
			return reply;
		}
		strncpy(url, s_options->server, s_options->server_len);
		strncpy(url + s_options->server_len, resource, resource_len);
		url[s_options->server_len + resource_len] = '\0';
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
			s_tb.promiscuous_context = 1;
			// NB no break, proceed
		case JF_REQUEST_SAX:
			curl_easy_setopt(s_handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback);
			break;
	}
	curl_easy_setopt(s_handle, CURLOPT_WRITEDATA, (void *)reply);
	if ((result = curl_easy_perform(s_handle)) != CURLE_OK) {
		free(reply->payload);
		reply->payload = (char *)curl_easy_strerror(result);
		reply->size = JF_REPLY_ERROR_NETWORK;
	} else {
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
			"x-emby-authorization: mediabrowser client=\"", s_options->client,
			"\", device=\"", s_options->device, 
			"\", deviceid=\"", s_options->deviceid,
			"\", version=\"", s_options->version,
			"\"")) == NULL ) {
		return (jf_reply *)NULL;
	}
	if ((s_headers_POST = curl_slist_append(s_headers_POST, tmp)) == NULL) {
		free(tmp);
		jf_reply *reply;
		if ((reply = jf_reply_new()) == NULL) {
			return (jf_reply *)NULL;
		}
		reply->size = JF_REPLY_ERROR_X_EMBY_AUTH;
		return reply;
	}
	free(tmp);

	// send request
	return jf_request("/users/authenticatebyname", 0, POST_payload);
}


void jf_network_cleanup(void)
{
	curl_slist_free_all(s_headers_POST);
	curl_easy_cleanup(s_handle);
	curl_global_cleanup();
}
