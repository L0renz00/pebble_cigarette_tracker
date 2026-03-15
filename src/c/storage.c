#include "storage.h"
#include <string.h>

#define KEY_STORAGE_VERSION  0
#define KEY_COUNT            1
#define KEY_LAST_TIME        2
#define KEY_HISTORY          3
#define KEY_TOTAL            4
#define KEY_TOTAL_DAYS       5
#define KEY_WEEK_HISTORY     6   // WeekEntry[WEEK_HISTORY_COUNT]
#define KEY_HOUR_HISTOGRAM   7   // uint8_t[24] — cigarettes per hour, this week
#define KEY_DAILY_GOAL       8   // int32 — user's daily cigarette limit; 0 = disabled

// --- Internal helpers --------------------------------------------------------

static time_t get_day_start(time_t t) {
  struct tm *tm_info = localtime(&t);
  tm_info->tm_hour = 0;
  tm_info->tm_min  = 0;
  tm_info->tm_sec  = 0;
  return mktime(tm_info);
}

static time_t get_week_start(time_t t) {
  time_t day = get_day_start(t);
  struct tm *tm_info = localtime(&day);
  int days_since_monday = (tm_info->tm_wday + 6) % 7;
  return day - (time_t)(days_since_monday * 24 * 60 * 60);
}

static void load_history_raw(DayEntry *entries) {
  if (persist_exists(KEY_HISTORY)) {
    persist_read_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);
  } else {
    memset(entries, 0, sizeof(DayEntry) * HISTORY_DAYS);
  }
}

static void archive_week(DayEntry *entries, time_t old_week_start) {
  WeekEntry weeks[WEEK_HISTORY_COUNT];
  int n = 0;

  if (persist_exists(KEY_WEEK_HISTORY)) {
    persist_read_data(KEY_WEEK_HISTORY, weeks,
                      sizeof(WeekEntry) * WEEK_HISTORY_COUNT);
    for (n = 0; n < WEEK_HISTORY_COUNT; n++) {
      if (weeks[n].week_timestamp == 0) break;
    }
  } else {
    memset(weeks, 0, sizeof(weeks));
  }

  int32_t week_total  = 0;
  int32_t days_active = 0;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (entries[i].day_timestamp != 0) {
      week_total  += entries[i].count;
      days_active += 1;
    }
  }

  if (days_active == 0) return;

  WeekEntry new_entry = {
    .week_timestamp = (int32_t)old_week_start,
    .total          = week_total,
    .days_active    = days_active,
  };

  if (n < WEEK_HISTORY_COUNT) {
    weeks[n] = new_entry;
  } else {
    memmove(&weeks[0], &weeks[1],
            sizeof(WeekEntry) * (WEEK_HISTORY_COUNT - 1));
    weeks[WEEK_HISTORY_COUNT - 1] = new_entry;
  }

  persist_write_data(KEY_WEEK_HISTORY, weeks,
                     sizeof(WeekEntry) * WEEK_HISTORY_COUNT);
}

static void ensure_this_week(DayEntry *entries) {
  time_t now         = time(NULL);
  time_t today_start = get_day_start(now);
  time_t week_start  = get_week_start(now);

  bool   is_this_week      = false;
  time_t stored_week_start = 0;

  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (entries[i].day_timestamp != 0) {
      time_t ts         = (time_t)entries[i].day_timestamp;
      time_t entry_week = get_week_start(ts);
      if (entry_week == week_start) {
        is_this_week = true;
        break;
      } else if (stored_week_start == 0) {
        stored_week_start = entry_week;
      }
    }
  }

  if (!is_this_week) {
    if (stored_week_start != 0) {
      archive_week(entries, stored_week_start);
    }
    memset(entries, 0, sizeof(DayEntry) * HISTORY_DAYS);
    // New week — the hourly histogram belongs to the previous week; reset it.
    persist_delete(KEY_HOUR_HISTOGRAM);
  }

  int today_index = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_index < 0 || today_index >= HISTORY_DAYS) today_index = 0;

  if ((time_t)entries[today_index].day_timestamp != today_start) {
    entries[today_index].day_timestamp = (int32_t)today_start;
    entries[today_index].count         = 0;
    int32_t total_days = persist_exists(KEY_TOTAL_DAYS)
                         ? persist_read_int(KEY_TOTAL_DAYS) : 0;
    persist_write_int(KEY_TOTAL_DAYS, total_days + 1);
  }
}

static void check_version(void) {
  int stored = persist_exists(KEY_STORAGE_VERSION)
               ? persist_read_int(KEY_STORAGE_VERSION) : 0;

  if (stored == STORAGE_VERSION) return;

  if (stored < 2) {
    // Pre-v2 schema is incompatible — wipe all data keys and start fresh.
    // This path is only taken on a first install or very old build; it does
    // NOT apply to users already running v2 (who upgrade to v3 below).
    persist_delete(KEY_COUNT);
    persist_delete(KEY_LAST_TIME);
    persist_delete(KEY_HISTORY);
    persist_delete(KEY_TOTAL);
    persist_delete(KEY_TOTAL_DAYS);
    persist_delete(KEY_WEEK_HISTORY);
    persist_delete(KEY_HOUR_HISTOGRAM);
    // KEY_DAILY_GOAL absent → storage_get_goal() returns 0 (disabled)
  }

  // v2 → v3: the only addition is KEY_DAILY_GOAL.  All existing keys are
  // untouched.  storage_get_goal() handles the absent key gracefully.

  persist_write_int(KEY_STORAGE_VERSION, STORAGE_VERSION);
}

// --- Public API --------------------------------------------------------------

void storage_save(int count, time_t last_time) {
  persist_write_int(KEY_COUNT,     (int32_t)count);
  persist_write_int(KEY_LAST_TIME, (int32_t)last_time);

  // Today's slot is already stamped by ensure_this_week() inside storage_load(),
  // which is always called before any save. We only need to update the count.
  DayEntry entries[HISTORY_DAYS];
  load_history_raw(entries);

  time_t today_start = get_day_start(time(NULL));
  time_t week_start  = get_week_start(time(NULL));
  int today_index    = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_index < 0 || today_index >= HISTORY_DAYS) today_index = 0;

  entries[today_index].count = (int32_t)count;
  persist_write_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);
}

void storage_load(int *count, time_t *last_time) {
  check_version();

  DayEntry entries[HISTORY_DAYS];
  load_history_raw(entries);
  ensure_this_week(entries);
  persist_write_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);

  time_t today_start = get_day_start(time(NULL));
  time_t week_start  = get_week_start(time(NULL));
  int today_index    = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_index < 0 || today_index >= HISTORY_DAYS) today_index = 0;

  *count     = (int)entries[today_index].count;
  *last_time = persist_exists(KEY_LAST_TIME)
               ? (time_t)persist_read_int(KEY_LAST_TIME) : 0;
}

void storage_get_history(DayEntry *entries, int *num_entries) {
  DayEntry all[HISTORY_DAYS];
  load_history_raw(all);
  int n = 0;
  for (int i = 0; i < HISTORY_DAYS; i++) {
    if (all[i].day_timestamp != 0) entries[n++] = all[i];
  }
  *num_entries = n;
}

void storage_get_week_history(WeekEntry *entries, int *num_entries) {
  WeekEntry all[WEEK_HISTORY_COUNT];
  int n = 0;
  if (persist_exists(KEY_WEEK_HISTORY)) {
    persist_read_data(KEY_WEEK_HISTORY, all,
                      sizeof(WeekEntry) * WEEK_HISTORY_COUNT);
    for (int i = 0; i < WEEK_HISTORY_COUNT; i++) {
      if (all[i].week_timestamp != 0) entries[n++] = all[i];
    }
  }
  *num_entries = n;
}

void storage_increment_total(void) {
  int32_t total = persist_exists(KEY_TOTAL) ? persist_read_int(KEY_TOTAL) : 0;
  persist_write_int(KEY_TOTAL, total + 1);
}

int32_t storage_get_total(void) {
  return persist_exists(KEY_TOTAL) ? persist_read_int(KEY_TOTAL) : 0;
}

int32_t storage_get_total_days(void) {
  return persist_exists(KEY_TOTAL_DAYS) ? persist_read_int(KEY_TOTAL_DAYS) : 0;
}

void storage_delete_today(void) {
  DayEntry entries[HISTORY_DAYS];
  load_history_raw(entries);

  time_t today_start = get_day_start(time(NULL));
  time_t week_start  = get_week_start(time(NULL));
  int today_index    = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_index < 0 || today_index >= HISTORY_DAYS) today_index = 0;

  int32_t today_count = entries[today_index].count;
  int32_t total = persist_exists(KEY_TOTAL) ? persist_read_int(KEY_TOTAL) : 0;
  total -= today_count;
  if (total < 0) total = 0;
  persist_write_int(KEY_TOTAL, total);

  entries[today_index].count = 0;
  persist_write_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);
  persist_write_int(KEY_COUNT,     0);
  persist_write_int(KEY_LAST_TIME, 0);
}

void storage_delete_all(void) {
  persist_delete(KEY_COUNT);
  persist_delete(KEY_LAST_TIME);
  persist_delete(KEY_HISTORY);
  persist_delete(KEY_TOTAL);
  persist_delete(KEY_TOTAL_DAYS);
  persist_delete(KEY_WEEK_HISTORY);
  persist_delete(KEY_HOUR_HISTOGRAM);
  // Intentionally keep KEY_STORAGE_VERSION and KEY_DAILY_GOAL — the user's
  // goal setting should survive a data wipe.
}

// --- Hour histogram ----------------------------------------------------------

void storage_log_hour(int hour) {
  if (hour < 0 || hour > 23) return;
  uint8_t hist[24];
  if (persist_exists(KEY_HOUR_HISTOGRAM)) {
    persist_read_data(KEY_HOUR_HISTOGRAM, hist, sizeof(hist));
  } else {
    memset(hist, 0, sizeof(hist));
  }
  if (hist[hour] < 255) hist[hour]++;
  persist_write_data(KEY_HOUR_HISTOGRAM, hist, sizeof(hist));
}

void storage_get_hour_histogram(uint8_t *out_24) {
  if (persist_exists(KEY_HOUR_HISTOGRAM)) {
    persist_read_data(KEY_HOUR_HISTOGRAM, out_24, 24);
  } else {
    memset(out_24, 0, 24);
  }
}



// --- Daily goal --------------------------------------------------------------

void storage_set_goal(int goal) {
  persist_write_int(KEY_DAILY_GOAL, (int32_t)goal);
}

int32_t storage_get_goal(void) {
  return persist_exists(KEY_DAILY_GOAL) ? persist_read_int(KEY_DAILY_GOAL) : 0;
}

// --- Seed / week start -------------------------------------------------------

void storage_seed_debug_data(void) {
  // Mon–Fri ~13–15 cigs, Sat–Sun ~10–11 — typical moderate smoker pattern.
  const int fake_counts[HISTORY_DAYS] = { 14, 13, 15, 12, 16, 11, 10 };
  time_t now         = time(NULL);
  time_t today_start = get_day_start(now);
  time_t week_start  = get_week_start(now);
  int today_slot = (int)((today_start - week_start) / (24 * 60 * 60));
  if (today_slot < 0 || today_slot >= HISTORY_DAYS) today_slot = 6;

  DayEntry entries[HISTORY_DAYS];
  for (int i = 0; i < HISTORY_DAYS; i++) {
    entries[i].day_timestamp = (i <= today_slot)
        ? (int32_t)(week_start + (time_t)(i * 24 * 60 * 60)) : 0;
    entries[i].count = (i <= today_slot) ? (int32_t)fake_counts[i] : 0;
  }
  persist_write_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);

  // 7 prior weeks, Mon–Sun.  Weekdays 11–16, weekends 8–12.
  // Slight upward drift toward the present is intentional — makes the
  // weekly-averages chart visually interesting with a clear trend.
  const int prior_week_data[7][7] = {
    { 10, 11,  9, 12, 10,  8,  8 },  // 7 weeks ago  avg ~9.7
    { 11, 12, 10, 13, 11,  9,  8 },  // 6 weeks ago  avg ~10.6
    { 12, 11, 13, 12, 14,  9,  9 },  // 5 weeks ago  avg ~11.4
    { 13, 12, 14, 11, 13, 10,  9 },  // 4 weeks ago  avg ~11.7
    { 14, 13, 12, 15, 13, 11, 10 },  // 3 weeks ago  avg ~12.6
    { 13, 15, 14, 13, 16, 11, 10 },  // 2 weeks ago  avg ~13.1
    { 14, 13, 15, 14, 16, 11, 10 },  // last week    avg ~13.3
  };

  WeekEntry weeks[WEEK_HISTORY_COUNT];
  memset(weeks, 0, sizeof(weeks));
  for (int w = 0; w < 7; w++) {
    time_t ws = week_start - (time_t)((7 - w) * 7 * 24 * 60 * 60);
    int32_t total = 0;
    for (int d = 0; d < 7; d++) total += prior_week_data[w][d];
    weeks[w].week_timestamp = (int32_t)ws;
    weeks[w].total          = total;
    weeks[w].days_active    = 7;
  }
  persist_write_data(KEY_WEEK_HISTORY, weeks,
                     sizeof(WeekEntry) * WEEK_HISTORY_COUNT);

  int32_t week_sum = 0;
  for (int i = 0; i <= today_slot; i++) week_sum += fake_counts[i];
  int32_t prior_total = 0;
  for (int w = 0; w < 7; w++)
    for (int d = 0; d < 7; d++) prior_total += prior_week_data[w][d];
  persist_write_int(KEY_TOTAL,      week_sum + prior_total);
  persist_write_int(KEY_TOTAL_DAYS, (7 * 7) + today_slot + 1);
  persist_write_int(KEY_COUNT,      fake_counts[today_slot]);
  // Last cigarette ~25 minutes ago — plausible mid-session gap.
  persist_write_int(KEY_LAST_TIME,  (int32_t)(time(NULL) - 25 * 60));

  // Hourly histogram — weekly totals for a ~13 cig/day moderate smoker.
  // Four natural peaks: morning routine (7h), work break (10h),
  // lunch (12h), post-work (17h).  Near-zero midnight–6h (sleep).
  const uint8_t fake_hist[24] = {
    0, 0, 0, 0, 0, 1,   //  0– 5h  asleep
    2, 5,               //  6– 7h  wake-up, first cigarette
    6, 7, 9, 6,         //  8–11h  morning work block, peak at 10h
    8, 6,               // 12–13h  lunch break
    6, 7, 7,            // 14–16h  afternoon work block
    9, 7, 6,            // 17–19h  post-work / commute peak
    5, 4, 3, 1,         // 20–23h  evening taper
  };
  persist_write_data(KEY_HOUR_HISTOGRAM, fake_hist, sizeof(fake_hist));
}

time_t storage_get_week_start(void) {
  return get_week_start(time(NULL));
}

// --- Retroactive logging -----------------------------------------------------

RetroResult storage_log_at(time_t retro_ts) {
  RetroResult result = { .is_today = false, .updated_last_time = false };
  time_t now = time(NULL);
  if (retro_ts <= 0 || retro_ts > now) return result;

  time_t today_start      = get_day_start(now);
  time_t week_start       = get_week_start(now);
  time_t retro_day_start  = get_day_start(retro_ts);
  time_t retro_week_start = get_week_start(retro_ts);
  bool is_today     = (retro_day_start == today_start);
  bool is_this_week = (retro_week_start == week_start);

  // Always increment all-time total.
  int32_t total = persist_exists(KEY_TOTAL) ? persist_read_int(KEY_TOTAL) : 0;
  persist_write_int(KEY_TOTAL, total + 1);

  // Update KEY_LAST_TIME only if the retroactive timestamp is more recent.
  time_t current_last = persist_exists(KEY_LAST_TIME)
                        ? (time_t)persist_read_int(KEY_LAST_TIME) : 0;
  if (retro_ts > current_last) {
    persist_write_int(KEY_LAST_TIME, (int32_t)retro_ts);
    result.updated_last_time = true;
  }

  if (is_this_week) {
    DayEntry entries[HISTORY_DAYS];
    load_history_raw(entries);

    int day_index = (int)((retro_day_start - week_start) / 86400);
    if (day_index < 0 || day_index >= HISTORY_DAYS) day_index = 0;

    // If the app was never opened on this day, stamp the slot and count it.
    if (entries[day_index].day_timestamp == 0) {
      entries[day_index].day_timestamp = (int32_t)retro_day_start;
      int32_t td = persist_exists(KEY_TOTAL_DAYS)
                   ? persist_read_int(KEY_TOTAL_DAYS) : 0;
      persist_write_int(KEY_TOTAL_DAYS, td + 1);
    }
    entries[day_index].count++;
    persist_write_data(KEY_HISTORY, entries, sizeof(DayEntry) * HISTORY_DAYS);

    if (is_today) {
      persist_write_int(KEY_COUNT, entries[day_index].count);
      result.is_today = true;
    }

    // Update hourly histogram for current-week entries only.
    // Copy struct tm — localtime() returns a static pointer (CLAUDE.md pitfall).
    struct tm retro_tm_copy;
    struct tm *tp = localtime(&retro_ts);
    retro_tm_copy = *tp;
    storage_log_hour(retro_tm_copy.tm_hour);

  } else {
    // Past week: update the archived WeekEntry total if it's still in history.
    WeekEntry weeks[WEEK_HISTORY_COUNT];
    memset(weeks, 0, sizeof(weeks));
    int n = 0;
    if (persist_exists(KEY_WEEK_HISTORY)) {
      persist_read_data(KEY_WEEK_HISTORY, weeks,
                        sizeof(WeekEntry) * WEEK_HISTORY_COUNT);
      for (n = 0; n < WEEK_HISTORY_COUNT; n++) {
        if (weeks[n].week_timestamp == 0) break;
      }
    }
    for (int i = 0; i < n; i++) {
      if (weeks[i].week_timestamp == (int32_t)retro_week_start) {
        weeks[i].total++;
        persist_write_data(KEY_WEEK_HISTORY, weeks,
                           sizeof(WeekEntry) * WEEK_HISTORY_COUNT);
        break;
      }
    }
    // No histogram update for past weeks — that data is gone.
  }

  return result;
}
