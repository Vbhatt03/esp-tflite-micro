//try3
/* Copyright 2019 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include "main_functions.h"

#include "detection_responder.h"
#include "image_provider.h"
#include "model_settings.h"
#include "person_detect_model_data.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_log.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <esp_log.h>
#include "esp_main.h"
#include <stdio.h>
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_http_server.h"
#include "esp_camera.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_camera.h"
#include <lwip/ip4_addr.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_timer.h"
typedef struct {
        httpd_req_t *req;
        size_t len;
} jpg_chunking_t;

#define TAG "VISIO"
#define SSID "VISIO_V1.1"
#define PASSWORD "VISIO_V1.1"

// Globals, used for compatibility with Arduino-style sketches.
namespace {
const tflite::Model* model = nullptr;
tflite::MicroInterpreter* interpreter = nullptr;
TfLiteTensor* input = nullptr;
size_t prev_jpg_buf_len;
int temp = 1024*1024;
uint8_t * prev_jpg_buf = (uint8_t *)heap_caps_malloc(temp,MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);


static size_t jpg_encode_stream(void * arg, size_t index, const void* data, size_t len){
    jpg_chunking_t *j = (jpg_chunking_t *)arg;
    if(!index){
        j->len = 0;
    }
    if(httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK){
        return 0;
    }
    j->len += len;
    return len;
}

// In order to use optimized tensorflow lite kernels, a signed int8_t quantized
// model is preferred over the legacy unsigned model format. This means that
// throughout this project, input images must be converted from unisgned to
// signed format. The easiest and quickest way to convert from unsigned to
// signed 8-bit integers is to subtract 128 from the unsigned value to get a
// signed value.

#ifdef CONFIG_IDF_TARGET_ESP32S3
constexpr int scratchBufSize = 40 * 1024;
#else
constexpr int scratchBufSize = 0;
#endif
// An area of memory to use for input, output, and intermediate arrays.
constexpr int kTensorArenaSize = 81 * 1024 + scratchBufSize;
static uint8_t *tensor_arena;//[kTensorArenaSize]; // Maybe we should move this to external
}  // namespace
esp_err_t camera_handler(httpd_req_t *req) {
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t fb_len = 0;
    int64_t fr_str = esp_timer_get_time();
    res = httpd_resp_set_type(req, "image/jpeg");
    if (res == ESP_OK) {
        res = httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    }
     printf("here1");
    if (res == ESP_OK) {
            jpg_chunking_t jchunk = {req, 0};
            MicroPrintf("sedning image with buf len %d",prev_jpg_buf_len);
            httpd_resp_send(req, (const char *)prev_jpg_buf, prev_jpg_buf_len);
    }

    //return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    int64_t fr_end = esp_timer_get_time();
    return res;
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        //ESP_LOGI(TAG, "station join, AID=%d",event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        //ESP_LOGI(TAG, "station leave, AID=%d",event->aid);
    }
}


esp_err_t mpu_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/csv");
    printf("mpu found ______________\n");
    char resp[512];
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

httpd_uri_t mpu_uri = {
    .uri      = "/mpu",
    .method   = HTTP_GET,
    .handler  = mpu_handler,
    .user_ctx = NULL
};
httpd_uri_t image_uri = {
    .uri      = "/image",
    .method   = HTTP_GET,
    .handler  = camera_handler,
    .user_ctx = NULL
};
void image_store()
{
  prev_jpg_buf_len = copy_jpg_buf_len;
  MicroPrintf("storing image with buf len %d",prev_jpg_buf_len);
  for(int i =0 ; i < copy_jpg_buf_len; i++)
  {
    prev_jpg_buf[i] = copy_jpg_buf[i]; 
  }
}

// The name of this function is important for Arduino compatibility.
void setup() {
  wifi_config_t wifi_config = {
        .ap = {
            .ssid = SSID,
            .password = PASSWORD,
            .ssid_len = strlen(SSID),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA2_WPA3_PSK,
            .max_connection = 4,
            
        },
    };

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t * p_netif = esp_netif_create_default_wifi_ap();
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        NULL));
  
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));

    ESP_ERROR_CHECK(esp_wifi_start());

  

    esp_netif_ip_info_t if_info;
    ESP_ERROR_CHECK(esp_netif_get_ip_info(p_netif, &if_info));
    ESP_LOGI(TAG, "ESP32 IP:" IPSTR, IP2STR(&if_info.ip));

    esp_netif_ip_info_t ipInfo;
    IP4_ADDR(&ipInfo.ip, 192,168,1,1);
    IP4_ADDR(&ipInfo.gw, 192,168,1,1);
    IP4_ADDR(&ipInfo.netmask, 255,255,255,0);
    esp_netif_dhcps_stop(p_netif);
    esp_netif_set_ip_info(p_netif, &ipInfo);
    esp_netif_dhcps_start(p_netif);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;
    config.core_id =1;
    config.send_wait_timeout=1;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &mpu_uri);
        httpd_register_uri_handler(server, &image_uri);
    }
  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  model = tflite::GetModel(g_person_detect_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    MicroPrintf("Model provided is schema version %d not equal to supported "
                "version %d.", model->version(), TFLITE_SCHEMA_VERSION);
    return;
  }

  if (tensor_arena == NULL) {
    tensor_arena = (uint8_t *) heap_caps_malloc(kTensorArenaSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  }
  if (tensor_arena == NULL) {
    printf("Couldn't allocate memory of %d bytes\n", kTensorArenaSize);
    return;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  //
  // tflite::AllOpsResolver resolver;
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroMutableOpResolver<5> micro_op_resolver;
  micro_op_resolver.AddAveragePool2D();
  micro_op_resolver.AddConv2D();
  micro_op_resolver.AddDepthwiseConv2D();
  micro_op_resolver.AddReshape();
  micro_op_resolver.AddSoftmax();

  // Build an interpreter to run the model with.
  // NOLINTNEXTLINE(runtime-global-variables)
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize);
  interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors.
  TfLiteStatus allocate_status = interpreter->AllocateTensors();
  if (allocate_status != kTfLiteOk) {
    MicroPrintf("AllocateTensors() failed");
    return;
  }

  // Get information about the memory area to use for the model's input.
  input = interpreter->input(0);

#ifndef CLI_ONLY_INFERENCE
  // Initialize Camera
  TfLiteStatus init_status = InitCamera();
  if (init_status != kTfLiteOk) {
    MicroPrintf("InitCamera failed\n");
    return;
  }
#endif
}

#ifndef CLI_ONLY_INFERENCE
// The name of this function is important for Arduino compatibility.
void loop() {
  // Get image from provider.
  if (kTfLiteOk != GetImage(kNumCols, kNumRows, kNumChannels, input->data.int8)) {
    MicroPrintf("Image capture failed.");
  }

  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    MicroPrintf("Invoke failed.");
  }

  TfLiteTensor* output = interpreter->output(0);

  // Process the inference results.
  int8_t person_score = output->data.uint8[kPersonIndex];
  int8_t no_person_score = output->data.uint8[kNotAPersonIndex];

  float person_score_f =
      (person_score - output->params.zero_point) * output->params.scale;
  float no_person_score_f =
      (no_person_score - output->params.zero_point) * output->params.scale;

  // Respond to detection
  image_store();
  RespondToDetection(person_score_f, no_person_score_f);
  vTaskDelay(1); // to avoid watchdog trigger
}
#endif

#if defined(COLLECT_CPU_STATS)
  long long total_time = 0;
  long long start_time = 0;
  extern long long softmax_total_time;
  extern long long dc_total_time;
  extern long long conv_total_time;
  extern long long fc_total_time;
  extern long long pooling_total_time;
  extern long long add_total_time;
  extern long long mul_total_time;
#endif

void run_inference(void *ptr) {
  /* Convert from uint8 picture data to int8 */
  for (int i = 0; i < kNumCols * kNumRows; i++) {
    input->data.int8[i] = ((uint8_t *) ptr)[i] ^ 0x80;
  }

#if defined(COLLECT_CPU_STATS)
  long long start_time = esp_timer_get_time();
#endif
  // Run the model on this input and make sure it succeeds.
  if (kTfLiteOk != interpreter->Invoke()) {
    MicroPrintf("Invoke failed.");
  }

#if defined(COLLECT_CPU_STATS)
  long long total_time = (esp_timer_get_time() - start_time);
  printf("Total time = %lld\n", total_time / 1000);
  //printf("Softmax time = %lld\n", softmax_total_time / 1000);
  printf("FC time = %lld\n", fc_total_time / 1000);
  printf("DC time = %lld\n", dc_total_time / 1000);
  printf("conv time = %lld\n", conv_total_time / 1000);
  printf("Pooling time = %lld\n", pooling_total_time / 1000);
  printf("add time = %lld\n", add_total_time / 1000);
  printf("mul time = %lld\n", mul_total_time / 1000);

  /* Reset times */
  total_time = 0;
  //softmax_total_time = 0;
  dc_total_time = 0;
  conv_total_time = 0;
  fc_total_time = 0;
  pooling_total_time = 0;
  add_total_time = 0;
  mul_total_time = 0;
#endif

  TfLiteTensor* output = interpreter->output(0);

  // Process the inference results.
  int8_t person_score = output->data.uint8[kPersonIndex];
  int8_t no_person_score = output->data.uint8[kNotAPersonIndex];

  float person_score_f =
      (person_score - output->params.zero_point) * output->params.scale;
  float no_person_score_f =
      (no_person_score - output->params.zero_point) * output->params.scale;
  RespondToDetection(person_score_f, no_person_score_f);
}
