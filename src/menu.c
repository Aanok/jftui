#include "menu.h"


jf_menu_item *jf_menu_item_new(jf_item_type type, char *id, jf_menu_item *children)
{
	jf_menu_item *menu_item;

	if ((menu_item = malloc(sizeof(jf_menu_item))) == NULL) {
		return (jf_menu_item *)NULL;
	}

	menu_item->type = type;
	menu_item->id = id;
	menu_item->children = children;

	return menu_item;
}


// TODO: review if free(id) is legit
bool jf_menu_item_free(jf_menu_item *menu_item)
{
	jf_menu_item *child;
	if (menu_item != NULL && ! JF_MENU_ITEM_TYPE_IS_PERSISTENT(menu_item->type)) {
		free(menu_item->id);
		child = menu_item->children;
		while (child) {
			jf_menu_item_free(child);
			child++;
		}
		return true;
	} else {
		return false;
	}
}

