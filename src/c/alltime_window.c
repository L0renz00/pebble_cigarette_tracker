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
static TextLayer *s_total_unit;      // "cigarettes" below the number (conditional)
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

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP, up_click_handler);
}

// --- Divider layer -----------------------------------------------------------

static void divider_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  int row_h = bounds.size.h / 3;

  // Inverted accent: fill row 0 with black
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, 0, bounds.size.w, row_h), 0, GCornerNone);

  // Single divider between rows 1 and 2 only
  graphics_context_set_stroke_color(ctx, GColorBlack);
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
  // Three equal rows. Fonts chosen by PreferredContentSize so that both
  // Basalt and Emery get appropriately-scaled text.

  int row_h = bounds.size.h / 3;
  int pad   = 8;
  int w     = bounds.size.w - 2 * pad;

  // ContentSize-adaptive font selection
  PreferredContentSize cs = preferred_content_size();

  GFont label_font, value_font;
  int   label_px, value_px;
  const int unit_px = 14;

  switch (cs) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      label_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      value_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      label_px = 24; value_px = 34;
      break;
    default: // Small, Medium
      label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      value_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      label_px = 18; value_px = 28;
      break;
  }

  GFont unit_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);

  // Show "cigarettes" unit label only if there is vertical space for it
  bool show_unit  = (label_px + value_px + unit_px) <= row_h;

  // Row 0: center label+value+(optional unit) block; clamp to 0 if row is too short
  int r0_content  = label_px + value_px + (show_unit ? unit_px : 0);
  int r0_top      = (row_h > r0_content) ? (row_h - r0_content) / 2 : 0;

  // Rows 1 & 2: center label+value block
  int r12_content = label_px + value_px;
  int r12_pad     = (row_h - r12_content) / 2;
  int r1_top      = row_h + r12_pad;
  int r2_top      = 2 * row_h + r12_pad;

  // Row 0 uses inverted colors (black bg, yellow text)
  // Rows 1 & 2 are on yellow bg with black text
  GColor row0_text  = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  GColor row12_text = GColorBlack;

  // --- Divider layer (added first so black fill is behind row 0 text) ---

  s_divider_layer = layer_create(bounds);
  layer_set_update_proc(s_divider_layer, divider_update_proc);
  layer_add_child(window_layer, s_divider_layer);

  // --- Row 0: all-time total ---

  s_total_label = text_layer_create(GRect(pad, r0_top, w, label_px));
  text_layer_set_text(s_total_label, "All-time");
  text_layer_set_font(s_total_label, label_font);
  text_layer_set_text_color(s_total_label, row0_text);
  text_layer_set_text_alignment(s_total_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_total_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_total_label));

  s_total_value = text_layer_create(GRect(pad, r0_top + label_px, w, value_px));
  text_layer_set_text(s_total_value, s_total_buf);
  text_layer_set_font(s_total_value, value_font);
  text_layer_set_text_color(s_total_value, row0_text);
  text_layer_set_text_alignment(s_total_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_total_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_total_value));

  if (show_unit) {
    s_total_unit = text_layer_create(
        GRect(pad, r0_top + label_px + value_px, w, unit_px));
    text_layer_set_text(s_total_unit, "cigarettes");
    text_layer_set_font(s_total_unit, unit_font);
    text_layer_set_text_color(s_total_unit, row0_text);
    text_layer_set_text_alignment(s_total_unit, GTextAlignmentCenter);
    text_layer_set_background_color(s_total_unit, GColorClear);
    layer_add_child(window_layer, text_layer_get_layer(s_total_unit));
  }

  // --- Row 1: average this week ---

  s_week_avg_label = text_layer_create(GRect(pad, r1_top, w, label_px));
  text_layer_set_text(s_week_avg_label, "Avg this week");
  text_layer_set_font(s_week_avg_label, label_font);
  text_layer_set_text_color(s_week_avg_label, row12_text);
  text_layer_set_text_alignment(s_week_avg_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_week_avg_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_week_avg_label));

  s_week_avg_value = text_layer_create(GRect(pad, r1_top + label_px, w, value_px));
  text_layer_set_text(s_week_avg_value, s_week_avg_buf);
  text_layer_set_font(s_week_avg_value, value_font);
  text_layer_set_text_alignment(s_week_avg_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_week_avg_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_week_avg_value));

  // --- Row 2: all-time daily average ---

  s_alltime_avg_label = text_layer_create(GRect(pad, r2_top, w, label_px));
  text_layer_set_text(s_alltime_avg_label, "Avg all-time");
  text_layer_set_font(s_alltime_avg_label, label_font);
  text_layer_set_text_color(s_alltime_avg_label, row12_text);
  text_layer_set_text_alignment(s_alltime_avg_label, GTextAlignmentCenter);
  text_layer_set_background_color(s_alltime_avg_label, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_alltime_avg_label));

  s_alltime_avg_value = text_layer_create(GRect(pad, r2_top + label_px, w, value_px));
  text_layer_set_text(s_alltime_avg_value, s_alltime_avg_buf);
  text_layer_set_font(s_alltime_avg_value, value_font);
  text_layer_set_text_alignment(s_alltime_avg_value, GTextAlignmentCenter);
  text_layer_set_background_color(s_alltime_avg_value, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_alltime_avg_value));

}

static void alltime_window_unload(Window *window) {
  layer_destroy(s_divider_layer);
  text_layer_destroy(s_total_label);
  text_layer_destroy(s_total_value);
  text_layer_destroy(s_total_unit);  // safe: SDK treats NULL as no-op
  text_layer_destroy(s_week_avg_label);
  text_layer_destroy(s_week_avg_value);
  text_layer_destroy(s_alltime_avg_label);
  text_layer_destroy(s_alltime_avg_value);
  window_destroy(s_alltime_window);
  s_alltime_window = NULL;
}

// --- Public API --------------------------------------------------------------

void alltime_window_push(void) {
  s_total_unit = NULL;  // reset before load so unload's destroy is a safe no-op
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
