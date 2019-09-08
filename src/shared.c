#include "shared.h"


////////// GLOBALS //////////
extern jf_global_state g_state;
extern mpv_handle *g_mpv_ctx;
/////////////////////////////


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, jf_menu_item **children,
		const char *id, const char *name, long long ticks)
{
	jf_menu_item *menu_item;

	if ((menu_item = malloc(sizeof(jf_menu_item))) == NULL) {
		return NULL;
	}

	menu_item->type = type;
	menu_item->children = children;
	if (id == NULL) {
		menu_item->id[0] = '\0';
	} else {
		strncpy(menu_item->id, id, JF_ID_LENGTH);
		menu_item->id[JF_ID_LENGTH] = '\0';
	}
	if (name == NULL) {
		menu_item->name = NULL;
	} else {
		menu_item->name = strdup(name);
	}
	menu_item->ticks = ticks;
	
	return menu_item;
}


void jf_menu_item_free(jf_menu_item *menu_item)
{
	jf_menu_item **child;

	if (menu_item == NULL) {
		return;
	}

	if (! (JF_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
		if ((child = menu_item->children) != NULL) {
			while (*child != NULL) {
				jf_menu_item_free(*child);
				child++;
			}
			free(menu_item->children);
		}
		free(menu_item->name);
		free(menu_item);
	}
}
//////////////////////////////////


////////// GLOBAL APPLICATION STATE //////////
bool jf_global_state_init(void)
{
	// runtime_dir
	if ((g_state.runtime_dir = getenv("XDG_DATA_HOME")) == NULL) {
		if ((g_state.runtime_dir = getenv("HOME")) == NULL) {
			return false;
		} else {
			g_state.runtime_dir = jf_concat(2, getenv("HOME"), "/.local/share/jftui");
		}
	} else {
		g_state.runtime_dir = jf_concat(2, g_state.runtime_dir, "/jftui");
	}

	// session_id
	if ((g_state.session_id = jf_generate_random_id(0)) == NULL) {
		return false;
	}

	return true;
}


void jf_global_state_clear()
{
	free(g_state.config_dir);
	free(g_state.runtime_dir);
	free(g_state.session_id);
	free(g_state.server_name);
}
//////////////////////////////////////////////


////////// THREAD BUFFER //////////
bool jf_thread_buffer_init(jf_thread_buffer *tb)
{
	tb->used = 0;
	tb->promiscuous_context = false;
	tb->state = JF_THREAD_BUFFER_STATE_CLEAR;
	tb->item_count = 0;
	pthread_mutex_init(&tb->mut, NULL);
	pthread_cond_init(&tb->cv_no_data, NULL);
	pthread_cond_init(&tb->cv_has_data, NULL);
	return true;
}
///////////////////////////////////


////////// GROWING BUFFER //////////
jf_growing_buffer *jf_growing_buffer_new(const size_t size)
{
	jf_growing_buffer *buffer;

	if ((buffer = malloc(sizeof(jf_growing_buffer))) == NULL) {
		return NULL;
	}

	if ((buffer->buf = malloc(size > 0 ? size : 1024)) == NULL) {
		free(buffer);
		return NULL;
	}
	buffer->size = size > 0 ? size : 1024;
	buffer->used = 0;

	return buffer;
}


bool jf_growing_buffer_append(jf_growing_buffer *buffer, const void *data, const size_t length)
{
	if (buffer == NULL) {
		return false;
	}

	if (buffer->used + length > buffer->size) {
		size_t new_size = 1.5 * (buffer->used + length);
		new_size = new_size >= buffer->size * 2 ? new_size : buffer->size * 2;
		void *tmp = realloc(buffer->buf, new_size);
		if (tmp == NULL) {
			return false;
		}
		buffer->buf = tmp;
		buffer->size = new_size;
	}
	memcpy(buffer->buf + buffer->used, data, length);
	buffer->used += length;
	return true;
}


bool jf_growing_buffer_empty(jf_growing_buffer *buffer)
{
	if (buffer == NULL) {
		return false;
	}
	
	buffer->used = 0;
	return true;
}


void jf_growing_buffer_clear(jf_growing_buffer *buffer)
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


void jf_print_zu(size_t n)                                               
{                                                                       
	static char str[20];                                              
	unsigned char i = 0;
	do {
		str[i++] = n % 10 + '0';
	} while ((n /= 10) > 0);
	while (i-- != 0) {
		write(1, str + i, 1);
	}
}


char *jf_generate_random_id(size_t len)
{
	char *rand_id;

	// default length
	len = len > 0 ? len : 10;

	if ((rand_id = malloc(len + 1)) != NULL) {
		rand_id[len] = '\0';
		srand(time(NULL));
		for (; len > 0; len--) {
			rand_id[len - 1] = '0' + rand() % 10;
		}
	}

	return rand_id;
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
