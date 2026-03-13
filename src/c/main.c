#include <pebble.h>
#include "storage.h"
#include "stats_window.h"
#include "confirm_window.h"
#include "settings_window.h"
#include "main.h"

static Window      *s_main_window;
static BitmapLayer *s_icon_layer;
static GBitmap     *s_icon_bitmap;
static TextLayer   *s_count_layer;
static TextLayer   *s_last_time_layer;
static TextLayer   *s_elapsed_layer;

static int    s_count     = 0;
static time_t s_last_time = 0;

static char s_count_buf[16];
static char s_last_time_buf[40];
static char s_elapsed_buf[16];

// --- Display -----------------------------------------------------------------

static void format_elapsed(char *buf, size_t size, time_t last_time) {
  if (last_time == 0) { snprintf(buf, size, "--"); return; }
  int total_minutes = (int)((time(NULL) - last_time) / 60);
  if (total_minutes < 0) total_minutes = 0;

  int days    = total_minutes / (24 * 60);
  int hours   = (total_minutes % (24 * 60)) / 60;
  int minutes = total_minutes % 60;

  if (days > 0)        snprintf(buf, size, "%dd %dh", days, hours);
  else if (hours > 0)  snprintf(buf, size, "%dh %dm", hours, minutes);
  else                 snprintf(buf, size, "%dm", minutes);
}

static void update_display(void) {
  snprintf(s_count_buf, sizeof(s_count_buf), "%d", s_count);
  text_layer_set_text(s_count_layer, s_count_buf);

  if (s_last_time == 0) {
    text_layer_set_text(s_last_time_layer, "Last Smoked @\n--:--");
  } else {
    struct tm *t = localtime(&s_last_time);
    strftime(s_last_time_buf, sizeof(s_last_time_buf),
             "Last Smoked @\n%H:%M", t);
    text_layer_set_text(s_last_time_layer, s_last_time_buf);
  }

  format_elapsed(s_elapsed_buf, sizeof(s_elapsed_buf), s_last_time);
  text_layer_set_text(s_elapsed_layer, s_elapsed_buf);
}

void main_window_refresh(void) {
  storage_load(&s_count, &s_last_time);
  update_display();
}

// --- Tick handler ------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  format_elapsed(s_elapsed_buf, sizeof(s_elapsed_buf), s_last_time);
  text_layer_set_text(s_elapsed_layer, s_elapsed_buf);
}

// --- Confirm callback --------------------------------------------------------

static void on_confirm(bool confirmed) {
  if (confirmed) {
    s_count++;
    s_last_time = time(NULL);
    storage_save(s_count, s_last_time);
    storage_increment_total();
    // Record which hour this cigarette was logged for the hourly histogram.
    struct tm *t = localtime(&s_last_time);
    storage_log_hour(t->tm_hour);
    update_display();
    vibes_short_pulse();
  }
}

// --- Tap handler (shake to log) ----------------------------------------------

static void tap_handler(AccelAxisType axis, int32_t direction) {
  // Only fire when the main window is on top — ignore shakes while the user
  // is browsing stats, settings, or confirmation dialogs.
  if (window_stack_get_top_window() != s_main_window) return;
  confirm_window_push(s_count, on_confirm);
}

// --- Click handlers ----------------------------------------------------------

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  confirm_window_push(s_count, on_confirm);
}

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  stats_window_push();
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  settings_window_push();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void main_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int slot_h    = bounds.size.h / 4;
  int block_y   = bounds.size.h / 6;
  int icon_size = 25;
  int half_w    = bounds.size.w / 2;

  s_count_layer = text_layer_create(GRect(0, block_y, half_w, slot_h));
  text_layer_set_text_alignment(s_count_layer, GTextAlignmentCenter);
  text_layer_set_font(s_count_layer,
                      fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS));
  text_layer_set_background_color(s_count_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_count_layer));

  s_icon_bitmap = gbitmap_create_with_resource(RESOURCE_ID_CIGARETTE);
  s_icon_layer  = bitmap_layer_create(
      GRect(half_w + (half_w - icon_size) / 2,
            block_y + (slot_h - icon_size) / 2,
            icon_size, icon_size));
  bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_icon_layer));

  s_last_time_layer = text_layer_create(
      GRect(0, bounds.size.h * 5 / 12, bounds.size.w, bounds.size.h / 3));
  text_layer_set_text_alignment(s_last_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_last_time_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(s_last_time_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_last_time_layer));

  s_elapsed_layer = text_layer_create(
      GRect(0, bounds.size.h * 3 / 4, bounds.size.w, slot_h));
  text_layer_set_text_alignment(s_elapsed_layer, GTextAlignmentCenter);
  text_layer_set_font(s_elapsed_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  text_layer_set_background_color(s_elapsed_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_elapsed_layer));

  update_display();
}

static void main_window_unload(Window *window) {
  bitmap_layer_destroy(s_icon_layer);
  gbitmap_destroy(s_icon_bitmap);
  text_layer_destroy(s_count_layer);
  text_layer_destroy(s_last_time_layer);
  text_layer_destroy(s_elapsed_layer);
}

// --- App lifecycle -----------------------------------------------------------

static void init(void) {
  storage_load(&s_count, &s_last_time);

  tick_timer_service_subscribe(MINUTE_UNIT, tick_handler);
  accel_tap_service_subscribe(tap_handler);

  s_main_window = window_create();
  window_set_background_color(s_main_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_main_window, click_config_provider);
  window_set_window_handlers(s_main_window, (WindowHandlers) {
    .load   = main_window_load,
    .unload = main_window_unload,
  });
  window_stack_push(s_main_window, true);
}

static void deinit(void) {
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
