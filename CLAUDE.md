# Zigarettentracker — Claude Code Guide

Pebble watchapp (SDK 3, C). Logs cigarettes with a button press or wrist shake,
tracks today's count and last-smoke time, and visualises daily/weekly history
across a chain of chart windows. Primary targets: **Basalt** (144×168px, the
physical watch in use) and **Emery** (200×228px, the new device). All other
platforms in `package.json` are bonus coverage.

---

## Build & deploy

Former CloudPebble project — now local. All source files in `src/c/`.
Theoretically, building is possible with the pebble cli. Leave building and testing
to the user. The `wscript` globs all `src/c/**/*.c` automatically; new files need no wscript edit.

---

## File map

### Data layer
| File | Role |
|------|------|
| `storage.h/.c` | **Only place that calls `persist_*`.** All windows go through this API. |

### Shared UI components
| File | Role |
|------|------|
| `ui_util.h/.c` | `ui_rule_update_proc` — 1px horizontal title-rule. Assign via `layer_set_update_proc`. |
| `area_chart_layer.h/.c` | Reusable filled area chart: animation, avg line, info strip, ring-dot, bottom labels. Used by W6, W7, W9. |
| `graph_layer.h/.c` | Horizontal animated bar chart. Used by W5 only. |
| `selection_layer.h/.c` | Digit-cell selector with slide/bump animations. Used by goal_window. Only knows font metrics for `GOTHIC_28_BOLD` and `GOTHIC_24_BOLD` — using other fonts breaks vertical centering. |

### Windows (navigation order)
| ID | File | Title |
|----|------|-------|
| W1 | `main.c/.h` | Home — count + last smoke |
| W2 | `confirm_window.c/.h` | "Log a cigarette?" |
| W3 | `settings_window.c/.h` | Settings menu |
| W4 | `dialog_choice_window.c/.h` | Generic yes/no dialog |
| W5 | `stats_window.c/.h` | This week — bar chart |
| W6 | `trend_window.c/.h` | This week — area chart |
| W7 | `hourly_window.c/.h` | By Hour — area chart |
| W8 | `alltime_window.c/.h` | All-time stats (3 rows) |
| W9 | `history_window.c/.h` | Weekly averages — area chart |
| — | `goal_window.c/.h` | Daily goal picker (SelectionLayer, 2-digit 0–60) |

### Navigation flow
```
W1 ──UP──► W5 ──UP──► W6 ──UP──► W9 (SELECT/DOWN → back)
│               │          │
│               │          SELECT──► W7 ──SELECT──► W8 ──UP──► W9
│               │                             │
│               DOWN──► W1                   DOWN──► W1 (pop-to-main loop)
│
SELECT/shake──► W2 (UP=confirm, DOWN=cancel)
DOWN──► W3 ──SELECT──► W4 (UP=confirm, DOWN=cancel)
            case 4 ──► goal_window (BACK=cancel, SELECT×2=confirm)
```

---

## Storage schema

Nine persist keys. **Never call `persist_*` outside `storage.c`.**

| Key | Type | Content |
|-----|------|---------|
| 0 `KEY_STORAGE_VERSION` | int32 | Schema version (currently **3**). Pre-v2 wipes all data keys; v2→v3 is non-destructive. |
| 1 `KEY_COUNT` | int32 | Today's cigarette count. |
| 2 `KEY_LAST_TIME` | int32 | Unix timestamp of last logged cigarette. |
| 3 `KEY_HISTORY` | `DayEntry[7]` | One slot per weekday (Mon=0…Sun=6), current week only. |
| 4 `KEY_TOTAL` | int32 | All-time cumulative count. |
| 5 `KEY_TOTAL_DAYS` | int32 | Days the app has been opened (for all-time avg). |
| 6 `KEY_WEEK_HISTORY` | `WeekEntry[8]` | Up to 8 completed past weeks. |
| 7 `KEY_HOUR_HISTOGRAM` | `uint8_t[24]` | Per-hour count, current week. Reset on rollover. |
| 8 `KEY_DAILY_GOAL` | int32 | Daily cigarette limit; 0 = disabled. Survives `storage_delete_all()`. |

### Week rollover
`storage_load()` → `ensure_this_week()` on every open. Past-week entries are
archived into a `WeekEntry`, then the 7-slot array and `KEY_HOUR_HISTOGRAM` are
wiped. Week boundaries use Monday-midnight (local time).

### Call ordering rule
Always call `storage_load()` before `storage_save()`. `storage_save()` only
writes `KEY_COUNT`, `KEY_LAST_TIME`, and today's slot count. Day initialisation
and `KEY_TOTAL_DAYS` increment are `ensure_this_week()`'s exclusive responsibility.

---

## Key patterns

### Layer data pattern
```c
MyLayer *layer = layer_create_with_data(frame, sizeof(MyLayerData));
MyLayerData *data = (MyLayerData *)layer_get_data(layer);
```
`typedef Layer MyLayer` is the SDK-idiomatic alias.

### Window lifecycle
Every window: create in `*_push()`, destroy self in `_unload`, set static pointer
to NULL. Every push is a fresh `window_create()` — no caching.

### Platform-conditional color/layout
- `PBL_IF_COLOR_ELSE(color, GColorBlack)` — Basalt and Emery are both color rects.
- `PBL_IF_ROUND_ELSE(round_val, rect_val)` — only Chalk is round.

### Proportional layout
All layout uses `bounds.size.h / N` or `bounds.size.w / N`. Title height is
`bounds.size.h / 7` across all chart windows.

### Animation ownership
Layer owns its animation completely. Cancel + destroy before starting a new one;
destroy before `layer_destroy()`. `anim_stopped` sets pointer to NULL on natural completion.

### Area chart data preparation
Windows do **all** data formatting in a `build_*_chart_data()` helper before
handing the struct to `AreaChartLayer`. The layer only draws.
- Integer y-values. `history_window` uses **tenths** (93 = 9.3 cigs/day).
- `hide_avg_line` and `wide_bottom_labels` must be explicitly set in every caller.

### Pop-to-main
```c
Window *main = main_window_get();
while (window_stack_get_top_window() != main) { window_stack_pop(false); }
```

---

## Invariants — do not break these

- **`persist_*` only in `storage.c`.**
- **`storage_load()` before `storage_save()`.**
- **`KEY_TOTAL_DAYS` incremented only in `ensure_this_week()`.**
- **`KEY_HOUR_HISTOGRAM` reset only in `ensure_this_week()`.**
- **No hardcoded window-stack depths.** Use `main_window_get()` + loop.
- **All layout proportional, using content size api for fonts**
- **Layer destroys its own animation before `layer_destroy()`.**

---

## Common pitfalls

**`localtime()` returns a static pointer.** Capture into a local `struct tm` if
you need two independent time breakdowns.

**`gpath_create()` allocates heap — always `gpath_destroy()` after drawing.**

**`animation_get_context(anim)`** returns the context passed to
`animation_set_handlers`, not `animation_set_implementation`. Update callback
gets `anim`; stopped callback gets `context` directly.

**`max_val` floor for chart scaling must start at 0, not 1.** Starting at 1
misses a real peak of exactly 1.

**`selection_layer.c` font support**: `prv_get_y_offset_*` hardcodes metrics only
for `GOTHIC_28_BOLD` (height=18, top_pad=10) and `GOTHIC_24_BOLD` (height=14,
top_pad=10). Any other font gets offset = `height/2`, placing text in the lower
half of the cell. Always use a supported font with SelectionLayer.

---

## Adding a new chart window

1. Create `my_window.c/.h` following `history_window.c`.
2. Write `build_my_chart_data(AreaChartData *cd, ...)` — all formatting here.
3. Use `area_chart_layer_create/set_data/animate_in/destroy`.
4. 1px rule: `layer_set_update_proc(rule_layer, ui_rule_update_proc)`.
5. Layout: `title_h = bounds.size.h / 7`, `chart_y = title_h + 3`.
6. Set `cd->hide_avg_line` and `cd->wide_bottom_labels` explicitly.
7. Destroy order: text → rule → chart → `window_destroy` → NULL.
