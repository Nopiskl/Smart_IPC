#include "nopiskl/media/media.h"
#include "nopiskl/media/factory.h"
#include "nopiskl/common/system.h"
#include "rk_mpi_sys.h"
#include "rk_mpi_vi.h"
#include "rk_mpi_venc.h"
#include "rk_mpi_mb.h"
#include "rk_aiq_user_api2_sysctl.h"
#include "rtsp_demo.h"
#include "nopiskl/ai/detector.h"
#include "opencv2/core.hpp"
#include "opencv2/imgproc.hpp"
#include "opencv2/highgui.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

namespace nopiskl {
static void check(int ret, const char *operation) {
    if (ret != RK_SUCCESS) {
        char message[160];
        std::snprintf(message, sizeof(message), "%s failed (0x%x)", operation,
                      static_cast<unsigned>(ret));
        throw std::runtime_error(message);
    }
}
// Get/Release 严格配对，异常也不会遗留驱动帧或码流。
struct ViFrame {
    VIDEO_FRAME_INFO_S frame{};
    bool held = false;
    ~ViFrame() {
        if (held) {
            RK_MPI_VI_ReleaseChnFrame(0, 0, &frame);
        }
    }
};
struct EncodedFrame {
    VENC_PACK_S pack{};
    VENC_STREAM_S stream{};
    bool held = false;
    EncodedFrame() {
        stream.pstPack = &pack;
    }
    ~EncodedFrame() {
        if (held) {
            RK_MPI_VENC_ReleaseStream(0, &stream);
        }
    }
};

class RkMedia : public Media {
    Json config_;
    bool use_ai_, capture_, ready_ = false, sys_ = false, isp_running_ = false;
    rk_aiq_sys_ctx_t *isp_ = nullptr;
    bool vi_dev_ = false, vi_channel_ = false, encoder_ = false;
    MB_POOL pool_ = MB_INVALID_POOLID;
    MB_BLK block_ = MB_INVALID_HANDLE;
    rtsp_demo_handle rtsp_ = nullptr;
    rtsp_session_handle session_ = nullptr;
    YoloV5Detector detector_;
    cv::VideoCapture camera_;
    cv::Mat raw_, display_, rgb_, nv12_;
    std::vector<Detection> detections_;
    std::string error_;
    uint64_t frames_ = 0, last_frame_ = 0, fps_since_ = 0;
    unsigned fps_frames_ = 0;
    double fps_ = 0;
    RK_U32 time_ref_ = 0;

    void close() {
        ready_ = false;
        if (rtsp_) {
            rtsp_del_demo(rtsp_);
        }
        rtsp_ = nullptr;
        session_ = nullptr;
        if (encoder_) {
            RK_MPI_VENC_StopRecvFrame(0);
            RK_MPI_VENC_DestroyChn(0);
        }
        encoder_ = false;
        if (vi_channel_) {
            RK_MPI_VI_DisableChn(0, 0);
        }
        vi_channel_ = false;
        if (vi_dev_) {
            RK_MPI_VI_DisableDev(0);
        }
        vi_dev_ = false;
        camera_.release();
        if (isp_running_) {
            rk_aiq_uapi2_sysctl_stop(isp_, false);
        }
        if (isp_) {
            rk_aiq_uapi2_sysctl_deinit(isp_);
        }
        isp_running_ = false;
        isp_ = nullptr;
        raw_.release();
        display_.release();
        rgb_.release();
        nv12_.release();
        if (block_) {
            RK_MPI_MB_ReleaseMB(block_);
        }
        block_ = MB_INVALID_HANDLE;
        if (pool_ != MB_INVALID_POOLID) {
            RK_MPI_MB_DestroyPool(pool_);
        }
        pool_ = MB_INVALID_POOLID;
        detector_.reset();
        detections_.clear();
        if (sys_) {
            RK_MPI_SYS_Exit();
        }
        sys_ = false;
        last_frame_ = 0;
    }
    void load_model() {
        if (use_ai_ && config_.at("ai").at("enabled").get<bool>()) {
            const auto &ai = config_.at("ai");
            detector_.load(ai.at("model_path"), ai.at("labels_path"));
        }
    }
    void open() {
        const auto &v = config_.at("video");
        int w = v.at("width"), h = v.at("height"), fps = v.at("fps");
        check(RK_MPI_SYS_Init(), "SYS_Init");
        sys_ = true;
        if (capture_) {
            if (!camera_.open(0)) {
                throw std::runtime_error("OpenCV camera open failed");
            }
            camera_.set(cv::CAP_PROP_FRAME_WIDTH, w);
            camera_.set(cv::CAP_PROP_FRAME_HEIGHT, h);
        } else {
            if (RK_MPI_VI_GetDevIsEnable(0) == RK_SUCCESS) {
                throw std::runtime_error("Camera is occupied; stop the existing IPC service first");
            }
            // 自己持有 ISP 上下文，prepare/start 失败也能 deinit，避免示例库的隐藏全局状态。
            rk_aiq_static_info_t sensor{};
            check(rk_aiq_uapi2_sysctl_enumStaticMetasByPhyId(0, &sensor), "ISP sensor enumeration");
            setenv("HDR_MODE", "0", 1);
            check(rk_aiq_uapi2_sysctl_preInit_devBufCnt(sensor.sensor_info.sensor_name, "rkraw_rx",
                                                        2),
                  "ISP buffers");
            isp_ = rk_aiq_uapi2_sysctl_init(sensor.sensor_info.sensor_name, "/etc/iqfiles", nullptr,
                                            nullptr);
            if (!isp_) {
                throw std::runtime_error("ISP initialization failed");
            }
            check(rk_aiq_uapi2_sysctl_prepare(isp_, 0, 0, RK_AIQ_WORKING_MODE_NORMAL),
                  "ISP prepare");
            check(rk_aiq_uapi2_sysctl_start(isp_), "ISP start");
            isp_running_ = true;
            VI_DEV_ATTR_S dev{};
            int ret = RK_MPI_VI_GetDevAttr(0, &dev);
            if (ret == RK_ERR_VI_NOT_CONFIG) {
                check(RK_MPI_VI_SetDevAttr(0, &dev), "VI_SetDevAttr");
            } else {
                check(ret, "VI_GetDevAttr");
            }
            if (RK_MPI_VI_GetDevIsEnable(0) != RK_SUCCESS) {
                check(RK_MPI_VI_EnableDev(0), "VI_EnableDev");
                vi_dev_ = true;
                VI_DEV_BIND_PIPE_S bind{};
                bind.u32Num = 1;
                bind.PipeId[0] = 0;
                check(RK_MPI_VI_SetDevBindPipe(0, &bind), "VI_SetDevBindPipe");
            } else {
                throw std::runtime_error("Camera is occupied; stop the existing IPC service first");
            }
            VI_CHN_ATTR_S channel{};
            channel.stIspOpt.u32BufCount = 2;
            channel.stIspOpt.enMemoryType = VI_V4L2_MEMORY_TYPE_DMABUF;
            channel.stSize.u32Width = w;
            channel.stSize.u32Height = h;
            channel.enPixelFormat = RK_FMT_YUV420SP;
            channel.enCompressMode = COMPRESS_MODE_NONE;
            channel.u32Depth = 2;
            channel.stFrameRate.s32SrcFrameRate = -1;
            channel.stFrameRate.s32DstFrameRate = fps;
            check(RK_MPI_VI_SetChnAttr(0, 0, &channel), "VI_SetChnAttr");
            check(RK_MPI_VI_EnableChn(0, 0), "VI_EnableChn");
            vi_channel_ = true;
        }
        load_model();
        MB_POOL_CONFIG_S pool{};
        pool.u64MBSize = static_cast<uint64_t>(w) * h * 3;
        pool.u32MBCnt = 1;
        pool.enAllocType = MB_ALLOC_TYPE_DMA;
        pool_ = RK_MPI_MB_CreatePool(&pool);
        if (pool_ == MB_INVALID_POOLID) {
            throw std::runtime_error("DMA pool allocation failed");
        }
        block_ = RK_MPI_MB_GetMB(pool_, pool.u64MBSize, RK_FALSE);
        if (!block_) {
            throw std::runtime_error("DMA buffer allocation failed");
        }
        auto address = RK_MPI_MB_Handle2VirAddr(block_);
        if (!address) {
            throw std::runtime_error("DMA buffer mapping failed");
        }
        rgb_ = cv::Mat(h, w, CV_8UC3, address);
        VENC_CHN_ATTR_S enc{};
        enc.stVencAttr.enType = RK_VIDEO_ID_AVC;
        enc.stVencAttr.enPixelFormat = RK_FMT_RGB888;
        enc.stVencAttr.u32Profile = H264E_PROFILE_HIGH;
        enc.stVencAttr.u32PicWidth = w;
        enc.stVencAttr.u32PicHeight = h;
        enc.stVencAttr.u32VirWidth = w;
        enc.stVencAttr.u32VirHeight = h;
        enc.stVencAttr.u32StreamBufCnt = 2;
        enc.stVencAttr.u32BufSize = w * h * 3 / 2;
        enc.stRcAttr.enRcMode = VENC_RC_MODE_H264CBR;
        auto &rc = enc.stRcAttr.stH264Cbr;
        rc.u32BitRate = v.at("bitrate_kbps");
        rc.u32Gop = v.at("gop");
        rc.u32SrcFrameRateNum = fps;
        rc.u32SrcFrameRateDen = 1;
        rc.fr32DstFrameRateNum = fps;
        rc.fr32DstFrameRateDen = 1;
        check(RK_MPI_VENC_CreateChn(0, &enc), "VENC_CreateChn");
        encoder_ = true;
        VENC_RECV_PIC_PARAM_S receive{};
        receive.s32RecvPicNum = -1;
        check(RK_MPI_VENC_StartRecvFrame(0, &receive), "VENC_StartRecvFrame");
        rtsp_ = create_rtsp_demo(v.at("rtsp_port"));
        if (!rtsp_) {
            throw std::runtime_error("Cannot bind RTSP listener");
        }
        session_ = rtsp_new_session(rtsp_, v.at("rtsp_path").get<std::string>().c_str());
        if (!session_) {
            throw std::runtime_error("Cannot create RTSP session");
        }
        check(rtsp_set_video(session_, RTSP_CODEC_ID_VIDEO_H264, nullptr, 0), "rtsp_set_video");
        rtsp_sync_video_ts(session_, rtsp_get_reltime(), rtsp_get_ntptime());
        fps_since_ = now_ms();
        fps_frames_ = 0;
        fps_ = 0;
        ready_ = true;
        error_.clear();
    }
    void detect() {
        detections_.clear();
        const auto &ai = config_.at("ai");
        if (!use_ai_ || !ai.at("enabled").get<bool>()) {
            return;
        }
        load_model();
        const size_t bytes = static_cast<size_t>(raw_.rows - 1) * raw_.step + raw_.cols * 3;
        detector_.detect({raw_.data, raw_.cols, raw_.rows, raw_.step, bytes}, ai.at("confidence"),
                         ai.at("nms"), detections_);
    }
    void draw(cv::Mat &image) {
        for (const auto &d : detections_) {
            if (d.right <= d.left || d.bottom <= d.top) {
                continue;
            }
            cv::rectangle(image, cv::Point(d.left, d.top), cv::Point(d.right, d.bottom),
                          cv::Scalar(0, 220, 100), 2);
            char text[96];
            std::snprintf(text, sizeof(text), "%s %.1f%%", d.label.c_str(), d.confidence * 100);
            cv::putText(image, text, cv::Point(d.left, std::max(18, d.top - 6)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 220, 100), 1);
        }
    }

public:
    RkMedia(bool ai, bool capture) : use_ai_(ai), capture_(capture) {
    }
    ~RkMedia() override {
        close();
    }
    void configure(const Json &next) override {
        bool reopen = !ready_ || config_.is_null() || next.at("video") != config_.at("video");
        if (reopen) {
            close();
        }
        config_ = next;
        try {
            if (reopen) {
                open();
            } else {
                load_model();
            }
        } catch (const std::exception &e) {
            close();
            error_ = e.what();
            throw;
        }
    }
    void step() override {
        if (!ready_) {
            return;
        }
        auto start = now_ms();
        try {
            int w = config_.at("video").at("width"), h = config_.at("video").at("height");
            if (capture_) {
                camera_ >> raw_;
                if (raw_.empty()) {
                    throw std::runtime_error("OpenCV capture failed");
                }
                if (raw_.cols != w || raw_.rows != h) {
                    cv::resize(raw_, raw_, cv::Size(w, h));
                }
            } else {
                ViFrame input;
                check(RK_MPI_VI_GetChnFrame(0, 0, &input.frame, 300), "VI_GetChnFrame");
                input.held = true;
                const auto &v = input.frame.stVFrame;
                auto *bytes = static_cast<unsigned char *>(RK_MPI_MB_Handle2VirAddr(v.pMbBlk));
                if (!bytes || v.u32Width != static_cast<unsigned>(w) ||
                    v.u32Height != static_cast<unsigned>(h) ||
                    v.u32VirWidth < static_cast<unsigned>(w) ||
                    v.u32VirHeight < static_cast<unsigned>(h)) {
                    throw std::runtime_error("Unexpected VI frame layout");
                }
                RK_MPI_SYS_MmzFlushCache(v.pMbBlk, RK_TRUE);
                nv12_.create(h + h / 2, w, CV_8UC1);
                // NV12 的 UV 平面位于 virtual_height 之后，不能直接忽略 stride。
                for (int y = 0; y < h; ++y) {
                    std::memcpy(nv12_.ptr(y), bytes + y * v.u32VirWidth, w);
                }
                auto *uv = bytes + v.u32VirWidth * v.u32VirHeight;
                for (int y = 0; y < h / 2; ++y) {
                    std::memcpy(nv12_.ptr(h + y), uv + y * v.u32VirWidth, w);
                }
                cv::cvtColor(nv12_, raw_, cv::COLOR_YUV2BGR_NV12);
            }
            detect();
            raw_.copyTo(display_);
            if (config_.at("ai").at("overlay").get<bool>()) {
                draw(display_);
            }
            if (!use_ai_) {
                char text[40];
                std::snprintf(text, sizeof(text), "nopiskl FPS %.1f", fps_);
                cv::putText(display_, text, cv::Point(24, 32), cv::FONT_HERSHEY_SIMPLEX, 0.7,
                            cv::Scalar(0, 220, 100), 2);
            }
            cv::cvtColor(display_, rgb_, cv::COLOR_BGR2RGB);
            RK_MPI_SYS_MmzFlushCache(block_, RK_FALSE);
            VIDEO_FRAME_INFO_S frame{};
            frame.stVFrame.u32Width = frame.stVFrame.u32VirWidth = w;
            frame.stVFrame.u32Height = frame.stVFrame.u32VirHeight = h;
            frame.stVFrame.enPixelFormat = RK_FMT_RGB888;
            frame.stVFrame.pMbBlk = block_;
            frame.stVFrame.u32TimeRef = time_ref_++;
            frame.stVFrame.u64PTS = now_ms() * 1000;
            check(RK_MPI_VENC_SendFrame(0, &frame, 300), "VENC_SendFrame");
            EncodedFrame output;
            check(RK_MPI_VENC_GetStream(0, &output.stream, 300), "VENC_GetStream");
            output.held = true;
            auto *encoded = static_cast<uint8_t *>(RK_MPI_MB_Handle2VirAddr(output.pack.pMbBlk));
            if (!encoded) {
                throw std::runtime_error("Encoded frame mapping failed");
            }
            rtsp_tx_video(session_, encoded + output.pack.u32Offset, output.pack.u32Len,
                          output.pack.u64PTS);
            rtsp_do_event(rtsp_);
            ++frames_;
            ++fps_frames_;
            last_frame_ = now_ms();
            error_.clear();
            if (last_frame_ - fps_since_ >= 1000) {
                fps_ = 1000.0 * fps_frames_ / (last_frame_ - fps_since_);
                fps_frames_ = 0;
                fps_since_ = last_frame_;
            }
        } catch (const std::exception &e) {
            // 编码超时后驱动可能仍引用 RGB DMA；先销毁通道，再释放/重用内存。
            close();
            error_ = e.what();
            throw;
        }
        auto interval = 1000u / config_.at("video").at("fps").get<unsigned>();
        auto elapsed = now_ms() - start;
        if (elapsed < interval) {
            std::this_thread::sleep_for(std::chrono::milliseconds(interval - elapsed));
        }
    }
    Image snapshot(const Json &options) override {
        if (!ready_ || raw_.empty() || now_ms() - last_frame_ > 2000) {
            throw Error(503, "frame_unavailable", "No recent camera frame");
        }
        cv::Mat image;
        raw_.copyTo(image);
        if (options.at("overlay").get<bool>()) {
            draw(image);
        }
        bool jpeg = options.at("format") == "jpeg";
        Image out;
        out.mime = jpeg ? "image/jpeg" : "image/bmp";
        std::vector<int> params{cv::IMWRITE_JPEG_QUALITY, options.at("quality").get<int>()};
        if (!cv::imencode(jpeg ? ".jpg" : ".bmp", image, out.bytes, params)) {
            throw std::runtime_error("Image encoding failed");
        }
        return out;
    }
    Json status() const override {
        return {{"ready", ready_},
                {"state", ready_ ? (error_.empty() ? "streaming" : "degraded") : "unavailable"},
                {"simulated", false},
                {"frames", frames_},
                {"fps", fps_},
                {"detections", detections_.size()},
                {"error", error_}};
    }
};
std::unique_ptr<Media> make_media(bool ai, bool capture) {
    return std::unique_ptr<Media>(new RkMedia(ai, capture));
}
} // namespace nopiskl
