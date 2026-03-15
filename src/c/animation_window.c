#include <pebble.h>
#include "animation_window.h"

static Window               *s_anim_window;
static Layer                *s_canvas_layer;
static GDrawCommandSequence *s_sequence;
static int                   s_frame_idx;
static AppTimer             *s_timer;

// Forward declaration so schedule_next_frame() can reference advance_frame().
static void advance_frame(void *context);

static void schedule_next_frame(void) {
  GDrawCommandFrame *frame =
      gdraw_command_sequence_get_frame_by_index(s_sequence, s_frame_idx);
  uint32_t dur = frame ? gdraw_command_frame_get_duration(frame) : 250;
  if (dur == 0) dur = 250;
  s_timer = app_timer_register(dur, advance_frame, NULL);
}

static void advance_frame(void *context) {
  s_timer = NULL;
  int n = gdraw_command_sequence_get_num_frames(s_sequence);
  s_frame_idx++;
  if (s_frame_idx >= n) {
    window_stack_pop(true);
    return;
  }
  layer_mark_dirty(s_canvas_layer);
  schedule_next_frame();
}

static void canvas_update_proc(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  GSize size   = gdraw_command_sequence_get_bounds_size(s_sequence);
  GPoint origin = GPoint((bounds.size.w - size.w) / 2,
                         (bounds.size.h - size.h) / 2);
  GDrawCommandFrame *frame =
      gdraw_command_sequence_get_frame_by_index(s_sequence, s_frame_idx);
  if (frame) {
    gdraw_command_frame_draw(ctx, s_sequence, frame, origin);
  }
}

static void anim_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect  bounds       = layer_get_unobstructed_bounds(window_layer);

  s_sequence  = gdraw_command_sequence_create_with_resource(
      RESOURCE_ID_CIGARETTE_SMOKE);
  s_frame_idx = 0;

  s_canvas_layer = layer_create(bounds);
  layer_set_update_proc(s_canvas_layer, canvas_update_proc);
  layer_add_child(window_layer, s_canvas_layer);

  schedule_next_frame();
}

static void anim_window_unload(Window *window) {
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
  layer_destroy(s_canvas_layer);
  gdraw_command_sequence_destroy(s_sequence);
  window_destroy(s_anim_window);
  s_anim_window = NULL;
}

static void back_click_handler(ClickRecognizerRef recognizer, void *context) {
  window_stack_pop(true);
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_BACK, back_click_handler);
}

void animation_window_push(void) {
  s_anim_window = window_create();
  window_set_background_color(s_anim_window,
      PBL_IF_COLOR_ELSE(GColorChromeYellow, GColorWhite));
  window_set_click_config_provider(s_anim_window, click_config_provider);
  window_set_window_handlers(s_anim_window, (WindowHandlers){
    .load   = anim_window_load,
    .unload = anim_window_unload,
  });
  window_stack_push(s_anim_window, true);
}
