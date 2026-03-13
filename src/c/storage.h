#pragma once
#include <pebble.h>

#define HISTORY_DAYS       7
#define WEEK_HISTORY_COUNT 8    // weeks of rolling history kept
#define STORAGE_VERSION    2

typedef struct {
  int32_t day_timestamp;
  int32_t count;
} DayEntry;

typedef struct {
  int32_t week_timestamp;  // Monday midnight of that week
  int32_t total;           // cigarettes that week
  int32_t days_active;     // days with a non-zero or visited slot
} WeekEntry;

void    storage_save(int count, time_t last_time);
void    storage_load(int *count, time_t *last_time);
void    storage_get_history(DayEntry *entries, int *num_entries);
void    storage_get_week_history(WeekEntry *entries, int *num_entries);
void    storage_seed_debug_data(void);
void    storage_delete_today(void);
void    storage_delete_all(void);
void    storage_increment_total(void);
int32_t storage_get_total(void);
int32_t storage_get_total_days(void);
time_t  storage_get_week_start(void);

// Hour-of-day histogram — 24 buckets (0..23h), reset each week rollover.
void storage_log_hour(int hour);
void storage_get_hour_histogram(uint8_t *out_24);
void storage_reset_hour_histogram(void);
