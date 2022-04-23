#include <string.h>

#include "dataobj/tabfile.h"

#include "simhotbar.h"

hotbar_t *hotbar_t::hotbar = nullptr;

void hotbar_t::load_tab() {
  dbg->message("hotbar_t", "load hotbar tab");
	tabfile_t hotbar_conf;
  hotbar_conf.open("hotbar.tab");
	tabfileobj_t contents;
	hotbar_conf.read(contents);
  {
	  const char *str = contents.get("n_slots");
    if (str) {
	    n_slots = (hotbar_size_t) atoi(str);
    }
    assert(n_slots > 0);
  }
  {
	  const char *str = contents.get("n_visible_hotbars");
    if (str) {
	    n_visible_hotbars = (hotbar_size_t) atoi(str);
    }
    assert(n_visible_hotbars > 0);
  }
  {
	  const char *str = contents.get("n_hotbars_per_row");
    if (str) {
	    n_hotbars_per_row = (uint16) atoi(str);
    }
    if (n_hotbars_per_row < 0) {
      n_hotbars_per_row = 1;
    }
  }
  {
	  const char *str = contents.get("vertical");
    if (str) {
	    vertical = (bool) atoi(str);
    }
  }
  {
	  const char *str = contents.get("slot_keys");
    if (str) {
      memset(slot_keys, 0, sizeof(slot_keys));
	    strcpy_s(slot_keys, sizeof(slot_keys), str);
    }
  }
  {
	  const char *str = contents.get("hotbar_keys");
    if (str) {
      memset(hotbar_keys, 0, sizeof(hotbar_keys));
	    strcpy_s(hotbar_keys, sizeof(hotbar_keys), str);
    }
  }
  {
	  const char *str = contents.get("get_modifier");
    if (str) {
	    get_key_mod = str_to_key(str, /* only_modifier = */ true).modifier;
    }
  }
  {
	  const char *str = contents.get("set_modifier");
    if (str) {
	    set_key_mod = str_to_key(str, /* only_modifier = */ true).modifier;
      dbg->message("hotbar", "set_key_mod: %d", set_key_mod);
    }
  }
  {
	  const char *str = contents.get("select_modifier");
    if (str) {
	    select_key_mod = str_to_key(str, /* only_modifier = */ true).modifier;
    }
  }
  {
	  const char *str = contents.get("toggle_hotbar");
    if (str) {
	    toggle_key = str_to_key(str);
    }
  }
  {
	  const char *str = contents.get("auto_select_tool");
    if (str) {
	    auto_select_tool = (bool) atoi(str);
    }
  }
}
