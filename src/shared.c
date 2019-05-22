#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <stdbool.h>
#include "shared.h"


char *jf_concat(size_t n, ...)
{
	char *buf;
	size_t *argv_len;
	size_t len = 0;
	size_t i;
	va_list ap;

	if ((argv_len = (size_t *)malloc(sizeof(size_t)* n)) == NULL) {
		return (char *)NULL;
	}
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		argv_len[i] = strlen(va_arg(ap, char*));
		len += argv_len[i];
	}
	va_end(ap);

	if ((buf = (char *)malloc(len + 1)) == NULL) {
		free(argv_len);
		return (char *)NULL;
	}
	len = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		memcpy(buf + len, va_arg(ap, char*), argv_len[i]);
		len += argv_len[i];
	}
	buf[len] = '\0';
	va_end(ap);
	
	free(argv_len);
	return buf;
}


bool jf_thread_buffer_init(jf_thread_buffer *tb)
{
	tb->used = 0;
	tb->promiscuous_context = false;
	pthread_mutex_init(&tb->mut, NULL);
	pthread_cond_init(&tb->cv_no_data, NULL);
	pthread_cond_init(&tb->cv_has_data, NULL);
	tb->parsed_ids_size = 512 * (1 + JF_ID_LENGTH);
	if ((tb->parsed_ids = malloc(tb->parsed_ids_size)) == NULL) {
		return false;
	}
	return true;
}


jf_options *jf_options_new(void)
{
	jf_options *opts;

	if ((opts = malloc(sizeof(jf_options))) == NULL) {
		return NULL;
	}

	// initialize to empty, will NULL pointers
	*opts = (jf_options){ 0 }; 

	// initialize fields where 0 is a valid value
	opts->ssl_verifyhost = JF_CONFIG_SSL_VERIFYHOST_DEFAULT;

	return opts;
}


void jf_options_fill_defaults(jf_options *opts)
{
	if (opts != NULL) {
		opts->client = JF_CONFIG_CLIENT_DEFAULT;
		opts->device = JF_CONFIG_DEVICE_DEFAULT;
		opts->deviceid = JF_CONFIG_DEVICEID_DEFAULT;
		opts->version = JF_CONFIG_VERSION_DEFAULT;
	}
}


void jf_options_free(jf_options *opts)
{
	if (opts != NULL) {
		free(opts->server);
		free(opts->token);
		free(opts->userid);
		free(opts->client);
		free(opts->device);
		free(opts->deviceid);
		free(opts->version);
		free(opts);
	}
}


// jf_synced_queue *jf_synced_queue_new(const size_t slots)
// {
// 	jf_synced_queue *q;
// 
// 	if ((q = malloc(sizeof(jf_synced_queue))) == NULL) {
// 		return (jf_synced_queue *)NULL;
// 	}
// 	if ((q->slots = calloc(slots, sizeof(void *))) == NULL) {
// 		free(q);
// 		return (jf_synced_queue *)NULL;
// 	}
// 	q->slot_count = slots;
// 	q->current = 0;
// 	q->next = 0;
// 	if (pthread_mutex_init(&q->mut, NULL) != 0) {
// 		free(q->slots);
// 		free(q);
// 		return (jf_synced_queue *)NULL;
// 	}
// 	if (pthread_cond_init(&q->cv_is_empty, NULL) != 0) {
// 		pthread_mutex_destroy(&q->mut);
// 		free(q->slots);
// 		free(q);
// 		return (jf_synced_queue *)NULL;
// 	}
// 	if (pthread_cond_init(&q->cv_is_full, NULL) != 0) {
// 		pthread_cond_destroy(&q->cv_is_full);
// 		pthread_mutex_destroy(&q->mut);
// 		free(q->slots);
// 		free(q);
// 		return (jf_synced_queue *)NULL;
// 	}
// 
// 	return q;
// }
// 
// 
// void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload)
// {
// 	if (payload != NULL) {
// 		pthread_mutex_lock(&q->mut);
// 		while (q->slots[q->next] != NULL) {
// 			pthread_cond_wait(&q->cv_is_full, &q->mut);
// 		}
// 		q->slots[q->next] = payload;
// 		q->next = (q->next + 1) % q->slot_count;
// 		pthread_mutex_unlock(&q->mut);
// 		pthread_cond_signal(&q->cv_is_empty);
// 	}
// }
// 
// 
// void *jf_synced_queue_dequeue(jf_synced_queue *q)
// {
// 	void *payload;
// 
// 	pthread_mutex_lock(&q->mut);
// 	while (q->slots[q->current] == NULL) {
// 		pthread_cond_wait(&q->cv_is_empty, &q->mut);
// 	}
// 	payload = (void *)q->slots[q->current];
// 	q->slots[q->current] = NULL;
// 	q->current = (q->current + 1) % q->slot_count;
// 	pthread_mutex_unlock(&q->mut);
// 	pthread_cond_signal(&q->cv_is_full);
// 
// 	return payload;
// }
// 
// 
// size_t jf_synced_queue_is_empty(const jf_synced_queue *q)
// {
// 	// NB it makes no sense to acquire lock because things may change at any time anyways
// 	// i.e. a better function name would be "_is_probably_empty" :p
// 	return q->slots[q->current] == NULL;
// }
// 
