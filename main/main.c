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
#include "esp_http_client.h"
#include "esp_tls.h"

#include "driver/gpio.h"
#include "neopixel.h"

//#include "led_strip.h"

// define the following:
// CONFIG_WIFI_SSID
// CONFIG_WIFI_PASSWORD
// CONFIG_HTTP_HOST
// CONFIG_HTTP_PORT
// CONFIG_HTTP_PATH

// CONFIG_LED_GPIO
// CONFIG_LED_COUNT

#include "config.h" 

#define TAG "main"
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define MAX_HTTP_OUTPUT_BUFFER 5000

typedef struct  {
    uint8_t color[18];        //  0-17: Six 24-bit colors in RGB triplets
    uint16_t position_offset; // 18-19: Starting position of this chaser
    int8_t position_speed;    //    20: The direction and speed of motion
    uint8_t color_offset;     //    21: Starting color of this chaser (0 to 239)
    uint8_t color_speed;      //    22: The speed of color shift
    uint8_t position_delay;   //    23: Move position every n'th frame (minimum 1)
    uint8_t color_delay;      //    24: Move color every n'th frame (minimum 1)
    uint8_t repeat;           //    25: Apply this to every n'th pixel (0 = no repeat)
    uint16_t limit_low;       // 26-27: Not implemented
    uint16_t limit_high;      // 28-29: Not implemented
    uint8_t reserved[2];      // 30-31: Not implemented
} chaser_data_t;


static int retry_num=0;
static int wifi_connected = 0;
static tNeopixelContext neopixel;
chaser_data_t *chaser_data = NULL;
tNeopixel chaser_pixel[CONFIG_LED_COUNT];
int8_t chaser_count = 0;


static void wifi_event_handler(void *event_handler_arg, esp_event_base_t event_base, int32_t event_id,void *event_data) {
    if(event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi starting");
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "WiFi connected");
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected");
        wifi_connected = 0;
        if(retry_num<5) {
            esp_wifi_connect();
            retry_num++;
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            ESP_LOGI(TAG, "WiFi request connection");
        }
    } else if (event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "WiFi got IP address");
        wifi_connected = 1;
    }
}


void init_wifi()
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


esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    static char *output_buffer;  // Buffer to store response of http request from event handler
    static int output_len;       // Stores number of bytes read
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGI(TAG, "HTTP_EVENT_ERROR");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            // Clean the buffer in case of a new request
            if (output_len == 0 && evt->user_data) {
                // we are just starting to copy the output data into the use
                memset(evt->user_data, 0, MAX_HTTP_OUTPUT_BUFFER);
            }
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    // The last byte in evt->user_data is kept for the NULL character in case of out-of-bound access.
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    int content_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        // We initialize output_buffer with 0 because it is used by strlen() and similar functions therefore should be null terminated.
                        output_buffer = (char *) calloc(content_len + 1, sizeof(char));
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (content_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_REDIRECT:
            ESP_LOGI(TAG, "HTTP_EVENT_REDIRECT");
            esp_http_client_set_header(evt->client, "From", "user@example.com");
            esp_http_client_set_header(evt->client, "Accept", "text/html");
            esp_http_client_set_redirection(evt->client);
            break;
    }
    return ESP_OK;
}


static void http_rest_with_url(void)
{
    char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER + 1] = {0};
    int content_len;
    uint8_t i;
    esp_http_client_config_t config = {
        .host = CONFIG_HTTP_HOST,
        .port = CONFIG_HTTP_PORT,
        .path = CONFIG_HTTP_PATH,
        .query = "",
        .event_handler = _http_event_handler,
        .user_data = local_response_buffer,        // Pass address of local buffer to get response
        .disable_auto_redirect = true,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);

    // GET
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %"PRId64,
                esp_http_client_get_status_code(client),
                esp_http_client_get_content_length(client));
    } else {
        ESP_LOGE(TAG, "HTTP GET request failed: %s", esp_err_to_name(err));
    }

    content_len = esp_http_client_get_content_length(client);

    ESP_LOG_BUFFER_HEX(TAG, local_response_buffer, content_len); // this is the data

    chaser_data = (chaser_data_t*) local_response_buffer;
    chaser_count = content_len / 32;
    for(i=0;i<chaser_count;i++) {
        if(chaser_data[i].position_delay==0) chaser_data[i].position_delay++;
        if(chaser_data[i].color_delay==0) chaser_data[i].color_delay++;
    }
    esp_http_client_cleanup(client);
}



static void http_test_task(void *pvParameters)
{
    http_rest_with_url();
    ESP_LOGI(TAG, "HTTP requests completed");
    vTaskDelete(NULL);
}



static void flash_leds(int8_t r, int8_t g, int8_t b) {
    for(int i=0;i<CONFIG_LED_COUNT;i++) {
        // led_strip_set_pixel(led_strip, i, r, g, b);
    }
    // led_strip_refresh(led_strip);
    vTaskDelay(5 / portTICK_PERIOD_MS);
    // led_strip_clear(led_strip);
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




void app_main(void) {
    nvs_flash_init();
    init_wifi();
    neopixel = neopixel_Init(CONFIG_LED_COUNT, CONFIG_LED_GPIO);
    while(wifi_connected==0) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
        flash_leds(32,0,0);
    }
    vTaskDelay(500 / portTICK_PERIOD_MS);
    xTaskCreate(&http_test_task, "http_request", 8192, NULL, 5, NULL);
    ESP_LOGI(TAG, "HTTP request complete!");

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





