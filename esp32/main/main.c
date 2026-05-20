// std apis
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <endian.h>
#include <math.h>

// esp-idf apis
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_system.h>
#include <esp_random.h>
#include <esp_log.h>
#include <nvs_flash.h>

// nimble stack apis
#include <nimble/ble.h>
#include <nimble/nimble_port.h>
#include <nimble/nimble_npl_os.h>
#include <host/ble_hs.h>
#include <host/ble_uuid.h>
#include <host/ble_gap.h>
#include <host/ble_gatt.h>
#include <services/gatt/ble_svc_gatt.h>
#include <services/gap/ble_svc_gap.h>
#include "host/ble_att.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_id.h"
#include "host/ble_store.h"
#include "os/os_mbuf.h"

char name[] = "Acceleration Sample Generator";
char short_name[] = "ASG";

ble_uuid128_t asg_svc_uuid = BLE_UUID128_INIT(
     0x11, 0x22, 0x33, 0x44,
     0x11, 0x22, 0x33, 0x44,
     0x11, 0x22, 0x33, 0x44,
     0x11, 0x22, 0x33, 0x44,
    );

ble_uuid128_t asg_chr_uuid = BLE_UUID128_INIT(
     0x55, 0x66, 0x77, 0x88,
     0x55, 0x66, 0x77, 0x88,
     0x55, 0x66, 0x77, 0x88,
     0x55, 0x66, 0x77, 0x88,
    );

uint16_t asg_char_attr_handle;

#define ACCEL_RANGE_G 16.0f
#define ATTENUATION 0.70f /* ejes secundarios = 70 % del principal */
typedef struct { float ax, ay, az; } accel_sample_t;
accel_sample_t simulate_accel(void) {
  /* Eje principal: sinusoide + ruido uniforme */
  static float phase = 0.0f;
  phase += 0.01f; /* avance de fase por muestra a 1000 Hz */
  float main_val = ACCEL_RANGE_G * sinf(phase)
    + ((float)rand() / RAND_MAX - 0.5f) * 1.0f;
  accel_sample_t s = {
    .ax = main_val,
    .ay = main_val * ATTENUATION + ((float)rand()/RAND_MAX - 0.5f)*0.5f,
    .az = main_val * ATTENUATION + ((float)rand()/RAND_MAX - 0.5f)*0.5f,
  };
  return s;
}

int chr_access(uint16_t conn_handle, uint16_t attr_handle,
    struct ble_gatt_access_ctxt *ctxt, void *arg) {

  if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
    if (attr_handle != asg_char_attr_handle) {
      return BLE_ATT_ERR_ATTR_NOT_FOUND;
    }

    accel_sample_t accel_sample = simulate_accel();
    os_mbuf_append(ctxt->om, &accel_sample, sizeof(accel_sample));


    printf("Se leyó el valores x=%f, y=%f, z=%f de la caracteristica accel_sample\n", accel_sample.ax, accel_sample.ay, accel_sample.az);
    return 0;
  }

  return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
}

struct ble_gatt_svc_def gatt_svcs[] = {
  {
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = &asg_svc_uuid.u,
    .characteristics = (struct ble_gatt_chr_def[]){
      {
        .uuid = &asg_chr_uuid.u,
        .access_cb = chr_access,
        .flags = BLE_GATT_CHR_F_READ,
        .val_handle = &asg_char_attr_handle,
      },
      {0}
    }
  },
  {0}
};

void on_stack_reset(int reason) {
  printf("NimBLE se reinició con motivo %d\n", reason);
}

void start_adv();

int connection_event_handler(struct ble_gap_event *event, void *arg) {
  if (event->type == BLE_GAP_EVENT_CONNECT) {
    printf("Conectado con cliente %d con estado %d\n", event->connect.conn_handle, event->connect.status);

  } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
    printf("Desconectado de cliente %d por motivo %d\n", event->disconnect.conn.conn_handle, event->disconnect.reason);
    start_adv();
  }

  return 0;
}

void start_adv() {
  uint16_t adv_interval = 200;
  uint8_t ble_addr_type = BLE_ADDR_PUBLIC;

  uint8_t addr_val[8];
  ble_hs_id_copy_addr(ble_addr_type, addr_val, NULL);

  struct ble_hs_adv_fields adv_fields = {
    .flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP,

    .name = (uint8_t*)short_name,
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
    .name = (uint8_t*)name,
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
  ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, connection_event_handler, NULL);

  printf("Advertisment comenzado\n");
};

void on_stack_sync() {
  start_adv();
}

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

  printf("NimBLE comenzado\n");
  nimble_port_run();

  printf("NimBLE se detuvo inesperadamente, reiniciando en 3 segundos");
  sleep(3);

  esp_restart();
}
