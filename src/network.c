#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

#include "network.h"
#include "shared.h"



////////// GLOBALS //////////
static CURL *g_handle = NULL;
static struct curl_slist *g_headers = NULL;
static struct curl_slist *g_headers_POST = NULL;
static const jf_options *g_options;
static jf_thread_buffer g_tb = {
	.used = 0,
	.mut = PTHREAD_MUTEX_INITIALIZER,
	.cv_no_data = PTHREAD_COND_INITIALIZER,
	.cv_has_data = PTHREAD_COND_INITIALIZER
};


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
size_t jf_thread_buffer_callback(char *payload, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	size_t written_data = 0;
	size_t chunk_size;

	pthread_mutex_lock(&g_tb.mut);
	while (written_data < real_size) {
		while (g_tb.used != 0) {
			pthread_cond_wait(&g_tb.cv_has_data, &g_tb.mut);
		}
		chunk_size = real_size - written_data <= TB_DATA_SIZE ? real_size - written_data : TB_DATA_SIZE;
		memcpy(g_tb.data, payload + written_data, chunk_size);
	    written_data += chunk_size;
		g_tb.used = chunk_size;
		pthread_cond_signal(&g_tb.cv_no_data);
	}
	pthread_mutex_unlock(&g_tb.mut);

	return written_data;
}
/////////////////////////////////////////////////


////////// NETWORKING FUNCTIONS //////////
size_t jf_network_init(const jf_options *options)
{
	curl_global_init(CURL_GLOBAL_ALL | CURL_GLOBAL_SSL);
	g_handle = curl_easy_init();
	g_options = options;

	// headers
	if (! jf_network_make_headers()) {
		return 0;
	}

	// security bypass stuff
	if (! g_options->ssl_verifyhost) {
		curl_easy_setopt(g_handle, CURLOPT_SSL_VERIFYHOST, 0);
	}

	// ask for compression (all kinds supported)
	curl_easy_setopt(g_handle, CURLOPT_ACCEPT_ENCODING, "");

	// follow redirects and keep POST method if using it
	curl_easy_setopt(g_handle, CURLOPT_FOLLOWLOCATION, 1);
	curl_easy_setopt(g_handle, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL);

	return 1;
}


size_t jf_network_make_headers(void)
{
	char *tmp;

	if (g_options->token != NULL) {
		if ((tmp = jf_concat(2, "x-emby-token: ", g_options->token)) == NULL) {
				return 0;
		}
		if ((g_headers = curl_slist_append(g_headers, tmp)) == NULL) {
			return 0;
		}
		free(tmp);
	}
	if ((g_headers = curl_slist_append(g_headers, "accept: application/json; charset=utf-8")) == NULL) {
		return 0;
	}

	// headers for POST: second list
	if ((g_headers_POST = curl_slist_append(g_headers, "content-type: application/json; charset=utf-8")) == NULL) {
		return 0;
	}

	return 1;
}


size_t jf_network_reload_token(void)
{
	curl_slist_free_all(g_headers_POST);
	g_headers = NULL;
	g_headers_POST = NULL;
	return jf_network_make_headers();
}


jf_reply *jf_request(const char *resource, size_t to_thread_buffer, const char *POST_payload)
{
	CURLcode result;
	long status_code;
	jf_reply *reply;
	
	if ((reply = jf_reply_new()) == NULL) {
		return (jf_reply *)NULL;
	}
	
	if (g_handle == NULL) {
		reply->size = JF_REPLY_ERROR_UNINITIALIZED;
		return reply;
	}

	// url
	{
		size_t resource_len = strlen(resource);
		char *url;
		if ((url = (char *)malloc(g_options->server_url_len + resource_len + 1)) == NULL) {
			reply->size = JF_REPLY_ERROR_MALLOC;
			return reply;
		}
		strncpy(url, g_options->server_url, g_options->server_url_len);
		strncpy(url + g_options->server_url_len, resource, resource_len);
		url[g_options->server_url_len + resource_len] = '\0';
		curl_easy_setopt(g_handle, CURLOPT_URL, url);
		free(url);
	}

	// POST and headers
	if (POST_payload != NULL) {
		curl_easy_setopt(g_handle, CURLOPT_POSTFIELDS, POST_payload);
		curl_easy_setopt(g_handle, CURLOPT_HTTPHEADER, g_headers_POST);
	} else {
		curl_easy_setopt(g_handle, CURLOPT_HTTPGET, 1);
		curl_easy_setopt(g_handle, CURLOPT_HTTPHEADER, g_headers);
	}
	
	// request
	if (to_thread_buffer) {
		curl_easy_setopt(g_handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback);
	} else {
		curl_easy_setopt(g_handle, CURLOPT_WRITEFUNCTION, jf_reply_callback);		
	}
	curl_easy_setopt(g_handle, CURLOPT_WRITEDATA, (void *)reply);
	if ((result = curl_easy_perform(g_handle)) != CURLE_OK) {
		free(reply->payload);
		reply->payload = (char *)curl_easy_strerror(result);
		reply->size = JF_REPLY_ERROR_NETWORK;
	} else {
		curl_easy_getinfo(g_handle, CURLINFO_RESPONSE_CODE, &status_code);
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
			"x-emby-authorization: mediabrowser client=\"", g_options->client,
			"\", device=\"", g_options->device, 
			"\", deviceid=\"", g_options->deviceid,
			"\", version=\"", g_options->version,
			"\"")) == NULL ) {
		return (jf_reply *)NULL;
	}
	if ((g_headers_POST = curl_slist_append(g_headers_POST, tmp)) == NULL) {
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
	curl_slist_free_all(g_headers_POST);
	curl_easy_cleanup(g_handle);
	curl_global_cleanup();
}
