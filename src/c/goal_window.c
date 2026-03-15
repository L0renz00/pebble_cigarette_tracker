#include <pebble.h>
#include "goal_window.h"
#include "selection_layer.h"
#include "storage.h"
#include "ui_util.h"

// Two digit cells: tens (0–6) and units (0–9).
// Combined value tens*10+units is clamped to [0, 60].
#define GOAL_NUM_CELLS  2
// Cell width + padding chosen to match pin_window.c (40px / 8px).
// selection_layer.c only knows the font metrics for GOTHIC_28_BOLD and
// GOTHIC_24_BOLD; using those fonts guarantees correct vertical centering.
#define GOAL_CELL_W     40
#define GOAL_CELL_PAD    8

static Window    *s_goal_window;
static Layer     *s_selection_layer;
static TextLayer *s_title_layer;
static Layer     *s_title_bg_layer;
static TextLayer *s_prompt_layer;

static int  s_digits[GOAL_NUM_CELLS]; // [0]=tens, [1]=units
static char s_field_bufs[GOAL_NUM_CELLS][4];

// --- SelectionLayer callbacks ------------------------------------------------

static char *get_cell_text(int index, void *context) {
  snprintf(s_field_bufs[index], sizeof(s_field_bufs[0]),
           "%d", s_digits[index]);
  return s_field_bufs[index];
}

static void on_complete(void *context) {
  int goal = s_digits[0] * 10 + s_digits[1];
  storage_set_goal(goal);
  window_stack_pop(true);
}

static void on_increment(int index, uint8_t clicks, void *context) {
  if (index == 0) {
    s_digits[0] = (s_digits[0] < 6) ? s_digits[0] + 1 : 0;
    if (s_digits[0] == 6 && s_digits[1] > 0) s_digits[1] = 0;
  } else {
    int max_units = (s_digits[0] == 6) ? 0 : 9;
    s_digits[1] = (s_digits[1] < max_units) ? s_digits[1] + 1 : 0;
  }
}

static void on_decrement(int index, uint8_t clicks, void *context) {
  if (index == 0) {
    s_digits[0] = (s_digits[0] > 0) ? s_digits[0] - 1 : 6;
    if (s_digits[0] == 6 && s_digits[1] > 0) s_digits[1] = 0;
  } else {
    int max_units = (s_digits[0] == 6) ? 0 : 9;
    s_digits[1] = (s_digits[1] > 0) ? s_digits[1] - 1 : max_units;
  }
}

// --- Window lifecycle --------------------------------------------------------

static void goal_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int title_h  = bounds.size.h / 7;
  int prompt_h = bounds.size.h / 6;

  // Title background (inverted accent bar)
  s_title_bg_layer = layer_create(GRect(0, 0, bounds.size.w, title_h));
  layer_set_update_proc(s_title_bg_layer, ui_title_bar_update_proc);
  layer_add_child(window_layer, s_title_bg_layer);

  // Title
  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "Daily Goal");
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text_color(s_title_layer,
      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  // Prompt
  s_prompt_layer = text_layer_create(
      GRect(0, title_h, bounds.size.w, prompt_h));
  text_layer_set_text(s_prompt_layer, "Pick a daily goal");
  text_layer_set_text_alignment(s_prompt_layer, GTextAlignmentCenter);
  text_layer_set_font(s_prompt_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_prompt_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_prompt_layer));

  // SelectionLayer — uses GOTHIC_28_BOLD so selection_layer.c can center it.
  // sel_h=38 gives y_offset = (38/2)-(18/2)-10 = 0 → text flush with cell top,
  // which vertically centers GOTHIC_28_BOLD glyphs within the 38px cell.
  int sel_h = 38;
  int sel_w = GOAL_NUM_CELLS * GOAL_CELL_W + (GOAL_NUM_CELLS - 1) * GOAL_CELL_PAD;
  int sel_x = (bounds.size.w - sel_w) / 2;
  int top_used = title_h + prompt_h; // title bar + prompt
  int sel_y = top_used + (bounds.size.h - top_used - sel_h) / 2;

  s_selection_layer = selection_layer_create(
      GRect(sel_x, sel_y, sel_w, sel_h), GOAL_NUM_CELLS);

  for (int i = 0; i < GOAL_NUM_CELLS; i++) {
    selection_layer_set_cell_width(s_selection_layer, i, GOAL_CELL_W);
  }
  selection_layer_set_cell_padding(s_selection_layer, GOAL_CELL_PAD);
  // Keep the default GOTHIC_28_BOLD — changing it breaks prv_get_y_offset_*.
  selection_layer_set_active_bg_color(s_selection_layer,
      PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack));
  selection_layer_set_inactive_bg_color(s_selection_layer,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  selection_layer_set_click_config_onto_window(s_selection_layer, window);
  selection_layer_set_callbacks(s_selection_layer, NULL,
      (SelectionLayerCallbacks){
        .get_cell_text = get_cell_text,
        .complete      = on_complete,
        .increment     = on_increment,
        .decrement     = on_decrement,
      });

  layer_add_child(window_layer, s_selection_layer);
}

static void goal_window_unload(Window *window) {
  selection_layer_destroy(s_selection_layer);
  text_layer_destroy(s_prompt_layer);
  layer_destroy(s_title_bg_layer);
  text_layer_destroy(s_title_layer);
  window_destroy(s_goal_window);
  s_goal_window = NULL;
}

// --- Public API --------------------------------------------------------------

void goal_window_push(int current_goal) {
  if (current_goal < 0 || current_goal > 60) current_goal = 0;
  s_digits[0] = current_goal / 10;
  s_digits[1] = current_goal % 10;

  s_goal_window = window_create();
  window_set_background_color(s_goal_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_window_handlers(s_goal_window, (WindowHandlers){
    .load   = goal_window_load,
    .unload = goal_window_unload,
  });
  window_stack_push(s_goal_window, true);
}
