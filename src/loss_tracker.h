#pragma once
#include "ocr.h"
#include <cstddef>
#include <deque>

// Turns the per-poll OCR results into a loss streak.
//
// A result only counts once it has been read on DEBOUNCE_COUNT polls in a row.
// After that the tracker sits out the rest of the end-of-match sequence
// (result banner, then the rank screen) and waits for the banner to clear,
// so one match is never counted twice.
// Time is passed in by the caller so the logic can be tested without a clock.
class LossTracker {
public:
    enum class Event { None, Win, Loss, LimitReached };

    static constexpr size_t DEBOUNCE_COUNT      = 2;
    static constexpr double RESULT_HOLD_SECONDS = 4.0;
    static constexpr double RANK_HOLD_SECONDS   = 3.0;

    explicit LossTracker(int limit) : limit_(limit) {}

    Event Update(Detection detection, double nowSeconds);

    int  Losses() const { return losses_; }
    int  Limit() const { return limit_; }
    bool Idle() const { return state_ == State::InGame; }

private:
    enum class State { InGame, ResultScreen, RankScreen };

    void Enter(State s, double now);

    int    limit_;
    int    losses_ = 0;
    State  state_ = State::InGame;
    double enteredAt_ = 0.0;
    bool   pendingLoss_ = false;
    std::deque<Detection> recent_;
};
