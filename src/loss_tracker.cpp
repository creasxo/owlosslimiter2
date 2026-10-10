#include "loss_tracker.h"
#include <algorithm>

void LossTracker::Enter(State s, double now) {
    state_ = s;
    enteredAt_ = now;
    recent_.clear();
}

LossTracker::Event LossTracker::Update(Detection detection, double now) {
    double elapsed = now - enteredAt_;

    switch (state_) {
    case State::InGame: {
        recent_.push_back(detection);
        if (recent_.size() > DEBOUNCE_COUNT) recent_.pop_front();
        if (recent_.size() < DEBOUNCE_COUNT) return Event::None;

        Detection first = recent_.front();
        if (first == Detection::None) return Event::None;
        if (!std::all_of(recent_.begin(), recent_.end(), [&](Detection d) { return d == first; }))
            return Event::None;

        pendingLoss_ = (first == Detection::Defeat);
        Enter(State::ResultScreen, now);
        return Event::None;
    }

    case State::ResultScreen:
        if (elapsed < RESULT_HOLD_SECONDS) return Event::None;
        Enter(State::RankScreen, now);
        if (!pendingLoss_) {
            losses_ = 0;
            return Event::Win;
        }
        ++losses_;
        return losses_ >= limit_ ? Event::LimitReached : Event::Loss;

    case State::RankScreen:
        // Wait for the banner to actually leave the screen before re-arming,
        // otherwise a long-lingering result would be counted again.
        if (elapsed >= RANK_HOLD_SECONDS && detection == Detection::None) Enter(State::InGame, now);
        return Event::None;
    }
    return Event::None;
}
