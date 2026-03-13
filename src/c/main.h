#pragma once

// Reloads persisted state and refreshes the main window display.
// Called by settings_window after a destructive storage operation.
void main_window_refresh(void);