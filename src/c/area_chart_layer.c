#include "area_chart_layer.h"
#include <string.h>

#define ANIM_DURATION_MS   450
#define PATH_MAX_PTS       (AREA_CHART_MAX_SLOTS + 2)

typedef struct {
  AreaChartData chart;
  int16_t       anim_progress;   // 0 = baseline, 100 = fully drawn
  Animation    *animation;
} AreaChartLayerData;

// ---- Drawing ----------------------------------------------------------------

static void area_chart_update_proc(Layer *layer, GContext *ctx) {
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  GRect bounds = layer_get_bounds(layer);

  if (d->chart.n == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx,
        d->chart.empty_message ? d->chart.empty_message : "No data yet.",
        fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
        bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // ---- Layout constants ----------------------------------------------------

  GFont label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont info_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  const int label_h = 14;
  const int info_h  = 18;
  const int ts      = d->chart.total_slots;
  const int slot_w  = bounds.size.w / ts;

  const int plot_top    = info_h;
  const int plot_bottom = bounds.size.h - label_h - 4;
  const int plot_h      = (plot_bottom > plot_top) ? (plot_bottom - plot_top) : 1;

  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  int    p  = d->anim_progress;   // 0–100

  // Max y value for scaling — floor at 1 to avoid division by zero.
  int max_y = 1;
  for (int i = 0; i < ts; i++) {
    if (d->chart.populated[i] && d->chart.y[i] > max_y)
      max_y = d->chart.y[i];
  }

  // Convenience macros — undef'd at the end of the proc.
#define SLOT_CX(i)   ((i) * slot_w + slot_w / 2)
#define Y_FULL(v)    (plot_bottom - ((v) * plot_h / max_y))
#define Y_ANIM(v)    (plot_bottom - ((plot_bottom - Y_FULL(v)) * p / 100))

  // ---- 1. Filled area path -------------------------------------------------

  GPoint pts[PATH_MAX_PTS];
  int npts = 0, first_x = -1, last_x = -1;
  for (int i = 0; i < ts; i++) {
    if (!d->chart.populated[i]) continue;
    int cx = SLOT_CX(i), y = Y_ANIM(d->chart.y[i]);
    if (first_x < 0) first_x = cx;
    last_x = cx;
    pts[npts + 1] = GPoint(cx, y);
    npts++;
  }
  if (npts >= 1) {
    pts[0]        = GPoint(first_x, plot_bottom);
    pts[npts + 1] = GPoint(last_x,  plot_bottom);
    GPathInfo pi  = { .num_points = npts + 2, .points = pts };
    GPath *area   = gpath_create(&pi);
    graphics_context_set_fill_color(ctx, d->chart.fill_color);
    gpath_draw_filled(ctx, area);
    gpath_destroy(area);
  }

  // ---- 2. Dotted average line ----------------------------------------------

  if (!d->chart.hide_avg_line) {
    int32_t sum = 0;
    for (int i = 0; i < ts; i++)
      if (d->chart.populated[i]) sum += d->chart.y[i];
    int avg_y_full = plot_bottom - (int)(sum * plot_h / (d->chart.n * max_y));
    int avg_y      = plot_bottom - ((plot_bottom - avg_y_full) * p / 100);
    graphics_context_set_stroke_color(ctx,
        PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
    for (int x = 0; x < bounds.size.w; x += 6) {
      int x2 = x + 2;
      if (x2 >= bounds.size.w) x2 = bounds.size.w - 1;
      graphics_draw_line(ctx, GPoint(x, avg_y), GPoint(x2, avg_y));
    }
  }

  // ---- 3. Connecting line --------------------------------------------------

  {
    int prev_x = -1, prev_y = -1;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    for (int i = 0; i < ts; i++) {
      if (!d->chart.populated[i]) { prev_x = -1; prev_y = -1; continue; }
      int cx = SLOT_CX(i), y = Y_ANIM(d->chart.y[i]);
      if (prev_x >= 0)
        graphics_draw_line(ctx, GPoint(prev_x, prev_y), GPoint(cx, y));
      prev_x = cx; prev_y = y;
    }
  }

  // ---- 4. Dots — ring on the designated slot -------------------------------
  //
  // On dense charts (slot_w < 10, e.g. 24-slot hourly) skip non-ring dots to
  // avoid a smeared blob of overlapping circles.

  for (int i = 0; i < ts; i++) {
    if (!d->chart.populated[i]) continue;
    int cx = SLOT_CX(i), y = Y_ANIM(d->chart.y[i]);
    bool ring = (i == d->chart.ring_idx);
    if (!ring && slot_w < 10) continue;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx, y), ring ? 4 : 3);
    if (ring) {
      graphics_context_set_fill_color(ctx, bg);
      graphics_fill_circle(ctx, GPoint(cx, y), 2);
    }
  }

  // ---- 5. Info strip (above the plot) --------------------------------------
  //
  //  Left    h_label      "H: 17" or "H: 9.3"    GOTHIC_14_BOLD, black
  //  Centre  l_label      "L: 4"  or "L: 5.1"    GOTHIC_14_BOLD, black (omitted if empty)
  //  Right   anchor_label "13"    or "9.3/d"      GOTHIC_18_BOLD, anchor_color

  {
    int stat_w = bounds.size.w / 3;
    graphics_context_set_text_color(ctx, GColorBlack);
    if (d->chart.h_label[0]) {
      graphics_draw_text(ctx, d->chart.h_label, info_font,
                         GRect(0, 0, stat_w, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }
    if (d->chart.l_label[0]) {
      graphics_draw_text(ctx, d->chart.l_label, info_font,
                         GRect(stat_w, 0, stat_w, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentLeft, NULL);
    }
    if (d->chart.anchor_label[0]) {
      graphics_context_set_text_color(ctx, d->chart.anchor_color);
      graphics_draw_text(ctx, d->chart.anchor_label,
                         fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD),
                         GRect(bounds.size.w - 44, 0, 44, info_h),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentRight, NULL);
    }
  }

  // ---- 6. Bottom labels per slot -------------------------------------------
  //
  // Normal mode: each label is drawn in its slot's rect (slot_w wide).
  // Wide mode:   each label is centered at slot_cx with a 36px box — use this
  //              when slot_w is too narrow for the font (e.g. 24-slot hourly).

  graphics_context_set_text_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  for (int i = 0; i < ts; i++) {
    if (!d->chart.bottom_labels[i][0]) continue;
    int lw = d->chart.wide_bottom_labels ? 36 : slot_w;
    int lx = d->chart.wide_bottom_labels ? (SLOT_CX(i) - lw / 2) : (i * slot_w);
    graphics_draw_text(ctx, d->chart.bottom_labels[i], label_font,
                       GRect(lx, bounds.size.h - label_h, lw, label_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

#undef SLOT_CX
#undef Y_FULL
#undef Y_ANIM
}

// ---- Animation --------------------------------------------------------------

static void anim_update(Animation *anim, AnimationProgress progress) {
  Layer *layer = (Layer *)animation_get_context(anim);
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  d->anim_progress = (int16_t)(progress * 100 / ANIMATION_NORMALIZED_MAX);
  layer_mark_dirty(layer);
}

static void anim_stopped(Animation *anim, bool finished, void *context) {
  Layer *layer = (Layer *)context;
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  d->anim_progress = 100;
  layer_mark_dirty(layer);
  d->animation = NULL;
}

// ---- Public API -------------------------------------------------------------

AreaChartLayer *area_chart_layer_create(GRect frame) {
  AreaChartLayer *layer =
      layer_create_with_data(frame, sizeof(AreaChartLayerData));
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  memset(d, 0, sizeof(AreaChartLayerData));
  d->chart.ring_idx = -1;
  d->anim_progress  = 100;   // fully drawn until animate_in() is called
  layer_set_update_proc(layer, area_chart_update_proc);
  return layer;
}

void area_chart_layer_destroy(AreaChartLayer *layer) {
  if (!layer) return;
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  if (d->animation) {
    animation_unschedule(d->animation);
    animation_destroy(d->animation);
    d->animation = NULL;
  }
  layer_destroy(layer);
}

void area_chart_layer_set_data(AreaChartLayer *layer,
                                const AreaChartData *data) {
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  d->chart = *data;   // struct copy — caller's AreaChartData can go out of scope
  layer_mark_dirty(layer);
}

void area_chart_layer_animate_in(AreaChartLayer *layer) {
  AreaChartLayerData *d = (AreaChartLayerData *)layer_get_data(layer);
  if (d->animation) {
    animation_unschedule(d->animation);
    animation_destroy(d->animation);
    d->animation = NULL;
  }
  d->anim_progress = 0;
  layer_mark_dirty(layer);

  static const AnimationImplementation s_impl = { .update = anim_update };
  Animation *anim = animation_create();
  animation_set_duration(anim, ANIM_DURATION_MS);
  animation_set_curve(anim, AnimationCurveEaseOut);
  animation_set_implementation(anim, &s_impl);
  animation_set_handlers(anim,
      (AnimationHandlers){ .stopped = anim_stopped }, layer);
  d->animation = anim;
  animation_schedule(anim);
}
