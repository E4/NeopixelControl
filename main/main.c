#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_tls.h"
#include "esp_system.h"

#include "driver/gpio.h"
#include "neopixel.h"

#include "chaser_data.h"

// define the following:
// CONFIG_WIFI_SSID
// CONFIG_WIFI_PASSWORD

// CONFIG_LED_GPIO
// CONFIG_LED_COUNT

#include "config.h" 

#define TAG "main"
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define MAX_HTTP_OUTPUT_BUFFER 5000

static inline int positive_mod(int value, int mod) {
  int result = value % mod;
  return (result < 0) ? result + mod : result;
}

#define set_leds(r, g, b) set_leds_int(NP_RGB((r), (g), (b)))
#define flash_leds(r, g, b) flash_leds_int(NP_RGB((r), (g), (b)))

static httpd_handle_t start_webserver(void);
static void stop_webserver(void);
static esp_err_t server_request_handler_get(httpd_req_t *req);
static esp_err_t server_request_handler_get_css(httpd_req_t *req);
static esp_err_t server_request_handler_get_js(httpd_req_t *req);
static esp_err_t server_request_handler_get_bin(httpd_req_t *req);
static esp_err_t server_request_handler_post(httpd_req_t *req);
static void move_chasers();
static void set_pixels_for_chaser(chaser_data_t chaser, uint32_t color);
static uint32_t get_interpolated_rgb_for_chaser(chaser_data_t *data);
static void set_leds_int(uint32_t c);
static void flash_leds_int(uint32_t c);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void init_wifi();


static tNeopixelContext neopixel;
static uint32_t refreshRate, taskDelay;
chaser_data_t *chaser_data = NULL;
tNeopixel chaser_pixel[CONFIG_LED_COUNT];
int8_t chaser_count = 0;
static SemaphoreHandle_t chaser_data_mutex = NULL;


static httpd_handle_t server = NULL;

extern const uint8_t html_start[]        asm("_binary_index_min_html_start");
extern const uint8_t html_end[]          asm("_binary_index_min_html_end");

extern const uint8_t css_start[] asm("_binary_s_css_start");
extern const uint8_t css_end[]   asm("_binary_s_css_end");

extern const uint8_t javascript_start[]  asm("_binary_j_js_start");
extern const uint8_t javascript_end[]    asm("_binary_j_js_end");

//static const char index_html_template[] = "<!DOCTYPE html><html><head><title>Chasers</title><link rel=\"stylesheet\" href=\"d.css\" media=\"(min-width: 801px)\"><link rel=\"stylesheet\" href=\"m.css\" media=\"(max-width: 800px)\"><link rel=\"icon\" href=\"data:;base64,iVBORw0KGgo=\"><script src=\"j.js\"></script></head></html>";

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
    ESP_LOGI(TAG, "WiFi starting");
    esp_wifi_connect();
  } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
    ESP_LOGI(TAG, "WiFi disconnected");
    esp_wifi_connect();
    if (server) {
      stop_webserver();
    }
  } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
    ESP_LOGI(TAG, "Got IP address");
    if (server == NULL) {
      server = start_webserver();
    }
  }
}


static void init_wifi() {
  esp_netif_init();
  esp_event_loop_create_default();
  esp_netif_create_default_wifi_sta();
  wifi_init_config_t wifi_initiation = WIFI_INIT_CONFIG_DEFAULT();
  esp_wifi_init(&wifi_initiation);
  esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL);
  esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL);
  wifi_config_t wifi_configuration = {
    .sta = {
      .ssid = CONFIG_WIFI_SSID,
      .password = CONFIG_WIFI_PASSWORD,
       }
     };
  esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_configuration);

  esp_wifi_start();
  esp_wifi_set_mode(WIFI_MODE_STA);
  esp_wifi_connect();
}


static void set_leds_int(uint32_t c) {
  for(int i=0;i<CONFIG_LED_COUNT;i++) {
    chaser_pixel[i].index = i;
    chaser_pixel[i].rgb = c;
  }
  neopixel_SetPixel(neopixel, chaser_pixel, ARRAY_SIZE(chaser_pixel));
}


static void flash_leds_int(uint32_t c) {
  set_leds_int(c);
  vTaskDelay(taskDelay);
  set_leds_int(0);
  vTaskDelay(taskDelay);
}


static uint32_t get_interpolated_rgb_for_chaser(chaser_data_t *data) {
  static int8_t i0;
  static int8_t i1;
  static int8_t interpolator;
  i0 = data->color_offset / 40;
  i1 = ((i0 + 1) % 6)*3;
  i0 *= 3;
  interpolator = data->color_offset % 40;
  return NP_RGB(
    ((data->color[i1+0]-data->color[i0+0])*interpolator/40)+data->color[i0+0],
    ((data->color[i1+1]-data->color[i0+1])*interpolator/40)+data->color[i0+1],
    ((data->color[i1+2]-data->color[i0+2])*interpolator/40)+data->color[i0+2]
  );
}


static void move_chasers() {
  static uint32_t frame;
  static int16_t i;
  static uint16_t new_position_offset;

  if (chaser_count <= 0 || chaser_data == NULL) return;
  if(++frame==0) frame=1;
  for(i=0;i<chaser_count;i++) {

    if(chaser_data[i].flags&FLAG_RANDOM_POSITION) {
      new_position_offset = esp_random()%(chaser_data[i].range_length<<4);
    } else {
      new_position_offset = (uint16_t)positive_mod((int)chaser_data[i].position_offset + chaser_data[i].position_speed, chaser_data[i].range_length<<4);
    }

    if((chaser_data[i].flags&FLAG_CLEAR_PREVIOUS)&&(new_position_offset>>4)!=chaser_data[i].position_offset>>4) {
      set_pixels_for_chaser(chaser_data[i],0);
    }

    chaser_data[i].position_offset = new_position_offset;


    if(frame%chaser_data[i].color_delay==0) {
      if(chaser_data[i].flags&FLAG_RANDOM_COLOR) {
        chaser_data[i].color_offset = esp_random() % 240;
      } else {
        chaser_data[i].color_offset = (chaser_data[i].color_offset + chaser_data[i].color_speed) % 240;
      }
    }
    set_pixels_for_chaser(chaser_data[i],get_interpolated_rgb_for_chaser(&chaser_data[i]));
  }

}


static void set_pixels_for_chaser(chaser_data_t chaser, uint32_t chaser_color) {
  static uint16_t repeats;
  static uint16_t r;
  static uint16_t position_offset;

  if(chaser.flags&FLAG_SINUSOIDAL) {
    position_offset = (uint16_t)((sin((float)chaser.position_offset/((float)(chaser.range_length<<4))*6.28)+1)*0.5*(float)chaser.range_length);
  } else {
    position_offset = chaser.position_offset>>4;
  }

  if(chaser.repeat) {
    repeats = chaser.range_length/chaser.repeat;
    for(r=0;r<repeats;r++) {
      chaser_pixel[r].index = ((position_offset + chaser.repeat * r) % chaser.range_length) + chaser.range_offset;
      chaser_pixel[r].rgb = chaser_color;
    }
    neopixel_SetPixel(neopixel, chaser_pixel, r);
  } else {
    chaser_pixel[0].index = position_offset + chaser.range_offset;
    chaser_pixel[0].rgb = chaser_color;
    neopixel_SetPixel(neopixel, chaser_pixel, 1);
  }
}


static esp_err_t server_request_handler_get(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, (const char*)html_start, html_end - html_start - 1);
  return ESP_OK;
}


static esp_err_t server_request_handler_get_css(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/css");
  httpd_resp_send(req, (const char*)css_start, css_end - css_start - 1);
  return ESP_OK;
}


static esp_err_t server_request_handler_get_js(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/javascript");
  httpd_resp_send(req, (const char*)javascript_start, javascript_end - javascript_start - 1);
  return ESP_OK;
}


static esp_err_t server_request_handler_get_bin(httpd_req_t *req) {
  httpd_resp_set_type(req, "application/octet-stream");

  if (chaser_count <= 0 || chaser_data == NULL) {
    return httpd_resp_send(req, NULL, 0);
  }

  size_t data_len = (size_t)chaser_count * sizeof(chaser_data_t);
  return httpd_resp_send(req, (const char *)chaser_data, data_len);
}


static esp_err_t server_request_handler_post(httpd_req_t *req) {
  httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);

  const size_t total_len = req->content_len;
  if (total_len == 0) {
    ESP_LOGI(TAG, "Received zero lenght data");
    return ESP_FAIL;
  }

  if (total_len % sizeof(chaser_data_t) != 0) {
    ESP_LOGI(TAG, "Received data length is not multiple of chaser data length");
    return ESP_FAIL;
  }

  chaser_data_t *new_data = malloc(total_len);
  if (new_data == NULL) {
    ESP_LOGI(TAG, "Could not allocate enough memory for the data");
    return ESP_ERR_NO_MEM;
  }

  size_t received = 0;
  while (received < total_len) {
    int ret = httpd_req_recv(req, (char *)new_data + received, total_len - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      free(new_data);
      return ESP_FAIL;
    }
    received += ret;
  }

  if (chaser_data_mutex != NULL) {
    xSemaphoreTake(chaser_data_mutex, portMAX_DELAY);
  }

  if (chaser_data != NULL) {
    free(chaser_data);
  }
  chaser_data = new_data;
  chaser_count = total_len / sizeof(chaser_data_t);

  for (int i = 0; i < chaser_count; i++) {
    if (chaser_data[i].position_delay == 0) chaser_data[i].position_delay = 1;
    if (chaser_data[i].color_delay == 0) chaser_data[i].color_delay = 1;
    if (chaser_data[i].range_length + chaser_data[i].range_offset > CONFIG_LED_COUNT) {
      chaser_data[i].range_length=0;
      chaser_data[i].range_offset=0;
    }
    if (chaser_data[i].range_length==0) {
      chaser_data[i].range_length = CONFIG_LED_COUNT;
    }
  }

  set_leds(0, 0, 0);

  if (chaser_data_mutex != NULL) {
    xSemaphoreGive(chaser_data_mutex);
  }
  ESP_LOGI(TAG, "Received chaser data");
  return ESP_OK;
}


static httpd_handle_t start_webserver(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Increase stack size to handle HTML generation and logging
  config.stack_size = 8192;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_register_uri_handler(server, &(httpd_uri_t){ .uri =      "/", .method =  HTTP_GET, .handler =     server_request_handler_get, .user_ctx = NULL });
    httpd_register_uri_handler(server, &(httpd_uri_t){ .uri = "/s.css", .method =  HTTP_GET, .handler = server_request_handler_get_css, .user_ctx = NULL });
    httpd_register_uri_handler(server, &(httpd_uri_t){ .uri =  "/j.js", .method =  HTTP_GET, .handler =  server_request_handler_get_js, .user_ctx = NULL });
    httpd_register_uri_handler(server, &(httpd_uri_t){ .uri =   "/bin", .method =  HTTP_GET, .handler = server_request_handler_get_bin, .user_ctx = NULL });
    httpd_register_uri_handler(server, &(httpd_uri_t){ .uri =      "/", .method = HTTP_POST, .handler =    server_request_handler_post, .user_ctx = NULL });

  }
  return server;
}


static void stop_webserver(void) {
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
}


void app_main(void) {
  neopixel = neopixel_Init(CONFIG_LED_COUNT, CONFIG_LED_GPIO);
  if(NULL == neopixel) {
    ESP_LOGE(TAG, "Neopixel initialization failed");
    return;
  }

  refreshRate = neopixel_GetRefreshRate(neopixel);
  if (refreshRate == 0) {
    taskDelay = 1;
  } else {
    taskDelay = MAX((TickType_t)1, (TickType_t)((configTICK_RATE_HZ + refreshRate - 1) / refreshRate));
  }

  chaser_data_mutex = xSemaphoreCreateMutex();
  if (chaser_data_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create chaser data mutex");
    return;
  }


  nvs_flash_init();
  init_wifi();

  while(chaser_count == 0) {
    ESP_LOGI(TAG, "Waiting for chaser data");
    set_leds(4,0,0);
    vTaskDelay(taskDelay);
    set_leds(0,4,0);
    vTaskDelay(taskDelay);
    set_leds(0,0,4);
    vTaskDelay(taskDelay);
    set_leds(0,0,0);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  flash_leds(255,255,255);

  while(true) {

    if (xSemaphoreTake(chaser_data_mutex, portMAX_DELAY) != pdTRUE) continue;
    move_chasers();
    xSemaphoreGive(chaser_data_mutex);

    vTaskDelay(taskDelay);
  }
  neopixel_Deinit(neopixel);
}



