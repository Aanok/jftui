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

// for hardcoded strings
#define JF_STATIC_STRLEN(str) (sizeof(str) - 1)

#define JF_STATIC_PRINT(str)					\
	do {										\
		write(1, str, JF_STATIC_STRLEN(str));	\
	} while (false)

#define JF_STATIC_PRINT_ERROR(str)				\
	do {										\
		write(2, str, JF_STATIC_STRLEN(str));	\
	} while (false)

#define JF_VERSION "prealpha"

#define JF_THREAD_BUFFER_DATA_SIZE (CURL_MAX_WRITE_SIZE +1)

#define JF_ID_LENGTH 32

#define JF_CONFIG_DEVICEID_MAX_LEN 32


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

// Special menu commands
#define JF_ITEM_TYPE_COMMAND_QUIT	10

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
	char id[JF_ID_LENGTH +1];
	struct jf_menu_item **children; // NULL-terminated
	char *name;
} jf_menu_item;


// Function: jf_menu_item_new
//
// Allocates a jf_menu_item struct in dynamic memory.
//
// Parameters:
// 	- type: the jf_item_type of the menu item being represented.
// 	- id: the string marking the id of the item. It will be copied to an internal buffer and must have JF_ID_LENGTH size but does not need to be \0-terminated. May be NULL for persistent menu items, in which case the internal buffer will contain a \0-terminated empty string.
// 	- children: a NULL-terminated array of pointers to jf_menu_item's that descend from the current one in the UI/library hierarchy.
//
// Returns:
//  A pointer to the newly allocated struct on success or NULL on failure.
jf_menu_item *jf_menu_item_new(jf_item_type type, const char *id, jf_menu_item **children);

// Function jf_menu_item_free
//
// Deallocates a jf_menu_item and all its descendants recursively, unless they are marked as persistent (as per JF_ITEM_TYPE_IS_PERSISTENT).
//
// Parameters:
// 	- menu_item: a pointer to the struct to deallocate. It may be NULL, in which case the function will no-op.
//
// Returns:
//  true if the item was deallocated or NULL was passed, false otherwise.
bool jf_menu_item_free(jf_menu_item *menu_item);
//////////////////////////////////////////////////////////


////////// GLOBAL APPLICATION STATE //////////
typedef struct jf_global_state {
	const char *config_dir;
	const char *runtime_dir;
	const char *session_id;
} jf_global_state;
//////////////////////////////////////////////


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
	unsigned char *parsed_ids;
	size_t parsed_ids_size;
	size_t item_count;
	pthread_mutex_t mut;
	pthread_cond_t cv_no_data;
	pthread_cond_t cv_has_data;
} jf_thread_buffer;


bool jf_thread_buffer_init(jf_thread_buffer *tb);
///////////////////////////////////


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
