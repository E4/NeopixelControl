#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
static esp_err_t server_request_handler_get_bin(httpd_req_t *req);
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
static SemaphoreHandle_t chaser_data_mutex = NULL;


static httpd_handle_t server = NULL;
static const char index_html_template[] = "<!DOCTYPE html><html><head><title>Chasers</title><style>body {background:black;color:white;font-family:sans-serif;max-width:100vh;margin:auto;padding:2%;}.c, .n {display:inline-block;margin:2vmin 0 0 0;font-size:4vmin;color:goldenrod;width:calc(100%/3);}.c {width:calc(100%/6);}.v {border-radius:1px;display:inline-block;width:100%;padding: 0;text-align:center;box-sizing: border-box;background:black;color:silver;font-size:7vmin;}.v[type=\"color\"] {height:12vmin;width:100%;}.r,.a {border-radius:2vmin;background:goldenrod;color:white;border:1px solid gray;}.r {width:40%;font-size:4vmin;margin:4vmin 0 7vmin 30%;background:red;}.a {padding:2vmin;width:40%;font-size:4vmin;margin:4vmin 0 7vmin 30%;background:goldenrod;}</style><script>var u=new Set([\"tagName\",\"element\",\"children\"]);function w(a,e=null){if(Array.isArray(a))for(let d=0;d<a.length;d++)x(a[d],e,d);else x(a,e)}function y(a,e){function d(){return f}function k(g){if(g==f)return f;f=g;w(a,null);return f}var f=a[e];return{set:u.has(e)?d:k,get:function(){return f}}}function x(a,e,d=null){if(a!=null){a.children==null&&(a.children=[]);if(!a.element){a.element=document.createElement(a.tagName);for(var k in a)Object.defineProperty(a,k,y(a,k));e&&A(a.element,e,d)}e&&a.element.parentElement!=e?A(a.element,e,d):e&&d!=null&&e.children[d]!=a.element&&A(a.element,e,d);a.children==null&&(a.children=[]);e={};for(var f in a)if(!u.has(f))if(f==\"text\")if(d=a.element,k=a.text,d.childNodes.length>0)for(let n=0;n<d.childNodes.length;n++){if(d.childNodes[n].nodeType==3){d.childNodes[n].textContent!=k&&(d.childNodes[n].textContent=k);break}}else d.innerText!=k&&(d.innerText=k);else f==\"class\"?a.element.className!=a[\"class\"]&&(a.element.className=a[\"class\"]):f.startsWith(\"on-\")?e[f.substring(3)]=a[f]:a.element[f]!=a[f]&&(a.element[f]=a[f]);f=a.element;d=f.h;d||(f.h=d={});for(var g in d)g in e&&d[g]==e[g]||g in e&&d[g]==e[g]||(f.removeEventListener(g,d[g]),delete d[g]);for(let n in e)n in d||(f.addEventListener(n,e[n]),d[n]=e[n]);g=a.element.g;var p;g||(a.element.g=g=[]);if(\"children\"in a)for(var m in a.children)x(a.children[m],a.element,m),g.indexOf(a.children[m])==-1&&g.push(a.children[m]);for(m=g.length-1;m>=0;m--)a.children.indexOf(g[m])==-1&&(p=g.splice(m,1)[0])&&p.element.remove()}}function A(a,e,d=null){d==null||d>=e.children.length?e.appendChild(a):e.insertBefore(a,e.children[d])}function B(a,e,d,k,f){a={tagName:a,\"class\":e,text:d,children:f};for(let g in k)a[g]=k[g];return a}var C=(a,e=null,d=[])=>B(\"div\",a,e,null,d),D=(a,e=null,d=null)=>B(\"button\",a,e,d,[]);function E(a,e,d,k=[]){var f={tagName:\"input\",\"class\":a,type:e,children:k};for(let p in d)f[p]=d[p];var g=f[\"on-change\"];f[\"on-change\"]=p=>{f.value=p.target.value;g&&g(p)};return f}function F(a,e,d){a={tagName:\"label\",\"class\":a,text:e,children:d};for(let k in null)a[k]=null[k];return a}function G(){async function a(b){b=new Uint8Array(b);return await fetch(\"/\",{method:\"POST\",headers:{\"Content-Type\":\"application/octet-stream\"},body:b})}function e(b){if(b&&b.byteLength){var c=new DataView(b);b=b.byteLength/32;for(let h=0;h<b;h++)d(c,h*32)}}function d(b,c){let h=C(\"csr\",\"\",[f(b.getUint8(c+0),b.getUint8(c+1),b.getUint8(c+2)),f(b.getUint8(c+3),b.getUint8(c+4),b.getUint8(c+5)),f(b.getUint8(c+6),b.getUint8(c+7),b.getUint8(c+8)),f(b.getUint8(c+9),b.getUint8(c+10),b.getUint8(c+11)),f(b.getUint8(c+12),b.getUint8(c+13),b.getUint8(c+14)),f(b.getUint8(c+15),b.getUint8(c+16),b.getUint8(c+17)),k(\"Position Offset\",n,b.getUint16(c+18,!0)),k(\"Position Speed\",H,b.getInt8(c+20)),k(\"Position Delay\",t,b.getUint8(c+21)),k(\"Color Offset\",t,b.getUint8(c+22)),k(\"Color Speed\",t,b.getUint8(c+23)),k(\"Color Delay\",t,b.getUint8(c+24)),k(\"Repeat\",t,b.getUint8(c+25)),k(\"Limit Low\",n,b.getUint16(c+26,!0)),k(\"Limit High\",n,b.getUint16(c+28,!0)),D(\"r\",\"remove\",{\"on-click\":function(){let l=r.indexOf(h);l!=-1&&r.splice(l,1);w(z,null);g()}})]);r.push(h);w(z,null)}function k(b,c,h){c=[...c];c[2].value=h.toString();c[2][\"on-change\"]=g;return F(\"n\",b,[E(...c)])}function f(b,c,h){var l=[...I];l[2].value=\"#\"+b.toString(16).padStart(2,\"0\")+c.toString(16).padStart(2,\"0\")+h.toString(16).padStart(2,\"0\");l[2][\"on-change\"]=g;return F(\"c\",null,[E(...l)])}function g(){var b=new ArrayBuffer(r.length*32),c=new DataView(b);for(let v=0;v<r.length;v++){var h=r[v].children,l=c,q=v*32;p(h[0],l,q+0);p(h[1],l,q+3);p(h[2],l,q+6);p(h[3],l,q+9);p(h[4],l,q+12);p(h[5],l,q+15);l.setUint16(q+18,m(h[6]),!0);l.setInt8(q+20,m(h[7]));l.setUint8(q+21,m(h[8]));l.setUint8(q+22,m(h[9]));l.setUint8(q+23,m(h[10]));l.setUint8(q+24,m(h[11]));l.setUint8(q+25,m(h[12]));l.setUint16(q+26,m(h[13]),!0);l.setUint16(q+28,m(h[14]),!0)}a(b)}function p(b,c,h){b=m(b);c.setUint8(h+0,parseInt(b.slice(1,3),16));c.setUint8(h+1,parseInt(b.slice(3,5),16));c.setUint8(h+2,parseInt(b.slice(5,7),16))}function m(b){return b.children.length?b.children[0].value:null}const n=[\"v\",\"number\",{min:\"0\",max:\"65535\",value:\"0\"}],t=[\"v\",\"number\",{min:\"0\",max:\"255\",value:\"0\"}],H=[\"v\",\"number\",{min:\"-128\",max:\"127\",value:\"0\"}],I=[\"v\",\"color\",{value:\"#000000\"}];let r,z=C(\"\",null,r=[]),J=C(\"\",null,[z,D(\"a\",\"add\",{\"on-click\":function(){e(new ArrayBuffer(32));g()}})]);w(J,document.body);(async function(){var b=await fetch(\"/bin\");if(!b.ok)return null;b=await b.arrayBuffer();var c;b.byteLength===0?c=[]:b instanceof ArrayBuffer?c=b:b instanceof Uint8Array&&(c=b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength));return c})().then(e)}window.addEventListener(\"load\",()=>new G);</script></head><body></body></html>";

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
  if (chaser_data_mutex == NULL) {
    return;
  }

  if (xSemaphoreTake(chaser_data_mutex, portMAX_DELAY) != pdTRUE) {
    return;
  }

  if (chaser_count <= 0 || chaser_data == NULL) {
    xSemaphoreGive(chaser_data_mutex);
    return;
  }
  if(++frame==0) frame=1;
  for(i=0;i<chaser_count;i++) {
    if(frame%chaser_data[i].position_delay==0) {
      int next_offset = positive_mod((int)chaser_data[i].position_offset + chaser_data[i].position_speed, CONFIG_LED_COUNT);
      chaser_data[i].position_offset = (uint16_t)next_offset;
    }
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

  xSemaphoreGive(chaser_data_mutex);
}


static esp_err_t server_request_handler_get(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_send(req, index_html_template, sizeof(index_html_template));
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
  const char resp[] = "OK\n";
  httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

  const size_t total_len = req->content_len;
  if (total_len == 0) {
    return ESP_FAIL;
  }

  if (total_len % sizeof(chaser_data_t) != 0) {
    return ESP_FAIL;
  }

  chaser_data_t *new_data = malloc(total_len);
  if (new_data == NULL) {
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
    if (chaser_data[i].position_delay == 0) {
      chaser_data[i].position_delay = 1;
    }
    if (chaser_data[i].color_delay == 0) {
      chaser_data[i].color_delay = 1;
    }
  }

  if (chaser_data_mutex != NULL) {
    xSemaphoreGive(chaser_data_mutex);
  }

  ESP_LOGI(TAG, "Updated chaser data with %d entries", chaser_count);

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
    httpd_uri_t get_bin_uri = {
      .uri = "/bin",
      .method = HTTP_GET,
      .handler = server_request_handler_get_bin,
      .user_ctx = NULL
    };
    httpd_uri_t post_uri = {
      .uri = "/",
      .method = HTTP_POST,
      .handler = server_request_handler_post,
      .user_ctx = NULL
    };
    httpd_register_uri_handler(server, &get_uri);
    httpd_register_uri_handler(server, &get_bin_uri);
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

  chaser_data_mutex = xSemaphoreCreateMutex();
  if (chaser_data_mutex == NULL) {
    ESP_LOGE(TAG, "Failed to create chaser data mutex");
    return;
  }


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



