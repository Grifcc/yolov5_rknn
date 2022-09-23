export LD_LIBRARY_PATH=/opt/aarch64-rockchip-linux-gnu/lib64:$LD_LIBRARY_PATH
make -j 4
make install
echo "start push to rk3588:192.168.1.146,please wait"
scp  -r ./install ema@192.168.1.146:/home/ema/Project
# adb push ./install /root
