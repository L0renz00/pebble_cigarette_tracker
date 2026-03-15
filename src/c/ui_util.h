#pragma once
#include <pebble.h>

// A Layer update_proc that fills its bounds with GColorBlack, creating an
// inverted title bar.  Place behind a title TextLayer with GColorClear
// background and accent-colored text.
void ui_title_bar_update_proc(Layer *layer, GContext *ctx);

// Returns the appropriate title font for the current content size preference.
GFont ui_get_title_font(void);

// Formats a week date range ("DD.MM - DD.MM") into buf, derived from
// week_start (the Monday midnight timestamp).
void ui_format_week_range(char *buf, size_t buf_size, time_t week_start);
