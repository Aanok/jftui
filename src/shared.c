#include "shared.h"


////////// GLOBALS //////////
extern jf_global_state g_state;
/////////////////////////////


////////// STATIC FUNCTIONS //////////
#ifdef JF_DEBUG
static void jf_menu_item_print_indented(const jf_menu_item *item, const size_t level);
#endif
//////////////////////////////////////


////////// JF_MENU_ITEM //////////
const char *jf_item_type_get_name(const jf_item_type type)
{
	switch (type) {
		case JF_ITEM_TYPE_NONE:
			return "None";
		case JF_ITEM_TYPE_AUDIO:
			return "Audio";
		case JF_ITEM_TYPE_AUDIOBOOK:
			return "Audiobook";
		case JF_ITEM_TYPE_EPISODE:
			return "Episde";
		case JF_ITEM_TYPE_MOVIE:
			return "Movie";
		case JF_ITEM_TYPE_VIDEO_SOURCE:
			return "Video_Source";
		case JF_ITEM_TYPE_VIDEO_SUB:
			return "Video_Sub";
		case JF_ITEM_TYPE_COLLECTION:
		case JF_ITEM_TYPE_COLLECTION_MUSIC:
		case JF_ITEM_TYPE_COLLECTION_SERIES:
		case JF_ITEM_TYPE_COLLECTION_MOVIES:
		case JF_ITEM_TYPE_USER_VIEW:
		case JF_ITEM_TYPE_FOLDER:
		case JF_ITEM_TYPE_PLAYLIST:
		case JF_ITEM_TYPE_ARTIST:
		case JF_ITEM_TYPE_ALBUM:
		case JF_ITEM_TYPE_SEASON:
		case JF_ITEM_TYPE_SERIES:
			return "Folder";
		case JF_ITEM_TYPE_SEARCH_RESULT:
			return "Search_Result";
		case JF_ITEM_TYPE_MENU_ROOT:
		case JF_ITEM_TYPE_MENU_FAVORITES:
		case JF_ITEM_TYPE_MENU_CONTINUE:
		case JF_ITEM_TYPE_MENU_NEXT_UP:
		case JF_ITEM_TYPE_MENU_LATEST_UNPLAYED:
		case JF_ITEM_TYPE_MENU_LIBRARIES:
			return "Persistent_Folder";
		default:
			return "Unrecognized";
	}
}


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
		while (*(menu_item->children) != NULL) {
			menu_item->children_count++;
			menu_item->children++;
		}
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


#ifdef JF_DEBUG
static void jf_menu_item_print_indented(const jf_menu_item *item, const size_t level)
{
	size_t i;

	if (item == NULL) return;

	JF_PRINTF_INDENT("Name: %s\n", item->name);
	JF_PRINTF_INDENT("Type: %s\n", jf_item_type_get_name(item->type));
	JF_PRINTF_INDENT("Id: %s\n", item->id);
	JF_PRINTF_INDENT("PB ticks: %lld, RT ticks: %lld\n", item->playback_ticks, item->runtime_ticks);
	if (item->children_count > 0) {
		JF_PRINTF_INDENT("Children:\n");
		for (i = 0; i < item->children_count; i++) {
			jf_menu_item_print_indented(item->children[i], level + 1);
		}
	}
}


void jf_menu_item_print(const jf_menu_item *item)
{
	jf_menu_item_print_indented(item, 0);
}
#endif
//////////////////////////////////


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
		size_t length)
{
	size_t estimate;

	if (buffer == NULL) return;

	if (length == 0) {
		length = strlen(data);
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
	if (buffer == NULL) return;
	
	buffer->used = 0;
}


void jf_growing_buffer_free(jf_growing_buffer *buffer)
{
	if (buffer == NULL) return;

	free(buffer->buf);
	free(buffer);
}
////////////////////////////////////


////////// SYNCED QUEUE //////////
jf_synced_queue *jf_synced_queue_new(const size_t slots)
{
	jf_synced_queue *q;

	assert((q = malloc(sizeof(jf_synced_queue))) != NULL);
	assert((q->slots = calloc(slots, sizeof(void *))) != NULL);
	q->slot_count = slots;
	q->current = 0;
	q->next = 0;
	assert(pthread_mutex_init(&q->mut, NULL) == 0);
	assert(pthread_cond_init(&q->cv_is_empty, NULL) == 0);
	assert(pthread_cond_init(&q->cv_is_full, NULL) == 0);
	return q;
}


void jf_synced_queue_free(jf_synced_queue *q)
{
	free(q);
}


void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload)
{
	if (payload == NULL) return;

	pthread_mutex_lock(&q->mut);
	while (q->slots[q->next] != NULL) {
		pthread_cond_wait(&q->cv_is_full, &q->mut);
	}
	q->slots[q->next] = payload;
	q->next = (q->next + 1) % q->slot_count;
	pthread_mutex_unlock(&q->mut);
	pthread_cond_signal(&q->cv_is_empty);
}


void *jf_synced_queue_dequeue(jf_synced_queue *q)
{
	void *payload;

	pthread_mutex_lock(&q->mut);
	while (q->slots[q->current] == NULL) {
		pthread_cond_wait(&q->cv_is_empty, &q->mut);
	}
	payload = (void *)q->slots[q->current];
	q->slots[q->current] = NULL;
	q->current = (q->current + 1) % q->slot_count;
	pthread_mutex_unlock(&q->mut);
	pthread_cond_signal(&q->cv_is_full);

	return payload;
}
//////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
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
		argv_len[i] = strlen(va_arg(ap, const char*));
		len += argv_len[i];
	}
	va_end(ap);

	assert((buf = malloc(len + 1)) != NULL);
	len = 0;
	va_start(ap, n);
	for (i = 0; i < n; i++) {
		memcpy(buf + len, va_arg(ap, const char*), argv_len[i]);
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
	assert((str = malloc(sizeof("xxx:xx:xx"))) != NULL);
	snprintf(str, sizeof("xxx:xx:xx"), "%02u:%02u:%02u", hours, minutes, seconds);
	return str;
}


inline size_t jf_clamp_zu(const size_t zu, const size_t min,
		const size_t max)
{
	return zu < min ? min : zu > max ? max : zu;
}


inline void jf_clear_stdin()
{
	fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);
	while (getchar() != EOF) ;
	fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);
}
///////////////////////////////////////////
