#ifndef _JF_MENU
#define _JF_MENU

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>

#include "shared.h"
#include "config.h"
#include "network.h"


#define JF_CLEAR_STDIN()									\
	do {													\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)|O_NONBLOCK);	\
		while (getchar() != EOF) ;							\
		fcntl(0, F_SETFL, fcntl(0, F_GETFL)& ~O_NONBLOCK);	\
	} while (false)


////////// JF_MENU_UI_STATUS //////////
typedef unsigned char jf_menu_ui_status;

#define JF_MENU_UI_STATUS_GO_ON		0
#define JF_MENU_UI_STATUS_PLAYBACK	1
#define JF_MENU_UI_STATUS_ERROR		2
#define JF_MENU_UI_STATUS_QUIT		3
///////////////////////////////////////


////////// JF_MENU_ITEM //////////

// Function: jf_menu_item_new
//
// Allocates a jf_menu_item struct in dynamic memory.
//
// Parameters:
// 	- type: the jf_item_type of the menu item being represented.
// 	- id: the string marking the id of the item. It will be copied to an internal buffer and must have JF_ID_LENGTH size but does not need to be \0-terminated. May be NULL for persistent menu items, in which case the internal buffer will contain a \0-terminated empty string.
// 	- children: a NULL-terminated array of jf_menu_item's that descend from the current one in the UI/library hierarchy.
//
// Returns:
//  A pointer to the newly allocated struct on success or NULL on failure.
jf_menu_item *jf_menu_item_new(jf_item_type type, const char *id, jf_menu_item *children);

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
//////////////////////////////////


////////// JF_MENU_STACK //////////
typedef struct jf_menu_stack {
	jf_menu_item **items;
	size_t size;
	size_t used;
} jf_menu_stack;

// Procedure: jf_menu_stack_init
//
// Initializes the global menu stack struct.
//
// Returns:
// 	true on success, false on failure.
bool jf_menu_stack_init(void);


// Procedure: jf_menu_stack_clear
//
// Clears the contents of the global menu stack, forcibly deallocating all items
// regardless of their persistency bit.
void jf_menu_stack_clear(void);
///////////////////////////////////


////////// USER INTERFACE LOOP //////////

// Function: jf_menu_child_is_folder
//
// Checks if the n-th child of the current open menu context is a folder type or not.
// This function exists for the sake of the command parser.
//
// Parameters:
// 	n - 1-indexed identifier of the child whose item_type to check.
//
// Returns:
//  true if there is a context to check, the child exists and it is a folder; false otherwise.
bool jf_menu_child_is_folder(const size_t n);


// Procedure: jf_menu_child_push
//
// Pushes the n-th child of the current open menu context onto the menu_stack.
// This function exists for the sake of the command parser.
//
// Parameters:
// 	n - 1-indexed identifier of the child to push on the menu_stack.
void jf_menu_child_push(const size_t n);


size_t jf_menu_child_count(void);


void jf_menu_dotdot(void);


void jf_menu_quit(void);


// Function: jf_menu_ui
//
// Runs the user interface loop until switching context to mpv or exiting.
//
// Returns:
// 	- JF_MENU_UI_STATUS_GO_ON if the UI loop should iterate again;
// 	- JF_MENU_UI_STATUS_PLAYBACK if the context should switch to mpv playback;
// 	- JF_MENU_UI_STATUS_ERROR on an error state;
// 	- JF_MENU_UI_STATUS_QUIT if the user requested the application to exit.
jf_menu_ui_status jf_menu_ui(void);
/////////////////////////////////////////


#endif
