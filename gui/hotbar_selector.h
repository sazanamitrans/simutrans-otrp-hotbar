/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#ifndef GUI_HOTBAR_SELECTOR_H
#define GUI_HOTBAR_SELECTOR_H


#include "gui_frame.h"
#include "../tpl/vector_tpl.h"
#include "simwin.h"
#include "../dataobj/environment.h"
#include "../simhotbar.h"

#include <string>

/**
 * This class defines all toolbar dialogues, floating bar of tools, i.e. the part the user will see
 */
class hotbar_selector_t : public gui_component_t
{
private:
  std::string *tooltip = nullptr;
  char labels_for_slots[2*MAX_HOTBAR_SLOTS] = {0};
  char labels_for_hotbars[2*MAX_HOTBARS] = {0};
  static karte_ptr_t world;
  bool visible;
public:
  hotbar_t *hotbar;
  int spacing_w = 2;
  int spacing_h = 2;
  int spacing_bars = 2;
  int bar_width();
  int bar_height();
  int height();
  int width();
  bool get_hotbar_slot(int x, int y, int &i, int &j);
  void get_hotbar_slot_pos(int i, int j, int &x, int &y);
  void on_click(int x, int y, event_t const *ev);
	scr_coord offset;
	hotbar_selector_t();
	bool is_hit(int x, int y);
	void draw(scr_coord offset) OVERRIDE;
	// since no information are needed to be saved to restore this, returning magic is enough
};

#endif
