#ifndef PRINT_TASK_H
#define PRINT_TASK_H

#include <time.h>
#include <stdbool.h>
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "coaster_types.h"

typedef struct {
    coaster_status_t coaster_status;
    bool park_opened_today;
    time_t t_current;
    time_t t_open_from;
    time_t t_closed_from;
} status_summary_t;

void start_print_task(QueueHandle_t q_coaster,
                      QueueHandle_t q_park,
                      QueueHandle_t q_time,
                      const char *target_ride_name,
                      SemaphoreHandle_t done_sem,
                      QueueHandle_t q_status_summary);

#endif /* PRINT_TASK_H */
