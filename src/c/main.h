#pragma once

// Reloads persisted state and refreshes the main window display.
// Called by settings_window after a destructive storage operation.
void main_window_refresh(void);
//
// Returns the main window pointer so other windows can pop back to it
// without hardcoding a stack depth.
Window *main_window_get(void);
