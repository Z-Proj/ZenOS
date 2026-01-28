#ifndef RTC_H
#define RTC_H
#include <stdint.h>
/// @brief The structure storing the format of the RTC Time.
typedef struct
{
    uint16_t milliseconds;
    uint8_t seconds;
    uint8_t minutes;
    uint8_t hours;
    uint8_t day;
    uint8_t month;
    uint8_t year;
} rtc_time_t;

rtc_time_t rtc_get_time(void);
void rtc_initialize(void);
uint32_t rtc_calculate_uptime(const rtc_time_t *start, const rtc_time_t *end);
rtc_time_t rtc_boottime(void);
void sleep(uint32_t time);
uint64_t rtc_get_ticks(void);

#endif