#ifndef _JF_SHARED
#define _JF_SHARED

#include <pthread.h>
#include <curl/curl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>

#include <mpv/client.h>


////////// CODE MACROS //////////
// for hardcoded strings
#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

#define JF_STATIC_WRITE(str)					\
	do {										\
		write(1, str, JF_STATIC_STRLEN(str));	\
	} while (false)

#define JF_STATIC_WRITE_ERROR(str)				\
	do {										\
		write(2, str, JF_STATIC_STRLEN(str));	\
	} while (false)

#define JF_TICKS_TO_SECS(t)	(t) / 10000000
#define JF_SECS_TO_TICKS(s)	(s) * 10000000
/////////////////////////////////


////////// CONSTANTS //////////
#define JF_VERSION "prealpha"

#define JF_THREAD_BUFFER_DATA_SIZE (CURL_MAX_WRITE_SIZE +1)

#define JF_ID_LENGTH 32

#define JF_CONFIG_DEVICEID_MAX_LEN 32
///////////////////////////////


////////// GENERIC JELLYFIN ITEM REPRESENTATION //////////
// Information about persistency is used to make part of the menu interface
// tree not get deallocated when navigating upwards
typedef char jf_item_type;

// Atoms
#define JF_ITEM_TYPE_NONE			0
#define JF_ITEM_TYPE_AUDIO			1
#define JF_ITEM_TYPE_EPISODE		2
#define JF_ITEM_TYPE_MOVIE			3
#define JF_ITEM_TYPE_AUDIOBOOK		4

// Folders
#define JF_ITEM_TYPE_COLLECTION			20
#define JF_ITEM_TYPE_COLLECTION_MUSIC	21
#define JF_ITEM_TYPE_COLLECTION_SERIES	22
#define JF_ITEM_TYPE_COLLECTION_MOVIES	23
#define JF_ITEM_TYPE_USER_VIEW			24
#define JF_ITEM_TYPE_FOLDER				25
#define JF_ITEM_TYPE_PLAYLIST			26
#define JF_ITEM_TYPE_ARTIST				27
#define JF_ITEM_TYPE_ALBUM				28
#define JF_ITEM_TYPE_SEASON				29
#define JF_ITEM_TYPE_SERIES				30

#define JF_ITEM_TYPE_SEARCH_RESULT		100

// Persistent folders
#define JF_ITEM_TYPE_MENU_ROOT		-1
#define JF_ITEM_TYPE_MENU_FAVORITES	-2
#define JF_ITEM_TYPE_MENU_CONTINUE  -3
#define JF_ITEM_TYPE_MENU_NEXT_UP	-4
#define JF_ITEM_TYPE_MENU_LATEST	-5
#define JF_ITEM_TYPE_MENU_LIBRARIES	-6

// Category macros. They're all expressions
// UPDATE THESE if you add item_type's or change the item_type representation!
#define JF_ITEM_TYPE_IS_PERSISTENT(t)			((t) < 0)
#define JF_ITEM_TYPE_IS_FOLDER(t)				((t) < 0 || (t) >= 20)
#define JF_ITEM_TYPE_HAS_DYNAMIC_CHILDREN(t)	((t) < -1 || (t) >= 20)


typedef struct jf_menu_item {
	jf_item_type type;
	struct jf_menu_item **children; // NULL-terminated
	char id[JF_ID_LENGTH +1];
	char *name;
	long long playback_ticks;
	long long runtime_ticks;
} jf_menu_item;


// Function: jf_menu_item_new
//
// Allocates a jf_menu_item struct in dynamic memory.
//
// Parameters:
// 	- type: the jf_item_type of the menu item being represented.
// 	- children: a NULL-terminated array of pointers to jf_menu_item's that descend from the current one in the UI/library hierarchy.
// 	- id: the string marking the id of the item. It will be copied to an internal buffer and must have JF_ID_LENGTH size but does not need to be \0-terminated. May be NULL for persistent menu items, in which case the internal buffer will contain a \0-terminated empty string.
// 	- name: the string marking the display name of the item. It must be \0-terminated. It will be copied by means of strdup. May be NULL, in which case the corresponding field of the jf_menu_item will be NULL.
// 	- runtime_ticks: length of underlying media item measured in Jellyfin ticks.
// 	- playback_ticks: progress marker for partially viewed items measured in Jellyfin ticks.
//
// Returns:
//  A pointer to the newly allocated struct on success or NULL on failure.
jf_menu_item *jf_menu_item_new(jf_item_type type, jf_menu_item **children,
		const char *id, const char *name, const long long runtime_ticks,
		const long long playback_ticks);

// Function jf_menu_item_free
//
// Deallocates a jf_menu_item and all its descendants recursively, unless they are marked as persistent (as per JF_ITEM_TYPE_IS_PERSISTENT).
//
// Parameters:
// 	- menu_item: a pointer to the struct to deallocate. It may be NULL, in which case the function will no-op.
void jf_menu_item_free(jf_menu_item *menu_item);


// children is set to NULL in dest
jf_menu_item *jf_menu_item_static_copy(jf_menu_item *dest, const jf_menu_item *src);
//////////////////////////////////////////////////////////


////////// GROWING BUFFER //////////
typedef struct jf_growing_buffer {
	char *buf;
	size_t size;
	size_t used;
} jf_growing_buffer;


jf_growing_buffer *jf_growing_buffer_new(const size_t size);
bool jf_growing_buffer_append(jf_growing_buffer *buffer, const void *data, const size_t length);
bool jf_growing_buffer_empty(jf_growing_buffer *buffer);
void jf_growing_buffer_free(jf_growing_buffer *buffer);
////////////////////////////////////


////////// THREAD_BUFFER //////////
typedef unsigned char jf_thread_buffer_state;

#define JF_THREAD_BUFFER_STATE_CLEAR			0
#define JF_THREAD_BUFFER_STATE_AWAITING_DATA	1
#define JF_THREAD_BUFFER_STATE_PENDING_DATA		2
#define JF_THREAD_BUFFER_STATE_PARSER_ERROR		3
#define JF_THREAD_BUFFER_STATE_PARSER_DEAD		4


typedef struct jf_thread_buffer {
	char data[JF_THREAD_BUFFER_DATA_SIZE];
	size_t used;
	bool promiscuous_context;
	jf_thread_buffer_state state;
	size_t item_count;
	pthread_mutex_t mut;
	pthread_cond_t cv_no_data;
	pthread_cond_t cv_has_data;
} jf_thread_buffer;


bool jf_thread_buffer_init(jf_thread_buffer *tb);
///////////////////////////////////


////////// GLOBAL APPLICATION STATE //////////
typedef char jf_jftui_state;

#define JF_STATE_STARTING				0
#define JF_STATE_STARTING_FULL_CONFIG	1
#define JF_STATE_STARTING_LOGIN			2
#define JF_STATE_MENU_UI				3
#define JF_STATE_PLAYBACK				4
#define JF_STATE_PLAYBACK_NAVIGATING	5
#define JF_STATE_PLAYBACK_START_MARK	6

#define JF_STATE_USER_QUIT	-1
#define JF_STATE_FAIL		-2


typedef struct jf_global_state {
	char *config_dir;
	char *runtime_dir;
	char *session_id;
	char *server_name;
	jf_jftui_state state;
	jf_menu_item now_playing;
} jf_global_state;


void jf_global_state_clear(void);
//////////////////////////////////////////////


////////// MISCELLANEOUS GARBAGE //////////
void jf_mpv_clear(void);


// returns a NULL-terminated, malloc'd string result of the concatenation of its (expected char *) arguments past the first
// the first argument is the number of following arguments
char *jf_concat(const size_t n, ...);


// Function: jf_print_zu
//
// Prints an unsigned, base-10 number to stdout. The function is NEITHER reentrant NOR thread-safe.
// IT WILL CAUSE UNDEFINED BEHAVIOUR if the base-10 representation of the argument is longer than 20 digits,
// which means the binary representation of the number takes more than 64 bits.
//
// Parameters:
// 	- n: The number to print. It is always treated as unsigned and base-10. Regardless of the system's
// 		 implementation of size_t, it must fit into 64 bits for the internal buffer not to overflow.
void jf_print_zu(size_t n);


// Function jf_generate_random_id
//
// Generates malloc'd string of random digits of arbitrary length.
//
// Parameters:
// 	- len: length of the random string. If 0, a default of 10 will be applied.
//
// Returns:
// 	Pointer to the string. It will need be free'd.
char *jf_generate_random_id(size_t length);


char *jf_make_timestamp(const long long ticks);
///////////////////////////////////////////


// UNUSED FOR NOW
// typedef struct jf_synced_queue {
// 	const void **slots;
// 	size_t slot_count;
// 	size_t current;
// 	size_t next;
// 	pthread_mutex_t mut;
// 	pthread_cond_t cv_is_empty;
// 	pthread_cond_t cv_is_full;
// } jf_synced_queue;
//
// jf_synced_queue *jf_synced_queue_new(const size_t slot_count);
// void jf_synced_queue_free(jf_synced_queue *q); // NB will NOT deallocate the contents of the queue! make sure it's empty beforehand to avoid leaks
// void jf_synced_queue_enqueue(jf_synced_queue *q, const void *payload);
// void *jf_synced_queue_dequeue(jf_synced_queue *q);
// size_t jf_synced_queue_is_empty(const jf_synced_queue *q);

#endif
