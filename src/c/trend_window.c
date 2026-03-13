#include <pebble.h>
#include "trend_window.h"
#include "hourly_window.h"
#include "history_window.h"
#include "storage.h"

// ============================================================
// TrendLayer — area chart for the current week.
//
// Additions over the original:
//   • Grow-from-baseline animation (EaseOut, 450 ms) on window load.
//   • Min/max value labels: the count is drawn just above the peak
//     dot and just below the trough dot so you can read the range
//     at a glance without extra chrome.
// ============================================================

typedef Layer TrendLayer;

#define TREND_PATH_MAX   (HISTORY_DAYS + 2)
#define ANIM_DURATION_MS 450

typedef struct {
  int32_t    counts[HISTORY_DAYS];
  bool       populated[HISTORY_DAYS];
  int        num_populated;
  int        today_index;       // 0=Mon..6=Sun, -1 if unknown
  int16_t    anim_progress;     // 0–100; 100 = fully drawn
  Animation *animation;
} TrendLayerData;

// --- Drawing helpers ---------------------------------------------------------

static void draw_dot(GContext *ctx, int cx, int cy, bool is_today,
                     GColor bg_color) {
  int outer_r = is_today ? 4 : 3;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, cy), outer_r);
  if (is_today) {
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
  //
  // info_h: fixed strip at the very top that holds the H/L labels and the
  // today-count anchor. The plot sits entirely below this strip so no label
  // ever collides with a data point.

  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont info_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int   label_h    = 14;
  int   info_h     = 18;
  int   slot_w     = bounds.size.w / HISTORY_DAYS;

  int plot_top    = info_h;
  int plot_bottom = bounds.size.h - label_h - 4;
  int plot_h      = plot_bottom - plot_top;
  if (plot_h < 1) plot_h = 1;

  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  int    p  = data->anim_progress;   // 0–100

  // ---- Scaling --------------------------------------------------------------

  int max_count = 1, min_count = INT32_MAX;
  int max_idx   = -1, min_idx  = -1;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) continue;
    int c = (int)data->counts[i];
    if (c > max_count || max_idx < 0) { max_count = c; max_idx = i; }
    if (c < min_count || min_idx < 0) { min_count = c; min_idx = i; }
  }
  // Edge case: only one populated slot — min == max, don't double-annotate.
  bool single_slot = (max_idx == min_idx);

#define SLOT_CX(i)       ((i) * slot_w + slot_w / 2)
#define COUNT_Y_FULL(c)  (plot_bottom - ((int)(c) * plot_h / max_count))
#define COUNT_Y(c)       (plot_bottom - ((plot_bottom - COUNT_Y_FULL(c)) * p / 100))

  // ---- 1. Filled area path --------------------------------------------------

  GPoint closed[TREND_PATH_MAX];
  int npts = 0, first_x = -1, last_x = -1;

  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) continue;
    int cx = SLOT_CX(i);
    int y  = COUNT_Y(data->counts[i]);
    if (first_x < 0) first_x = cx;
    last_x = cx;
    closed[npts + 1] = GPoint(cx, y);
    npts++;
  }

  if (npts >= 1) {
    closed[0]        = GPoint(first_x, plot_bottom);
    closed[npts + 1] = GPoint(last_x,  plot_bottom);
    GPathInfo pi = { .num_points = npts + 2, .points = closed };
    GPath *area = gpath_create(&pi);
    graphics_context_set_fill_color(ctx,
        PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack));
    gpath_draw_filled(ctx, area);
    gpath_destroy(area);
  }

  // ---- 2. Dotted average line -----------------------------------------------

  {
    int32_t total = 0;
    for (int i = 0; i < HISTORY_DAYS; i++)
      if (data->populated[i]) total += data->counts[i];
    int avg_y_full = plot_bottom -
        (int)(total * plot_h / (data->num_populated * max_count));
    int avg_y = plot_bottom - ((plot_bottom - avg_y_full) * p / 100);

    graphics_context_set_stroke_color(ctx,
        PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    for (int x = 0; x < bounds.size.w; x += 6) {
      int x2 = x + 2;
      if (x2 >= bounds.size.w) x2 = bounds.size.w - 1;
      graphics_draw_line(ctx, GPoint(x, avg_y), GPoint(x2, avg_y));
    }
  }

  // ---- 3. Connecting line ---------------------------------------------------

  graphics_context_set_stroke_color(ctx, GColorBlack);
  int prev_x = -1, prev_y = -1;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) { prev_x = -1; prev_y = -1; continue; }
    int cx = SLOT_CX(i);
    int y  = COUNT_Y(data->counts[i]);
    if (prev_x >= 0) graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(cx, y));
    prev_x = cx; prev_y = y;
  }

  // ---- 4. Dots --------------------------------------------------------------

  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (!data->populated[i]) continue;
    int cx = SLOT_CX(i);
    int y  = COUNT_Y(data->counts[i]);
    draw_dot(ctx, cx, y, (i == data->today_index), bg);
  }

  // ---- 5. Info strip (top, above plot) -------------------------------------
  //
  // Left   "H: X"  highest day    (GOTHIC_14_BOLD, black)
  // Centre "L: X"  lowest day     (GOTHIC_14_BOLD, black) — omitted if 1 slot
  // Right  today count            (GOTHIC_18_BOLD, BlueMoon)

  {
    int stat_w = bounds.size.w / 3;

    graphics_context_set_text_color(ctx, GColorBlack);

    if (max_idx >= 0) {
      char max_buf[8];
      snprintf(max_buf, sizeof(max_buf), "H: %d", max_count);
      graphics_draw_text(ctx, max_buf, info_font,
                         GRect(0, 0, stat_w, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }

    if (min_idx >= 0 && !single_slot) {
      char min_buf[8];
      snprintf(min_buf, sizeof(min_buf), "L: %d", min_count);
      graphics_draw_text(ctx, min_buf, info_font,
                         GRect(stat_w, 0, stat_w, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }

    if (data->today_index >= 0 &&
        data->today_index < HISTORY_DAYS &&
        data->populated[data->today_index]) {
      char buf[8];
      snprintf(buf, sizeof(buf), "%d", (int)data->counts[data->today_index]);
      graphics_context_set_text_color(ctx,
          PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack));
      graphics_draw_text(ctx, buf,
                         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         GRect(bounds.size.w - 30, 0, 30, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentRight, NULL);
    }
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
#undef COUNT_Y_FULL
#undef COUNT_Y
}

// --- Animation ---------------------------------------------------------------

static void anim_update(Animation *anim, AnimationProgress progress) {
  Layer *layer = (Layer *)animation_get_context(anim);
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  data->anim_progress = (int16_t)(progress * 100 / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(layer);
}

static void anim_stopped(Animation *anim, bool finished, void *context) {
  Layer *layer = (Layer *)context;
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  data->anim_progress = 100;
  layer_mark_dirty(layer);
  data->animation = NULL;
}

static void trend_layer_animate_in(TrendLayer *layer) {
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
  }
  data->anim_progress = 0;
  layer_mark_dirty(layer);

  static const AnimationImplementation s_impl = { .update = anim_update };

  Animation *anim = animation_create();
  animation_set_duration(anim, ANIM_DURATION_MS);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_impl);
  animation_set_handlers(anim, (AnimationHandlers){ .stopped = anim_stopped }, layer);
  data->animation = anim;
  animation_schedule(anim);
}

// --- TrendLayer public API ---------------------------------------------------

static TrendLayer *trend_layer_create(GRect frame) {
  TrendLayer *layer = layer_create_with_data(frame, sizeof(TrendLayerData));
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(TrendLayerData));
  data->today_index   = -1;
  data->anim_progress = 100;
  layer_set_update_proc(layer, trend_layer_update_proc);
  return layer;
}

static void trend_layer_destroy(TrendLayer *layer) {
  if (!layer) return;
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
  }
  layer_destroy(layer);
}

static void trend_layer_set_data(TrendLayer *layer,
                                  DayEntry *entries, int num_entries,
                                  time_t week_start) {
  TrendLayerData *data = (TrendLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(TrendLayerData));
  data->today_index   = -1;
  data->anim_progress = 100;

  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  now_tm->tm_hour = 0; now_tm->tm_min = 0; now_tm->tm_sec = 0;
  time_t today_start = mktime(now_tm);
  int today_slot = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_slot >= 0 && today_slot < HISTORY_DAYS)
    data->today_index = today_slot;

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

static Window     *s_trend_window;
static TextLayer  *s_title_layer;
static Layer      *s_title_rule_layer;
static TrendLayer *s_trend_layer;

static char s_title_buf[24];

static void title_rule_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w - 1, 0));
}

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

static void trend_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

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
  trend_layer_animate_in(s_trend_layer);
}

static void trend_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_rule_layer);
  trend_layer_destroy(s_trend_layer);
  window_destroy(s_trend_window);
  s_trend_window = NULL;
}

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
