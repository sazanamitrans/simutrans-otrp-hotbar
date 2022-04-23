/*
 * This file is part of the Simutrans project under the Artistic License.
 * (see LICENSE.txt)
 */

#include "simevent.h"
#include "sys/simsys.h"
#include "tpl/slist_tpl.h"
#include "unicode.h"

#include <sstream>
#include <string>

// system-independent event handling routines

static int cx = -1; // coordinates of last mouse click event
static int cy = -1; // initialised to "nowhere"
static int control_shift_state = 0; // none pressed
static event_t meta_event(EVENT_NONE); // for storing meta-events like double-clicks and triple-clicks
static unsigned int last_meta_class = EVENT_NONE;
static slist_tpl<event_t *> queued_events;

int event_get_last_control_shift()
{
	// shift = 1
	// ctrl  = 2
	return control_shift_state & 0x03;
}


unsigned int last_meta_event_get_class()
{
	return last_meta_class;
}


/**
 * each drag event contains the origin of the first click.
 * if the window is being dragged, it is convenient to change this
 * so the origin keeps pointing to the window top bar.
 *  Mainly to prevent copied, double code.
 */
void change_drag_start(int x, int y)
{
	cx += x;
	cy += y;
}


static void fill_event(event_t* const ev)
{
	// variables for detecting double-clicks and triple-clicks
	const  uint32        interval = 400;
	static unsigned int  prev_ev_class = EVENT_NONE;
	static unsigned int  prev_ev_code = 0;
	static uint32        prev_ev_time = 0;
	static unsigned char repeat_count = 0; // number of consecutive sequences of click-release

	// for autorepeat buttons we track button state, press time and a repeat time

	static int  pressed_buttons = 0; // assume: at startup no button pressed (needed for some backends)
	static uint32 lb_time = 0;
	static uint32 repeat_time = 500;

	ev->ev_class = EVENT_NONE;

	ev->mx = sys_event.mx;
	ev->my = sys_event.my;
	ev->cx = cx;
	ev->cy = cy;

	// always put key mod code into event
	ev->ev_key_mod = sys_event.key_mod;
	control_shift_state = sys_event.key_mod;

	switch (sys_event.type) {
		case SIM_KEYBOARD:
			ev->ev_class = EVENT_KEYBOARD;
			ev->ev_code  = sys_event.code;
			break;

		case SIM_STRING:
			ev->ev_class = EVENT_STRING;
			ev->ev_ptr   = sys_event.ptr;
			break;

		case SIM_MOUSE_BUTTONS:
			// press only acknowledged when no buttons are pressed
			pressed_buttons = sys_event.mb;
			switch (sys_event.code) {
				case SIM_MOUSE_LEFTBUTTON:
					ev->ev_class = EVENT_CLICK;
					pressed_buttons |= MOUSE_LEFTBUTTON;
					ev->ev_code = MOUSE_LEFTBUTTON;
					ev->cx = cx = sys_event.mx;
					ev->cy = cy = sys_event.my;
					break;

				case SIM_MOUSE_RIGHTBUTTON:
					ev->ev_class = EVENT_CLICK;
					pressed_buttons |= MOUSE_RIGHTBUTTON;
					ev->ev_code = MOUSE_RIGHTBUTTON;
					ev->cx = cx = sys_event.mx;
					ev->cy = cy = sys_event.my;
					break;

				case SIM_MOUSE_MIDBUTTON:
					ev->ev_class = EVENT_CLICK;
					pressed_buttons |= MOUSE_MIDBUTTON;
					ev->ev_code = MOUSE_MIDBUTTON;
					ev->cx = cx = sys_event.mx;
					ev->cy = cy = sys_event.my;
					break;

				case SIM_MOUSE_WHEELUP:
					ev->ev_class = EVENT_CLICK;
					ev->ev_code = MOUSE_WHEELUP;
					ev->cx = cx = sys_event.mx;
					ev->cy = cy = sys_event.my;
					break;

				case SIM_MOUSE_WHEELDOWN:
					ev->ev_class = EVENT_CLICK;
					ev->ev_code = MOUSE_WHEELDOWN;
					ev->cx = cx = sys_event.mx;
					ev->cy = cy = sys_event.my;
					break;

				case SIM_MOUSE_LEFTUP:
					ev->ev_class = EVENT_RELEASE;
					ev->ev_code = MOUSE_LEFTBUTTON;
					pressed_buttons &= ~MOUSE_LEFTBUTTON;
					break;

				case SIM_MOUSE_RIGHTUP:
					ev->ev_class = EVENT_RELEASE;
					ev->ev_code = MOUSE_RIGHTBUTTON;
					pressed_buttons &= ~MOUSE_RIGHTBUTTON;
					break;

				case SIM_MOUSE_MIDUP:
					ev->ev_class = EVENT_RELEASE;
					ev->ev_code = MOUSE_MIDBUTTON;
					pressed_buttons &= ~MOUSE_MIDBUTTON;
					break;
			}
			break;

		case SIM_MOUSE_MOVE:
			if (sys_event.mb) { // drag
				ev->ev_class = EVENT_DRAG;
				ev->ev_code  = sys_event.mb;
			}
			else { // move
				ev->ev_class = EVENT_MOVE;
				ev->ev_code  = 0;
			}
			break;

		case SIM_SYSTEM:
			ev->ev_class = EVENT_SYSTEM;
			ev->ev_code  = sys_event.code;
			ev->size_x = sys_event.size_x;
			ev->size_y = sys_event.size_y;
			break;
	}

	// check for double-clicks and triple-clicks
	const uint32 curr_time = dr_time();
	if(  ev->ev_class==EVENT_CLICK  ) {
		if(  prev_ev_class==EVENT_RELEASE  &&  prev_ev_code==ev->ev_code  &&  curr_time-prev_ev_time<=interval  ) {
			// case : a mouse click which forms an unbroken sequence with the previous clicks and releases
			prev_ev_class = EVENT_CLICK;
			prev_ev_time = curr_time;
		}
		else {
			// case : initial click or broken click-release sequence -> prepare for the start of a new sequence
			prev_ev_class = EVENT_CLICK;
			prev_ev_code = ev->ev_code;
			prev_ev_time = curr_time;
			repeat_count = 0;
		}
	}
	else if(  ev->ev_class==EVENT_RELEASE  &&  prev_ev_class==EVENT_CLICK  &&  prev_ev_code==ev->ev_code  &&  curr_time-prev_ev_time<=interval  ) {
		// case : a mouse release which forms an unbroken sequence with the previous clicks and releases
		prev_ev_class = EVENT_RELEASE;
		prev_ev_time = curr_time;
		++repeat_count;

		// create meta-events where necessary
		if(  repeat_count==2  ) {
			// case : double-click
			meta_event = *ev;
			meta_event.ev_class = EVENT_DOUBLE_CLICK;
		}
		else if(  repeat_count==3  ) {
			// case : triple-click
			meta_event = *ev;
			meta_event.ev_class = EVENT_TRIPLE_CLICK;
			repeat_count = 0; // reset -> start over again
		}
	}
	else if(  ev->ev_class!=EVENT_NONE  &&  prev_ev_class!=EVENT_NONE  ) {
		// case : broken click-release sequence -> simply reset
		prev_ev_class = EVENT_NONE;
		prev_ev_code = 0;
		prev_ev_time = 0;
		repeat_count = 0;
	}

	if (IS_LEFTCLICK(ev)) {
		// remember button press
		lb_time = curr_time;
		repeat_time = 400;
	}
	else if (pressed_buttons == 0) {
		lb_time = 0;
	}
	else { // the else is to prevent race conditions
		/* this would transform non-left button presses always
		 * to repeat events. I need right button clicks.
		 * I have no idea how this can be done cleanly, currently just
		 * disabling the repeat feature for non-left buttons
		 */
		if (pressed_buttons == MOUSE_LEFTBUTTON) {
			if (curr_time - lb_time > repeat_time) {
				repeat_time = 100;
				lb_time = curr_time;
				ev->ev_class = EVENT_REPEAT;
				ev->ev_code = pressed_buttons;
			}
		}
	}

	ev->button_state = pressed_buttons;
}


void display_poll_event(event_t* const ev)
{
	if( !queued_events.empty() ) {
		// We have a queued (injected programatically) event, return it.
		event_t *elem = queued_events.remove_first();
		*ev = *elem;
		delete elem;
		return ;
	}
	// if there is any pending meta-event, consume it instead of fetching a new event from the system
	if(  meta_event.ev_class!=EVENT_NONE  ) {
		*ev = meta_event;
		last_meta_class = meta_event.ev_class;
		meta_event.ev_class = EVENT_NONE;
	}
	else {
		last_meta_class = EVENT_NONE;
		GetEventsNoWait();
		fill_event(ev);
		// prepare for next event
		sys_event.type = SIM_NOEVENT;
		sys_event.code = 0;
	}
}


void display_get_event(event_t* const ev)
{
	if(  !queued_events.empty()  ) {
		// We have a queued (injected programatically) event, return it.
		event_t *elem = queued_events.remove_first();
		*ev = *elem;
		delete elem;
		return ;
	}
	// if there is any pending meta-event, consume it instead of fetching a new event from the system
	if(  meta_event.ev_class!=EVENT_NONE  ) {
		*ev = meta_event;
		meta_event.ev_class = EVENT_NONE;
	}
	else {
		GetEvents();
		fill_event(ev);
		// prepare for next event
		sys_event.type = SIM_NOEVENT;
		sys_event.code = 0;
	}
}


void queue_event(event_t *events)
{
	queued_events.append(events);
}

std::string key_to_str(key_t key, bool only_modifier) {
  std::ostringstream ret;
  for (int i = 0; i < NUM_MODIFIER_KEYS; i++) {
    if (key.modifier & (1 << i)) {
      ret << modifier_key_name[i] << " ";
    }
  }
  if (only_modifier) {
    return ret.str();
  }
  if (SIM_KEY_F1 <= key.code && key.code < SIM_KEY_F1 + 15) {
			ret << "F" << (key.code + 1);
  } else {
    switch(key.code) {
      case SIM_KEY_BACKSPACE: ret << "BACKSPACE"; break;
      case SIM_KEY_TAB      : ret << "TAB"      ; break;
      case SIM_KEY_ENTER    : ret << "ENTER"    ; break;
      case SIM_KEY_ESCAPE   : ret << "ESCAPE"   ; break;
      case SIM_KEY_SPACE    : ret << "SPACE"    ; break;
      case SIM_KEY_DELETE   : ret << "DELETE"   ; break;
      case SIM_KEY_UP       : ret << "UP"       ; break;
      case SIM_KEY_DOWN     : ret << "DOWN"     ; break;
      case SIM_KEY_RIGHT    : ret << "RIGHT"    ; break;
      case SIM_KEY_LEFT     : ret << "LEFT"     ; break;
      case SIM_KEY_HOME     : ret << "HOME"     ; break;
      case SIM_KEY_END      : ret << "END"      ; break;
      case SIM_KEY_PGUP     : ret << "PGUP"     ; break;
      case SIM_KEY_PGDN     : ret << "PGDN"     ; break;
      // TODO: Unicode
      default: ret << (char) key.code;
    }
  }
  return ret.str();
}

key_t str_to_key(std::string str, bool only_modifier) {
  key_t key = {0, 0};
  /* split at space */
  size_t pos_start = 0;
  for (size_t pos = 0; pos < str.length(); pos++) {
    bool split = (str[pos] == ' ' || str[pos] == '\t' || str[pos] == ',' || pos == str.length() - 1);
    size_t pos_end = (pos == str.length() - 1) ? pos + 1 : pos;
    if (split && (str[pos] == ' ' || str[pos] == '\t' || only_modifier)) {
      std::string modifier_str = str.substr(pos_start, pos_end - pos_start);
      for (int i = 0; i < NUM_MODIFIER_KEYS; i++) {
        if (modifier_str == modifier_key_name[i]) {
          key.modifier |= (1 << i);
          break;
        }
      }
      pos_start = pos + 1;
    } else if (split) {
      std::string key_str = str.substr(pos_start, pos_end - pos_start);
      if (key_str == "ENTER") {
        key.code = SIM_KEY_ENTER;
      } else if (key_str == "BACKSPACE") {
        key.code = SIM_KEY_BACKSPACE;
      } else if (key_str == "ESCAPE") {
        key.code = SIM_KEY_ESCAPE;
      } else if (key_str == "SPACE") {
        key.code = SIM_KEY_SPACE;
      } else if (key_str == "TAB") {
        key.code = SIM_KEY_TAB;
      } else if (key_str == "DELETE") {
        key.code = SIM_KEY_DELETE;
      } else if (key_str == "UP") {
        key.code = SIM_KEY_UP;
      } else if (key_str == "DOWN") {
        key.code = SIM_KEY_DOWN;
      } else if (key_str == "RIGHT") {
        key.code = SIM_KEY_RIGHT;
      } else if (key_str == "LEFT") {
        key.code = SIM_KEY_LEFT;
      } else if (key_str == "HOME") {
        key.code = SIM_KEY_HOME;
      } else if (key_str == "END") {
        key.code = SIM_KEY_END;
      } else if (key_str == "PGUP") {
        key.code = SIM_KEY_PGUP;
      } else if (key_str == "PGDN") {
        key.code = SIM_KEY_PGDN;
      } else if (key_str == "COMMA") {
        key.code = ',';
      } else if (key_str == "F1") {
        key.code = SIM_KEY_F1;
      } else if (key_str == "F2") {
        key.code = SIM_KEY_F2;
      } else if (key_str == "F3") {
        key.code = SIM_KEY_F3;
      } else if (key_str == "F4") {
        key.code = SIM_KEY_F4;
      } else if (key_str == "F5") {
        key.code = SIM_KEY_F5;
      } else if (key_str == "F6") {
        key.code = SIM_KEY_F6;
      } else if (key_str == "F7") {
        key.code = SIM_KEY_F7;
      } else if (key_str == "F8") {
        key.code = SIM_KEY_F8;
      } else if (key_str == "F9") {
        key.code = SIM_KEY_F9;
      } else if (key_str == "F10") {
        key.code = SIM_KEY_F10;
      } else if (key_str == "F11") {
        key.code = SIM_KEY_F11;
      } else if (key_str == "F12") {
        key.code = SIM_KEY_F12;
      } else if (key_str == "F13") {
        key.code = SIM_KEY_F13;
      } else if (key_str == "F14") {
        key.code = SIM_KEY_F14;
      } else if (key_str == "F15") {
        key.code = SIM_KEY_F15;
      } else {
        // TODO: Unicode
        key.code = key_str[0];
      }
      break;
    }
  }
  dbg->message("simevent.cc str_to_key", "Key Parsed %s => %s", str.c_str(), key_to_str(key).c_str());
  return key;
}
