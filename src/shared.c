#include "shared.h"


////////// GLOBALS //////////
extern jf_global_state g_state;
extern mpv_handle *g_mpv_ctx;
/////////////////////////////


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, jf_menu_item **children,
		const char *id, const char *name, const long long runtime_ticks,
		const long long playback_ticks)
{
	jf_menu_item *menu_item;

	assert((menu_item = malloc(sizeof(jf_menu_item))) != NULL);

	menu_item->type = type;
	menu_item->children = children;
	menu_item->children_count = 0;
	if (children != NULL) {
		while (menu_item->children != NULL) {
			menu_item->children_count++;
			menu_item->children++;
		}
		assert((menu_item->children =
				malloc(menu_item->children_count * sizeof(jf_menu_item *))) != NULL);
		memcpy(children, menu_item->children,
				menu_item->children_count * sizeof(jf_menu_item *));
		menu_item->children = children;
	}
	if (id == NULL) {
		menu_item->id[0] = '\0';
	} else {
		strncpy(menu_item->id, id, JF_ID_LENGTH);
		menu_item->id[JF_ID_LENGTH] = '\0';
	}
	menu_item->name = name == NULL ? NULL : strdup(name);
	menu_item->runtime_ticks = runtime_ticks;
	menu_item->playback_ticks = playback_ticks;
	
	return menu_item;
}


void jf_menu_item_free(jf_menu_item *menu_item)
{
	size_t i;

	if (menu_item == NULL) {
		return;
	}

	if (! (JF_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
		for (i = 0; i < menu_item->children_count; i++) {
			jf_menu_item_free(menu_item->children[i]);
		}
		free(menu_item->children);
		free(menu_item->name);
		free(menu_item);
	}
}


jf_menu_item *jf_menu_item_static_copy(jf_menu_item *dest, const jf_menu_item *src)
{
	if (dest != NULL) {
		memcpy(dest, src, sizeof(jf_menu_item));
		dest->children = NULL;
		dest->children_count = 0;
	}
	return dest;
}
//////////////////////////////////


////////// GLOBAL APPLICATION STATE //////////
void jf_global_state_clear()
{
	free(g_state.config_dir);
	free(g_state.runtime_dir);
	free(g_state.session_id);
	free(g_state.server_name);
}
//////////////////////////////////////////////


////////// THREAD BUFFER //////////
void jf_thread_buffer_init(jf_thread_buffer *tb)
{
	tb->used = 0;
	tb->promiscuous_context = false;
	tb->state = JF_THREAD_BUFFER_STATE_CLEAR;
	tb->item_count = 0;
	assert(pthread_mutex_init(&tb->mut, NULL) == 0);
	assert(pthread_cond_init(&tb->cv_no_data, NULL) == 0);
	assert(pthread_cond_init(&tb->cv_has_data, NULL) == 0);
}
///////////////////////////////////


////////// GROWING BUFFER //////////
jf_growing_buffer *jf_growing_buffer_new(const size_t size)
{
	jf_growing_buffer *buffer;
	assert((buffer = malloc(sizeof(jf_growing_buffer))) != NULL);
	assert((buffer->buf = malloc(size > 0 ? size : 1024)) != NULL);
	buffer->size = size > 0 ? size : 1024;
	buffer->used = 0;
	return buffer;
}


void jf_growing_buffer_append(jf_growing_buffer *buffer, const void *data,
		const size_t length)
{
	size_t estimate;

	if (buffer == NULL) {
		return;
	}

	if (buffer->used + length > buffer->size) {
		estimate = (buffer->used + length) / 2 * 3;
		buffer->size = estimate >= buffer->size * 2 ? estimate : buffer->size * 2;
		assert((buffer->buf = realloc(buffer->buf, buffer->size)) != NULL);
	}
	memcpy(buffer->buf + buffer->used, data, length);
	buffer->used += length;
}


void jf_growing_buffer_empty(jf_growing_buffer *buffer)
{
	if (buffer == NULL) {
		return;
	}
	
	buffer->used = 0;
}


void jf_growing_buffer_free(jf_growing_buffer *buffer)
{
	if (buffer == NULL) {
		return;
	}

	free(buffer->buf);
	free(buffer);
}
////////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
void jf_mpv_clear()
{
	mpv_terminate_destroy(g_mpv_ctx);
}


char *jf_concat(size_t n, ...)
{
	char *buf;
	size_t *argv_len;
	size_t len = 0;
	size_t i;
	va_list ap;

	assert((argv_len = malloc(sizeof(size_t) * n)) != NULL);
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		argv_len[i] = strlen(va_arg(ap, char*));
		len += argv_len[i];
	}
	va_end(ap);

	assert((buf = malloc(len + 1)) != NULL);
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


void jf_print_zu(size_t n)                                               
{                                                                       
	static char str[20];                                              
	unsigned char i = 0;
	do {
		str[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	while (i-- != 0) {
		fwrite(str + i, 1, 1, stdout);
	}
}


char *jf_generate_random_id(size_t len)
{
	char *rand_id;

	// default length
	len = len > 0 ? len : 10;

	assert((rand_id = malloc(len + 1)) != NULL);
	rand_id[len] = '\0';
	srand((unsigned int)time(NULL));
	for (; len > 0; len--) {
		rand_id[len - 1] = '0' + rand() % 10;
	}
	return rand_id;
}


char *jf_make_timestamp(const long long ticks)
{
	char *str;
	unsigned char seconds, minutes, hours;
	seconds = (ticks / 10000000) % 60;
	minutes = (ticks / 10000000 / 60) % 60;
	hours = (unsigned char)(ticks / 10000000 / 60 / 60);

	// allocate with overestimate. we shan't cry
	if ((str = malloc(sizeof("xxx:xx:xx"))) == NULL) {
		return NULL;
	}
	snprintf(str, sizeof("xxx:xx:xx"), "%02u:%02u:%02u", hours, minutes, seconds);
	return str;
}


JF_FORCE_INLINE size_t jf_clamp_zu(const size_t zu, const size_t min,
		const size_t max)
{
	return zu < min ? min : zu > max ? max : zu;
}


JF_FORCE_INLINE void jf_clear_stdin()
{
	fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);
	while (getchar() != EOF) ;
	fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);
}
///////////////////////////////////////////


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
