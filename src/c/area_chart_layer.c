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

  GFont anchor_font;
  int   info_h;
  switch (preferred_content_size()) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      anchor_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      info_h = 24;
      break;
    default:
      anchor_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      info_h = 18;
      break;
  }

  if (d->chart.n == 0) {
    graphics_context_set_text_color(ctx, GColorBlack);
    graphics_draw_text(ctx,
        d->chart.empty_message ? d->chart.empty_message : "No data yet.",
        anchor_font,
        bounds, GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL);
    return;
  }

  // ---- Layout constants ----------------------------------------------------

  GFont label_font = d->chart.larger_labels
      ? fonts_get_system_font(FONT_KEY_GOTHIC_18)
      : fonts_get_system_font(FONT_KEY_GOTHIC_14);
  GFont info_font  = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  const int label_h = d->chart.larger_labels ? 18 : 14;
  const int ts          = d->chart.total_slots;
  const int left_margin = d->chart.show_y_axis ? 14 : 0;
  const int slot_w      = (bounds.size.w - left_margin) / ts;

  const int plot_top    = info_h;
  const int plot_bottom = bounds.size.h - label_h - 4;
  const int plot_h      = (plot_bottom > plot_top) ? (plot_bottom - plot_top) : 1;

  GColor bg = PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite);
  int    p  = d->anim_progress;   // 0–100

  // Dynamic display range: track both min and max, then add padding.
  int max_y = 0, min_y = 0;
  bool has_data = false;
  for (int i = 0; i < ts; i++) {
    if (d->chart.populated[i]) {
      if (!has_data || d->chart.y[i] > max_y) max_y = d->chart.y[i];
      if (!has_data || d->chart.y[i] < min_y) min_y = d->chart.y[i];
      has_data = true;
    }
  }
  int range         = (max_y > min_y) ? (max_y - min_y) : 1;
  int pad           = (range > 3) ? range / 4 : 1;
  int display_max   = max_y + pad;
  int display_min   = (min_y >= pad) ? min_y - pad : 0;
  if (display_max == display_min) display_max = display_min + 1;
  int display_range = display_max - display_min;

  // Convenience macros — undef'd at the end of the proc.
#define SLOT_CX(i)   (left_margin + (i) * slot_w + slot_w / 2)
#define Y_FULL(v)    (plot_bottom - (((v) - display_min) * plot_h / display_range))
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
    int avg_y_full = plot_bottom - (int)((sum - (int32_t)d->chart.n * display_min) * plot_h / ((int32_t)d->chart.n * display_range));
    int avg_y      = plot_bottom - ((plot_bottom - avg_y_full) * p / 100);
    graphics_context_set_stroke_color(ctx,
        PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack));
    for (int x = 0; x < bounds.size.w; x += 6) {
      int x2 = x + 2;
      if (x2 >= bounds.size.w) x2 = bounds.size.w - 1;
      graphics_draw_line(ctx, GPoint(x, avg_y), GPoint(x2, avg_y));
    }
  }

  // ---- 3. Goal line --------------------------------------------------------

  if (d->chart.goal > 0) {
    int goal_y_full = Y_FULL(d->chart.goal);
    if (goal_y_full >= plot_top && goal_y_full <= plot_bottom) {
      int goal_y = plot_bottom - ((plot_bottom - goal_y_full) * p / 100);
      graphics_context_set_stroke_color(ctx, PBL_IF_COLOR_ELSE(GColorRed, GColorBlack));
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, GPoint(0, goal_y), GPoint(bounds.size.w, goal_y));
      graphics_context_set_stroke_width(ctx, 1);
    }
  }

  // ---- 4. Connecting line --------------------------------------------------

  {
    int prev_x = -1, prev_y = -1;
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_stroke_width(ctx, 2);
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

  if (!d->chart.hide_dots) for (int i = 0; i < ts; i++) {
    if (!d->chart.populated[i]) continue;
    int cx = SLOT_CX(i), y = Y_ANIM(d->chart.y[i]);
    bool ring = (i == d->chart.ring_idx);
    if (!ring && slot_w < 10) continue;
    int r_outer = d->chart.larger_labels ? 5 : 4;
    int r_inner = d->chart.larger_labels ? 3 : 2;
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_circle(ctx, GPoint(cx, y), ring ? r_outer : 3);
    if (ring) {
      graphics_context_set_fill_color(ctx, bg);
      graphics_fill_circle(ctx, GPoint(cx, y), r_inner);
      if (d->chart.ring_label[0]) {
        GFont lbl_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
        int lw = 28;
        int ly = y - 16;
        if (ly < plot_top) ly = plot_top;
        graphics_context_set_text_color(ctx, GColorBlack);
        graphics_draw_text(ctx, d->chart.ring_label, lbl_font,
                           GRect(cx - lw / 2, ly, lw, 14),
                           GTextOverflowModeTrailingEllipsis,
                           GTextAlignmentCenter, NULL);
      }
    }
  }

  // ---- 5. Info strip (above the plot) --------------------------------------
  //
  //  Left    h_label      "H: 17" or "H: 9.3"    GOTHIC_14_BOLD, black
  //  Centre  l_label      "L: 4"  or "L: 5.1"    GOTHIC_14_BOLD, black (omitted if empty)
  //  Right   anchor_label "13"    or "9.3/d"      GOTHIC_18_BOLD, anchor_color

  {
    int stat_w = bounds.size.w / 3;
    // When there is no centre label, give h_label two thirds of the width
    // so longer strings (e.g. "Peak: 22h") are not truncated.
    int h_label_w = d->chart.l_label[0] ? stat_w : stat_w * 2;
    graphics_context_set_text_color(ctx, GColorBlack);
    if (d->chart.h_label[0]) {
      graphics_draw_text(ctx, d->chart.h_label, info_font,
                         GRect(0, 0, h_label_w, info_h),
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
                         anchor_font,
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
      PBL_IF_COLOR_ELSE(GColorBlack, GColorBlack));
  for (int i = 0; i < ts; i++) {
    if (!d->chart.bottom_labels[i][0]) continue;
    int lw = d->chart.wide_bottom_labels ? 36 : slot_w;
    int lx = d->chart.wide_bottom_labels ? (SLOT_CX(i) - lw / 2) : (left_margin + i * slot_w);
    graphics_draw_text(ctx, d->chart.bottom_labels[i], label_font,
                       GRect(lx, bounds.size.h - label_h, lw, label_h),
                       GTextOverflowModeTrailingEllipsis,
                       GTextAlignmentCenter, NULL);
  }

  // ---- 7. Y-axis labels (adaptive) -------------------------------------------
  //
  // Drawn last so they appear on top of the fill.  Step size targets 3–5 ticks
  // across the padded display range (display_min…display_max).  Ticks start at
  // the first clean multiple of y_step that falls within the display range.
  // Uses Y_FULL so labels stay fixed during the grow-from-baseline animation.

  if (d->chart.show_y_axis && left_margin > 0) {
    // ceil(display_range / 4) gives a raw step that yields at most ~5 labels;
    // round up to the nearest "nice" value so numbers stay legible.
    int raw_step = (display_range + 3) / 4;
    if (raw_step < 1) raw_step = 1;
    int y_step;
    if      (raw_step <= 1)  y_step = 1;
    else if (raw_step <= 2)  y_step = 2;
    else if (raw_step <= 5)  y_step = 5;
    else if (raw_step <= 10) y_step = 10;
    else if (raw_step <= 20) y_step = 20;
    else if (raw_step <= 25) y_step = 25;
    else if (raw_step <= 50) y_step = 50;
    else                     y_step = 100;

    // First tick at lowest multiple of y_step >= display_min.
    int tick_start = ((display_min + y_step - 1) / y_step) * y_step;

    GFont y_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
    graphics_context_set_text_color(ctx, GColorBlack);
    for (int tick = tick_start; tick <= display_max; tick += y_step) {
      int ty = Y_FULL(tick);
      if (ty < plot_top || ty > plot_bottom) continue;
      char buf[6];
      snprintf(buf, sizeof(buf), "%d", tick);
      graphics_draw_text(ctx, buf, y_font,
                         GRect(0, ty - 7, left_margin - 1, 14),
                         GTextOverflowModeTrailingEllipsis,
                         GTextAlignmentRight, NULL);
    }
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
