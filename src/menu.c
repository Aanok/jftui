#include "menu.h"


////////// STATIC VARIABLES //////////
static jf_menu_stack s_menu_stack;
//////////////////////////////////////


////////// STATIC FUNCTIONS //////////
// Function: jf_menu_item_conditional_free
//
// Deallocates a menu item, with a switch for checking or ignoring the persistence bit.
// It is static, used privately by jf_menu_item_free and jf_menu_item_forced_free.
//
// Parameters:
// 	menu_item - The item to deallocate
// 	check_persistence - Boolean for checking or not the persistence bit.
//
// Returns:
// 	true on success or menu_itme == NULL, false otherwise
static bool jf_menu_item_conditional_free(jf_menu_item *menu_item, const bool check_persistence);
//////////////////////////////////////


////////// JF_MENU_ITEM //////////
jf_menu_item *jf_menu_item_new(jf_item_type type, char *id, jf_menu_item *children) {
	jf_menu_item *menu_item;

	if ((menu_item = malloc(sizeof(jf_menu_item))) == NULL) {
		return (jf_menu_item *)NULL;
	}

	menu_item->type = type;
	menu_item->id = id;
	menu_item->children = children;

	return menu_item;
}


bool jf_menu_item_free(jf_menu_item *menu_item)
{
	return jf_menu_item_conditional_free(menu_item, true);
}


bool jf_menu_item_force_free(jf_menu_item *menu_item)
{
	return jf_menu_item_conditional_free(menu_item, false);
}


// TODO: review if free(id) is legit
static bool jf_menu_item_conditional_free(jf_menu_item *menu_item, const bool check_persistent)
{
	jf_menu_item *child;

	if (menu_item == NULL) {
		return true;
	}

	if (! (check_persistent && JF_MENU_ITEM_TYPE_IS_PERSISTENT(menu_item->type))) {
		free(menu_item->id);
		child = menu_item->children;
		while (child) {
			jf_menu_item_conditional_free(child, check_persistent);
			child++;
		}
		free(menu_item);
		return true;
	}

	return false;
}
//////////////////////////////////


////////// JF_MENU_STACK //////////
bool jf_menu_stack_init()
{
	if ((s_menu_stack.items = malloc(10 * sizeof(jf_menu_item *))) == NULL) {
		return false;
	}
	s_menu_stack.size = 10;
	s_menu_stack.used = 0;
	return true;
}


bool jf_menu_stack_push(jf_menu_item *menu_item)
{
	if (menu_item == NULL) {
		return true;
	}

	if (s_menu_stack.size == s_menu_stack.used) {
		jf_menu_item **tmp;
		tmp = realloc(s_menu_stack.items, s_menu_stack.size * 2 * sizeof(jf_menu_item *));
		if (tmp == NULL) {
			return false;
		}
		s_menu_stack.size *= 2;
		s_menu_stack.items = tmp;
	}

	s_menu_stack.items[s_menu_stack.used++] = menu_item;
	return true;
}


jf_menu_item *jf_menu_stack_pop()
{
	jf_menu_item *retval;

	if (s_menu_stack.used == 0) {
		return NULL;
	}

	retval = s_menu_stack.items[--s_menu_stack.used];
	s_menu_stack.items[s_menu_stack.used] = NULL;
	return retval;
}


const jf_menu_item *jf_menu_stack_peek()
{
	if (s_menu_stack.used == 0) {
		return NULL;
	}
	return s_menu_stack.items[s_menu_stack.used - 1];
}


void jf_menu_stack_clear()
{
	while (s_menu_stack.used > 0) {
		jf_menu_item_force_free(jf_menu_stack_pop());
	}
}
///////////////////////////////////
