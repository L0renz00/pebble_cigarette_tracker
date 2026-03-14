#pragma once
#include <pebble.h>

// A Layer update_proc that draws a single 1px horizontal rule across the full
// width of the layer. Assign this to any 1px-tall Layer used as a section
// divider beneath a window title — avoids copy-pasting the same 5 lines into
// every window file.
void ui_rule_update_proc(Layer *layer, GContext *ctx);
