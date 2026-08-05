#pragma once
#include "config.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

class Limiter {
public:
    explicit Limiter(Config cfg);
    ~Limiter();

    void Start();
    void Stop();
    bool IsRunning() const { return running_.load(); }

private:
    void Run();
    bool InterruptibleWait(double seconds);

    Config cfg_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    std::mutex cvMutex_;
    std::condition_variable cv_;
    bool stopRequested_ = false;
};
