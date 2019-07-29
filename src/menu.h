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
// Function: jf_user_interface
//
// Runs the user interface loop until switching context to mpv or exiting.
bool jf_user_interface(void);
/////////////////////////////////////////


#endif
