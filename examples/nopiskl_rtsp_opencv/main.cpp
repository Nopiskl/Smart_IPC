#include "example_runner.h"
// MPI 采集，OpenCV 添加 FPS，硬件 H.264 编码并通过 RTSP 推流。
int main(int argc, char **argv) {
    return nopiskl::run_example(argc, argv, false);
}
