#include "menu.h"


////////// GLOBAL VARIABLES //////////
extern jf_options g_options;
//////////////////////////////////////


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

static jf_menu_item *jf_menu_make_ui(void);
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


static jf_menu_item *jf_menu_make_ui()
{
	{
		jf_menu_item *libraries;
		if ((libraries = jf_menu_item_new(JF_ITEM_TYPE_MENU_LIBRARIES,
						jf_concat(3, "/users/", g_options.userid, "/views"), NULL)) == NULL) {
			return NULL;
		}
		return libraries;
	}
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

	// create persistent interface tree

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
		jf_menu_item_conditional_free(jf_menu_stack_pop(), false);
	}
}


bool jf_user_interface()
{
		jf_menu_item *context, *child;
		jf_reply *reply;
		size_t i = 0;

		if ((context = jf_menu_stack_pop()) == NULL) {
			if ((context = jf_menu_make_ui()) == NULL) {
				fprintf(stderr, "FATAL: jf_menu_make_ui() returned NULL.\n");
				return false;
			}
			jf_menu_stack_push(context);
		}

		// TODO print title

		// dispatch; download more info if necessary
		switch (context->type) {
			// dynamic directories: fetch children, parser prints entries
			case JF_ITEM_TYPE_COLLECTION:
			case JF_ITEM_TYPE_FOLDER:
			case JF_ITEM_TYPE_PLAYLIST:
			case JF_ITEM_TYPE_ARTIST:
			case JF_ITEM_TYPE_ALBUM:
			case JF_ITEM_TYPE_SEASON:
			case JF_ITEM_TYPE_SERIES:
			case JF_ITEM_TYPE_MENU_LIBRARIES:
				if ((reply = jf_request(context->id, JF_REQUEST_SAX, NULL)) == NULL) {
					fprintf(stderr, "FATAL: could not allocate jf_request.\n");
					return false;
				}
				if (reply->size < 0) {
					fprintf(stderr, "ERROR: request for resource %s failed: %s\n",
							context->id, jf_reply_error_string(reply));
					jf_reply_free(reply);
					return false;
				}
				jf_reply_free(reply);
				break;
			// persistent menu directories: fetch nothing, print entries by hand
			case JF_ITEM_TYPE_MENU_ROOT:
			case JF_ITEM_TYPE_MENU_FAVORITES:
			case JF_ITEM_TYPE_MENU_ON_DECK:
			case JF_ITEM_TYPE_MENU_LATEST:
				child = context->children;
				while (child) {
					switch (child->type) {
						case JF_ITEM_TYPE_MENU_FAVORITES:
							printf("D %zu. Favorites\n", i);
							break;
						case JF_ITEM_TYPE_MENU_ON_DECK:
							printf("D %zu. On Deck\n", i);
							break;
						default:
							printf("[!!!] %zu. WARNING: unrecognized menu item. Undefined behaviour if chosen. This is a bug.\n", i);
							break;
					}
					child++;
					i++;
				}
				break;
				// TODO: individual items should be handled by another function
			default:
				printf("Individual item; id: %s\n", context->id);
				break;
		}

		// TODO read command

		return true;
}
///////////////////////////////////
