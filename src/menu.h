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


jf_menu_item *jf_menu_item_new(jf_item_type type, char *id, jf_menu_item *children);
bool jf_menu_item_free(jf_menu_item *menu_item);

#endif
