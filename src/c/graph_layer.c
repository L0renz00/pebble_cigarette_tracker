#include "graph_layer.h"
#include <string.h>

// Total animation duration in milliseconds.
#define ANIM_DURATION_MS  450
// How many progress units (0..100) are reserved per bar for the stagger.
// With 7 bars the last bar (index 6) starts at 6*ANIM_STAGGER = 36,
// leaving 64 units to grow — still snappy at EaseOut.
#define ANIM_STAGGER      6

typedef struct {
  DayEntry entries[HISTORY_DAYS];
  int      num_entries;
  int32_t  daily_goal;       // 0 = disabled
  // Animation progress in [0, 100].  100 = fully drawn (steady state).
  int16_t  anim_progress;
  Animation *animation;
} GraphLayerData;

// ---- Drawing ----------------------------------------------------------------

static void graph_layer_update_proc(Layer *layer, GContext *ctx) {
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);

  if (data->num_entries == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx, "No data yet.",
                       fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                       bounds,
                       GTextOverflowModeWordWrap,
                       GTextAlignmentCenter, NULL);
    return;
  }

  int max_count = 1;
  for (int i = 0; i < data->num_entries; i++) {
    if ((int)data->entries[i].count > max_count)
      max_count = (int)data->entries[i].count;
  }

  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  now_tm->tm_hour = 0; now_tm->tm_min = 0; now_tm->tm_sec = 0;
  int32_t today_start = (int32_t)mktime(now_tm);

  int row_h     = bounds.size.h / data->num_entries;
  int label_w   = bounds.size.w / 5;
  int count_w   = bounds.size.w / 8;
  int padding   = 2;
  int max_bar_w = bounds.size.w - label_w - count_w - (padding * 3);

  for (int i = 0; i < data->num_entries; i++) {
    int y     = i * row_h;
    int bar_h = row_h - (padding * 2);

    bool is_today = (data->entries[i].day_timestamp == today_start);
    (void)is_today;  // only used inside PBL_IF_COLOR_ELSE; unused on B&W platforms

    char date_str[4];
    time_t t = (time_t)data->entries[i].day_timestamp;
    strftime(date_str, sizeof(date_str), "%a", localtime(&t));

    graphics_context_set_text_color(ctx,
        PBL_IF_COLOR_ELSE(is_today ? GColorBlueMoon : GColorBlack,
                          GColorBlack));
    graphics_draw_text(ctx, date_str, font,
                       GRect(0, y, label_w, row_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentLeft, NULL);

    // --- Animated bar width -------------------------------------------------
    // Each bar has a personal progress window that starts STAGGER units after
    // the previous bar's window, giving a top-to-bottom cascade.
    // bar_start: the global progress value at which this bar begins growing.
    // bar_range: the portion of [0,100] over which this bar grows.
    int bar_start = i * ANIM_STAGGER;
    int bar_range = 100 - bar_start;
    if (bar_range < 1) bar_range = 1;

    int local_p = data->anim_progress - bar_start;
    if (local_p < 0) local_p = 0;
    if (local_p > bar_range) local_p = bar_range;

    int full_bar_w = ((int)data->entries[i].count * max_bar_w) / max_count;
    int bar_w = (full_bar_w * local_p) / bar_range;

    if (bar_w > 0) {
      bool over_goal = data->daily_goal > 0 &&
                       (int)data->entries[i].count > (int)data->daily_goal;
      GColor bar_color = PBL_IF_COLOR_ELSE(
          over_goal  ? GColorRed :
          is_today   ? GColorBlueMoon :
                       GColorOrange,
          GColorBlack);
      graphics_context_set_fill_color(ctx, bar_color);
      graphics_fill_rect(ctx,
                         GRect(label_w + padding, y + padding, bar_w, bar_h),
                         2, GCornersAll);
    }

    // Only draw the count number once the bar for that row has finished
    // (local_p reached bar_range), so numbers don't float ahead of bars.
    if (local_p >= bar_range) {
      char count_str[8];
      snprintf(count_str, sizeof(count_str), "%d",
               (int)data->entries[i].count);
      graphics_context_set_text_color(ctx, GColorBlack);
      graphics_draw_text(ctx, count_str, font,
                         GRect(label_w + padding + max_bar_w + padding,
                               y, count_w, row_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }
  }
}

// ---- Animation --------------------------------------------------------------

static void anim_update(Animation *anim, AnimationProgress progress) {
  // Find the layer from the animation context.
  Layer *layer = (Layer *)animation_get_context(anim);
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  // Map AnimationProgress (0..ANIMATION_NORMALIZED_MAX) to 0..100.
  data->anim_progress =
      (int16_t)(progress * 100 / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(layer);
}

static void anim_stopped(Animation *anim, bool finished, void *context) {
  Layer *layer = (Layer *)context;
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  // Ensure fully drawn even if the callback fires slightly early.
  data->anim_progress = 100;
  layer_mark_dirty(layer);
  data->animation = NULL;
}

// ---- Public API -------------------------------------------------------------

GraphLayer *graph_layer_create(GRect frame) {
  GraphLayer *layer = layer_create_with_data(frame, sizeof(GraphLayerData));
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  memset(data, 0, sizeof(GraphLayerData));
  // Start fully drawn — animation is opt-in via graph_layer_animate_in().
  data->anim_progress = 100;
  layer_set_update_proc(layer, graph_layer_update_proc);
  return layer;
}

void graph_layer_destroy(GraphLayer *layer) {
  if (!layer) return;
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
  }
  layer_destroy(layer);
}

void graph_layer_set_data(GraphLayer *layer,
                          DayEntry *entries, int num_entries) {
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  if (num_entries < 0) num_entries = 0;
  if (num_entries > HISTORY_DAYS) num_entries = HISTORY_DAYS;
  memcpy(data->entries, entries, sizeof(DayEntry) * num_entries);
  data->num_entries = num_entries;
  layer_mark_dirty(layer);
}

void graph_layer_set_daily_goal(GraphLayer *layer, int32_t goal) {
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);
  data->daily_goal = goal;
  layer_mark_dirty(layer);
}

void graph_layer_animate_in(GraphLayer *layer) {
  GraphLayerData *data = (GraphLayerData *)layer_get_data(layer);

  // Cancel any running animation from a previous push.
  if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
  }

  data->anim_progress = 0;
  layer_mark_dirty(layer);

  static const AnimationImplementation s_impl = {
    .update = anim_update,
  };

  Animation *anim = animation_create();
  animation_set_duration(anim, ANIM_DURATION_MS);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_impl);
  animation_set_handlers(anim, (AnimationHandlers){
    .stopped = anim_stopped,
  }, layer);

  // animation_get_context(anim) returns the context passed to
  // animation_set_handlers — which is `layer` — so anim_update can reach it.
  data->animation = anim;
  animation_schedule(anim);
}
