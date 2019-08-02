#ifndef _JF_MENU
#define _JF_MENU

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>

#include "shared.h"
#include "config.h"
#include "network.h"


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, char *id, jf_menu_item *children);
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


// Procedure: jf_menu_push_item
//
// Pushes the n-th child of the current open menu context onto the menu_stack.
// This function exists for the sake of the command parser.
//
// Parameters:
// 	n - 1-indexed identifier of the child to push on the menu_stack.
void jf_menu_push_item(const size_t n);


// Function: jf_menu_ui
//
// Runs the user interface loop until switching context to mpv or exiting.
bool jf_menu_ui(void);
/////////////////////////////////////////


#endif
