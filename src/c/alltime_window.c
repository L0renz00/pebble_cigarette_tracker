#include <pebble.h>
#include "alltime_window.h"
#include "history_window.h"
#include "storage.h"
#include "main.h"

static Window *s_alltime_window;

// Divider layer draws two horizontal rules between the three stat blocks.
static Layer *s_divider_layer;

static TextLayer *s_total_label;
static TextLayer *s_total_value;
static TextLayer *s_total_unit;      // "cigarettes" below the number
static TextLayer *s_week_avg_label;
static TextLayer *s_week_avg_value;
static TextLayer *s_alltime_avg_label;
static TextLayer *s_alltime_avg_value;

static char s_total_buf[16];
static char s_week_avg_buf[16];
static char s_alltime_avg_buf[16];

// --- Click handlers ----------------------------------------------------------

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  history_window_push();
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  Window *main = main_window_get();
  while (window_stack_get_top_window() != main) {
    window_stack_pop(false);
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

// --- Divider layer -----------------------------------------------------------

static void divider_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int row_h = bounds.size.h / 3;

  graphics_context_set_stroke_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));

  // Line between row 0 and row 1
  graphics_draw_line(ctx,
      GPoint(8, row_h),
      GPoint(bounds.size.w - 8, row_h));

  // Line between row 1 and row 2
  graphics_draw_line(ctx,
      GPoint(8, 2 * row_h),
      GPoint(bounds.size.w - 8, 2 * row_h));
}

// --- Helpers -----------------------------------------------------------------

// Formats a fixed-point value (numerator/denominator) to one decimal place.
// e.g. format_1dp(53, 7, buf, size) -> "7.5/day"
static void format_1dp(int32_t numerator, int32_t denominator,
                        char *buf, size_t size) {
  if (denominator <= 0) {
    snprintf(buf, size, "--");
    return;
  }
  // Clamp negative inputs — storage totals should never be negative, but
  // corrupted flash could produce them; negative C modulo is also a trap.
  if (numerator < 0) numerator = 0;
  int32_t tenths = (numerator * 10) / denominator;
  snprintf(buf, size, "%d.%d/day", (int)(tenths / 10), (int)(tenths % 10));
}

// --- Window lifecycle --------------------------------------------------------

static void alltime_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  // --- Compute stats ---

  int32_t total      = storage_get_total();
  int32_t total_days = storage_get_total_days();

  DayEntry entries[HISTORY_DAYS];
  int num_entries;
  storage_get_history(entries, &num_entries);
  int32_t week_sum = 0;
  for (int i = 0; i < num_entries; i++) week_sum += entries[i].count;

  snprintf(s_total_buf, sizeof(s_total_buf), "%d", (int)total);
  format_1dp(week_sum, (int32_t)num_entries, s_week_avg_buf, sizeof(s_week_avg_buf));
  format_1dp(total, total_days, s_alltime_avg_buf, sizeof(s_alltime_avg_buf));

  // --- Layout -----------------------------------------------------------------
  //
  // Three equal rows. Within each row:
  //   label_h  = row_h / 4   (GOTHIC_14_BOLD, muted)
  //   value_h  = row_h / 2   (GOTHIC_24_BOLD, black)
  //   unit_h   = row_h / 4   (GOTHIC_14, muted) — row 0 only
  //
  // On Pebble Time (168 px): row_h=56, label_h=14, value_h=28, unit_h=14.
  // On Pebble Time 2 (228 px): row_h=76, label_h=19, value_h=38, unit_h=19.

  int row_h   = bounds.size.h / 3;
  int label_h = row_h / 4;
  int value_h = row_h / 2;
  int unit_h  = row_h - label_h - value_h;  // remaining slice, same as label_h
  int pad     = 8;
  int w       = bounds.size.w - 2 * pad;

  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  GFont value_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
  GFont unit_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  GColor muted = PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack);

  // --- Row 0: all-time total ---

  s_total_label = text_layer_create(GRect(pad, 0, w, label_h));
  text_layer_set_text(s_total_label, "All-time");
  text_layer_set_font(s_total_label, label_font);
  text_layer_set_text_color(s_total_label, muted);
  text_layer_set_text_alignment(s_total_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_total_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_total_label));

  s_total_value = text_layer_create(GRect(pad, label_h, w, value_h));
  text_layer_set_text(s_total_value, s_total_buf);
  text_layer_set_font(s_total_value, value_font);
  text_layer_set_text_alignment(s_total_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_total_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_total_value));

  s_total_unit = text_layer_create(GRect(pad, label_h + value_h, w, unit_h));
  text_layer_set_text(s_total_unit, "cigarettes");
  text_layer_set_font(s_total_unit, unit_font);
  text_layer_set_text_color(s_total_unit, muted);
  text_layer_set_text_alignment(s_total_unit, GTextAlignmentCenter);
  text_layer_set_background_color(s_total_unit, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_total_unit));

  // --- Row 1: average this week ---

  s_week_avg_label = text_layer_create(GRect(pad, 1 * row_h, w, label_h));
  text_layer_set_text(s_week_avg_label, "Avg this week");
  text_layer_set_font(s_week_avg_label, label_font);
  text_layer_set_text_color(s_week_avg_label, muted);
  text_layer_set_text_alignment(s_week_avg_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_week_avg_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_week_avg_label));

  s_week_avg_value = text_layer_create(
      GRect(pad, 1 * row_h + label_h, w, value_h));
  text_layer_set_text(s_week_avg_value, s_week_avg_buf);
  text_layer_set_font(s_week_avg_value, value_font);
  text_layer_set_text_alignment(s_week_avg_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_week_avg_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_week_avg_value));

  // --- Row 2: all-time daily average ---

  s_alltime_avg_label = text_layer_create(GRect(pad, 2 * row_h, w, label_h));
  text_layer_set_text(s_alltime_avg_label, "Avg all-time");
  text_layer_set_font(s_alltime_avg_label, label_font);
  text_layer_set_text_color(s_alltime_avg_label, muted);
  text_layer_set_text_alignment(s_alltime_avg_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_alltime_avg_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_alltime_avg_label));

  s_alltime_avg_value = text_layer_create(
      GRect(pad, 2 * row_h + label_h, w, value_h));
  text_layer_set_text(s_alltime_avg_value, s_alltime_avg_buf);
  text_layer_set_font(s_alltime_avg_value, value_font);
  text_layer_set_text_alignment(s_alltime_avg_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_alltime_avg_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_alltime_avg_value));

  // --- Divider layer (drawn on top so lines aren't clipped by text layers) ---

  s_divider_layer = layer_create(bounds);
  layer_set_update_proc(s_divider_layer, divider_update_proc);
  layer_add_child(window_layer, s_divider_layer);
}

static void alltime_window_unload(Window *window) {
  layer_destroy(s_divider_layer);
  text_layer_destroy(s_total_label);
  text_layer_destroy(s_total_value);
  text_layer_destroy(s_total_unit);
  text_layer_destroy(s_week_avg_label);
  text_layer_destroy(s_week_avg_value);
  text_layer_destroy(s_alltime_avg_label);
  text_layer_destroy(s_alltime_avg_value);
  window_destroy(s_alltime_window);
  s_alltime_window = NULL;
}

// --- Public API --------------------------------------------------------------

void alltime_window_push(void) {
  s_alltime_window = window_create();
  window_set_background_color(s_alltime_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_alltime_window, click_config_provider);
  window_set_window_handlers(s_alltime_window, (WindowHandlers) {
    .load   = alltime_window_load,
    .unload = alltime_window_unload,
  });
  window_stack_push(s_alltime_window, true);
}
