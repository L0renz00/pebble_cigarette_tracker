#pragma once
#include <pebble.h>

typedef void (*DialogChoiceCallback)(bool confirmed);

// Push a choice dialog with a warning icon, message text, and tick/cross
// action bar. callback(true) is called if UP is pressed, callback(false) if
// DOWN is pressed. The dialog pops itself before invoking the callback.
void dialog_choice_window_push(const char *message,
                               uint32_t icon_resource_id,
                               DialogChoiceCallback callback);