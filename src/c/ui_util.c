#include "ui_util.h"

void ui_title_bar_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);
}

GFont ui_get_title_font(void) {
  switch (preferred_content_size()) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      return fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
    default:
      return fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  }
}

void ui_format_week_range(char *buf, size_t buf_size, time_t week_start) {
  time_t week_end = week_start + 6 * 24 * 60 * 60;
  char start_str[8], end_str[8];
  struct tm t1 = *localtime(&week_start);
  struct tm t2 = *localtime(&week_end);
  strftime(start_str, sizeof(start_str), "%d.%m", &t1);
  strftime(end_str,   sizeof(end_str),   "%d.%m", &t2);
  snprintf(buf, buf_size, "%s - %s", start_str, end_str);
}
