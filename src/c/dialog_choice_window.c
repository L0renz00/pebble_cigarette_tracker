#include "dialog_choice_window.h"

static Window         *s_dialog_window;
static BitmapLayer    *s_icon_layer;
static TextLayer      *s_label_layer;
static ActionBarLayer *s_action_bar_layer;

static GBitmap *s_icon_bitmap;
static GBitmap *s_tick_bitmap;
static GBitmap *s_cross_bitmap;

static DialogChoiceCallback s_callback;
static char s_message_buf[48];

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

static void dialog_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  // Icon in the upper portion, inset from the action bar on the right
  const GEdgeInsets icon_insets = {
    .top    = bounds.size.h / 12,
    .right  = ACTION_BAR_WIDTH + 4,
    .bottom = bounds.size.h / 2,
    .left   = bounds.size.w / 6
  };
  s_icon_layer = bitmap_layer_create(grect_inset(bounds, icon_insets));
  bitmap_layer_set_bitmap(s_icon_layer, s_icon_bitmap);
  bitmap_layer_set_compositing_mode(s_icon_layer, GCompOpSet);
  layer_add_child(window_layer, bitmap_layer_get_layer(s_icon_layer));

  // Message label in the lower portion
  const GEdgeInsets label_insets = {
    .top    = bounds.size.h / 2,
    .right  = ACTION_BAR_WIDTH,
    .left   = ACTION_BAR_WIDTH / 2
  };
  s_label_layer = text_layer_create(grect_inset(bounds, label_insets));
  text_layer_set_background_color(s_label_layer, GColorClear);
  text_layer_set_text_color(s_label_layer, GColorBlack);
  text_layer_set_text_alignment(s_label_layer, GTextAlignmentCenter);
  text_layer_set_font(s_label_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_text(s_label_layer, s_message_buf);   // owned copy
  layer_add_child(window_layer, text_layer_get_layer(s_label_layer));

  // Action bar
  s_tick_bitmap  = gbitmap_create_with_resource(RESOURCE_ID_TICK);
  s_cross_bitmap = gbitmap_create_with_resource(RESOURCE_ID_CROSS);

  s_action_bar_layer = action_bar_layer_create();
  action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_UP,   s_tick_bitmap);
  action_bar_layer_set_icon(s_action_bar_layer, BUTTON_ID_DOWN, s_cross_bitmap);
  action_bar_layer_set_click_config_provider(s_action_bar_layer,
                                             click_config_provider);
  action_bar_layer_add_to_window(s_action_bar_layer, window);
}

static void dialog_window_unload(Window *window) {
  bitmap_layer_destroy(s_icon_layer);
  text_layer_destroy(s_label_layer);
  action_bar_layer_destroy(s_action_bar_layer);

  gbitmap_destroy(s_icon_bitmap);
  gbitmap_destroy(s_tick_bitmap);
  gbitmap_destroy(s_cross_bitmap);

  window_destroy(s_dialog_window);
  s_dialog_window = NULL;
}

// --- Public API --------------------------------------------------------------

void dialog_choice_window_push(const char *message,
                               uint32_t icon_resource_id,
                               DialogChoiceCallback callback) {
  s_callback = callback;

  // Copy message into the module-owned buffer so the text layer's pointer
  // stays valid regardless of the caller's storage lifetime.
  snprintf(s_message_buf, sizeof(s_message_buf), "%s", message);

  // Load icon bitmap before window_load so it's ready for the BitmapLayer
  s_icon_bitmap = gbitmap_create_with_resource(icon_resource_id);

  s_dialog_window = window_create();
  window_set_background_color(s_dialog_window,
                              PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_window_handlers(s_dialog_window, (WindowHandlers) {
    .load   = dialog_window_load,
    .unload = dialog_window_unload,
  });
  window_stack_push(s_dialog_window, true);
}