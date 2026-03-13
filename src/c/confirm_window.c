#include "confirm_window.h"

static Window          *s_confirm_window;
static TextLayer       *s_label_layer;
static ActionBarLayer  *s_action_bar_layer;

static GBitmap *s_tick_bitmap;
static GBitmap *s_cross_bitmap;

static ConfirmWindowCallback s_callback;

// Label buffer — lives as long as the window
static char s_label_buf[32];

// --- Click handlers ----------------------------------------------------------

static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
  if (s_callback) s_callback(true);
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
  if (s_callback) s_callback(false);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,   up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN, down_click_handler);
}

// --- Window lifecycle --------------------------------------------------------

static void window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  const GEdgeInsets label_insets = {
    .top    = bounds.size.h / 4,
    .bottom = bounds.size.h / 4,
    .right  = ACTION_BAR_WIDTH,
    .left   = ACTION_BAR_WIDTH / 2
  };
  s_label_layer = text_layer_create(grect_inset(bounds, label_insets));
  text_layer_set_text(s_label_layer, s_label_buf);
  text_layer_set_background_color(s_label_layer, GColorClear);
  text_layer_set_text_color(s_label_layer, GColorBlack);
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_font(s_label_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
  layer_add_child(window_layer, text_layer_get_layer(s_label_layer));

  s_tick_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_TICK);
  s_cross_bitmap = gbitmap_create_with_resource(RESOURCE_ID_CROSS);

  s_action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP,   s_tick_bitmap);
  action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, s_cross_bitmap);
  action_bar_layer_set_click_config_provider(s_action_bar_layer,
                                             click_config_provider);
  action_bar_layer_add_to_window(s_action_bar_layer, window);
}

static void window_unload(Window *window) {
  text_layer_destroy(s_label_layer);
  action_bar_layer_destroy(s_action_bar_layer);
  gbitmap_destroy(s_tick_bitmap);
  gbitmap_destroy(s_cross_bitmap);
  window_destroy(s_confirm_window);
  s_confirm_window = NULL;
}

// --- Public API --------------------------------------------------------------

void confirm_window_push(int current_count, ConfirmWindowCallback callback) {
  s_callback = callback;
  snprintf(s_label_buf, sizeof(s_label_buf), "Log a\ncigarette?\n(%d today)", current_count);

  if (!s_confirm_window) {
    s_confirm_window = window_create();
    window_set_background_color(s_confirm_window,
                    PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
    window_set_window_handlers(s_confirm_window, (WindowHandlers) {
      .load   = window_load,
      .unload = window_unload,
    });
  }
  window_stack_push(s_confirm_window, true);
}