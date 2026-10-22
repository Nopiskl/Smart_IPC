#include "example_runner.h"
// 使用 OpenCV VideoCapture 采集，共享编码、推流、错误处理和退出逻辑。
int main(int argc, char **argv) {
    return nopiskl::run_example(argc, argv, true);
}
