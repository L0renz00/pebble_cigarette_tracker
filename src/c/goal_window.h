#pragma once
#include <pebble.h>

// Push a goal-picker window onto the stack.
// current_goal: the value to pre-populate (0 = disabled / not set).
void goal_window_push(int current_goal);
