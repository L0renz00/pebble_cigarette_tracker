#pragma once
#include <pebble.h>
#include "storage.h"

// GraphLayer is a plain Layer with bundled data — matches the progress_layer
// pattern from the Pebble ui-patterns examples.
typedef Layer GraphLayer;

GraphLayer *graph_layer_create(GRect frame);
void        graph_layer_destroy(GraphLayer *layer);
void        graph_layer_set_data(GraphLayer *layer,
                                 DayEntry *entries, int num_entries);

// Triggers a staggered bar-grow animation. Call once after layer_add_child.
void        graph_layer_animate_in(GraphLayer *layer);
