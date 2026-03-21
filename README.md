# Zigarettentracker

A Pebble smartwatch app for tracking cigarettes. Log with a button press or wrist shake, see today's count and last-smoke time, and browse daily, hourly, and weekly charts.

Built with Pebble SDK 4.9. Primary targets: **Basalt** (144×168) and **Emery** (200×228), with support for all Pebble platforms.
Get it on the [Pebble App Store](https://apps.repebble.com/25ae7bcfc43540f6b8461f20)

## Features

- **Quick logging** — press SELECT or shake your wrist to log a cigarette
- **Daily goal** — set a target (0–60); color-coded warnings as you approach it
- **Retroactive logging** — log missed cigarettes at past times via the config page
- **Charts & stats**
  - This week: bar chart and area chart
  - By hour: 24-hour histogram
  - Weekly averages: past 8 weeks
  - All-time: total count and daily average
- **Data export** — send history to your phone via the companion config page
- **Animated UI** — smoke animation on confirmation, animated chart transitions

## Navigation

```
Home ──UP──► Week (bar) ──UP──► Week (area) ──UP──► Weekly Averages
│                                    │
│                                    SELECT──► By Hour ──SELECT──► All-Time
│
SELECT/shake──► Confirm (UP=yes, DOWN=cancel)
DOWN──► Settings
```

## Building

Requires the [Pebble SDK](https://developer.rebble.io/developer.pebble.com/sdk/index.html). All source files live in `src/c/` and are auto-globbed by the wscript.

```sh
pebble build
pebble install --phone <IP>
```

## Project Structure

```
src/
├── c/
│   ├── main.c/.h              Home screen
│   ├── storage.c/.h           Persistent data (only file using persist_*)
│   ├── confirm_window.c/.h    Log confirmation dialog
│   ├── settings_window.c/.h   Settings menu
│   ├── stats_window.c/.h      Weekly bar chart
│   ├── trend_window.c/.h      Weekly area chart
│   ├── hourly_window.c/.h     Hourly breakdown
│   ├── alltime_window.c/.h    All-time statistics
│   ├── history_window.c/.h    Weekly averages chart
│   ├── goal_window.c/.h       Daily goal picker
│   ├── dialog_choice_window.c/.h  Generic yes/no dialog
│   ├── area_chart_layer.c/.h  Reusable area chart component
│   ├── graph_layer.c/.h       Horizontal bar chart component
│   ├── selection_layer.c/.h   Digit selector component
│   └── ui_util.c/.h           Shared UI helpers
├── pkjs/
│   └── index.js               Config page & data export
resources/
└── images/                    Icons and animations
```

