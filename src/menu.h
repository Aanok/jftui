#ifndef _JF_MENU
#define _JF_MENU

#include <stdlib.h>
#include <stdbool.h>

#include "shared.h"

typedef struct jf_menu_item {
	jf_item_type type;
	char *id;
	struct jf_menu_item *children; // NULL-terminated
} jf_menu_item;


typedef struct jf_menu_stack {
	jf_menu_item **items;
	size_t size;
	size_t used;
} jf_menu_stack;


jf_menu_item *jf_menu_item_new(jf_item_type type, char *id, jf_menu_item *children);
bool jf_menu_item_free(jf_menu_item *menu_item);
bool jf_menu_item_force_free(jf_menu_item *menu_item);


// Procedure: jf_menu_stack_init
//
// Initializes the global menu stack struct.
//
// Returns:
// 	true on success, false on failure.
bool jf_menu_stack_init(void);

// Function: jf_menu_stack_push
//
// Pushes a jf_menu_item on the global menu stack. No-op if NULL is passed.
// REQUIRES: global menu stack struct initialized.
//
// Parameters:
//
// 	menu_item - A pointer to the item to be pushed.
//
// Returns:
// 	true if success or NULL was passed, false otherwise.
bool jf_menu_stack_push(jf_menu_item *menu_item);

// Function: jf_menu_stack_pop
//
// Pops the top item out of the global menu stack.
// The caller assumes ownership of the popped item (i.e. will have to free it).
// REQUIRES: global menu stack struct initialized.
//
// Returns:
// 	A pointer to the item popped or NULL if the stack is empty.
jf_menu_item *jf_menu_stack_pop(void);

// Function: jf_menu_stack_peek
//
// Returns a pointer to the top item of the global menu stack without popping it.
// REQUIRES: global menu stack struct initialized.
//
// Returns:
// 	A constant pointer to the top item or NULL if the stack is empty.
const jf_menu_item *jf_menu_stack_peek(void);


// Procedure: jf_menu_stack_clear
//
// Clears the contents of the global menu stack, forcibly deallocating all items
// regardless of their persistency bit.
void jf_menu_stack_clear(void);


// Function: jf_user_interface
//
// Runs the user interface loop until switching context to mpv or exiting.
// TODO
void jf_user_interface(void);

#endif
