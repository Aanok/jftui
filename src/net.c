#include "net.h"


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
//////////////////////////////////////


////////// STATIC VARIABLES //////////
static CURL *s_handle = NULL;
static struct curl_slist *s_headers = NULL;
static struct curl_slist *s_headers_POST = NULL;
static char s_curl_errorbuffer[CURL_ERROR_SIZE];
static jf_thread_buffer s_tb;
static jf_synced_queue *s_async_queue = NULL;
static pthread_mutex_t s_async_mut;
static pthread_cond_t s_async_cv;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
static void jf_thread_buffer_wait_parsing_done(void);

static size_t jf_reply_callback(char *payload,
		size_t size,
		size_t nmemb,
		void *userdata);

static size_t jf_thread_buffer_callback(char *payload,
		size_t size,
		size_t nmemb,
		void *userdata);

static size_t jf_detach_callback(char *payload,
		size_t size,
		size_t nmemb,
		void *userdata);

static void jf_net_handle_init(CURL *handle);

static void jf_net_handle_before_perform(CURL *handle,
		const char *resource,
		jf_request_type request_type,
		const char *POST_payload,
		const jf_reply *reply);

static void jf_net_handle_after_perform(CURL *handle,
		const CURLcode result,
		const jf_request_type request_type,
		jf_reply *reply);

static void *jf_net_async_worker_thread(void *arg);
//////////////////////////////////////


////////// JF_REPLY //////////
jf_reply *jf_reply_new()
{
	jf_reply *r;
	assert((r = malloc(sizeof(jf_reply))) != NULL);
	r->payload = NULL;
	r->size = 0;
	r->is_resolved = false;
	return r;
}


void jf_reply_free(jf_reply *r)
{
	if (r == NULL) return;
	free(r->payload);
	free(r);
}


char *jf_reply_error_string(const jf_reply *r)
{
	if (r == NULL) {
		return "jf_reply is NULL";
	}

	if (r->size >= 0) {
		return "no error";
	}

	switch (r->size) {
		case JF_REPLY_ERROR_STUB:
			return "stub functionality";
		case JF_REPLY_ERROR_HTTP_401:
			return "http request returned error 401: unauthorized; you likely need to renew your auth token. Restart with --login";
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
			return r->payload;
		// TODO ASYNC ERRORS!!
		default:
			return "unknown error. This is a bug";
	}
}


static size_t jf_reply_callback(char *payload, size_t size, size_t nmemb, void *userdata)
{
	size_t real_size = size * nmemb;
	jf_reply *reply = (jf_reply *)userdata;
	assert(reply != NULL);
	assert((reply->payload = realloc(reply->payload,
					(size_t)reply->size + real_size + 1)) != NULL);
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
		}
		// send data
		chunk_size = real_size - written_data < JF_THREAD_BUFFER_DATA_SIZE - 1 ? real_size - written_data : JF_THREAD_BUFFER_DATA_SIZE - 2;
		memcpy(s_tb.data, payload + written_data, chunk_size);
	    written_data += chunk_size;
		s_tb.data[chunk_size + 1] = '\0';
		s_tb.used = chunk_size;
		s_tb.state = JF_THREAD_BUFFER_STATE_PENDING_DATA;
		pthread_cond_signal(&s_tb.cv_no_data);
	}
	pthread_mutex_unlock(&s_tb.mut);

	return written_data;
}


size_t jf_thread_buffer_item_count()
{
	return s_tb.item_count;
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
void jf_net_init()
{
	// TODO check libcurl version
	
	char *tmp;
	pthread_t sax_parser_thread;
	pthread_t async_threads[3];
	int i;

	// global config stuff
	assert(curl_global_init(CURL_GLOBAL_ALL | CURL_GLOBAL_SSL) == 0);
	// security bypass
	if (! g_options.ssl_verifyhost) {
		curl_easy_setopt(s_handle, CURLOPT_SSL_VERIFYHOST, 0);
	}
	// headers
	if (g_options.token != NULL) {
		tmp = jf_concat(2, "x-emby-token: ", g_options.token);
		assert((s_headers = curl_slist_append(s_headers, tmp)) != NULL);
		free(tmp);
	}
	assert((s_headers = curl_slist_append(s_headers, "accept: application/json; charset=utf-8")) != NULL);
	// headers for POST: second list
	assert((s_headers_POST = curl_slist_append(s_headers, "content-type: application/json; charset=utf-8")) != NULL);

	// setup handle for blocking requests
	assert((s_handle = curl_easy_init()) != NULL);
	s_curl_errorbuffer[0] = '\0';
	JF_CURL_ASSERT(curl_easy_setopt(s_handle, CURLOPT_ERRORBUFFER, s_curl_errorbuffer));
	jf_net_handle_init(s_handle);

	// sax parser thread
	jf_thread_buffer_init(&s_tb);
	assert(pthread_create(&sax_parser_thread, NULL, jf_json_sax_thread, (void *)&(s_tb)) != -1);
	assert(pthread_detach(sax_parser_thread) == 0);

	// async networking
	s_async_queue = jf_synced_queue_new(10);
	assert(pthread_mutex_init(&s_async_mut, NULL) == 0);
	assert(pthread_cond_init(&s_async_cv, NULL) == 0);

	for (i = 0; i < 3; i++) {
		assert(pthread_create(async_threads + i, NULL, jf_net_async_worker_thread, NULL) != -1);
		assert(pthread_detach(async_threads[i]) == 0);
	}
}


void jf_net_clear()
{
	curl_slist_free_all(s_headers_POST);
	curl_easy_cleanup(s_handle);
	curl_global_cleanup();
}
//////////////////////////////////


////////// NETWORKING //////////
static void jf_net_handle_init(CURL *handle)
{
	// TODO curl_share bollocks

	// ask for compression (all kinds supported)
	JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, ""));

	// follow redirects and keep POST method if using it
	JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1));
	JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_POSTREDIR, CURL_REDIR_POST_ALL));
}


static void jf_net_handle_before_perform(CURL *handle,
		const char *resource,
		const jf_request_type request_type,
		const char *POST_payload,
		const jf_reply *reply)
{
	char *url;

	// url
	url = jf_concat(2, g_options.server, resource);
	JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_URL, url));
	free(url);

	// POST and headers
	if (POST_payload != NULL) {
		JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_POSTFIELDS, POST_payload));
		JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, s_headers_POST));
	} else {
		JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_HTTPGET, 1));
		JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_HTTPHEADER, s_headers));
	}
	
	// request
	switch (request_type) {
		case JF_REQUEST_IN_MEMORY:
		case JF_REQUEST_ASYNC_IN_MEMORY:
			JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, jf_reply_callback));
			break;
		case JF_REQUEST_SAX_PROMISCUOUS:
			s_tb.promiscuous_context = true;
			JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback));
			break;
		case JF_REQUEST_SAX:
			s_tb.promiscuous_context = false;
			JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, jf_thread_buffer_callback));
			break;
		case JF_REQUEST_ASYNC_DETACH:
			JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, jf_detach_callback));
			break;
	}
	JF_CURL_ASSERT(curl_easy_setopt(handle, CURLOPT_WRITEDATA, (void *)reply));
}


static void jf_net_handle_after_perform(CURL *handle,
		const CURLcode result,
		const jf_request_type request_type,
		jf_reply *reply)
{
	long status_code;

	if (request_type == JF_REQUEST_ASYNC_DETACH || reply == NULL) {
		jf_reply_free(reply);
		return;
	}

	if (result != CURLE_OK) {
		// don't overwrite error messages we've already set ourselves
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
		JF_CURL_ASSERT(curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status_code));
		switch (status_code) { 
			case 200:
			case 204:
				break;
			case 401:
				reply->size = JF_REPLY_ERROR_HTTP_401;
				break;
			default:
				free(reply->payload);
				assert((reply->payload = malloc(34)) != NULL);
				snprintf(reply->payload, 34, "http request returned status %ld", status_code);
				reply->size = JF_REPLY_ERROR_HTTP_NOT_OK;
				break;
		}
	}
	reply->is_resolved = true;
}


jf_reply *jf_net_request(const char *resource,
		const jf_request_type request_type,
		const char *POST_payload)
{
	jf_reply *reply;
	jf_async_request *a_r;
	
	assert(s_handle != NULL);

	if (JF_REQUEST_TYPE_IS_ASYNC(request_type)) {
		a_r = jf_async_request_new(resource,
				request_type,
				POST_payload);
		reply = a_r->reply;
		jf_synced_queue_enqueue(s_async_queue, a_r);
	} else {
		reply = jf_reply_new();
		jf_net_handle_before_perform(s_handle,
				resource,
				request_type,
				POST_payload,
				reply);
		jf_net_handle_after_perform(s_handle,
				curl_easy_perform(s_handle),
				request_type,
				reply);
	}

	return reply;
}


jf_reply *jf_net_login_request(const char *POST_payload)
{
	char *tmp;

	assert(s_handle != NULL);

	// add x-emby-authorization header
	tmp = jf_concat(9,
			"x-emby-authorization: mediabrowser client=\"", g_options.client,
			"\", device=\"", g_options.device, 
			"\", deviceid=\"", g_options.deviceid,
			"\", version=\"", g_options.version,
			"\"");
	assert((s_headers_POST = curl_slist_append(s_headers_POST, tmp)) != NULL);

	free(tmp);

	// send request
	return jf_net_request("/emby/Users/authenticatebyname", 0, POST_payload);
}
///////////////////////////////////


////////// ASYNC NETWORKING //////////
jf_async_request *jf_async_request_new(const char *resource,
		jf_request_type request_type,
		const char *POST_payload)
{
	jf_async_request *a_r;

	assert((a_r = malloc(sizeof(jf_async_request))) != NULL);
	a_r->reply = request_type == JF_REQUEST_ASYNC_DETACH ? NULL : jf_reply_new();
	assert((a_r->resource = strdup(resource)) != NULL);
	a_r->type = request_type;
	if (POST_payload == NULL) {
		a_r->POST_payload = NULL;
	} else {
		assert((a_r->POST_payload = strdup(POST_payload)) != NULL);
	}

	return a_r;
}


void jf_async_request_free(jf_async_request *a_r)
{
	if (a_r == NULL) return;
	free(a_r->resource);
	free(a_r->POST_payload);
	free(a_r);
}


static size_t jf_detach_callback(__attribute__((unused)) char *payload,
		size_t size,
		size_t nmemb,
		__attribute__((unused)) void *userdata)
{
	// discard everything
	return size * nmemb;
}


static void *jf_net_async_worker_thread(__attribute__((unused)) void *arg)
{
	CURL *handle;
	char errorbuffer[CURL_ERROR_SIZE];
	jf_async_request *request;

	assert((handle = curl_easy_init()) != NULL);

	// tell curl where to write informative error messages
	s_curl_errorbuffer[0] = '\0';
	JF_CURL_ASSERT(curl_easy_setopt(handle,
				CURLOPT_ERRORBUFFER,
				errorbuffer));

	// generic networking options and curl_share optimizations
	jf_net_handle_init(handle);

	while (true) {
		request = (jf_async_request *)jf_synced_queue_dequeue(s_async_queue);
		jf_net_handle_before_perform(handle,
				request->resource,
				request->type,
				request->POST_payload,
				request->reply);
		jf_net_handle_after_perform(handle,
				curl_easy_perform(handle),
				request->type,
				request->reply);
		jf_async_request_free(request);
		assert(pthread_cond_broadcast(&s_async_cv) == 0);
	}
}


jf_reply *jf_net_await(jf_reply *reply)
{
	assert(reply != NULL);
	pthread_mutex_lock(&s_async_mut);
	while (reply->is_resolved == false) {
		pthread_cond_wait(&s_async_cv, &s_async_mut);
	}
	pthread_mutex_unlock(&s_async_mut);
	return reply;
}
//////////////////////////////////////


////////// MISCELLANEOUS GARBAGE ///////////
char *jf_net_urlencode(const char *url)
{
	char *tmp, *retval;
	assert(s_handle != NULL);
	assert((tmp = curl_easy_escape(s_handle, url, 0)) != NULL);
	retval = strdup(tmp);
	curl_free(tmp);
	assert(retval != NULL);
	return retval;
}

	
bool jf_net_url_is_valid(const char *url)
{
#if LIBCURL_VERSION_MAJOR == 7 && LIBCURL_VERSION_MINOR >= 62
	CURLU *curlu;

	if ((curlu = curl_url()) == NULL) {
		fprintf(stderr, "Error: curlu curl_url returned NULL.\n");
		curl_url_cleanup(curlu);
		return false;
	}
	
	if (curl_url_set(curlu, CURLUPART_URL, url, 0) == CURLUE_OK) {
		curl_url_cleanup(curlu);
		return true;
	} else {
		curl_url_cleanup(curlu);
		return false;
	}
#else
	fprintf(stderr, "Warning: the libcurl version jftui was compiled against will defer URL validation to the first network request.\n");
	fprintf(stderr, "If the URL you have entered turns out to be invalid, repeat the login process by passing --login to try again.\n");
	return true;
#endif
}
////////////////////////////////////////////
