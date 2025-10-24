#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
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

#define set_leds(r, g, b) set_leds_int(NP_RGB((r), (g), (b)))
#define flash_leds(r, g, b) flash_leds_int(NP_RGB((r), (g), (b)))

static httpd_handle_t start_webserver(void);
static void stop_webserver(void);
static esp_err_t server_request_handler_get(httpd_req_t *req);
static esp_err_t server_request_handler_post(httpd_req_t *req);
static void move_chasers();
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


static httpd_handle_t server = NULL;
static char html[2048];
static const char index_html_template[] = "<!DOCTYPE html><html><head><meta charset='utf-8'/><title>Chasers</title></head><body>%d %d %d</body></html>";

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
  static int16_t repeats;
  static int16_t r;
  static uint32_t chaser_color;
  if(++frame==0) frame=1;
  for(i=0;i<chaser_count;i++) {
    if(frame%chaser_data[i].position_delay==0) chaser_data[i].position_offset = (chaser_data[i].position_offset + chaser_data[i].position_speed) % CONFIG_LED_COUNT;
    if(frame%chaser_data[i].color_delay==0) chaser_data[i].color_offset = (chaser_data[i].color_offset + chaser_data[i].color_speed) % 240;
    chaser_color = get_interpolated_rgb_for_chaser(&chaser_data[i]);
    if(chaser_data[i].repeat) {
      repeats = CONFIG_LED_COUNT/chaser_data[i].repeat;
      for(r=0;r<repeats;r++) {
        chaser_pixel[r].index = (chaser_data[i].position_offset + chaser_data[i].repeat * r) % CONFIG_LED_COUNT;
        chaser_pixel[r].rgb = chaser_color;
      }
      neopixel_SetPixel(neopixel, chaser_pixel, r);
    } else {
      chaser_pixel[0].index = chaser_data[i].position_offset;
      chaser_pixel[0].rgb = chaser_color;
      neopixel_SetPixel(neopixel, chaser_pixel, 1);
    }
  }
}


static esp_err_t server_request_handler_post(httpd_req_t *req) {
  const size_t total_len = req->content_len;
  if (total_len == 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Empty POST body");
    return ESP_FAIL;
  }

  if (total_len % sizeof(chaser_data_t) != 0) {
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid chaser data length");
    return ESP_FAIL;
  }

  chaser_data_t *new_data = malloc(total_len);
  if (new_data == NULL) {
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
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
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to receive POST data");
      return ESP_FAIL;
    }
    received += ret;
  }

  if (chaser_data != NULL) {
    free(chaser_data);
  }
  chaser_data = new_data;
  chaser_count = total_len / sizeof(chaser_data_t);

  for (int i = 0; i < chaser_count; i++) {
    if (chaser_data[i].position_delay == 0) {
      chaser_data[i].position_delay = 1;
    }
    if (chaser_data[i].color_delay == 0) {
      chaser_data[i].color_delay = 1;
    }
  }

  ESP_LOGI(TAG, "Updated chaser data with %d entries", chaser_count);

  const char resp[] = "OK\n";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

static esp_err_t server_request_handler_get(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  int len = snprintf(html, sizeof(html), index_html_template, 0, 0, 0);
  httpd_resp_send(req, html, len);
  return ESP_OK;
}


static httpd_handle_t start_webserver(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Increase stack size to handle HTML generation and logging
  config.stack_size = 8192;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t get_uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = server_request_handler_get,
      .user_ctx = NULL
    };
    httpd_uri_t post_uri = {
      .uri = "/",
      .method = HTTP_POST,
      .handler = server_request_handler_post,
      .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_uri);
    httpd_register_uri_handler(server, &post_uri);
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
    ESP_LOGE(TAG, "[%s] Initialization failed\n", __func__);
    return;
  }

  refreshRate = neopixel_GetRefreshRate(neopixel);
  taskDelay = MAX(1, pdMS_TO_TICKS(1000UL / refreshRate));


  nvs_flash_init();
  init_wifi();

  flash_leds(255,0,0);
  vTaskDelay(pdMS_TO_TICKS(500));
  flash_leds(0,255,0);
  vTaskDelay(pdMS_TO_TICKS(500));
  flash_leds(0,0,255);
  vTaskDelay(pdMS_TO_TICKS(500));

  while(chaser_count == 0) {
    ESP_LOGI(TAG, "chaser count: %d", chaser_count);
    flash_leds(4,4,4);
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  flash_leds(255,255,255);

  while(true) {
    move_chasers();
    vTaskDelay(14 / portTICK_PERIOD_MS);
  }
  neopixel_Deinit(neopixel);
}



