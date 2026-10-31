#ifndef WATCH_TYPES_H
#define WATCH_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum { WATCH_OK=0, WATCH_INVALID_ARGUMENT, WATCH_NOT_IMPLEMENTED, WATCH_NOT_READY, WATCH_IO_ERROR, WATCH_TIMEOUT, WATCH_INVALID_DATA } watch_status_t;
typedef struct { float ax_g, ay_g, az_g, gx_dps, gy_dps, gz_dps; uint32_t timestamp_ms; bool valid; } motion_sample_t;
typedef struct { uint32_t red, infrared, timestamp_ms; bool valid; } ppg_sample_t;
typedef struct { uint16_t millivolts; uint8_t percent; bool valid; } battery_state_t;
typedef struct { float bpm, spo2_estimate; bool heart_rate_valid, spo2_valid; } health_result_t;
typedef struct { uint32_t utc_seconds, value; uint16_t type; } record_t;
#endif
