#include <pebble.h>
#include "settings_window.h"
#include "storage.h"
#include "dialog_choice_window.h"
#include "goal_window.h"
#include "main.h"

#define SETTINGS_NUM_ROWS    3
#define SETTINGS_CELL_HEIGHT 44

static Window    *s_settings_window;
static MenuLayer *s_menu_layer;
static TextLayer *s_hint_layer;

// --- Confirmation callbacks --------------------------------------------------

static void on_delete_today_confirmed(bool confirmed) {
  if (confirmed) {
    storage_delete_today();
    main_window_refresh();
    window_stack_pop(true);
  }
}

static void on_delete_all_confirmed(bool confirmed) {
  if (confirmed) {
    storage_delete_all();
    main_window_refresh();
    window_stack_pop(true);
  }
}

// --- MenuLayer callbacks -----------------------------------------------------

static uint16_t get_num_rows_callback(MenuLayer *menu_layer,
                                      uint16_t section_index, void *context) {
  return SETTINGS_NUM_ROWS;
}

static void draw_row_callback(GContext *ctx, const Layer *cell_layer,
                               MenuIndex *cell_index, void *context) {
  switch (cell_index->row) {
    case 0:
      menu_cell_basic_draw(ctx, cell_layer, "Delete Today", NULL, NULL);
      break;
    case 1:
      menu_cell_basic_draw(ctx, cell_layer, "Delete All Data", NULL, NULL);
      break;
    case 2: {
      static char goal_sub[16];
      int32_t g = storage_get_goal();
      if (g > 0) snprintf(goal_sub, sizeof(goal_sub), "Current: %d", (int)g);
      else        snprintf(goal_sub, sizeof(goal_sub), "Not set");
      menu_cell_basic_draw(ctx, cell_layer, "Daily Goal", goal_sub, NULL);
      break;
    }
    default:
      break;
  }
}

static int16_t get_cell_height_callback(struct MenuLayer *menu_layer,
                                         MenuIndex *cell_index, void *context) {
  return PBL_IF_ROUND_ELSE(
    menu_layer_is_index_selected(menu_layer, cell_index)
      ? MENU_CELL_ROUND_FOCUSED_SHORT_CELL_HEIGHT
      : MENU_CELL_ROUND_UNFOCUSED_TALL_CELL_HEIGHT,
    SETTINGS_CELL_HEIGHT);
}

static void select_callback(struct MenuLayer *menu_layer,
                             MenuIndex *cell_index, void *context) {
  switch (cell_index->row) {
    case 0:
      dialog_choice_window_push(
        "Delete today's data?",
        RESOURCE_ID_WARNING,
        on_delete_today_confirmed);
      break;
    case 1:
      dialog_choice_window_push(
        "Delete ALL data?",
        RESOURCE_ID_WARNING,
        on_delete_all_confirmed);
      break;
    case 2:
      goal_window_push((int)storage_get_goal());
      break;
    default:
      break;
  }
}

// --- Window lifecycle --------------------------------------------------------

static void settings_window_load(Window *window) {
  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_unobstructed_bounds(window_layer);

  int menu_height = bounds.size.h - 30;

  s_menu_layer = menu_layer_create(
      GRect(0, 0, bounds.size.w, menu_height));
  menu_layer_set_click_config_onto_window(s_menu_layer, window);
#if defined(PBL_COLOR)
  menu_layer_set_normal_colors(s_menu_layer, GColorWhite, GColorBlack);
  menu_layer_set_highlight_colors(s_menu_layer, GColorChromeYellow, GColorBlack);
#endif
  menu_layer_set_callbacks(s_menu_layer, NULL, (MenuLayerCallbacks) {
    .get_num_rows    = get_num_rows_callback,
    .draw_row        = draw_row_callback,
    .get_cell_height = get_cell_height_callback,
    .select_click    = select_callback,
  });
  layer_add_child(window_layer, menu_layer_get_layer(s_menu_layer));

  // Hint label below the menu
  const GEdgeInsets hint_insets = { .top = bounds.size.h - 30 };
  s_hint_layer = text_layer_create(grect_inset(bounds, hint_insets));
  text_layer_set_text(s_hint_layer, "Settings");
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_font(s_hint_layer,
                      fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  layer_add_child(window_layer, text_layer_get_layer(s_hint_layer));
}

static void settings_window_unload(Window *window) {
  menu_layer_destroy(s_menu_layer);
  text_layer_destroy(s_hint_layer);
  window_destroy(s_settings_window);
  s_settings_window = NULL;
}

// --- Public API --------------------------------------------------------------

void settings_window_push(void) {
  s_settings_window = window_create();
  window_set_window_handlers(s_settings_window, (WindowHandlers) {
    .load   = settings_window_load,
    .unload = settings_window_unload,
  });
  window_stack_push(s_settings_window, true);
}