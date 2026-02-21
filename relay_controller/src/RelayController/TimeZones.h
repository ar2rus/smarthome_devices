#ifndef TIME_ZONES_H
#define TIME_ZONES_H

#include <stddef.h>
#include <WString.h>
#include <TZ.h>

#define DEFAULT_TIMEZONE_ID "Europe/Samara"

enum TimeZoneKey {
  TZKEY_UTC,
  TZKEY_Europe_Kaliningrad,
  TZKEY_Europe_Moscow,
  TZKEY_Europe_Samara,
  TZKEY_Asia_Yekaterinburg,
  TZKEY_Asia_Omsk,
  TZKEY_Asia_Krasnoyarsk,
  TZKEY_Asia_Irkutsk,
  TZKEY_Asia_Yakutsk,
  TZKEY_Asia_Vladivostok,
  TZKEY_Asia_Magadan,
  TZKEY_Asia_Kamchatka,
  TZKEY_America_New_York,
  TZKEY_America_Chicago,
  TZKEY_America_Denver,
  TZKEY_America_Los_Angeles
};

struct TimeZoneOption {
  const char* id;
  const char* label;
  TimeZoneKey key;
};

static const TimeZoneOption TIME_ZONES[] = {
  { "UTC", "UTC", TZKEY_UTC },
  { "Europe/Kaliningrad", "Europe/Kaliningrad", TZKEY_Europe_Kaliningrad },
  { "Europe/Moscow", "Europe/Moscow", TZKEY_Europe_Moscow },
  { "Europe/Samara", "Europe/Samara", TZKEY_Europe_Samara },
  { "Asia/Yekaterinburg", "Asia/Yekaterinburg", TZKEY_Asia_Yekaterinburg },
  { "Asia/Omsk", "Asia/Omsk", TZKEY_Asia_Omsk },
  { "Asia/Krasnoyarsk", "Asia/Krasnoyarsk", TZKEY_Asia_Krasnoyarsk },
  { "Asia/Irkutsk", "Asia/Irkutsk", TZKEY_Asia_Irkutsk },
  { "Asia/Yakutsk", "Asia/Yakutsk", TZKEY_Asia_Yakutsk },
  { "Asia/Vladivostok", "Asia/Vladivostok", TZKEY_Asia_Vladivostok },
  { "Asia/Magadan", "Asia/Magadan", TZKEY_Asia_Magadan },
  { "Asia/Kamchatka", "Asia/Kamchatka", TZKEY_Asia_Kamchatka },
  { "America/New_York", "America/New_York", TZKEY_America_New_York },
  { "America/Chicago", "America/Chicago", TZKEY_America_Chicago },
  { "America/Denver", "America/Denver", TZKEY_America_Denver },
  { "America/Los_Angeles", "America/Los_Angeles", TZKEY_America_Los_Angeles }
};

static const size_t TIME_ZONES_COUNT = sizeof(TIME_ZONES) / sizeof(TIME_ZONES[0]);

inline const TimeZoneOption* findTimeZoneById(const String& id) {
  for (size_t i = 0; i < TIME_ZONES_COUNT; i++) {
    if (id == TIME_ZONES[i].id) {
      return &TIME_ZONES[i];
    }
  }
  return nullptr;
}

inline const TimeZoneOption* getDefaultTimeZone() {
  const TimeZoneOption* zone = findTimeZoneById(DEFAULT_TIMEZONE_ID);
  if (zone != nullptr) {
    return zone;
  }
  return &TIME_ZONES[0];
}

inline const char* getPosixTimeZone(TimeZoneKey key) {
  switch (key) {
    case TZKEY_Europe_Kaliningrad:
      return TZ_Europe_Kaliningrad;
    case TZKEY_Europe_Moscow:
      return TZ_Europe_Moscow;
    case TZKEY_Europe_Samara:
      return TZ_Europe_Samara;
    case TZKEY_Asia_Yekaterinburg:
      return TZ_Asia_Yekaterinburg;
    case TZKEY_Asia_Omsk:
      return TZ_Asia_Omsk;
    case TZKEY_Asia_Krasnoyarsk:
      return TZ_Asia_Krasnoyarsk;
    case TZKEY_Asia_Irkutsk:
      return TZ_Asia_Irkutsk;
    case TZKEY_Asia_Yakutsk:
      return TZ_Asia_Yakutsk;
    case TZKEY_Asia_Vladivostok:
      return TZ_Asia_Vladivostok;
    case TZKEY_Asia_Magadan:
      return TZ_Asia_Magadan;
    case TZKEY_Asia_Kamchatka:
      return TZ_Asia_Kamchatka;
    case TZKEY_America_New_York:
      return TZ_America_New_York;
    case TZKEY_America_Chicago:
      return TZ_America_Chicago;
    case TZKEY_America_Denver:
      return TZ_America_Denver;
    case TZKEY_America_Los_Angeles:
      return TZ_America_Los_Angeles;
    case TZKEY_UTC:
    default:
      return "UTC0";
  }
}

#endif
