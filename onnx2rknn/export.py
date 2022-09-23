from rknn.api import RKNN

OUTPUT_PATH="./install/"
ONNX_MODEL = './onnx2rknn/visdrone_yolov5s_leakyrelu_1280_rm_reshape.onnx'
DATASET="/workspace/yolov5_tracker/yolov5_rknn/dataset/VisDrone2019/VisDrone2019-MOT-val/sequences/uav0000086_00000_v_datasets.txt"
RKNN_MODEL = OUTPUT_PATH+"visdrone_yolov5s_leakyrelu_1280_rm_reshape.rknn"

QUANTIZE_ON = True

if __name__ == '__main__':

  # Create RKNN object
  rknn = RKNN(verbose=True)

  # pre-process config
  print('--> Config model')
  rknn.config(mean_values=[[0,0,0]], std_values=[[255,255,255]],target_platform='rk3588')
  print('done')

  # Load ONNX model
  print('--> Loading model')
  ret = rknn.load_onnx(model=ONNX_MODEL)
  rknn.load_pytorch
  if ret != 0:
      print('Load model failed!')
      exit(ret)
  print('done')

  # Build model
  print('--> Building model')
  ret = rknn.build(do_quantization=True,dataset=DATASET)
  if ret != 0:
      print('Build model failed!')
      exit(ret)
  print('done')

  # Export RKNN model
  print('--> Export rknn model')
  ret = rknn.export_rknn(RKNN_MODEL)
  if ret != 0:
      print('Export rknn model failed!')
      exit(ret)
  print('done')