#ifndef DEBUG_H
#define DEBUG_H

#include "esp_log.h"

#ifndef DEBUG
#define DEBUG 1
#endif

#if DEBUG
#define DEBUG_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#define DEBUG_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#else
#define DEBUG_LOGI(tag, fmt, ...) do { (void)(tag); } while (0)
#define DEBUG_LOGE(tag, fmt, ...) do { (void)(tag); } while (0)
#endif

#endif // DEBUG_H
