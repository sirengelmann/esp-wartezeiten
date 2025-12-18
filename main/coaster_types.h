// ==============================================
// File: main/coaster_types.h
// ==============================================
#pragma once
#include <stdbool.h>
#include <strings.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COASTER_OPENED = 0,
    COASTER_VIRTUALQUEUE = 1,
    COASTER_MAINTENANCE = 2,
    COASTER_CLOSED_ICE = 3,
    COASTER_CLOSED_WEATHER = 4,
    COASTER_CLOSED = 5,
    COASTER_UNKNOWN = -1
} coaster_status_t;

static inline coaster_status_t coaster_status_from_string(const char *s)
{
    if      (!strcasecmp(s, "opened"))           return COASTER_OPENED;
    else if (!strcasecmp(s, "virtualqueue"))   return COASTER_VIRTUALQUEUE;
    else if (!strcasecmp(s, "maintenance"))    return COASTER_MAINTENANCE;
    else if (!strcasecmp(s, "closedice"))      return COASTER_CLOSED_ICE;
    else if (!strcasecmp(s, "closedweather"))  return COASTER_CLOSED_WEATHER;
    else if (!strcasecmp(s, "closed"))         return COASTER_CLOSED;
    else                                       return COASTER_UNKNOWN;
}

static inline const char* coaster_status_to_string(coaster_status_t status)
{
    switch (status) {
        case COASTER_OPENED:          return "Geöffnet";
        case COASTER_VIRTUALQUEUE:    return "Virtuelle Warteschlange";
        case COASTER_MAINTENANCE:     return "Wartung";
        case COASTER_CLOSED_ICE:      return "Geschlossen (Eis)";
        case COASTER_CLOSED_WEATHER:  return "Geschlossen (Wetter)";
        case COASTER_CLOSED:          return "Geschlossen";
        default:                      return "Unbekannt";
    }
}

typedef struct {
    int              waitingtime;
    coaster_status_t status;
    char*            name;
} coaster_data_t;

typedef struct {
    bool  opened_today;
    char* open_from;
    char* closed_from;
} park_data_t;

#ifdef __cplusplus
}
#endif