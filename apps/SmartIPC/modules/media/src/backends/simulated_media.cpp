#include "nopiskl/media/media.h"
#include "nopiskl/media/factory.h"
#include "nopiskl/config/config.h"
#include <chrono>
#include <thread>
namespace nopiskl {
// 主机联调用替身：不声称存在摄像头/NPU/RTSP，生成可下载的 BMP 测试图。
class SimulatedMedia : public Media {
    Json config_;
    uint64_t frames_ = 0;

public:
    void configure(const Json &c) override {
        validate_config(c);
        config_ = c;
    }
    void step() override {
        ++frames_;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Image snapshot(const Json &) override {
        constexpr int width = 320, height = 240, row = width * 3, size = 54 + row * height;
        Image img;
        img.mime = "image/bmp";
        img.bytes.assign(size, 0);
        auto put = [&](int off, uint32_t value, int count) {
            for (int i = 0; i < count; ++i) {
                img.bytes[off + i] = (value >> (8 * i)) & 255;
            }
        };
        img.bytes[0] = 'B';
        img.bytes[1] = 'M';
        put(2, size, 4);
        put(10, 54, 4);
        put(14, 40, 4);
        put(18, width, 4);
        put(22, height, 4);
        put(26, 1, 2);
        put(28, 24, 2);
        put(34, row * height, 4);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                int p = 54 + y * row + x * 3;
                img.bytes[p] = x * 255 / width;
                img.bytes[p + 1] = y * 255 / height;
                img.bytes[p + 2] = 40;
            }
        }
        return img;
    }
    Json status() const override {
        return {{"ready", true}, {"state", "simulated"}, {"simulated", true}, {"frames", frames_},
                {"fps", 0},      {"detections", 0},      {"error", ""}};
    }
};
std::unique_ptr<Media> make_media(bool, bool) {
    return std::unique_ptr<Media>(new SimulatedMedia);
}
} // namespace nopiskl
