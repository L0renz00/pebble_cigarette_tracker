#include <pebble.h>
#include "history_window.h"
#include "storage.h"
#include "area_chart_layer.h"
#include "ui_util.h"
#include "main.h"

// Only the most recent N completed weeks are shown — more than that and the
// per-slot date labels become unreadably narrow on Basalt (144px / 6 = 24px).
#define HISTORY_DISPLAY_WEEKS  6

static Window         *s_history_window;
static TextLayer      *s_title_layer;
static Layer          *s_title_bg_layer;
static AreaChartLayer *s_chart_layer;

// --- Data preparation --------------------------------------------------------
//
// Converts the WeekEntry array into AreaChartData.  Averages are stored in
// tenths (e.g. 93 = 9.3 cigs/day) so the integer chart scale handles one
// decimal place without floating point.

static void build_history_chart_data(AreaChartData *cd,
                                      WeekEntry *entries, int num_entries) {
  memset(cd, 0, sizeof(AreaChartData));

  // Cap to the most recent HISTORY_DISPLAY_WEEKS entries.
  if (num_entries > HISTORY_DISPLAY_WEEKS) {
    entries    += (num_entries - HISTORY_DISPLAY_WEEKS);
    num_entries = HISTORY_DISPLAY_WEEKS;
  }

  cd->total_slots        = num_entries;
  cd->n                  = num_entries;
  cd->ring_idx           = -1;
  cd->fill_color         = PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack);
  cd->wide_bottom_labels = false;
  cd->hide_avg_line      = true;
  cd->show_y_axis        = true;
  cd->y_axis_tenths      = true;
  cd->empty_message      = "No history yet.\nCome back next\nweek!";

  for (int i = 0; i < num_entries; i++) {
    int32_t da = entries[i].days_active;
    cd->y[i]         = (da > 0) ? (int)((entries[i].total * 10) / da) : 0;
    cd->populated[i] = true;

    // Bottom label: Monday date of that week, day-of-month only, e.g. "10".
    time_t ws = (time_t)entries[i].week_timestamp;
    strftime(cd->bottom_labels[i], AREA_CHART_LABEL_LEN, "%d",
             localtime(&ws));
  }

  // Trend delta: newest avg minus oldest avg, shown as "▲ +1.2/d" / "▼ -0.8/d".
  // The arrow glyph is drawn as a filled GPath triangle (h_label_arrow); the
  // label text carries only the sign and magnitude.
  if (num_entries >= 2) {
    int delta = cd->y[num_entries - 1] - cd->y[0];
    int abs_d = delta < 0 ? -delta : delta;
    cd->h_label_arrow = (delta > 0) ? 1 : (delta < 0) ? -1 : 0;
    snprintf(cd->h_label, AREA_CHART_INFO_LEN,
             "%c%d.%d/d",
             delta >= 0 ? '+' : '-',
             abs_d / 10, abs_d % 10);
  }
}

// --- Click handlers ----------------------------------------------------------

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  Window *main = main_window_get();
  while (window_stack_get_top_window() != main) {
    window_stack_pop(false);
  }
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void history_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int title_h = bounds.size.h / 7;

  GFont title_font = ui_get_title_font();

  s_title_bg_layer = layer_create(GRect(0, 0, bounds.size.w, title_h));
  layer_set_update_proc(s_title_bg_layer, ui_title_bar_update_proc);
  layer_add_child(window_layer, s_title_bg_layer);

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "Weekly Avg");
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

  WeekEntry weeks[WEEK_HISTORY_COUNT];
  int num_weeks;
  storage_get_week_history(weeks, &num_weeks);

  AreaChartData cd;
  build_history_chart_data(&cd, weeks, num_weeks);
  area_chart_layer_set_data(s_chart_layer, &cd);

  layer_add_child(window_layer, s_chart_layer);
  area_chart_layer_animate_in(s_chart_layer);
}

static void history_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_bg_layer);
  area_chart_layer_destroy(s_chart_layer);
  window_destroy(s_history_window);
  s_history_window = NULL;
}

// --- Public API --------------------------------------------------------------

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
