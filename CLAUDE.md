# Zigarettentracker — Claude Code Guide

Pebble watchapp (SDK 3, C). Logs cigarettes with a button press or wrist shake,
tracks today's count and last-smoke time, and visualises daily/weekly history
across a chain of chart windows. Primary targets: **Basalt** (144×168px, the
physical watch in use) and **Emery** (200×228px, the new device). All other
platforms in `package.json` are bonus coverage.

---

## Build & deploy

This is a CloudPebble project. There is no local build toolchain. All source
files live in `src/c/` in a flat structure — CloudPebble requires this. Upload
changed `.c` / `.h` files individually through the CloudPebble UI and use the
cloud build button. The `wscript` globs all `src/c/**/*.c` automatically, so new
files are picked up without editing it.

---

## File map

### Data layer
| File | Role |
|------|------|
| `storage.h/.c` | **Only place that calls `persist_*`.** All windows go through this API. |

### Shared UI components
| File | Role |
|------|------|
| `ui_util.h/.c` | `ui_rule_update_proc` — the 1px horizontal title-rule drawn under every window title. Assign to any 1px `Layer` via `layer_set_update_proc`. |
| `area_chart_layer.h/.c` | Reusable filled area chart: grow-from-baseline animation, dotted average line, info strip (H/L/anchor labels), ring-dot, bottom labels. Used by W6 and W9. |
| `graph_layer.h/.c` | Horizontal animated bar chart. Used by W5 only. |

### Windows (navigation order)
| ID | File | Title |
|----|------|-------|
| W1 | `main.c/.h` | Home — count + last smoke |
| W2 | `confirm_window.c/.h` | "Log a cigarette?" |
| W3 | `settings_window.c/.h` | Settings menu |
| W4 | `dialog_choice_window.c/.h` | Generic yes/no dialog |
| W5 | `stats_window.c/.h` | This week — bar chart |
| W6 | `trend_window.c/.h` | This week — area chart |
| W7 | `hourly_window.c/.h` | This week — hourly line chart |
| W8 | `alltime_window.c/.h` | All-time stats (3 rows) |
| W9 | `history_window.c/.h` | Weekly averages — area chart |

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
```

---

## Storage schema

Eight persist keys. **Never call `persist_*` outside `storage.c`.**

| Key | Type | Content |
|-----|------|---------|
| 0 `KEY_STORAGE_VERSION` | int32 | Schema version (currently 2). Mismatch wipes all other keys. |
| 1 `KEY_COUNT` | int32 | Today's cigarette count. |
| 2 `KEY_LAST_TIME` | int32 | Unix timestamp of last logged cigarette. |
| 3 `KEY_HISTORY` | `DayEntry[7]` | One slot per weekday (Mon=0…Sun=6), current week only. |
| 4 `KEY_TOTAL` | int32 | All-time cumulative count. |
| 5 `KEY_TOTAL_DAYS` | int32 | Days the app has been opened (for all-time avg). |
| 6 `KEY_WEEK_HISTORY` | `WeekEntry[8]` | Up to 8 completed past weeks. |
| 7 `KEY_HOUR_HISTOGRAM` | `uint8_t[24]` | Per-hour count accumulated across the **current** week. Reset on rollover. |

### Week rollover
`storage_load()` calls `ensure_this_week()` on every app open. If the stored
`DayEntry` timestamps belong to a past week, that week is compressed into a
`WeekEntry` and appended to `KEY_WEEK_HISTORY`, then the 7-slot array and
`KEY_HOUR_HISTOGRAM` are wiped. Week boundaries use Monday-midnight (UTC).

### Call ordering rule
Always call `storage_load()` before `storage_save()` in any given session.
`storage_save()` only writes `KEY_COUNT`, `KEY_LAST_TIME`, and today's slot
count — it does **not** re-initialise the day or increment `KEY_TOTAL_DAYS`.
Day initialisation is `ensure_this_week()`'s exclusive responsibility.

---

## Key patterns

### Layer data pattern
All custom layers use `layer_create_with_data` (not a separate heap allocation):
```c
MyLayer *layer = layer_create_with_data(frame, sizeof(MyLayerData));
MyLayerData *data = (MyLayerData *)layer_get_data(layer);
```
`typedef Layer MyLayer` is the SDK-idiomatic alias — it doesn't change the
type, it just signals intent.

### Window lifecycle
Every window allocates in `*_push()`, destroys itself in its own `_unload`
handler, and sets its module-static pointer to `NULL` after destruction. This
means windows are never cached between pushes — every push is a fresh
`window_create()`.

```c
static void my_window_unload(Window *window) {
  // destroy all child layers first
  window_destroy(s_my_window);
  s_my_window = NULL;
}
```

### Platform-conditional color
Use `PBL_IF_COLOR_ELSE(color_value, GColorBlack)` for fill/stroke colors.
Use `PBL_IF_ROUND_ELSE(round_value, rect_value)` for layout differences.
Basalt and Emery are both rectangular color displays — these macros treat them
identically, which is correct.

### Proportional layout — no hardcoded pixel values
All layout uses `bounds.size.h / N` or `bounds.size.w / N`. This ensures
Basalt (168px tall) and Emery (228px tall) both render correctly without
per-platform branches.

### Animation ownership
A layer that runs an animation owns it completely. Always:
1. Cancel and destroy any in-flight animation before starting a new one.
2. Destroy the animation in the layer's `_destroy()` before calling
   `layer_destroy()`.
```c
if (data->animation) {
    animation_unschedule(data->animation);
    animation_destroy(data->animation);
    data->animation = NULL;
}
```
The `anim_stopped` callback sets `data->animation = NULL` on natural
completion. Always check for NULL before touching the pointer.

### Area chart data preparation
Windows that use `AreaChartLayer` do **all** formatting before handing off to
the layer — see `build_trend_chart_data()` and `build_history_chart_data()`.
The layer only draws; it never reads from storage or formats strings. Keep this
separation: if you add a new chart window, add a `build_*_chart_data()` helper
in the window file.

`AreaChartData` uses integer y-values. `history_window` stores weekly averages
in **tenths** (93 = 9.3 cigs/day) to express one decimal place without floats.
Format back with `val / 10` and `val % 10`.

### Pop-to-main from deep in the stack
Use `main_window_get()` (declared in `main.h`) rather than a hardcoded pop
count:
```c
Window *main = main_window_get();
while (window_stack_get_top_window() != main) {
    window_stack_pop(false);
}
```

---

## Invariants — do not break these

- **`persist_*` only in `storage.c`.** No window touches flash directly.
- **`storage_load()` before `storage_save()`.** Day-init runs only in `load`.
- **`KEY_TOTAL_DAYS` incremented only in `ensure_this_week()`.** It is not
  touched by `storage_save()`.
- **`KEY_HOUR_HISTOGRAM` reset only in `ensure_this_week()`.** The public
  `storage_reset_hour_histogram()` was removed specifically to enforce this.
- **No hardcoded window-stack depths.** Use `main_window_get()` + a loop.
- **All layout proportional.** `bounds.size.h / N`, never raw pixel constants.
- **Layer destroys its own animation before `layer_destroy()`.** No exceptions.

---

## Common pitfalls

**`localtime()` returns a pointer to a static struct.** Two calls in the same
expression will alias. Capture into a local `struct tm` copy if you need two
independent time breakdowns.

**`gpath_create()` allocates on the heap — always call `gpath_destroy()` after
drawing.** The area chart does this inside `area_chart_update_proc`; if you
draw GPath elsewhere, follow the same pattern.

**`animation_get_context(anim)` returns the context passed to
`animation_set_handlers`, not to `animation_set_implementation`.** The update
callback receives `anim`; call `animation_get_context(anim)` to reach the
layer. The stopped callback receives the context directly as `void *context`.
These are different — see `area_chart_layer.c` for the correct pattern.

**`max_val` floors for chart scaling must start at 0, not 1.** Starting at 1
causes the peak-detection loop to miss a real peak of exactly 1 — the ring dot
stays on slot 0 regardless of where the actual peak is. The `hourly_window`
peak detection was bitten by this; it now initialises to 0 and relies on the
`total == 0` guard above it to ensure the divisor is always ≥ 1 by the time
scaling runs.

**`strftime` with `localtime` on Pebble**: always check buffer sizes.
`"%d.%m"` produces 5 chars + NUL = 6 minimum; `"%a %d.%m"` produces up to
9 + NUL. The existing buffers are sized correctly — preserve the sizes if you
copy a pattern.

---

## Adding a new chart window

1. Create `my_window.c/.h` following the pattern in `history_window.c`.
2. Write a `build_my_chart_data(AreaChartData *cd, ...)` helper that fills the
   struct — all formatting here, no formatting in the layer.
3. Use `area_chart_layer_create/set_data/animate_in/destroy` for the chart
   layer.
4. Use `ui_rule_update_proc` for the title rule (1px `Layer`, height=1).
5. Call `layer_set_update_proc(rule_layer, ui_rule_update_proc)` — do not
   define a local copy.
6. Proportional layout: `title_h = bounds.size.h / 6`, `chart_y = title_h + 3`.
7. Destroy order in `_unload`: text layer → rule layer → chart layer →
   `window_destroy` → set pointer to NULL.
