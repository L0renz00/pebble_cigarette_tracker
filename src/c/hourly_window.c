#include <pebble.h>
#include "hourly_window.h"
#include "alltime_window.h"
#include "storage.h"
#include "area_chart_layer.h"
#include "ui_util.h"

// --- Data preparation --------------------------------------------------------
//
// Maps the 24-bucket hour histogram into AreaChartData.  All 24 slots are
// always populated so the fill forms a continuous shape from hour 0 to 23.
// H/L labels are not used; the info strip shows "Peak" (left) and the peak
// hour (right, in accent colour).

static void build_hourly_chart_data(AreaChartData *cd, uint8_t *hist) {
  memset(cd, 0, sizeof(AreaChartData));

  int total = 0;
  for (int h = 0; h < 24; h++) total += hist[h];

  cd->total_slots        = 24;
  cd->fill_color         = PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack);
  cd->anchor_color       = PBL_IF_COLOR_ELSE(GColorVividCerulean, GColorBlack);
  cd->empty_message      = "No data yet.\nLog your first\ncigarette!";
  cd->wide_bottom_labels = true;
  cd->hide_avg_line      = true;
  cd->ring_idx           = -1;

  if (total == 0) return;   // n == 0 triggers the empty message

  int max_val = 0, peak_h = 0;
  for (int h = 0; h < 24; h++) {
    if (hist[h] > max_val) { max_val = hist[h]; peak_h = h; }
  }

  cd->n        = 24;
  cd->ring_idx = peak_h;

  for (int h = 0; h < 24; h++) {
    cd->y[h]         = hist[h];
    cd->populated[h] = true;
  }

  // Info strip: "Peak" label on the left, peak hour in accent on the right.
  snprintf(cd->h_label,      AREA_CHART_INFO_LEN, "Peak");
  snprintf(cd->anchor_label, AREA_CHART_INFO_LEN, "%dh", peak_h);

  // X-axis: label only the three meaningful time boundaries.
  snprintf(cd->bottom_labels[6],  AREA_CHART_LABEL_LEN, "6");
  snprintf(cd->bottom_labels[12], AREA_CHART_LABEL_LEN, "12");
  snprintf(cd->bottom_labels[18], AREA_CHART_LABEL_LEN, "18");
}

// --- Window ------------------------------------------------------------------

static Window         *s_hourly_window;
static TextLayer      *s_title_layer;
static Layer          *s_title_rule_layer;
static AreaChartLayer *s_chart_layer;

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  alltime_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

static void hourly_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

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

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "By Hour");
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer, title_font);
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  s_title_rule_layer = layer_create(
      GRect(8, title_h, bounds.size.w - 16, 1));
  layer_set_update_proc(s_title_rule_layer, ui_rule_update_proc);
  layer_add_child(window_layer, s_title_rule_layer);

  int chart_y = title_h + 3;
  GRect chart_frame = GRect(4, chart_y,
                            bounds.size.w - 8,
                            bounds.size.h - chart_y - 2);
  s_chart_layer = area_chart_layer_create(chart_frame);

  uint8_t hist[24];
  storage_get_hour_histogram(hist);

  AreaChartData cd;
  build_hourly_chart_data(&cd, hist);
  area_chart_layer_set_data(s_chart_layer, &cd);

  layer_add_child(window_layer, s_chart_layer);
  area_chart_layer_animate_in(s_chart_layer);
}

static void hourly_window_unload(Window *window) {
  text_layer_destroy(s_title_layer);
  layer_destroy(s_title_rule_layer);
  area_chart_layer_destroy(s_chart_layer);
  window_destroy(s_hourly_window);
  s_hourly_window = NULL;
}

void hourly_window_push(void) {
  s_hourly_window = window_create();
  window_set_background_color(s_hourly_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_hourly_window, click_config_provider);
  window_set_window_handlers(s_hourly_window, (WindowHandlers) {
    .load   = hourly_window_load,
    .unload = hourly_window_unload,
  });
  window_stack_push(s_hourly_window, true);
}
