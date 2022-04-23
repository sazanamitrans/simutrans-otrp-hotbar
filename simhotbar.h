#ifndef SIMHOTBAR_H
#define SIMHOTBAR_H

#include "simevent.h"
#include "simtool.h"
#include "dataobj/clonable.h"

typedef int_fast8_t hotbar_size_t;
const hotbar_size_t MAX_HOTBARS = 0x7f;
/* max number of slots per hotbar */
const hotbar_size_t MAX_HOTBAR_SLOTS = 32;
class hotbar_t {
  private:
    inline static tool_t *maybe_clone(tool_t* source) {
      clonable *clonable_source = dynamic_cast<clonable*>(source);
      tool_t *dest = nullptr;
      if (clonable_source) {
        dest = dynamic_cast<tool_t*>(clonable_source->clone());
      }
      if (dest) {
        return dest;
      }
      return source;
    }
  public:
    static hotbar_t *hotbar; /* singleton instance */
    key_t toggle_key = {0, 0};
    bool visible = true;
    tool_t *slots[MAX_HOTBARS * MAX_HOTBAR_SLOTS];
    char hotbar_keys[MAX_HOTBARS] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'};
    char slot_keys[MAX_HOTBAR_SLOTS] = {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0'}; 
    uint32_t get_key_mod = 0;
    uint32_t set_key_mod = 1 << MODIFIER_CTRL;
    uint32_t select_key_mod = 1 << MODIFIER_ALT;
    hotbar_size_t visible_hotbars[MAX_HOTBAR_SLOTS] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    hotbar_size_t n_visible_hotbars = 4;
    hotbar_size_t n_slots = 10;
    hotbar_size_t n_hotbars_per_row = 2;
    hotbar_size_t selected_hotbar = 0;
    bool auto_select_tool = 0;
    bool vertical = 0;
    inline hotbar_size_t key_to_hotbar(char key) {
      for (hotbar_size_t i = 0; i < MAX_HOTBARS; i++) {
        if (hotbar_keys[i] == key) {
          return i;
        }
      }
      return -1;
    };
    inline hotbar_size_t key_to_slot(char key) {
      for (hotbar_size_t i = 0; i < MAX_HOTBAR_SLOTS; i++) {
        if (slot_keys[i] == key) {
          return i;
        }
      }
      return -1;
    };
    inline tool_t* get(hotbar_size_t slot_index) {
            return slots[selected_hotbar * MAX_HOTBAR_SLOTS + slot_index];
    };
    inline tool_t* get(hotbar_size_t hotbar_index, hotbar_size_t slot_index) {
            return slots[hotbar_index * MAX_HOTBAR_SLOTS + slot_index]; 
    };
    inline tool_t* get_visible_slot(hotbar_size_t hotbar_index, hotbar_size_t slot_index) {
            return slots[visible_hotbars[hotbar_index] * MAX_HOTBAR_SLOTS + slot_index]; 
    };
    inline void set(hotbar_size_t slot_index, tool_t* value) {
            value = maybe_clone(value);
            slots[selected_hotbar * MAX_HOTBAR_SLOTS + slot_index] = value;
    };
    inline void set(hotbar_size_t hotbar_index, hotbar_size_t slot_index, tool_t* value) {
            value = maybe_clone(value);
            slots[hotbar_index * MAX_HOTBAR_SLOTS + slot_index] = value;
    };
    inline void set_visible_slot(hotbar_size_t hotbar_index, hotbar_size_t slot_index, tool_t* value) {
            value = maybe_clone(value);
            slots[visible_hotbars[hotbar_index] * MAX_HOTBAR_SLOTS + slot_index] = value;
    };
    inline void select(hotbar_size_t hotbar_index) {
            hotbar_size_t old_hotbar_index = selected_hotbar, j = 0;
            selected_hotbar = hotbar_index;
            for (hotbar_size_t i = 0; i < n_visible_hotbars; i++) {
              if (visible_hotbars[i] == hotbar_index) {
                return;
              }
              if (visible_hotbars[i] == old_hotbar_index) {
                j = i;
              }
            }
            visible_hotbars[j] = hotbar_index;
    };
    /* rotate visible hotbars */
    inline void rotate() {
            hotbar_size_t head = visible_hotbars[0];
            for (hotbar_size_t i = 0; i < n_visible_hotbars; i++) {
                    visible_hotbars[i] = visible_hotbars[i + 1];
            }
            visible_hotbars[n_visible_hotbars - 1] = head;
    };
    void load_tab();
};
#endif
