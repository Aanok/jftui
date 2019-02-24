#include <curl/curl.h>
#include <string.h>
#include <stdlib.h>

#include "network.h"
#include "shared.h"



////////// GLOBALS //////////
static CURL *g_handle = NULL;
static struct curl_slist *g_headers = NULL;
static const jf_options *g_options;



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
	free(r->payload);
	free(r);
}


char *jf_reply_error_string(jf_reply *r)
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



////////// NETWORKING FUNCTIONS //////////
size_t jf_network_init(jf_options *options)
{
	curl_global_init(CURL_GLOBAL_ALL | CURL_GLOBAL_SSL);
	g_handle = curl_easy_init();
	g_options = options;

	// auth token
	char *token_header;
	if (!(token_header = (char*)malloc(47))) {
		return 0;
	}
	snprintf(token_header, 46, "x-emby-token: %s", g_options->token);
	if (! (g_headers = curl_slist_append(g_headers, token_header))) {
		return 0;
	}
	free(token_header);
	curl_easy_setopt(g_handle, CURLOPT_HTTPHEADER, g_headers);

	// security bypass stuff
	if (! g_options->ssl_verifyhost) {
		curl_easy_setopt(g_handle, CURLOPT_SSL_VERIFYHOST, 0);
	}

	return 1;
}


jf_reply *jf_request(char *resource, size_t to_file)
{
	CURLcode result;
	long status_code;
	jf_reply *reply = jf_reply_new();
	if (! g_handle) {
		reply->size = JF_REPLY_ERROR_UNINITIALIZED;
		return reply;
	}

	// url
	{
		size_t resource_len = strlen(resource);
		char *url;
		if (!(url = (char *)malloc(g_options->server_url_len + resource_len + 1))) {
			reply->size = JF_REPLY_ERROR_MALLOC;
			return reply;
		}
		strncpy(url, g_options->server_url, g_options->server_url_len);
		strncpy(url + g_options->server_url_len, resource, resource_len);
		url[g_options->server_url_len + resource_len] = '\0';
		curl_easy_setopt(g_handle, CURLOPT_URL, url);
		free(url);
	}

	// request
	if (to_file) {
		//TODO implement
		reply->size = JF_REPLY_ERROR_STUB;
	} else {
		curl_easy_setopt(g_handle, CURLOPT_WRITEFUNCTION, jf_reply_callback);		
		curl_easy_setopt(g_handle, CURLOPT_WRITEDATA, (void *)reply);
		if ((result = curl_easy_perform(g_handle)) != CURLE_OK) {
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
					if (!(reply->payload = (char *)malloc(34))) {
						reply->size = JF_REPLY_ERROR_MALLOC;
						return reply;
					}
					snprintf(reply->payload, 34, "http request returned status %ld", status_code);
					reply->size = JF_REPLY_ERROR_HTTP_NOT_OK;
					break;
			}
		}	
	}

	return reply;
}


void jf_network_cleanup(void)
{
	curl_slist_free_all(g_headers);
	curl_easy_cleanup(g_handle);
	curl_global_cleanup();
}
