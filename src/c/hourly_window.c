#include <pebble.h>
#include "hourly_window.h"
#include "alltime_window.h"
#include "storage.h"
#include "area_chart_layer.h"
#include "ui_util.h"

// --- Data preparation --------------------------------------------------------
//
// Buckets the 24-hour histogram into 12 × 2-hour windows, normalises to
// percentages, applies symmetric weighted smoothing, and populates AreaChartData.

#define HOURLY_BUCKETS 12
#define HOURLY_BUCKET_SZ 2   // hours per bucket

static void build_hourly_chart_data(AreaChartData *cd, uint8_t *hist) {
  memset(cd, 0, sizeof(AreaChartData));

  cd->total_slots        = HOURLY_BUCKETS;
  cd->fill_color         = PBL_IF_COLOR_ELSE(GColorBlueMoon, GColorBlack);
  cd->empty_message      = "No data yet.\nLog your first\ncigarette!";
  cd->wide_bottom_labels = true;
  cd->hide_avg_line      = true;
  cd->hide_dots          = true;
  cd->show_y_axis        = false;
  cd->larger_labels      = false;
  cd->ring_idx           = -1;

  // Step 1: bucket 24h → 12 × 2h
  int buckets[HOURLY_BUCKETS] = {0};
  int total = 0;
  for (int h = 0; h < 24; h++) {
    buckets[h / HOURLY_BUCKET_SZ] += hist[h];
    total += hist[h];
  }

  if (total == 0) return;   // n == 0 triggers the empty message

  // Step 2: percentage normalise
  int pct[HOURLY_BUCKETS];
  for (int i = 0; i < HOURLY_BUCKETS; i++) {
    pct[i] = buckets[i] * 100 / total;
  }

  // Step 3: symmetric weighted smooth — (prev + 3·cur + next) / 5
  cd->n = HOURLY_BUCKETS;
  for (int i = 0; i < HOURLY_BUCKETS; i++) {
    int prev = (i > 0)                  ? pct[i - 1] : pct[i];
    int next = (i < HOURLY_BUCKETS - 1) ? pct[i + 1] : pct[i];
    cd->y[i]         = (prev + 3 * pct[i] + next) / 5;
    cd->populated[i] = true;
  }

  // Step 4: peak window label (raw bucket, before smoothing)
  int peak_b = 0;
  for (int i = 1; i < HOURLY_BUCKETS; i++) {
    if (pct[i] > pct[peak_b]) peak_b = i;
  }
  snprintf(cd->h_label, sizeof(cd->h_label), "Peak: %dh-%dh",
           peak_b * HOURLY_BUCKET_SZ, (peak_b + 1) * HOURLY_BUCKET_SZ);

  // Step 5: bottom labels — every 6h: slot 0=0h, slot 3=6h, slot 6=12h, slot 9=18h
  snprintf(cd->bottom_labels[0], AREA_CHART_LABEL_LEN, "0");
  snprintf(cd->bottom_labels[3], AREA_CHART_LABEL_LEN, "6");
  snprintf(cd->bottom_labels[6], AREA_CHART_LABEL_LEN, "12");
  snprintf(cd->bottom_labels[9], AREA_CHART_LABEL_LEN, "18");
}

#undef HOURLY_BUCKETS
#undef HOURLY_BUCKET_SZ

// --- Window ------------------------------------------------------------------

static Window         *s_hourly_window;
static TextLayer      *s_title_layer;
static Layer          *s_title_bg_layer;
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

  s_title_bg_layer = layer_create(GRect(0, 0, bounds.size.w, title_h));
  layer_set_update_proc(s_title_bg_layer, ui_title_bar_update_proc);
  layer_add_child(window_layer, s_title_bg_layer);

  s_title_layer = text_layer_create(GRect(0, 0, bounds.size.w, title_h));
  text_layer_set_text(s_title_layer, "By Hour");
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_font(s_title_layer, title_font);
  text_layer_set_text_color(s_title_layer,
      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  text_layer_set_background_color(s_title_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_title_layer));

  GRect chart_frame = GRect(4, title_h,
                            bounds.size.w - 8,
                            bounds.size.h - title_h - 2);
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
  layer_destroy(s_title_bg_layer);
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
