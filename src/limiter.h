#pragma once
#include "config.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <functional>

class Limiter {
public:
    // onFinished runs on the worker thread when the limiter stops by itself
    // (after closing the game). It is not called for Stop().
    Limiter(Config cfg, std::function<void()> onFinished);
    ~Limiter();

    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void Run();
    bool InterruptibleWait(double seconds);

    Config cfg_;
    std::function<void()> onFinished_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex cvMutex_;
    std::condition_variable cv_;
    bool stopRequested_ = false;
};
