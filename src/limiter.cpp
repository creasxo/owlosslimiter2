#include "limiter.h"
#include "process_utils.h"
#include "screen_capture.h"
#include "ocr.h"
#include "logging.h"
#include "strutil.h"
#include <windows.h>
#include <deque>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace {
    constexpr double RESULT_HOLD_SECONDS = 4.0;
    constexpr double RANK_HOLD_SECONDS   = 3.0;
    constexpr size_t DEBOUNCE_COUNT      = 2;
    constexpr double ERROR_LOG_THROTTLE_SECONDS = 30.0;

    enum class GameState { InGame, ResultScreen, RankScreen };

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
}

Limiter::Limiter(Config cfg) : cfg_(std::move(cfg)) {}

Limiter::~Limiter() {
    Stop();
}

void Limiter::Start() {
    if (running_.load()) return;
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

    int losses = 0;
    GameState state = GameState::InGame;
    bool pendingLoss = false;
    std::deque<Detection> debounceBuf;
    auto enteredAt = std::chrono::steady_clock::now();

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
            log.Info("Overwatch not running - waiting...");
            if (InterruptibleWait(5.0)) break;
            continue;
        }

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

        if (cfg_.debug && captured) {
            auto dir = AppDir() / "debug_crops";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            auto now = std::chrono::system_clock::now();
            std::time_t t = std::chrono::system_clock::to_time_t(now);
            std::tm tmBuf;
            localtime_s(&tmBuf, &t);
            char ts[32];
            std::strftime(ts, sizeof(ts), "%H%M%S", &tmBuf);
            std::string filename = std::string(ts) + "_" + DetectionName(detection) + ".png";
            auto bytes = EncodePng(img);
            std::ofstream out((dir / filename).string(), std::ios::binary);
            out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        }

        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - enteredAt).count();

        if (state == GameState::InGame) {
            debounceBuf.push_back(detection);
            if (debounceBuf.size() > DEBOUNCE_COUNT) debounceBuf.pop_front();

            bool allSame = debounceBuf.size() == DEBOUNCE_COUNT &&
                            std::all_of(debounceBuf.begin(), debounceBuf.end(),
                                        [&](Detection d) { return d == debounceBuf.front(); });
            Detection first = debounceBuf.empty() ? Detection::None : debounceBuf.front();

            if (allSame && (first == Detection::Defeat || first == Detection::Victory)) {
                pendingLoss = (first == Detection::Defeat);
                state = GameState::ResultScreen;
                enteredAt = std::chrono::steady_clock::now();
                debounceBuf.clear();
                log.Info(std::string(pendingLoss ? "DEFEAT" : "VICTORY") + " detected");
            }
        } else if (state == GameState::ResultScreen) {
            if (elapsed >= RESULT_HOLD_SECONDS) {
                if (pendingLoss) {
                    losses++;
                    log.Info("Loss streak: " + std::to_string(losses) + "/" + std::to_string(cfg_.losses));
                    if (losses >= cfg_.losses) {
                        log.Info("Loss limit reached - closing Overwatch.");
                        KillProcess(pid, [&](const std::string& m) { log.Info(m); });
                        return;
                    }
                } else {
                    log.Info("Win - streak reset (was " + std::to_string(losses) + ")");
                    losses = 0;
                }
                state = GameState::RankScreen;
                enteredAt = std::chrono::steady_clock::now();
            }
        } else if (state == GameState::RankScreen) {
            if (elapsed >= RANK_HOLD_SECONDS) {
                state = GameState::InGame;
                enteredAt = std::chrono::steady_clock::now();
                debounceBuf.clear();
                log.Info("Back in game.");
            }
        }

        if (InterruptibleWait(cfg_.poll)) break;
    }
}
