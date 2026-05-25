// std apis
#include <endian.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// esp-idf apis
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <sdkconfig.h>

// nimble stack apis
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <nimble/ble.h>
#include <nimble/nimble_npl_os.h>
#include <nimble/nimble_port.h>
#include <services/gap/ble_svc_gap.h>
#include <services/gatt/ble_svc_gatt.h>
// #include "host/ble_att.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_store.h"
#include "portmacro.h"
// #include "os/os_mbuf.h"

char name[] = "GATT BLE Server";
char short_name[] = "GBS";

ble_uuid128_t asg_svc_uuid =
    BLE_UUID128_INIT(0x11, 0x22, 0x33, 0x44, 0x11, 0x22, 0x33, 0x44, 0x11, 0x22,
                     0x33, 0x44, 0x11, 0x22, 0x33, 0x44, );

ble_uuid128_t asg_chr_uuid =
    BLE_UUID128_INIT(0x55, 0x66, 0x77, 0x88, 0x55, 0x66, 0x77, 0x88, 0x55, 0x66,
                     0x77, 0x88, 0x55, 0x66, 0x77, 0x88, );

ble_uuid128_t tsg_svc_uuid =
    BLE_UUID128_INIT(0x22, 0x33, 0x44, 0x55, 0x22, 0x33, 0x44, 0x55, 0x22, 0x33,
                     0x44, 0x55, 0x22, 0x33, 0x44, 0x55, );

ble_uuid128_t tsg_chr_uuid =
    BLE_UUID128_INIT(0x66, 0x77, 0x88, 0x99, 0x66, 0x77, 0x88, 0x99, 0x66, 0x77,
                     0x88, 0x99, 0x66, 0x77, 0x88, 0x99, );

uint16_t asg_char_attr_handle;
uint16_t tsg_char_attr_handle;

#define ACCEL_RANGE_G 16.0f
#define ATTENUATION 0.70f /* ejes secundarios = 70 % del principal */
typedef struct {
  uint32_t t;
  float ax, ay, az;
} accel_sample_t;
accel_sample_t simulate_accel(void) {
  /* Eje principal: sinusoide + ruido uniforme */
  static float phase = 0.0f;
  phase += 0.01f; /* avance de fase por muestra a 1000 Hz */
  float main_val =
      ACCEL_RANGE_G * sinf(phase) + ((float)rand() / RAND_MAX - 0.5f) * 1.0f;
  accel_sample_t s = {
      .t = esp_log_timestamp(),
      .ax = main_val,
      .ay = main_val * ATTENUATION + ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
      .az = main_val * ATTENUATION + ((float)rand() / RAND_MAX - 0.5f) * 0.5f,
  };
  return s;
}

#define TEMP_MIN 20.0f
#define TEMP_MAX 30.0f
float simulate_temperature(void) {
  static float temp = 25.0f;
  float delta = ((float)rand() / RAND_MAX - 0.5f) * 0.4f;
  temp += delta;
  if (temp < TEMP_MIN)
    temp = TEMP_MIN;
  if (temp > TEMP_MAX)
    temp = TEMP_MAX;
  return temp;
}

int chr_access(uint16_t conn_handle, uint16_t attr_handle,
               struct ble_gatt_access_ctxt *ctxt, void *arg) {
  return 0;
}

struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &asg_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &asg_chr_uuid.u,
                    .access_cb = chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                    .val_handle = &asg_char_attr_handle,
                },
                {0}},
    },
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &tsg_svc_uuid.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {
                    .uuid = &tsg_chr_uuid.u,
                    .access_cb = chr_access,
                    .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_READ,
                    .val_handle = &tsg_char_attr_handle,
                },
                {0}},
    },
    {0}};

void on_stack_reset(int reason) {
  printf("NimBLE se reinició con motivo %d\n", reason);
}

void start_adv();

uint8_t accel_notify_status = 0;
uint16_t accel_notify_attr = 0;
uint16_t accel_notify_conn = 0;

uint8_t temp_notify_status = 0;
uint16_t temp_notify_attr = 0;
uint16_t temp_notify_conn = 0;

int connection_event_handler(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    printf("Conectado con cliente %d con estado %d\n",
           event->connect.conn_handle, event->connect.status);

  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    printf("Desconectado de cliente %d por motivo %d\n",
           event->disconnect.conn.conn_handle, event->disconnect.reason);
    start_adv();

  } else if (event->type == BLE_GAP_EVENT_SUBSCRIBE) {
    printf("Subscripción de cliente %d a atributo %d actualizada a %d\n",
           event->subscribe.conn_handle, event->subscribe.attr_handle,
           event->subscribe.cur_notify);

    if (event->subscribe.attr_handle == asg_char_attr_handle) {
      accel_notify_status = event->subscribe.cur_notify;

      if (accel_notify_status) {
        accel_notify_attr = event->subscribe.attr_handle;
        accel_notify_conn = event->subscribe.conn_handle;
      } else {
        accel_notify_attr = 0;
        accel_notify_conn = 0;
      }
    } else if (event->subscribe.attr_handle == tsg_char_attr_handle) {
      temp_notify_status = event->subscribe.cur_notify;

      if (temp_notify_status) {
        temp_notify_attr = event->subscribe.attr_handle;
        temp_notify_conn = event->subscribe.conn_handle;
      } else {
        temp_notify_attr = 0;
        temp_notify_conn = 0;
      }
    }
  }

  return 0;
}

void accel_notifier_task(void *arg) {

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!accel_notify_status) {
      continue;
    }

    accel_sample_t sample = simulate_accel();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&sample, sizeof(sample));

    int err =
        ble_gatts_notify_custom(accel_notify_conn, asg_char_attr_handle, om);

    if (err) {
      printf("Notify error: %d\n", err);
    } else {
      printf("Notify OK t=%lu x=%f y=%f z=%f\n", sample.t, sample.ax, sample.ay,
             sample.az);
    }
  }
}

void temp_notifier_task(void *arg) {

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(15000));

    if (!temp_notify_status) {
      continue;
    }

    float sample = simulate_temperature();
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&sample, sizeof(sample));

    int err =
        ble_gatts_notify_custom(temp_notify_conn, tsg_char_attr_handle, om);

    if (err) {
      printf("Notify error: %d\n", err);
    } else {
      printf("Notify OK temp=%f\n", sample);
    }
  }
}

void start_adv() {
  uint16_t adv_interval = 200;
  uint8_t ble_addr_type = BLE_ADDR_PUBLIC;

  uint8_t addr_val[8];
  ble_hs_id_copy_addr(ble_addr_type, addr_val, NULL);

  struct ble_hs_adv_fields adv_fields = {
      .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,

      .name = (uint8_t *)short_name,
      .name_len = strlen(short_name),
      .name_is_complete = 0,

      .adv_itvl = BLE_GAP_ADV_ITVL_MS(adv_interval),
      .adv_itvl_is_present = 1,

      .device_addr = addr_val,
      .device_addr_type = ble_addr_type,
      .device_addr_is_present = 1,
  };
  ble_gap_adv_set_fields(&adv_fields);

  struct ble_hs_adv_fields rsp_fields = {
      .name = (uint8_t *)name,
      .name_len = strlen(name),
      .name_is_complete = 1,
  };
  ble_gap_adv_rsp_set_fields(&rsp_fields);

  struct ble_gap_adv_params adv_params = {
      .conn_mode = BLE_GAP_CONN_MODE_UND,
      .disc_mode = BLE_GAP_DISC_MODE_GEN,

      .itvl_min = BLE_GAP_ADV_ITVL_MS(adv_interval),
      .itvl_max = BLE_GAP_ADV_ITVL_MS(adv_interval + 10),
  };
  ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                    connection_event_handler, NULL);

  printf("Advertisment comenzado\n");
};

void on_stack_sync() { start_adv(); }

void app_main(void) {
  nvs_flash_init();

  nimble_port_init();

  ble_svc_gap_init();
  ble_svc_gap_device_name_set(name);

  ble_svc_gatt_init();
  ble_gatts_count_cfg(gatt_svcs);
  ble_gatts_add_svcs(gatt_svcs);

  ble_hs_cfg.reset_cb = on_stack_reset;
  ble_hs_cfg.sync_cb = on_stack_sync;
  ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

  xTaskCreate(accel_notifier_task, "AccelNotifier", 4096, NULL, 5, NULL);
  xTaskCreate(temp_notifier_task, "TempNotifier", 4096, NULL, 5, NULL);

  printf("NimBLE comenzado\n");
  nimble_port_run();

  printf("NimBLE se detuvo inesperadamente, reiniciando en 3 segundos");
  sleep(3);

  esp_restart();
}
