#include <pebble.h>
#include "trend_window.h"
#include "stats_window.h"
#include "hourly_window.h"
#include "storage.h"
#include "area_chart_layer.h"
#include "ui_util.h"

static Window         *s_trend_window;
static TextLayer      *s_title_layer;
static Layer          *s_title_bg_layer;
static AreaChartLayer *s_chart_layer;

static char s_title_buf[24];

// --- Data preparation --------------------------------------------------------
//
// Converts this week's DayEntry array into the flat AreaChartData struct that
// the chart layer needs.  All text formatting lives here so the layer only
// draws.

static void build_trend_chart_data(AreaChartData *cd,
                                    DayEntry *entries, int num_entries,
                                    time_t week_start) {
  memset(cd, 0, sizeof(AreaChartData));
  cd->total_slots        = HISTORY_DAYS;
  cd->ring_idx           = -1;
  cd->fill_color         = PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack);
  cd->empty_message      = "No data yet.";
  cd->wide_bottom_labels = false;
  cd->hide_avg_line      = true;
  cd->show_y_axis        = true;

  static const char * const day_labels[HISTORY_DAYS] = {
    "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"
  };
  for (int i = 0; i < HISTORY_DAYS; i++) {
    snprintf(cd->bottom_labels[i], AREA_CHART_LABEL_LEN, "%s", day_labels[i]);
  }

  // Determine today's slot for the ring-dot and anchor label.
  time_t now = time(NULL);
  struct tm *now_tm = localtime(&now);
  now_tm->tm_hour = 0; now_tm->tm_min = 0; now_tm->tm_sec = 0;
  time_t today_start = mktime(now_tm);
  int today_slot = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_slot >= 0 && today_slot < HISTORY_DAYS) cd->ring_idx = today_slot;

  // Map each DayEntry to its weekday slot; accumulate total for avg.
  int total = 0;
  for (int i = 0; i < num_entries; i++) {
    int slot = (int)(((time_t)entries[i].day_timestamp - week_start)
                     / (24 * 60 * 60));
    if (slot < 0 || slot >= HISTORY_DAYS) continue;
    int v = (int)entries[i].count;
    cd->y[slot]         = v;
    cd->populated[slot] = true;
    cd->n++;
    total += v;
  }

  // Info strip: weekly average per day (one decimal place), e.g. "Avg: 9.3/d".
  if (cd->n > 0) {
    int avg_int  = total / cd->n;
    int avg_frac = (total * 10 / cd->n) % 10;
    snprintf(cd->h_label, AREA_CHART_INFO_LEN, "Avg: %d.%d/d", avg_int, avg_frac);
  }
}

// --- Click handlers ----------------------------------------------------------

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  hourly_window_push();
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  stats_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void trend_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  time_t week_start = storage_get_week_start();
  time_t week_end   = week_start + 6 * 24 * 60 * 60;
  char start_str[8], end_str[8];
  strftime(start_str, sizeof(start_str), "%d.%m", localtime(&week_start));
  strftime(end_str,   sizeof(end_str),   "%d.%m", localtime(&week_end));
  snprintf(s_title_buf, sizeof(s_title_buf), "%s - %s", start_str, end_str);

  int title_h = bounds.size.h / 7;

  GFont title_font;
  switch (preferred_content_size()) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      title_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      break;
    default:
      title_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      break;
  }

  s_title_bg_layer = layer_create(GRect(0, 0, bounds.size.w, title_h));
  layer_set_update_proc(s_title_bg_layer, ui_title_bar_update_proc);
  layer_add_child(window_layer, s_title_bg_layer);

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, s_title_buf);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer, title_font);
  text_layer_set_text_color(s_title_layer,
      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  int chart_y = title_h;
  GRect chart_frame = GRect(4, chart_y,
                            bounds.size.w - 8,
                            bounds.size.h - chart_y - 2);
  s_chart_layer = area_chart_layer_create(chart_frame);

  DayEntry entries[HISTORY_DAYS];
  int num_entries;
  storage_get_history(entries, &num_entries);

  AreaChartData cd;
  build_trend_chart_data(&cd, entries, num_entries, week_start);
  cd.goal = (int)storage_get_goal();
  area_chart_layer_set_data(s_chart_layer, &cd);

  layer_add_child(window_layer, s_chart_layer);
  area_chart_layer_animate_in(s_chart_layer);
}

static void trend_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_bg_layer);
  area_chart_layer_destroy(s_chart_layer);
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
