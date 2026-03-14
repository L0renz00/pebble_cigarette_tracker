#include "ui_util.h"

void ui_rule_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx,
      PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack));
  graphics_draw_line(ctx, GPoint(0, 0), GPoint(bounds.size.w - 1, 0));
}
