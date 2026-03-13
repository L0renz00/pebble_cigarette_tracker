#include <pebble.h>
#include "history_window.h"
#include "storage.h"

// ============================================================
// HistoryLayer — area chart of weekly daily averages (W9).
//
// Matches W6 (trend_window) visually: filled area (GColorOrange),
// connecting line, dotted overall-average line, ring-dot on the
// most recent entry.
//
// Extras:
//   • Grow-from-baseline animation (EaseOut, 450 ms) on window load.
//   • Min / max avg labels drawn above the peak and below the
//     trough data point respectively.
//
// Capped at HISTORY_DISPLAY_WEEKS (5) for label readability.
// ============================================================

#define HISTORY_DISPLAY_WEEKS  5
#define HISTORY_PATH_MAX       (HISTORY_DISPLAY_WEEKS + 2)
#define ANIM_DURATION_MS       450

typedef Layer HistoryLayer;

typedef struct {
  WeekEntry entries[HISTORY_DISPLAY_WEEKS];
  int       num_entries;
  int16_t   anim_progress;   // 0–100; 100 = fully drawn
  Animation *animation;
} HistoryLayerData;

// --- Drawing helpers ---------------------------------------------------------

static void draw_dot(GContext *ctx, int cx, int cy, bool is_latest, GColor bg) {
  int r = is_latest ? 4 : 3;
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, GPoint(cx, cy), r);
  if (is_latest) {
    graphics_context_set_fill_color(ctx, bg);
    graphics_fill_circle(ctx, GPoint(cx, cy), 2);
  }
}

// --- Update procedure --------------------------------------------------------

static void history_layer_update_proc(Layer *layer, GContext *ctx) {
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  if (data->num_entries == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "No history yet.\nCome back next\nweek!",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       bounds, GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  // ---- Averages in tenths (93 = 9.3 cigs/day) ------------------------------

  int avg_tenths[HISTORY_DISPLAY_WEEKS];
  int max_tenths = 1, min_tenths = INT32_MAX;
  int max_idx = 0, min_idx = 0;

  for (int i = 0; i < data->num_entries; i++) {
    int32_t da = data->entries[i].days_active;
    avg_tenths[i] = (da > 0)
        ? (int)((data->entries[i].total * 10) / da)
        : 0;
    if (avg_tenths[i] > max_tenths) { max_tenths = avg_tenths[i]; max_idx = i; }
    if (avg_tenths[i] < min_tenths) { min_tenths = avg_tenths[i]; min_idx = i; }
  }
  bool single_entry = (max_idx == min_idx) || (data->num_entries == 1);

  // ---- Layout ---------------------------------------------------------------

  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont info_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  int   label_h    = 14;
  int   info_h     = 18;
  int   slot_w     = bounds.size.w / data->num_entries;

  int plot_top    = info_h;
  int plot_bottom = bounds.size.h - label_h - 4;
  int plot_h      = plot_bottom - plot_top;
  if (plot_h < 1) plot_h = 1;

  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  int    p  = data->anim_progress;

#define SLOT_CX(i)        ((i) * slot_w + slot_w / 2)
#define AVG_Y_FULL(i)     (plot_bottom - (avg_tenths[i] * plot_h / max_tenths))
#define AVG_Y(i)          (plot_bottom - ((plot_bottom - AVG_Y_FULL(i)) * p / 100))

  // ---- 1. Filled area path --------------------------------------------------

  GPoint closed[HISTORY_PATH_MAX];
  for (int i = 0; i < data->num_entries; i++)
    closed[i + 1] = GPoint(SLOT_CX(i), AVG_Y(i));
  closed[0]                     = GPoint(SLOT_CX(0),                    plot_bottom);
  closed[data->num_entries + 1] = GPoint(SLOT_CX(data->num_entries - 1), plot_bottom);

  GPathInfo pi = { .num_points = data->num_entries + 2, .points = closed };
  GPath *area  = gpath_create(&pi);
  graphics_context_set_fill_color(ctx,
      PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack));
  gpath_draw_filled(ctx, area);
  gpath_destroy(area);

  // ---- 2. Dotted overall-average line ---------------------------------------

  {
    int32_t total_t = 0;
    for (int i = 0; i < data->num_entries; i++) total_t += avg_tenths[i];
    int avg_y_full = plot_bottom -
        (int)(total_t * plot_h / (data->num_entries * max_tenths));
    int avg_y = plot_bottom - ((plot_bottom - avg_y_full) * p / 100);

    graphics_context_set_stroke_color(ctx,
        PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack));
    for (int x = 0; x < bounds.size.w; x += 6) {
      int x2 = x + 2;
      if (x2 >= bounds.size.w) x2 = bounds.size.w - 1;
      graphics_draw_line(ctx, GPoint(x, avg_y), GPoint(x2, avg_y));
    }
  }

  // ---- 3. Connecting line ---------------------------------------------------

  graphics_context_set_stroke_color(ctx, GColorBlack);
  for (int i = 1; i < data->num_entries; i++) {
    graphics_draw_line(ctx,
        GPoint(SLOT_CX(i - 1), AVG_Y(i - 1)),
        GPoint(SLOT_CX(i),     AVG_Y(i)));
  }

  // ---- 4. Dots — ring on most recent ----------------------------------------

  int latest = data->num_entries - 1;
  for (int i = 0; i < data->num_entries; i++)
    draw_dot(ctx, SLOT_CX(i), AVG_Y(i), (i == latest), bg);

  // ---- 5. Info strip (top, above plot) -------------------------------------
  //
  // Left   "H: X.X"  highest weekly avg  (GOTHIC_14_BOLD, black)
  // Centre "L: X.X"  lowest weekly avg   (GOTHIC_14_BOLD, black) — omitted if 1 entry
  // Right  most recent avg               (GOTHIC_18_BOLD, Orange)

  {
    int stat_w = bounds.size.w / 3;

    graphics_context_set_text_color(ctx, GColorBlack);

    char max_buf[10];
    snprintf(max_buf, sizeof(max_buf), "H: %d.%d",
             avg_tenths[max_idx] / 10, avg_tenths[max_idx] % 10);
    graphics_draw_text(ctx, max_buf, info_font,
                       GRect(0, 0, stat_w, info_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);

    if (!single_entry) {
      char min_buf[10];
      snprintf(min_buf, sizeof(min_buf), "L: %d.%d",
               avg_tenths[min_idx] / 10, avg_tenths[min_idx] % 10);
      graphics_draw_text(ctx, min_buf, info_font,
                         GRect(stat_w, 0, stat_w, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }

    char anchor[16];
    snprintf(anchor, sizeof(anchor), "%d.%d/d",
             avg_tenths[latest] / 10, avg_tenths[latest] % 10);
    graphics_context_set_text_color(ctx,
        PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack));
    graphics_draw_text(ctx, anchor,
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       GRect(bounds.size.w - 44, 0, 44, info_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentRight, NULL);
  }

  // ---- 6. Week-start date labels along the bottom ---------------------------

  for (int i = 0; i < data->num_entries; i++) {
    char lbl[6];
    time_t ws = (time_t)data->entries[i].week_timestamp;
    strftime(lbl, sizeof(lbl), "%d.%m", localtime(&ws));
    graphics_context_set_text_color(ctx,
        PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    graphics_draw_text(ctx, lbl, label_font,
                       GRect(i * slot_w, bounds.size.h - label_h,
                             slot_w, label_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

#undef SLOT_CX
#undef AVG_Y_FULL
#undef AVG_Y
}

// --- Animation ---------------------------------------------------------------

static void anim_update(Animation *anim, AnimationProgress progress) {
  Layer *layer = (Layer *)animation_get_context(anim);
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  data->anim_progress = (int16_t)(progress * 100 / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(layer);
}

static void anim_stopped(Animation *anim, bool finished, void *context) {
  Layer *layer = (Layer *)context;
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  data->anim_progress = 100;
  layer_mark_dirty(layer);
  data->animation = NULL;
}

static void history_layer_animate_in(HistoryLayer *layer) {
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
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

// --- HistoryLayer public API -------------------------------------------------

static HistoryLayer *history_layer_create(GRect frame) {
  HistoryLayer *layer =
      layer_create_with_data(frame, sizeof(HistoryLayerData));
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(HistoryLayerData));
  data->anim_progress = 100;
  layer_set_update_proc(layer, history_layer_update_proc);
  return layer;
}

static void history_layer_destroy(HistoryLayer *layer) {
  if (!layer) return;
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
  }
  layer_destroy(layer);
}

static void history_layer_set_data(HistoryLayer *layer,
                                    WeekEntry *entries, int num_entries) {
  HistoryLayerData *data = (HistoryLayerData *)layer_get_data(layer);
  if (num_entries < 0) num_entries = 0;
  if (num_entries > HISTORY_DISPLAY_WEEKS) {
    entries    += (num_entries - HISTORY_DISPLAY_WEEKS);
    num_entries = HISTORY_DISPLAY_WEEKS;
  }
  memcpy(data->entries, entries, sizeof(WeekEntry) * num_entries);
  data->num_entries = num_entries;
  layer_mark_dirty(layer);
}

// ============================================================
// History window
// ============================================================

static Window       *s_history_window;
static TextLayer    *s_title_layer;
static Layer        *s_title_rule_layer;
static HistoryLayer *s_history_layer;

static void title_rule_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w - 1, 0));
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_DOWN,   back_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, back_click_handler);
}

static void history_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int title_h = bounds.size.h / 6;

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "Weekly Avg");
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_title_rule_layer = layer_create(
      GRect(8, title_h, bounds.size.w - 16, 1));
  layer_set_update_proc(s_title_rule_layer, title_rule_update_proc);
  layer_add_child(window_layer, s_title_rule_layer);

  int chart_y = title_h + 3;
  GRect chart_frame = GRect(4, chart_y,
                            bounds.size.w - 8,
                            bounds.size.h - chart_y - 2);
  s_history_layer = history_layer_create(chart_frame);

  WeekEntry weeks[WEEK_HISTORY_COUNT];
  int num_weeks;
  storage_get_week_history(weeks, &num_weeks);
  history_layer_set_data(s_history_layer, weeks, num_weeks);

  layer_add_child(window_layer, s_history_layer);
  history_layer_animate_in(s_history_layer);
}

static void history_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_rule_layer);
  history_layer_destroy(s_history_layer);
  window_destroy(s_history_window);
  s_history_window = NULL;
}

void history_window_push(void) {
  s_history_window = window_create();
  window_set_background_color(s_history_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_history_window, click_config_provider);
  window_set_window_handlers(s_history_window, (WindowHandlers) {
    .load   = history_window_load,
    .unload = history_window_unload,
  });
  window_stack_push(s_history_window, true);
}
