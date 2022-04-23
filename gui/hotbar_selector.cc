/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */


#include "../dataobj/environment.h"
#include "../display/simimg.h"
#include "../display/simgraph.h"
#include "../player/simplay.h"
#include "../utils/for.h"
#include "../utils/simstring.h"
#include "../simworld.h"
#include "../simmenu.h"
#include "../simskin.h"
#include "../utils/cbuffer_t.h"

#include "gui_frame.h"
#include "simwin.h"
#include "hotbar_selector.h"

#include <iostream>

#define MIN_WIDTH (80)

karte_ptr_t hotbar_selector_t::world;

hotbar_selector_t::hotbar_selector_t() : gui_component_t() {
	// set_table_layout(0, 0); // needed?
	pos = scr_coord(0, 0);
  hotbar = hotbar_t::hotbar;
  spacing_w = 2;
  spacing_h = 2;
  spacing_bars = 2;
	// set_windowsize(scr_size(width(), height()));
}

int hotbar_selector_t::bar_width() {
	return (hotbar->n_slots + 1) * (env_t::iconsize.w + spacing_w);
}

int hotbar_selector_t::bar_height() {
	return env_t::iconsize.w + spacing_h;
}

int hotbar_selector_t::height() {
  if (hotbar->vertical) {
	  return hotbar->n_hotbars_per_row * (bar_width() + spacing_bars);
  } else {
	  return spacing_h + (1 + (hotbar->n_visible_hotbars - 1) / hotbar->n_hotbars_per_row) * bar_height();
  }
}

int hotbar_selector_t::width() {
  if (hotbar->vertical) {
	  return spacing_h + (1 + (hotbar->n_visible_hotbars - 1) / hotbar->n_hotbars_per_row) * bar_height();
  } else {
	  return hotbar->n_hotbars_per_row * (bar_width() + spacing_bars);
  }
}

bool hotbar_selector_t::get_hotbar_slot(int x, int y, int &i, int &j) {
  if (hotbar->vertical) {
    int x_tmp = x;
    x = y;
    y = x_tmp;
  }
  y = y - spacing_h;
  y = y < 0 ? 0 : y;
  int a = x / bar_width();
	int b = y / bar_height();
  i = b * hotbar->n_hotbars_per_row + a;
  x = x - a * bar_width();
	j = x / (env_t::iconsize.w + spacing_w) - 1;
  if (j < 0) {
          j = 0;
          return false;
  } else {
          return true;
  }
}

void hotbar_selector_t::get_hotbar_slot_pos(int i, int j, int &x, int &y) {
	int a = i / hotbar->n_hotbars_per_row;
  int b = i - a * hotbar->n_hotbars_per_row;
  x = b * bar_width() + (j + 1) * (env_t::iconsize.w + spacing_w);
  y = a * bar_height() + spacing_h;
  if (hotbar->vertical) {
    int x_tmp = x;
    x = y;
    y = x_tmp;
  }
}

bool hotbar_selector_t::is_hit(int x, int y) {
	return visible && (pos.x <= x && x < pos.x + width() && pos.y <= y && y < pos.y + height());
}

void hotbar_selector_t::on_click(int x, int y, const event_t *ev) {
  int i = 0, j = 0;
  bool slot_selected = get_hotbar_slot(x - pos.x, y - pos.y, i, j);
  if (slot_selected) {
    if (IS_RIGHTCLICK(ev)) {
      hotbar->set_visible_slot(i, j, nullptr);
    } else {
      tool_t *tool = hotbar->get_visible_slot(i, j);
      if (tool) {
        world->set_tool(tool, world->get_active_player());
      } else {
	      tool = world->get_tool(world->get_active_player_nr());
        hotbar->set_visible_slot(i, j, tool);
      }
    }
  } else {
    hotbar->select(hotbar->visible_hotbars[i]);
  }
}

void hotbar_selector_t::draw(scr_coord /* offset */) {
  if (hotbar->visible != visible) {
    mark_rect_dirty_wc(pos.x, pos.y, width(), height());
    visible = hotbar->visible;
  }
  if (!visible) {
    return;
  }
	display_fillbox_wh_rgb(pos.x, pos.y, width(), height(), color_idx_to_rgb(MN_GREY1), false);
	// set_windowsize(scr_size(width(), height()));
	player_t *player = world->get_active_player();
  // if hotbar->slot_keys has changed:
  for (int i = 0; i < MAX_HOTBAR_SLOTS; i++) {
    labels_for_slots[2*i] = hotbar->slot_keys[i];
  }
  for (int i = 0; i < MAX_HOTBARS; i++) {
    labels_for_hotbars[2*i] = hotbar->hotbar_keys[i];
  }
	// display_push_clip_wh( pos.x, pos.y, sz.w, sz.h CLIP_NUM_PAR );
	for (int i = 0; i < hotbar->n_visible_hotbars; i++) {
    int x, y;
    get_hotbar_slot_pos(i, -1, x, y);
    /* show hotkey for the bar */
    if (hotbar->selected_hotbar == hotbar->visible_hotbars[i]) {
		  display_fillbox_wh_clip_rgb(pos.x + x, pos.y + y, env_t::iconsize.w, env_t::iconsize.h, color_idx_to_rgb(COL_ORANGE), true);
    }
		display_proportional_clip_rgb(pos.x + x, pos.y + y, &labels_for_hotbars[2*hotbar->visible_hotbars[i]],
      ALIGN_LEFT, color_idx_to_rgb(COL_BLACK), true);
    
    for (int j = 0; j < hotbar->n_slots; j++) {
      int x, y;
      get_hotbar_slot_pos(i, j, x, y);
		  const scr_coord draw_pos = pos + scr_coord(x, y);
		  const tool_t *tool = hotbar->get(hotbar->visible_hotbars[i], j);
      // draw background first
		  display_fillbox_wh_clip_rgb(draw_pos.x, draw_pos.y, env_t::iconsize.w, env_t::iconsize.h, color_idx_to_rgb(MN_GREY2), false);
      if (tool) {
        const image_id icon_img = tool->get_icon(player);
		    if (icon_img == IMG_EMPTY) {
          // TODO: draw default icon 
		      display_fillbox_wh_clip_rgb(draw_pos.x, draw_pos.y, env_t::iconsize.w, env_t::iconsize.h, color_idx_to_rgb(MN_GREY4), false);
        } else {
			    display_color_img(icon_img, draw_pos.x, draw_pos.y, player->get_player_nr(), false, true);
        }
      } else {
        /* Empty */
      }
      /* show hotkey for the slot */
		  display_fillbox_wh_clip_rgb(draw_pos.x, draw_pos.y, 0.25 * env_t::iconsize.w, 0.33333 * env_t::iconsize.h, color_idx_to_rgb(MN_GREY4), false);
		  display_proportional_clip_rgb(draw_pos.x, draw_pos.y, &labels_for_slots[2*j], ALIGN_LEFT, color_idx_to_rgb(COL_BLACK), false);
		}
	}
	// display_pop_clip_wh(CLIP_NUM_VAR);
	// tooltips?
	const int16_t mx = get_mouse_x();
	const int16_t my = get_mouse_y();
  if (is_hit(mx, my)) {
    tool_t *tool;
    int i, j;
    bool hover_slot = get_hotbar_slot(mx - pos.x, my - pos.y, i, j);
    std::string tooltip_text;
    cbuffer_t buf;
    if (hover_slot) {
      tool = hotbar->get_visible_slot(i, j);
      if (tool) {
        tooltip_text = tool->get_tooltip(world->get_active_player());
      } else {
        if (hotbar->selected_hotbar == hotbar->visible_hotbars[i]) {
          key_t key = {hotbar->slot_keys[j], hotbar->set_key_mod};
          buf.printf(translator::translate("Left-click or press [%s] to assign this slot to the current tool. Right-click to empty the slot."), key_to_str(key).c_str());
        } else {
          buf.printf(translator::translate("Left-click to assign this slot to the current tool. Right-click to empty the slot."));
        }
        tooltip_text = buf.get_str();
      }
    } else {
      key_t key = {hotbar->hotbar_keys[hotbar->visible_hotbars[i]], hotbar->select_key_mod};
      buf.printf(translator::translate("Left-click or press [%s] to select this hotbar."), key_to_str(key).c_str());
      tooltip_text = buf.get_str();
    }
    delete tooltip;
    tooltip = new std::string(tooltip_text);
		win_set_tooltip(get_mouse_x() + TOOLTIP_MOUSE_OFFSET_X, get_mouse_y() + TOOLTIP_MOUSE_OFFSET_Y, tooltip->c_str(), nullptr, this);
	}
	//as we do not call gui_frame_t::draw, we reset dirty flag explicitly
	// unset_dirty();
}
