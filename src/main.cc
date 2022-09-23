/*-------------------------------------------
                Includes
-------------------------------------------*/
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <assert.h>
#include <cstdlib>

#define _BASETSD_H

#include "RgaUtils.h"
#include "im2d.h"
#include "opencv2/core/core.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/imgproc.hpp"

#include "postprocess.h"
#include "rga.h"
#include "rknn_api.h"

#define PERF_WITH_POST 1
/*-------------------------------------------
                  Functions
-------------------------------------------*/

static void dump_tensor_attr(rknn_tensor_attr *attr)
{
  printf("  index=%d, name=%s, n_dims=%d, dims=[%d, %d, %d, %d], n_elems=%d, size=%d, fmt=%s, type=%s, qnt_type=%s, "
         "zp=%d, scale=%f\n",
         attr->index, attr->name, attr->n_dims, attr->dims[0], attr->dims[1], attr->dims[2], attr->dims[3],
         attr->n_elems, attr->size, get_format_string(attr->fmt), get_type_string(attr->type),
         get_qnt_type_string(attr->qnt_type), attr->zp, attr->scale);
}

double __get_us(struct timeval t) { return (t.tv_sec * 1000000 + t.tv_usec); }

static unsigned char *load_data(FILE *fp, size_t ofst, size_t sz)
{
  unsigned char *data;
  int ret;

  data = NULL;

  if (NULL == fp)
  {
    return NULL;
  }

  ret = fseek(fp, ofst, SEEK_SET);
  if (ret != 0)
  {
    printf("blob seek failure.\n");
    return NULL;
  }

  data = (unsigned char *)malloc(sz);
  if (data == NULL)
  {
    printf("buffer malloc failure.\n");
    return NULL;
  }
  ret = fread(data, 1, sz, fp);
  return data;
}

static unsigned char *load_model(const char *filename, int *model_size)
{
  FILE *fp;
  unsigned char *data;

  fp = fopen(filename, "rb");
  if (NULL == fp)
  {
    printf("Open file %s failed.\n", filename);
    return NULL;
  }

  fseek(fp, 0, SEEK_END);
  int size = ftell(fp);

  data = load_data(fp, 0, size);

  fclose(fp);

  *model_size = size;
  return data;
}

static int saveFloat(const char *file_name, float *output, int element_size)
{
  FILE *fp;
  fp = fopen(file_name, "w");
  for (int i = 0; i < element_size; i++)
  {
    fprintf(fp, "%.6f\n", output[i]);
  }
  fclose(fp);
  return 0;
}

static int saveStr(const char *file_name, char *output, int element_size)
{
  FILE *fp;
  fp = fopen(file_name, "w");
  for (int i = 0; i < element_size; i++)
  {
    fprintf(fp, "%c", output[i]);
  }
  fclose(fp);
  return 0;
}

static int pad_img(cv::Mat ori_img, cv::Mat pad_img, int *pad_bar)
{
  int h, w;
  h = ori_img.rows;
  w = ori_img.cols;
  if (h == w)
    return 1;
  else if (h > w)
  {
    cv::copyMakeBorder(ori_img, pad_img, 0, 0, (int)((h - w) / 2.0), h - w - (int)((h - w) / 2.0), cv::BORDER_CONSTANT);
    *pad_bar = 0;
    *(pad_bar + 1) = (int)((h - w) / 2.0);
  }
  else if (w > h)
  {
    cv::copyMakeBorder(ori_img, pad_img, (int)((w - h) / 2.0), w - h - (int)((w - h) / 2.0), 0, 0, cv::BORDER_CONSTANT);
    *pad_bar = (int)((w - h) / 2.0);
    *(pad_bar + 1) = 0;
  }
  return 0;
}
static float scale_img(cv::Mat ori_img, cv::Mat S_img, int imgz = 640)
{
  assert(ori_img.cols == ori_img.rows), "不是矩形";
  float gain;
  gain = ori_img.cols / (float)imgz;
  cv::resize(ori_img, S_img, cv::Size(imgz, imgz));
  return gain;
}

static void scale_coords(int *pad_bar, float gain, BOX_RECT ori_boxes, int w, int h, BOX_RECT *box)
{

  float x1 = (float)(ori_boxes.x1);
  float y1 = (float)(ori_boxes.y1);
  float x2 = (float)(ori_boxes.x2);
  float y2 = (float)(ori_boxes.y2);

  x1 = x1 * gain - *(pad_bar + 1);
  y1 = y1 * gain - *(pad_bar);
  x2 = x2 * gain - *(pad_bar + 1);
  y2 = y2 * gain - *(pad_bar);

  x1 = x1 < 0 ? 0 : x1;
  y1 = y1 < 0 ? 0 : y1;
  x2 = x2 > w ? w : x2;
  y2 = y2 > h ? h : y2;

  box->x1 = (int)x1;
  box->y1 = (int)y1;
  box->x2 = (int)x2;
  box->y2 = (int)y2;
}

/*-------------------------------------------
                  Main Functions
-------------------------------------------*/
int main(int argc, char **argv)
{
  int status = 0;
  char *model_name = NULL;
  rknn_context ctx;
  size_t actual_size = 0;
  int img_width = 0;
  int img_height = 0;
  int img_channel = 0;
  const float nms_threshold = NMS_THRESH;
  const float box_conf_threshold = BOX_THRESH;
  struct timeval start_time, stop_time;
  int ret;

  int seed = time(0);
  srand(seed);

  // init rga context
  rga_buffer_t src;
  rga_buffer_t dst;
  im_rect src_rect;
  im_rect dst_rect;
  memset(&src_rect, 0, sizeof(src_rect));
  memset(&dst_rect, 0, sizeof(dst_rect));
  memset(&src, 0, sizeof(src));
  memset(&dst, 0, sizeof(dst));

  if (argc != 3)
  {
    printf("Usage: %s <rknn model> <jpg> \n", argv[0]);
    return -1;
  }

  printf("post process config: box_conf_threshold = %.2f, nms_threshold = %.2f\n", box_conf_threshold, nms_threshold);

  model_name = (char *)argv[1];
  char *image_name = argv[2];

  printf("Read %s ...\n", image_name);
  cv::Mat orig_img = cv::imread(image_name, 1);
  if (!orig_img.data)
  {
    printf("cv::imread %s fail!\n", image_name);
    return -1;
  }
  cv::Mat img;
  cv::cvtColor(orig_img, img, cv::COLOR_BGR2RGB);
  img_width = img.cols;
  img_height = img.rows;
  printf("img width = %d, img height = %d\n", img_width, img_height);

  /* Create the neural network */
  printf("Loading mode...\n");
  int model_data_size = 0;
  unsigned char *model_data = load_model(model_name, &model_data_size);
  ret = rknn_init(&ctx, model_data, model_data_size, RKNN_FLAG_COLLECT_PERF_MASK, NULL);
  if (ret < 0)
  {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }

  rknn_sdk_version version;
  ret = rknn_query(ctx, RKNN_QUERY_SDK_VERSION, &version, sizeof(rknn_sdk_version));
  if (ret < 0)
  {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }
  printf("sdk version: %s driver version: %s\n", version.api_version, version.drv_version);

  rknn_input_output_num io_num;
  ret = rknn_query(ctx, RKNN_QUERY_IN_OUT_NUM, &io_num, sizeof(io_num));
  if (ret < 0)
  {
    printf("rknn_init error ret=%d\n", ret);
    return -1;
  }
  printf("model input num: %d, output num: %d\n", io_num.n_input, io_num.n_output);

  rknn_tensor_attr input_attrs[io_num.n_input];
  memset(input_attrs, 0, sizeof(input_attrs));
  for (int i = 0; i < io_num.n_input; i++)
  {
    input_attrs[i].index = i;
    ret = rknn_query(ctx, RKNN_QUERY_INPUT_ATTR, &(input_attrs[i]), sizeof(rknn_tensor_attr));
    if (ret < 0)
    {
      printf("rknn_init error ret=%d\n", ret);
      return -1;
    }
    dump_tensor_attr(&(input_attrs[i]));
  }

  rknn_tensor_attr output_attrs[io_num.n_output];
  memset(output_attrs, 0, sizeof(output_attrs));
  for (int i = 0; i < io_num.n_output; i++)
  {
    output_attrs[i].index = i;
    ret = rknn_query(ctx, RKNN_QUERY_OUTPUT_ATTR, &(output_attrs[i]), sizeof(rknn_tensor_attr));
    dump_tensor_attr(&(output_attrs[i]));
  }

  int channel = 3;
  int width = 0;
  int height = 0;
  if (input_attrs[0].fmt == RKNN_TENSOR_NCHW)
  {
    printf("model is NCHW input fmt\n");
    channel = input_attrs[0].dims[1];
    width = input_attrs[0].dims[2];
    height = input_attrs[0].dims[3];
  }
  else
  {
    printf("model is NHWC input fmt\n");
    width = input_attrs[0].dims[1];
    height = input_attrs[0].dims[2];
    channel = input_attrs[0].dims[3];
  }

  printf("model input height=%d, width=%d, channel=%d\n", height, width, channel);

  rknn_input inputs[1];
  memset(inputs, 0, sizeof(inputs));
  inputs[0].index = 0;
  inputs[0].type = RKNN_TENSOR_UINT8;
  inputs[0].size = width * height * channel;
  inputs[0].fmt = RKNN_TENSOR_NHWC;
  inputs[0].pass_through = 0;

  // You may not need resize when src resulotion equals to dst resulotion
  void *resize_buf = nullptr;
  int pad_bar[2];
  float gain;
  if (img_width != width || img_height != height)
  {
    // printf("resize with RGA!\n");
    resize_buf = malloc(height * width * channel);
    memset(resize_buf, 0x00, height * width * channel);

    // src = wrapbuffer_virtualaddr((void *)img.data, img_width, img_height, RK_FORMAT_RGB_888);
    // dst = wrapbuffer_virtualaddr((void *)resize_buf, width, height, RK_FORMAT_RGB_888);
    // ret = imcheck(src, dst, src_rect, dst_rect);
    // if (IM_STATUS_NOERROR != ret)
    // {
    //   printf("%d, check error! %s", __LINE__, imStrError((IM_STATUS)ret));
    //   return -1;
    // }
    // IM_STATUS STATUS = imresize(src, dst);

    // for debug
    int imgz = img.rows > img.cols ? img.rows : img.cols;
    void *pad_buf = nullptr;

    printf("resize with opencv!\n");
    pad_buf = malloc(imgz * imgz * channel);
    memset(pad_buf, 0x00, imgz * imgz * channel);
    cv::Mat P_img(cv::Size(imgz, imgz), CV_8UC3, pad_buf);
    pad_img(img, P_img, pad_bar);
    cv::Mat resize_img(cv::Size(width, height), CV_8UC3, resize_buf);
    gain = scale_img(P_img, resize_img, width);

    cv::cvtColor(resize_img, img, cv::COLOR_BGR2RGB);
    cv::imwrite("resize_input.jpg", img);

    inputs[0].buf = resize_buf;
  }
  else
  {
    inputs[0].buf = (void *)img.data;
  }

  gettimeofday(&start_time, NULL);
  rknn_inputs_set(ctx, io_num.n_input, inputs);

  rknn_output outputs[io_num.n_output];
  memset(outputs, 0, sizeof(outputs));
  for (int i = 0; i < io_num.n_output; i++)
  {
    outputs[i].want_float = 0;
  }

  ret = rknn_run(ctx, NULL);
  rknn_perf_detail perf_detail;
  ret = rknn_query(ctx, RKNN_QUERY_PERF_DETAIL, &perf_detail,
                   sizeof(perf_detail));
  ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
  // rknn_mem_size mem_size;
  // ret = rknn_query(ctx, RKNN_QUERY_MEM_SIZE, &mem_size,
  //                  sizeof(mem_size));
  gettimeofday(&stop_time, NULL);
  printf("once run use %f ms\n", (__get_us(stop_time) - __get_us(start_time)) / 1000);

  // save perf
  printf("perf_detail size:%d\n", perf_detail.data_len);
  saveStr("perf_detail.txt", perf_detail.perf_data, perf_detail.data_len);
  // printf("total_weight_size:%d\n",mem_size.total_weight_size);
  // printf("total_internal_size:%d\n",mem_size.total_internal_size);
  // post process
  float scale_w = (float)width / img_width;
  float scale_h = (float)height / img_height;

  detect_result_group_t detect_result_group;
  std::vector<float> out_scales;
  std::vector<int32_t> out_zps;
  for (int i = 0; i < io_num.n_output; ++i)
  {
    out_scales.push_back(output_attrs[i].scale);
    out_zps.push_back(output_attrs[i].zp);
  }

  post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, height, width,
               box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &detect_result_group);

  printf("post_process done\n");
  // Draw Objects
  char text[256];
  BOX_RECT box;
  for (int i = 0; i < detect_result_group.count; i++)
  {
    detect_result_t *det_result = &(detect_result_group.results[i]);
    sprintf(text, "%s %.1f%%", det_result->name, det_result->prop * 100);
    scale_coords(pad_bar, gain, det_result->box, orig_img.cols, orig_img.rows, &box);
    int x1 = box.x1;
    int y1 = box.y1;
    int x2 = box.x2;
    int y2 = box.y2;
    printf("%s @ (%d %d %d %d) %f\n", det_result->name, x1, y1, x2, y2, det_result->prop);
    cv::Scalar color = cv::Scalar(rand() % 255, rand() % 255, rand() % 255);
    rectangle(orig_img, cv::Point(x1, y1), cv::Point(x2, y2), color, 3, 16);
    int baseline = 1;
    auto tsize = cv::getTextSize(text, 0, 1, 3, &baseline);
    rectangle(orig_img, cv::Point(x1, y1 - tsize.height), cv::Point(x1 + tsize.width, y1), color, -1, cv::LINE_AA);
    putText(orig_img, text, cv::Point(x1, y1 - 2), cv::FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 255, 255), 2, cv::LINE_AA);
  }

  imwrite("./out.jpg", orig_img);
  ret = rknn_outputs_release(ctx, io_num.n_output, outputs);

  // // loop test
  // int test_count = 1000;
  // gettimeofday(&start_time, NULL);
  //   for (int i = 0; i < test_count; ++i)
  //   {
  //     rknn_inputs_set(ctx, io_num.n_input, inputs);
  //     ret = rknn_run(ctx, NULL);
  //     ret = rknn_outputs_get(ctx, io_num.n_output, outputs, NULL);
  // #if PERF_WITH_POST
  //     post_process((int8_t *)outputs[0].buf, (int8_t *)outputs[1].buf, (int8_t *)outputs[2].buf, height, width,
  //                  box_conf_threshold, nms_threshold, scale_w, scale_h, out_zps, out_scales, &detect_result_group);
  // #endif
  //     ret = rknn_outputs_release(ctx, io_num.n_output, outputs);
  //   }
  //   gettimeofday(&stop_time, NULL);
  //   printf("loop count = %d , average run  %f ms\n", test_count,
  //          (__get_us(stop_time) - __get_us(start_time)) / 1000.0 / test_count);

  deinitPostProcess();

  // release
  ret = rknn_destroy(ctx);

  if (model_data)
  {
    free(model_data);
  }

  if (resize_buf)
  {
    free(resize_buf);
  }

  return 0;
}
