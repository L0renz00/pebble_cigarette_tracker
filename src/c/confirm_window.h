#pragma once
#include <pebble.h>

typedef void (*ConfirmWindowCallback)(bool confirmed);

void confirm_window_push(int current_count, ConfirmWindowCallback callback);