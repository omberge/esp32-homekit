#include <string.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "nvs_flash.h"
#include "my_ssid.h"

#include "hap.h"

#define TAG "GARAGEDOOR"

#define ACCESSORY_NAME  "GARAGE DOOR"
#define MANUFACTURER_NAME   "OLAV"
#define MODEL_NAME  "ESP32_GARAGE_OPENER"
#define PINCODE "111-11-111"
#define ARRAY_SIZE(array) (sizeof(array) / sizeof(array[0]))

static gpio_num_t ONBOARD_BLUE_LED = GPIO_NUM_2;
static gpio_num_t SWITCH1_PORT = GPIO_NUM_22;
static gpio_num_t SWITCH2_PORT = GPIO_NUM_23;
static gpio_num_t BUTTON_PORT = GPIO_NUM_0;
static gpio_num_t RELAY_PORT = GPIO_NUM_4;

// FreeRTOS event group to signal when we are connected & ready to make a request
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// The event group allows multiple bits for each event, but we only care about one event - are we connected to the AP with an IP?
static void* a;
static void* _ev_handle_target;
static void* _ev_handle_currentstate;
static void* _ev_handle_obstructed;

int garagedoor_target = 0;
int garagedoor_currentstate = 0;
bool garagedoor_obstructed  = false;

void current_state_monitoring_task(void* arm)
{
  while(1)
  {
    
    static int i = 0;

    if (!gpio_get_level(SWITCH1_PORT) && !gpio_get_level(SWITCH2_PORT))
      i = 0; // OPEN
    else if (!gpio_get_level(SWITCH1_PORT) && gpio_get_level(SWITCH2_PORT))
      i = 1; // CLOSED
    else if (gpio_get_level(SWITCH1_PORT) && !gpio_get_level(SWITCH2_PORT))
      i = 2; // OPENING
    else if (gpio_get_level(SWITCH1_PORT) && gpio_get_level(SWITCH2_PORT))
      i = 3; // CLOSING

    if (!gpio_get_level(BUTTON_PORT)) // Low when button is pressed
    {
      i = 4; // OBSTRUCTED
      garagedoor_obstructed = true;
    }
    else
    {
      garagedoor_obstructed = false;
    }

    
    if (garagedoor_currentstate != i) // Only notify on change
    {
      garagedoor_currentstate = i;
      switch(garagedoor_currentstate)
      {
        case 0: printf("==>> [MAIN] garagedoor_currentstate = %d OPEN      \n", garagedoor_currentstate); break;
        case 1: printf("==>> [MAIN] garagedoor_currentstate = %d CLOSED    \n", garagedoor_currentstate); break;
        case 2: printf("==>> [MAIN] garagedoor_currentstate = %d OPENING   \n", garagedoor_currentstate); break;
        case 3: printf("==>> [MAIN] garagedoor_currentstate = %d CLOSING   \n", garagedoor_currentstate); break;
        case 4: printf("==>> [MAIN] garagedoor_currentstate = %d STOPPED   \n", garagedoor_currentstate); break;
      }

      if (_ev_handle_currentstate)
        hap_event_response(a, _ev_handle_currentstate, (void*)garagedoor_currentstate);

      if (_ev_handle_obstructed)
        hap_event_response(a, _ev_handle_obstructed, (void*)garagedoor_obstructed);

    }
    
    vTaskDelay( 1000 / portTICK_RATE_MS ); // Run every 1 second
  }
  
}



///////////////////////////////////////////////////////////////////////////////////////////
void* garagedoor_gettarget(void* arg)
{
  printf("==>> [MAIN] garagedoor_gettarget\n");
  return (void*)garagedoor_target;
}

void garagedoor_settarget(void* arg, void* value, int len)
{

  garagedoor_target = (int)value;
  printf("==>> [MAIN] garagedoor_settarget: %d\n", garagedoor_target);

  if (value)
  {
    gpio_set_level(ONBOARD_BLUE_LED, 0);
  }
  else
  {
    gpio_set_level(ONBOARD_BLUE_LED, 1);
  }

  gpio_set_level(RELAY_PORT, 1);
  vTaskDelay(50);
  gpio_set_level(RELAY_PORT, 0);

  if (_ev_handle_target)
  {
    hap_event_response(a, _ev_handle_target, (void*)garagedoor_target);
  }

  return;
}

void garagedoor_target_notify(void* arg, void* ev_handle, bool enable)
{
  printf("==>> [MAIN] garagedoor_target_notify\n");
  if (enable)
  {
    _ev_handle_target = ev_handle;
  }
  else
  {
    _ev_handle_target = NULL;
  }
  return;
}

///////////////////////////////////////////////////////////////////////////////////////////
void* garagedoor_getcurrentstate(void* arg)
{
  printf("==>> [MAIN] garagedoor_getcurrentstate\n");
  return (void*)garagedoor_currentstate;
}

void garagedoor_currentstate_notify(void* arg, void* ev_handle, bool enable)
{
  printf("==>> [MAIN] garagedoor_currentstate_notify\n");
  if (enable)
  {
    _ev_handle_currentstate = ev_handle;
  }
  else
  {
    _ev_handle_currentstate = NULL;
  }
  return;
}

///////////////////////////////////////////////////////////////////////////////////////////
void* garagedoor_getobstruction(void* arg)
{
  printf("==>> [MAIN] garagedoor_getobstruction\n");
  return (void*)garagedoor_obstructed;
}

void garagedoor_obstructed_notify(void* arg, void* ev_handle, bool enable)
{
  printf("==>> [MAIN] garagedoor_obstructed_notify\n");
  if (enable)
  {
    _ev_handle_obstructed = ev_handle;
  }
  else
  {
    _ev_handle_obstructed = NULL;
  }
  return;
}

///////////////////////////////////////////////////////////////////////////////////////////

void* identify_read(void* arg)
{
  return (void*)true;
}

void hap_object_init(void* arg)
{
  void* accessory_object = hap_accessory_add(a);
  struct hap_characteristic cs[] = 
  {
    {HAP_CHARACTER_IDENTIFY,            (void*)true,                    NULL, identify_read,              NULL,                 NULL},
    {HAP_CHARACTER_MANUFACTURER,        (void*)MANUFACTURER_NAME,       NULL, NULL,                       NULL,                 NULL},
    {HAP_CHARACTER_MODEL,               (void*)MODEL_NAME,              NULL, NULL,                       NULL,                 NULL},
    {HAP_CHARACTER_NAME,                (void*)ACCESSORY_NAME,          NULL, NULL,                       NULL,                 NULL},
    {HAP_CHARACTER_SERIAL_NUMBER,       (void*)"0123456789",            NULL, NULL,                       NULL,                 NULL},
    {HAP_CHARACTER_FIRMWARE_REVISION,   (void*)"1.0",                   NULL, NULL,                       NULL,                 NULL},
  };

  hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_ACCESSORY_INFORMATION, cs, ARRAY_SIZE(cs));

  struct hap_characteristic cc[] = 
  {
    {HAP_CHARACTER_CURRENT_DOOR_STATE,  (void*)garagedoor_currentstate, NULL, garagedoor_getcurrentstate, NULL,                 garagedoor_currentstate_notify},
    {HAP_CHARACTER_TARGET_DOORSTATE,    (void*)garagedoor_target,       NULL, garagedoor_gettarget,       garagedoor_settarget, garagedoor_target_notify},
    {HAP_CHARACTER_OBSTRUCTION_DETECTED,(void*)garagedoor_obstructed,   NULL, garagedoor_getobstruction,  NULL,                 garagedoor_obstructed_notify},
  };

  hap_service_and_characteristics_add(a, accessory_object, HAP_SERVICE_GARAGE_DOOR_OPENER, cc, ARRAY_SIZE(cc));
}

static esp_err_t event_handler(void *ctx, system_event_t *event)
{
  printf("event_handler\n");
  switch(event->event_id)
  {
    ////////////////////////////////////////
    case SYSTEM_EVENT_STA_START:
      esp_wifi_connect();
    break;
    ////////////////////////////////////////
    case SYSTEM_EVENT_STA_GOT_IP:
      ESP_LOGI(TAG, "got ip:%s",
      ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
      xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
      {
        hap_init();
        uint8_t mac[6];
        esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
        char accessory_id[32] = {0,};
        sprintf(accessory_id, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        hap_accessory_callback_t callback;
        callback.hap_object_init = hap_object_init;
        a = hap_accessory_register((char*)ACCESSORY_NAME, accessory_id, (char*)PINCODE, (char*)MANUFACTURER_NAME, HAP_ACCESSORY_CATEGORY_GARAGE, 811, 1, NULL, &callback);
      }
    break;
    ////////////////////////////////////////
    case SYSTEM_EVENT_STA_DISCONNECTED:
      esp_wifi_connect();
      xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    break;
    ////////////////////////////////////////
    default:
    break;
    ////////////////////////////////////////
  }

  return ESP_OK;
}

void wifi_init_sta()
{
  wifi_event_group = xEventGroupCreate();

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL) );

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  
  wifi_config_t wifi_config =
  {
    .sta = 
    {
      .ssid = EXAMPLE_ESP_WIFI_SSID,
      .password = EXAMPLE_ESP_WIFI_PASS
    },
  };

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config) );
  ESP_ERROR_CHECK(esp_wifi_start() );

  ESP_LOGI(TAG, "wifi_init_sta finished.");
  ESP_LOGI(TAG, "connect to ap SSID:%s password: yyy",
  EXAMPLE_ESP_WIFI_SSID);
}

void app_main()
{
  ESP_ERROR_CHECK( nvs_flash_init() );

  gpio_pad_select_gpio(ONBOARD_BLUE_LED);
  gpio_set_direction(ONBOARD_BLUE_LED, GPIO_MODE_OUTPUT);

  gpio_pad_select_gpio(RELAY_PORT);
  gpio_set_direction(RELAY_PORT, GPIO_MODE_OUTPUT);

  gpio_pad_select_gpio(SWITCH1_PORT);
  gpio_set_direction(SWITCH1_PORT, GPIO_MODE_INPUT);

  gpio_pad_select_gpio(SWITCH2_PORT);
  gpio_set_direction(SWITCH2_PORT, GPIO_MODE_INPUT);

  gpio_pad_select_gpio(BUTTON_PORT);
  gpio_set_direction(BUTTON_PORT, GPIO_MODE_INPUT);

  // http://esp32.info/docs/esp_idf/html/dd/d3c/group__xTaskCreate.html
  xTaskCreate( &current_state_monitoring_task, "curr_state_mon", 4096, NULL, 5, NULL );

  wifi_init_sta();
}
