#pragma once
#include <pebble.h>

// AreaChartLayer — a reusable filled-area chart with grow-from-baseline
// animation, dotted average line, connecting line, ring-dot, and a three-item
// info strip at the top.
//
// Usage:
//   1. Fill an AreaChartData struct (the window does all data prep and text
//      formatting; the layer only draws).
//   2. Call area_chart_layer_set_data() to hand the data to the layer.
//   3. Call area_chart_layer_animate_in() after adding the layer to the tree.
//
// The layer stores a copy of AreaChartData internally, so the caller's struct
// can go out of scope after set_data().

#define AREA_CHART_MAX_SLOTS  24   // max columns (hourly = 24, weekly = 7, history = 8)
#define AREA_CHART_LABEL_LEN   8   // bottom label per slot, e.g. "Mo", "13.03", "6am"
#define AREA_CHART_INFO_LEN   16   // info strip strings, e.g. "H: 9.3", "Peak: 22h-24h"

typedef struct {
  // ---- slot geometry -------------------------------------------------------
  int   total_slots;                                       // total x-columns
  int   n;                                                 // populated slot count
  bool  populated[AREA_CHART_MAX_SLOTS];                   // which slots have data
  int   y[AREA_CHART_MAX_SLOTS];                           // y-value per slot

  // ---- labels (pre-formatted by the window) --------------------------------
  char  bottom_labels[AREA_CHART_MAX_SLOTS][AREA_CHART_LABEL_LEN];
  char  h_label[AREA_CHART_INFO_LEN];       // top-left,    "" = omit
  char  l_label[AREA_CHART_INFO_LEN];       // top-centre,  "" = omit
  char  anchor_label[AREA_CHART_INFO_LEN];  // top-right,   e.g. "13" or "9.3/d"

  // ---- visual --------------------------------------------------------------
  int    ring_idx;              // slot index for the ring-dot; -1 = none
  char   ring_label[8];         // drawn above the ring-dot; "" = omit
  GColor fill_color;
  GColor anchor_color;          // color for anchor_label text
  bool   wide_bottom_labels;    // center each label at slot_cx with a 36px box
                                // (use when slot_w is too narrow for the font)
  bool   hide_avg_line;         // suppress the dotted average line
  bool   hide_dots;             // suppress all data-point dots (and ring-dot)
  bool   larger_labels;         // use GOTHIC_18 labels and bigger ring-dot (hourly chart)
  bool   show_y_axis;           // draw adaptive y-axis labels on the left

  // ---- empty state ---------------------------------------------------------
  const char *empty_message;   // displayed when n == 0; must be a static string
} AreaChartData;

typedef Layer AreaChartLayer;

AreaChartLayer *area_chart_layer_create(GRect frame);
void            area_chart_layer_destroy(AreaChartLayer *layer);
void            area_chart_layer_set_data(AreaChartLayer *layer,
                                          const AreaChartData *data);
void            area_chart_layer_animate_in(AreaChartLayer *layer);
