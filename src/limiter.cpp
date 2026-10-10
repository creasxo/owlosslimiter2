#include "limiter.h"
#include "loss_tracker.h"
#include "process_utils.h"
#include "screen_capture.h"
#include "ocr.h"
#include "logging.h"
#include "strutil.h"
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ctime>

namespace {
    constexpr double ERROR_LOG_THROTTLE_SECONDS = 30.0;

    const char* DetectionName(Detection d) {
        switch (d) {
            case Detection::Defeat:  return "DEFEAT";
            case Detection::Victory: return "VICTORY";
            default:                 return "NONE";
        }
    }

    std::filesystem::path AppDir() {
        wchar_t buf[MAX_PATH];
        GetModuleFileNameW(nullptr, buf, MAX_PATH);
        return std::filesystem::path(buf).parent_path();
    }

    double NowSeconds() {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    void SaveDebugCrop(const CapturedImage& img, Detection detection) {
        auto dir = AppDir() / "debug_crops";
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        std::time_t t = std::time(nullptr);
        std::tm tmBuf;
        localtime_s(&tmBuf, &t);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%H%M%S", &tmBuf);
        std::string filename = std::string(ts) + "_" + DetectionName(detection) + ".png";
        auto bytes = EncodePng(img);
        std::ofstream out(dir / filename, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
}

Limiter::Limiter(Config cfg, std::function<void()> onFinished)
    : cfg_(std::move(cfg)), onFinished_(std::move(onFinished)) {}

Limiter::~Limiter() {
    Stop();
}

void Limiter::Start() {
    if (running_.load()) return;
    if (thread_.joinable()) thread_.join();
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread(&Limiter::Run, this);
}

void Limiter::Stop() {
    {
        std::lock_guard<std::mutex> lock(cvMutex_);
        stopRequested_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
    running_ = false;
}

bool Limiter::InterruptibleWait(double seconds) {
    std::unique_lock<std::mutex> lock(cvMutex_);
    auto dur = std::chrono::duration<double>(seconds);
    cv_.wait_for(lock, dur, [this] { return stopRequested_; });
    return stopRequested_;
}

void Limiter::Run() {
    auto& log = Logger::Instance();
    std::wstring tesseractExe = ResolveTesseractPath(cfg_);
    LossTracker tracker(cfg_.losses);
    bool gameWasRunning = true;

    auto lastCaptureErrorLog = std::chrono::steady_clock::now() - std::chrono::seconds(3600);
    auto lastOcrErrorLog     = lastCaptureErrorLog;

    log.Info("Limiter started - limit=" + std::to_string(cfg_.losses) +
             " poll=" + std::to_string(cfg_.poll) + "s");

    while (true) {
        {
            std::lock_guard<std::mutex> lock(cvMutex_);
            if (stopRequested_) break;
        }

        DWORD pid = FindProcessId(cfg_.process);
        if (pid == 0) {
            if (gameWasRunning) log.Info(cfg_.process + " not running - waiting...");
            gameWasRunning = false;
            if (InterruptibleWait(5.0)) break;
            continue;
        }
        if (!gameWasRunning) log.Info(cfg_.process + " found (pid " + std::to_string(pid) + ").");
        gameWasRunning = true;

        CapturedImage img;
        std::string error;
        bool captured = CaptureRoi(img, error);

        Detection detection = Detection::None;
        if (captured) {
            std::string ocrError;
            detection = RunOcr(img, tesseractExe, ocrError);
            if (!ocrError.empty()) {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - lastOcrErrorLog).count() > ERROR_LOG_THROTTLE_SECONDS) {
                    log.Info("OCR error: " + ocrError);
                    lastOcrErrorLog = now;
                }
            }
        } else {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration<double>(now - lastCaptureErrorLog).count() > ERROR_LOG_THROTTLE_SECONDS) {
                log.Info("Capture error: " + error);
                lastCaptureErrorLog = now;
            }
        }

        if (cfg_.debug && captured) SaveDebugCrop(img, detection);

        bool wasIdle = tracker.Idle();
        int lossesBefore = tracker.Losses();
        auto event = tracker.Update(detection, NowSeconds());
        if (wasIdle && !tracker.Idle()) {
            log.Info(std::string(DetectionName(detection)) + " detected");
        }

        switch (event) {
        case LossTracker::Event::Win:
            log.Info("Win - streak reset (was " + std::to_string(lossesBefore) + ")");
            break;
        case LossTracker::Event::Loss:
            log.Info("Loss streak: " + std::to_string(tracker.Losses()) + "/" + std::to_string(cfg_.losses));
            break;
        case LossTracker::Event::LimitReached:
            log.Info("Loss streak: " + std::to_string(tracker.Losses()) + "/" + std::to_string(cfg_.losses));
            log.Info("Loss limit reached - closing " + cfg_.process + ".");
            KillProcess(pid, [&](const std::string& m) { log.Info(m); });
            running_ = false;
            if (onFinished_) onFinished_();
            return;
        case LossTracker::Event::None:
            break;
        }

        if (InterruptibleWait(cfg_.poll)) break;
    }
}
