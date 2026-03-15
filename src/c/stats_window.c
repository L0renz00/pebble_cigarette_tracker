#include <pebble.h>
#include "stats_window.h"
#include "storage.h"
#include "graph_layer.h"
#include "history_window.h"
#include "hourly_window.h"
#include "ui_util.h"

static Window     *s_stats_window;
static TextLayer  *s_title_layer;
static Layer      *s_title_bg_layer;
static GraphLayer *s_graph_layer;

static char s_title_buf[24];

// --- Click handlers ----------------------------------------------------------

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  history_window_push();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  hourly_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void stats_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  time_t week_start = storage_get_week_start();
  time_t week_end   = week_start + 6 * 24 * 60 * 60;
  char start_str[8], end_str[8];
  strftime(start_str, sizeof(start_str), "%d.%m", localtime(&week_start));
  strftime(end_str,   sizeof(end_str),   "%d.%m", localtime(&week_end));
  snprintf(s_title_buf, sizeof(s_title_buf), "%s - %s", start_str, end_str);

  int title_h = bounds.size.h / 7;

  GFont title_font;
  switch (preferred_content_size()) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      break;
    default:
      title_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      break;
  }

  s_title_bg_layer = layer_create(GRect(0, 0, bounds.size.w, title_h));
  layer_set_update_proc(s_title_bg_layer, ui_title_bar_update_proc);
  layer_add_child(window_layer, s_title_bg_layer);

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer, title_font);
  text_layer_set_text_color(s_title_layer,
      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  int graph_y = title_h;
  GRect graph_frame = GRect(4, graph_y,
                            bounds.size.w - 8,
                            bounds.size.h - graph_y - 2);
  s_graph_layer = graph_layer_create(graph_frame);

  DayEntry entries[HISTORY_DAYS];
  int num_entries;
  storage_get_history(entries, &num_entries);
  graph_layer_set_data(s_graph_layer, entries, num_entries);
  graph_layer_set_daily_goal(s_graph_layer, storage_get_goal());

  layer_add_child(window_layer, s_graph_layer);

  // Trigger the staggered bar-grow animation once the layer is in the tree.
  graph_layer_animate_in(s_graph_layer);
}

static void stats_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_bg_layer);
  graph_layer_destroy(s_graph_layer);
  window_destroy(s_stats_window);
  s_stats_window = NULL;
}

// --- Public API --------------------------------------------------------------

void stats_window_push(void) {
  s_stats_window = window_create();
  window_set_background_color(s_stats_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_stats_window, click_config_provider);
  window_set_window_handlers(s_stats_window, (WindowHandlers) {
    .load   = stats_window_load,
    .unload = stats_window_unload,
  });
  window_stack_push(s_stats_window, true);
}
