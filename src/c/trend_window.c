#include <pebble.h>
#include "trend_window.h"
#include "hourly_window.h"
#include "history_window.h"
#include "storage.h"

// ============================================================
// TrendLayer — custom layer, self-contained via layer_create_with_data
// ============================================================

typedef Layer TrendLayer;

// +2 for the two bottom-closing corners of the filled area path
#define TREND_PATH_MAX (HISTORY_DAYS + 2)

typedef struct {
  int32_t counts[HISTORY_DAYS];     // count per weekday slot; 0 if not visited
  bool    populated[HISTORY_DAYS];  // true if that slot has real data
  int     num_populated;
  int     today_index;              // 0=Mon..6=Sun, -1 if unknown
} TrendLayerData;

// --- Drawing helpers ---------------------------------------------------------

// Dot at (cx, cy): filled circle, optionally with a hollow centre for today.
static void draw_dot(GContext *ctx, int cx, int cy, bool is_today,
                     GColor bg_color) {
  int outer_r = is_today ? 4 : 3;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, cy), outer_r);
  if (is_today) {
    // Ring effect: punch a 2 px hole in the window background colour.
    graphics_context_set_fill_color(ctx, bg_color);
    graphics_fill_circle(ctx, GPoint(cx, cy), 2);
  }
}

// --- Update procedure --------------------------------------------------------

static void trend_layer_update_proc(Layer *layer, GContext *ctx) {
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  if (data->num_populated == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "No data yet.",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       bounds, GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  // ---- Layout ---------------------------------------------------------------

  GFont  label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  int    label_h    = 14;
  int    top_pad    = 8;   // breathing room above the tallest bar
  int    slot_w     = bounds.size.w / HISTORY_DAYS;

  int plot_top    = top_pad;
  int plot_bottom = bounds.size.h - label_h - 4;
  int plot_h      = plot_bottom - plot_top;
  if (plot_h < 1) plot_h = 1;

  // Background colour passed to draw_dot for the ring punch-out.
  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);

  // ---- Scaling --------------------------------------------------------------

  int max_count = 1;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (data->populated[i] && (int)data->counts[i] > max_count)
      max_count = (int)data->counts[i];
  }

  // Inline helpers — map a count to a y pixel and a slot to a centre x pixel.
#define SLOT_CX(i)   ((i) * slot_w + slot_w / 2)
#define COUNT_Y(c)   (plot_bottom - ((int)(c) * plot_h / max_count))

  // ---- 1. Filled area path --------------------------------------------------
  //
  // Trace populated points left-to-right, then close to the baseline.
  // Path layout: [bottom-left, data points..., bottom-right]

  GPoint closed[TREND_PATH_MAX];
  int    npts = 0;
  int    first_x = -1, last_x = -1;

  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) continue;
    int cx = SLOT_CX(i);
    int y  = COUNT_Y(data->counts[i]);
    if (first_x < 0) first_x = cx;
    last_x = cx;
    closed[npts + 1] = GPoint(cx, y);  // leave slot 0 for bottom-left corner
    npts++;
  }

  if (npts >= 1) {
    closed[0]        = GPoint(first_x, plot_bottom);   // bottom-left
    closed[npts + 1] = GPoint(last_x,  plot_bottom);   // bottom-right

    GPathInfo pi = { .num_points = npts + 2, .points = closed };
    GPath *area_path = gpath_create(&pi);
    graphics_context_set_fill_color(ctx,
        PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack));
    gpath_draw_filled(ctx, area_path);
    gpath_destroy(area_path);
  }

  // ---- 2. Dotted average line -----------------------------------------------

  if (data->num_populated > 0) {
    int32_t total = 0;
    for (int i = 0; i < HISTORY_DAYS; i++)
      if (data->populated[i]) total += data->counts[i];

    // Use full-precision multiply before dividing to keep one decimal of accuracy.
    int avg_y = plot_bottom - (int)(total * plot_h / (data->num_populated * max_count));

    graphics_context_set_stroke_color(ctx,
        PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    for (int x = 0; x < bounds.size.w; x += 6) {
      int x2 = x + 2;
      if (x2 >= bounds.size.w) x2 = bounds.size.w - 1;
      graphics_draw_line(ctx, GPoint(x, avg_y), GPoint(x2, avg_y));
    }
  }

  // ---- 3. Line connecting populated points ----------------------------------

  graphics_context_set_stroke_color(ctx, GColorBlack);
  int prev_x = -1, prev_y = -1;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) { prev_x = -1; prev_y = -1; continue; }
    int cx = SLOT_CX(i);
    int y  = COUNT_Y(data->counts[i]);
    if (prev_x >= 0)
      graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(cx, y));
    prev_x = cx;
    prev_y = y;
  }

  // ---- 4. Dots at each data point -------------------------------------------

  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) continue;
    int cx       = SLOT_CX(i);
    int y        = COUNT_Y(data->counts[i]);
    bool is_today = (i == data->today_index);
    draw_dot(ctx, cx, y, is_today, bg);
  }

  // ---- 5. Today's count, top-right ------------------------------------------

  if (data->today_index >= 0 &&
      data->today_index < HISTORY_DAYS &&
      data->populated[data->today_index]) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", (int)data->counts[data->today_index]);
    graphics_context_set_text_color(ctx,
        PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack));
    graphics_draw_text(ctx, buf,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(bounds.size.w - 30, 0, 30, 20),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentRight, NULL);
  }

  // ---- 6. Day labels along the bottom ---------------------------------------

  static const char * const day_labels[HISTORY_DAYS] = {
    "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"
  };
  graphics_context_set_text_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  for (int i = 0; i < HISTORY_DAYS; i++) {
    graphics_draw_text(ctx, day_labels[i], label_font,
                       GRect(i * slot_w, bounds.size.h - label_h, slot_w, label_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

#undef SLOT_CX
#undef COUNT_Y
}

// --- TrendLayer public API ---------------------------------------------------

static TrendLayer *trend_layer_create(GRect frame) {
  TrendLayer *layer = layer_create_with_data(frame, sizeof(TrendLayerData));
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(TrendLayerData));
  data->today_index = -1;
  layer_set_update_proc(layer, trend_layer_update_proc);
  return layer;
}

static void trend_layer_destroy(TrendLayer *layer) {
  if (layer) layer_destroy(layer);
}

// Populates the layer from a DayEntry array (from storage_get_history) plus
// the current week's Monday-midnight timestamp (from storage_get_week_start).
static void trend_layer_set_data(TrendLayer *layer,
                                 DayEntry *entries, int num_entries,
                                 time_t week_start) {
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(TrendLayerData));
  data->today_index = -1;

  // Map today to its slot index.
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  now_tm->tm_hour = 0; now_tm->tm_min = 0; now_tm->tm_sec = 0;
  time_t today_start = mktime(now_tm);
  int today_slot = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_slot >= 0 && today_slot < HISTORY_DAYS)
    data->today_index = today_slot;

  // Fill counts into their positional slots.
  for (int i = 0; i < num_entries && i < HISTORY_DAYS; i++) {
    int slot = (int)(((time_t)entries[i].day_timestamp - week_start)
                     / (24 * 60 * 60));
    if (slot < 0 || slot >= HISTORY_DAYS) continue;
    data->counts[slot]    = entries[i].count;
    data->populated[slot] = true;
    data->num_populated++;
  }

  layer_mark_dirty(layer);
}

// ============================================================
// Trend window
// ============================================================

static Window    *s_trend_window;
static TextLayer *s_title_layer;
static Layer     *s_title_rule_layer;
static TrendLayer *s_trend_layer;

static char s_title_buf[24];

// --- Title rule --------------------------------------------------------------

static void title_rule_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w - 1, 0));
}

// --- Click handlers ----------------------------------------------------------

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  history_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  hourly_window_push();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void trend_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  // Title: same date-range style as stats_window
  time_t week_start = storage_get_week_start();
  time_t week_end   = week_start + 6 * 24 * 60 * 60;
  char start_str[8], end_str[8];
  strftime(start_str, sizeof(start_str), "%d.%m", localtime(&week_start));
  strftime(end_str,   sizeof(end_str),   "%d.%m", localtime(&week_end));
  snprintf(s_title_buf, sizeof(s_title_buf), "%s - %s", start_str, end_str);

  int title_h = bounds.size.h / 6;

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_title_rule_layer = layer_create(GRect(8, title_h, bounds.size.w - 16, 1));
  layer_set_update_proc(s_title_rule_layer, title_rule_update_proc);
  layer_add_child(window_layer, s_title_rule_layer);

  int chart_y = title_h + 3;
  GRect chart_frame = GRect(4, chart_y,
                            bounds.size.w - 8,
                            bounds.size.h - chart_y - 2);
  s_trend_layer = trend_layer_create(chart_frame);

  DayEntry entries[HISTORY_DAYS];
  int num_entries;
  storage_get_history(entries, &num_entries);
  trend_layer_set_data(s_trend_layer, entries, num_entries, week_start);

  layer_add_child(window_layer, s_trend_layer);
}

static void trend_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_rule_layer);
  trend_layer_destroy(s_trend_layer);
  window_destroy(s_trend_window);
  s_trend_window = NULL;
}

// --- Public API --------------------------------------------------------------

void trend_window_push(void) {
  s_trend_window = window_create();
  window_set_background_color(s_trend_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_trend_window, click_config_provider);
  window_set_window_handlers(s_trend_window, (WindowHandlers) {
    .load   = trend_window_load,
    .unload = trend_window_unload,
  });
  window_stack_push(s_trend_window, true);
}