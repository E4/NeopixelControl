#include <stdio.h>
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


static httpd_handle_t start_webserver(void);
static void stop_webserver(void);
static esp_err_t server_request_handler(httpd_req_t *req);
static void move_chasers();
static uint32_t get_interpolated_rgb_for_chaser(chaser_data_t *data);
static void flash_leds(int8_t r, int8_t g, int8_t b);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void init_wifi();


static tNeopixelContext neopixel;
chaser_data_t *chaser_data = NULL;
tNeopixel chaser_pixel[CONFIG_LED_COUNT];
int8_t chaser_count = 0;

static httpd_handle_t server = NULL;
static char html[2048];
static const char index_html_template[] = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"/><title>Lightie</title><style> html, body { margin: 0; background-color: black; } body { display: flex; flex-direction: column; align-items: center; min-height: 100svmin; } .top { width: 100svmin; text-align: center; padding: 10svmin 0; height: 20svmin; } .sliders { display: flex; align-items: center; justify-content: center; width: 100svmin; height: 60svmin; padding: 5svmin; box-sizing: border-box; } input[type=\"range\"] { writing-mode: bt-lr; -webkit-appearance: slider-vertical; height: 100%%; flex: 1 1 0; min-width: 0; } #c { width: 50svmin; height: 20svmin; border: none; outline: none; cursor: pointer; } </style></head><body><div class=\"top\"><input id=\"c\" type=\"color\"></div><div class=\"sliders\"><input id=\"r\" type=\"range\" min=\"0\" max=\"255\" value=\"0\"/><input id=\"g\" type=\"range\" min=\"0\" max=\"255\" value=\"0\"/><input id=\"b\" type=\"range\" min=\"0\" max=\"255\" value=\"0\"/></div><script>const c=()=>\"#\"+(16777216|a.r<<16|a.g<<8|a.b).toString(16).slice(1),d=document.getElementById(\"r\"),e=document.getElementById(\"g\"),f=document.getElementById(\"b\"),g=document.getElementById(\"c\");let h,a={r:%d,g:%d,b:%d},k=0;function l(b){b.target==g?(b=g.value,b=b.replace(/^#/,\"\"),b=parseInt(b,16),a={r:b>>16&255,g:b>>8&255,b:b&255},d.value=a.r,e.value=a.g,f.value=a.b):(a.r=d.value,a.g=e.value,a.b=f.value,g.value=c());performance.now()>k+100?m():(clearTimeout(h),h=setTimeout(m,k+100-performance.now()))}function m(){k=performance.now();fetch(`/?r=${a.r}&g=${a.g}&b=${a.b}`,{method:\"GET\",cache:\"no-store\",keepalive:!0}).catch(()=>{})}[d,e,f,g].forEach(b=>b.addEventListener(\"input\",l));g.value=c();d.value=a.r;e.value=a.g;f.value=a.b;</script></body></html>";

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


static void init_wifi()
{
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


static void flash_leds(int8_t r, int8_t g, int8_t b) {
  for(int i=0;i<CONFIG_LED_COUNT;i++) {
    // led_strip_set_pixel(led_strip, i, r, g, b);
  }
  // led_strip_refresh(led_strip);
  vTaskDelay(5 / portTICK_PERIOD_MS);
  // led_strip_clear(led_strip);
}






static uint32_t get_interpolated_rgb_for_chaser(chaser_data_t *data)
{
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


static void move_chasers()
{
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



static esp_err_t server_request_handler(httpd_req_t *req)
{
  char query[256];
  bool has_rgb = false;
  if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
    ESP_LOGI(TAG, "Request query: %s", query);
    char value[8];
    if (httpd_query_key_value(query, "r", value, sizeof(value)) == ESP_OK) {
      atoi(value);
      has_rgb = true;
    }
    if (httpd_query_key_value(query, "g", value, sizeof(value)) == ESP_OK) {
      atoi(value);
      has_rgb = true;
    }
    if (httpd_query_key_value(query, "b", value, sizeof(value)) == ESP_OK) {
      atoi(value);
      has_rgb = true;
    }
  }
  if (has_rgb) {
    const char resp[] = "OK\n";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
  } else {
    httpd_resp_set_type(req, "text/html");
    int len = snprintf(html, sizeof(html), index_html_template, 0, 0, 0);
    httpd_resp_send(req, html, len);
  }
  return ESP_OK;
}

static httpd_handle_t start_webserver(void)
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  // Increase stack size to handle HTML generation and logging
  config.stack_size = 8192;
  httpd_handle_t server = NULL;
  if (httpd_start(&server, &config) == ESP_OK) {
    httpd_uri_t uri = {
      .uri = "/",
      .method = HTTP_GET,
      .handler = server_request_handler,
      .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &uri);
  }
  return server;
}


static void stop_webserver(void)
{
  if (server) {
    httpd_stop(server);
    server = NULL;
  }
}




void app_main(void) {
  nvs_flash_init();
  init_wifi();
  neopixel = neopixel_Init(CONFIG_LED_COUNT, CONFIG_LED_GPIO);

  while(chaser_count == 0) {
    ESP_LOGI(TAG, "chaser count: %d", chaser_count);
    flash_leds(0,0,32);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
  flash_leds(0,32,0);

  while(1) {
    move_chasers();
    vTaskDelay(14 / portTICK_PERIOD_MS);
  }
  neopixel_Deinit(neopixel);
}





