#include <pebble.h>
#include "hourly_window.h"
#include "alltime_window.h"
#include "storage.h"
#include "ui_util.h"

// ============================================================
// HourlyLayer — line chart of cigarettes per hour of day (0–23h).
//
// No fill — clean 2px line only. Single ring-dot at peak hour.
// Three fixed-position x-axis labels ("0", "12", "23") anchored
// absolutely so they always render clearly regardless of slot width.
// ============================================================

typedef Layer HourlyLayer;

typedef struct {
  uint8_t hist[24];
} HourlyLayerData;

// --- Drawing helpers ---------------------------------------------------------

static void draw_dot(GContext *ctx, int cx, int cy, GColor bg) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, cy), 4);
  graphics_context_set_fill_color(ctx, bg);
  graphics_fill_circle(ctx, GPoint(cx, cy), 2);
}

// --- Update procedure --------------------------------------------------------

static void hourly_layer_update_proc(Layer *layer, GContext *ctx) {
  HourlyLayerData *data = (HourlyLayerData *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  int total = 0;
  for (int h = 0; h < 24; h++) total += data->hist[h];

  if (total == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "No data yet.\nLog your first\ncigarette!",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       bounds, GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  // ---- Layout ---------------------------------------------------------------

  // 18px label row — tall enough for GOTHIC_14_BOLD on basalt.
  int label_h  = 18;
  int top_pad  = 8;
  int slot_w   = bounds.size.w / 24;

  int plot_top    = top_pad;
  int plot_bottom = bounds.size.h - label_h - 4;
  int plot_h      = plot_bottom - plot_top;
  if (plot_h < 1) plot_h = 1;

  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);

  // ---- Scaling --------------------------------------------------------------

  // Start at 0 so the first non-zero hour correctly becomes peak_h.
  // max_val reaches at least 1 here because the total == 0 guard above
  // has already ensured at least one cigarette is recorded this week.
  int max_val = 0;
  int peak_h  = 0;
  for (int h = 0; h < 24; h++) {
    if (data->hist[h] > max_val) { max_val = data->hist[h]; peak_h = h; }
  }

#define HOUR_CX(h)  ((h) * slot_w + slot_w / 2)
#define HIST_Y(v)   (plot_bottom - ((int)(v) * plot_h / max_val))

  // ---- 1. Line — 2px, no fill -----------------------------------------------

  graphics_context_set_stroke_color(ctx, GColorBlack);
  graphics_context_set_stroke_width(ctx, 2);
  for (int h = 1; h < 24; h++) {
    graphics_draw_line(ctx,
        GPoint(HOUR_CX(h - 1), HIST_Y(data->hist[h - 1])),
        GPoint(HOUR_CX(h),     HIST_Y(data->hist[h])));
  }
  graphics_context_set_stroke_width(ctx, 1);

  // ---- 2. Peak-hour ring dot ------------------------------------------------

  draw_dot(ctx, HOUR_CX(peak_h), HIST_Y(data->hist[peak_h]), bg);

  // ---- 3. Peak-hour label, top-right ----------------------------------------

  char peak_buf[6];
  snprintf(peak_buf, sizeof(peak_buf), "%dh", peak_h);
  graphics_context_set_text_color(ctx,
      PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack));
  graphics_draw_text(ctx, peak_buf,
                     fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                     GRect(bounds.size.w - 36, 0, 36, 20),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);

  // ---- 4. X-axis labels — fixed 24px boxes, position-anchored --------------
  //
  // On basalt slot_w = 144/24 = 6px — far too narrow for any font. We
  // anchor three labels by absolute screen position instead.

  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int   label_w    = 24;
  int   label_y    = bounds.size.h - label_h + 2;

  graphics_context_set_text_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));

  graphics_draw_text(ctx, "0", label_font,
                     GRect(0, label_y, label_w, label_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentLeft, NULL);

  graphics_draw_text(ctx, "12", label_font,
                     GRect(bounds.size.w / 2 - label_w / 2, label_y,
                           label_w, label_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentCenter, NULL);

  graphics_draw_text(ctx, "23", label_font,
                     GRect(bounds.size.w - label_w, label_y,
                           label_w, label_h),
                     GTextOverflowModeTrailingEllipsis,
                     GTextAlignmentRight, NULL);

#undef HOUR_CX
#undef HIST_Y
}

// --- HourlyLayer public API --------------------------------------------------

static HourlyLayer *hourly_layer_create(GRect frame) {
  HourlyLayer *layer =
      layer_create_with_data(frame, sizeof(HourlyLayerData));
  HourlyLayerData *data = (HourlyLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(HourlyLayerData));
  layer_set_update_proc(layer, hourly_layer_update_proc);
  return layer;
}

static void hourly_layer_destroy(HourlyLayer *layer) {
  if (layer) layer_destroy(layer);
}

static void hourly_layer_set_data(HourlyLayer *layer, uint8_t *hist_24) {
  HourlyLayerData *data = (HourlyLayerData *)layer_get_data(layer);
  memcpy(data->hist, hist_24, 24);
  layer_mark_dirty(layer);
}

// ============================================================
// Hourly window
// ============================================================

static Window      *s_hourly_window;
static TextLayer   *s_title_layer;
static Layer       *s_title_rule_layer;
static HourlyLayer *s_hourly_layer;

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  alltime_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

static void hourly_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int title_h = bounds.size.h / 6;

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "This Week");
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_title_rule_layer = layer_create(
      GRect(8, title_h, bounds.size.w - 16, 1));
  layer_set_update_proc(s_title_rule_layer, ui_rule_update_proc);
  layer_add_child(window_layer, s_title_rule_layer);

  int chart_y = title_h + 3;
  GRect chart_frame = GRect(4, chart_y,
                            bounds.size.w - 8,
                            bounds.size.h - chart_y - 2);
  s_hourly_layer = hourly_layer_create(chart_frame);

  uint8_t hist[24];
  storage_get_hour_histogram(hist);
  hourly_layer_set_data(s_hourly_layer, hist);

  layer_add_child(window_layer, s_hourly_layer);
}

static void hourly_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_rule_layer);
  hourly_layer_destroy(s_hourly_layer);
  window_destroy(s_hourly_window);
  s_hourly_window = NULL;
}

void hourly_window_push(void) {
  s_hourly_window = window_create();
  window_set_background_color(s_hourly_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_hourly_window, click_config_provider);
  window_set_window_handlers(s_hourly_window, (WindowHandlers) {
    .load   = hourly_window_load,
    .unload = hourly_window_unload,
  });
  window_stack_push(s_hourly_window, true);
}
