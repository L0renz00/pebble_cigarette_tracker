#include <pebble.h>
#include "storage.h"
#include "trend_window.h"
#include "confirm_window.h"
#include "settings_window.h"
#include "animation_window.h"
#include "main.h"

static Window      *s_main_window;
static BitmapLayer *s_icon_layer;
static GBitmap     *s_icon_bitmap;
static TextLayer   *s_count_layer;
static TextLayer   *s_goal_layer;
static TextLayer   *s_last_time_layer;
static TextLayer   *s_clock_layer;

static int    s_count     = 0;
static time_t s_last_time = 0;
static int32_t s_goal     = 0;

static char s_count_buf[16];
static char s_goal_buf[12];
static char s_last_time_buf[40];
static char s_clock_buf[8];

// --- Display -----------------------------------------------------------------

static void update_clock(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  strftime(s_clock_buf, sizeof(s_clock_buf), "%H:%M", t);
  text_layer_set_text(s_clock_layer, s_clock_buf);
}

static void update_display(void) {
  // --- Goal color coding -----------------------------------------------
  // Under by more than 2: black (no warning)
  // Within 2 of goal:     orange (approaching)
  // At or over goal:      red    (limit reached)
  GColor accent = GColorBlack;
  if (s_goal > 0) {
    if (s_count >= s_goal)
      accent = PBL_IF_COLOR_ELSE(GColorRed, GColorBlack);
    else if (s_count >= s_goal - 2)
      accent = PBL_IF_COLOR_ELSE(GColorOrange, GColorBlack);
  }

  // --- Count -----------------------------------------------------------
  snprintf(s_count_buf, sizeof(s_count_buf), "%d", s_count);
  text_layer_set_text(s_count_layer, s_count_buf);
  text_layer_set_text_color(s_count_layer, accent);

  // --- Goal layer (replaces icon when goal is set) ---------------------
  if (s_goal > 0) {
    snprintf(s_goal_buf, sizeof(s_goal_buf), "of %d", (int)s_goal);
    text_layer_set_text_color(s_goal_layer, accent);
    text_layer_set_text(s_goal_layer, s_goal_buf);
    layer_set_hidden(text_layer_get_layer(s_goal_layer), false);
    layer_set_hidden(bitmap_layer_get_layer(s_icon_layer), true);
  } else {
    layer_set_hidden(text_layer_get_layer(s_goal_layer), true);
    layer_set_hidden(bitmap_layer_get_layer(s_icon_layer), false);
  }

  // --- Last smoked / elapsed -------------------------------------------
  if (s_last_time == 0) {
    text_layer_set_text(s_last_time_layer, "Last Smoked @\n--:--");
  } else {
    struct tm *t = localtime(&s_last_time);
    strftime(s_last_time_buf, sizeof(s_last_time_buf),
             "Last Smoked @\n%H:%M", t);
    text_layer_set_text(s_last_time_layer, s_last_time_buf);
  }

}

void main_window_refresh(void) {
  storage_load(&s_count, &s_last_time);
  s_goal = storage_get_goal();
  update_display();
}

Window *main_window_get(void) {
  return s_main_window;
}

// --- Tick handler ------------------------------------------------------------

static void tick_handler(struct tm *tick_time, TimeUnits units_changed) {
  update_clock();
}

// --- Confirm callback --------------------------------------------------------

static void on_confirm(bool confirmed) {
  if (confirmed) {
    time_t now = time(NULL);
    RetroResult res = storage_log_at(now);
    if (res.is_today)          s_count++;
    if (res.updated_last_time) s_last_time = now;
    update_display();
    vibes_short_pulse();
    animation_window_push();
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
  trend_window_push();
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

  GFont label_font, value_font;
  switch (preferred_content_size()) {
    case PreferredContentSizeLarge:
    case PreferredContentSizeExtraLarge:
      label_font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
      value_font = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
      break;
    default:
      label_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
      value_font = fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD);
      break;
  }

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

  // Goal layer occupies the same right-half slot as the icon; they are
  // mutually exclusive — update_display() shows one and hides the other.
  s_goal_layer = text_layer_create(GRect(half_w, block_y, half_w, slot_h));
  text_layer_set_text_alignment(s_goal_layer, GTextAlignmentCenter);
  text_layer_set_font(s_goal_layer, label_font);
  text_layer_set_background_color(s_goal_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_goal_layer));

  s_last_time_layer = text_layer_create(
      GRect(0, bounds.size.h * 5 / 12, bounds.size.w, bounds.size.h / 3));
  text_layer_set_text_alignment(s_last_time_layer, GTextAlignmentCenter);
  text_layer_set_font(s_last_time_layer, value_font);
  text_layer_set_background_color(s_last_time_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_last_time_layer));

  s_clock_layer = text_layer_create(GRect(0, 2, bounds.size.w - 4, 18));
  text_layer_set_text_alignment(s_clock_layer, GTextAlignmentCenter);
  text_layer_set_font(s_clock_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_background_color(s_clock_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_clock_layer));

  update_display();
  update_clock();
}

static void main_window_unload(Window *window) {
  bitmap_layer_destroy(s_icon_layer);
  gbitmap_destroy(s_icon_bitmap);
  text_layer_destroy(s_count_layer);
  text_layer_destroy(s_goal_layer);
  text_layer_destroy(s_last_time_layer);
  text_layer_destroy(s_clock_layer);
}

// --- AppMessage (retroactive logging + data export) --------------------------

static int       s_export_step    = -1;
static int       s_export_n_days  = 0;
static int       s_export_n_weeks = 0;
static DayEntry  s_export_days[HISTORY_DAYS];
static WeekEntry s_export_weeks[WEEK_HISTORY_COUNT];

static void export_send_step(void) {
  if (s_export_step < 0) return;

  DictionaryIterator *iter;
  if (app_message_outbox_begin(&iter) != APP_MSG_OK) return;

  int done_step = 2 + s_export_n_days + s_export_n_weeks;

  if (s_export_step == 0) {
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_TYPE, 0);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_A, storage_get_total());
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_B, storage_get_total_days());
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_C, storage_get_goal());
  } else if (s_export_step == 1) {
    uint8_t hist[24];
    storage_get_hour_histogram(hist);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_TYPE, 3);
    dict_write_data(iter, MESSAGE_KEY_EXPORT_HOURS, hist, sizeof(hist));
  } else if (s_export_step < 2 + s_export_n_days) {
    int i = s_export_step - 2;
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_TYPE, 1);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_IDX, i);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_A, s_export_days[i].day_timestamp);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_B, s_export_days[i].count);
  } else if (s_export_step < done_step) {
    int i = s_export_step - 2 - s_export_n_days;
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_TYPE, 2);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_IDX, i);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_A, s_export_weeks[i].week_timestamp);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_B, s_export_weeks[i].total);
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_C, s_export_weeks[i].days_active);
  } else {
    dict_write_int32(iter, MESSAGE_KEY_EXPORT_TYPE, 4);
    s_export_step = -1;
  }

  app_message_outbox_send();
}

static void outbox_sent_handler(DictionaryIterator *iter, void *context) {
  if (s_export_step < 0) return;
  s_export_step++;
  export_send_step();
}

static void inbox_received_handler(DictionaryIterator *iter, void *context) {
  Tuple *req = dict_find(iter, MESSAGE_KEY_EXPORT_REQUEST);
  if (req) {
    storage_get_history(s_export_days, &s_export_n_days);
    storage_get_week_history(s_export_weeks, &s_export_n_weeks);
    s_export_step = 0;
    export_send_step();
    return;
  }

  Tuple *t = dict_find(iter, MESSAGE_KEY_RETROACTIVE_TIMESTAMP);
  if (!t) return;

  time_t retro_ts = (time_t)t->value->int32;
  RetroResult res = storage_log_at(retro_ts);

  if (res.is_today) {
    s_count++;
  }
  if (res.updated_last_time) {
    s_last_time = retro_ts;
  }
  if (window_stack_get_top_window() == s_main_window) {
    update_display();
  }
  vibes_short_pulse();
}

// --- App lifecycle -----------------------------------------------------------

static void init(void) {
  storage_load(&s_count, &s_last_time);
  s_goal = storage_get_goal();

  app_message_register_inbox_received(inbox_received_handler);
  app_message_register_outbox_sent(outbox_sent_handler);
  app_message_open(128, 128);

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
  app_message_deregister_callbacks();
  tick_timer_service_unsubscribe();
  accel_tap_service_unsubscribe();
  window_destroy(s_main_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
