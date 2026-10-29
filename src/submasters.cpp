#include "engine.h"
#include <cmath>
using namespace Flame;

bool Engine::setSubmasterLevel(int index, double level) {
    if (index < 0 || index >= submasters.size() || !std::isfinite(level) || level < 0 || level > 1) return false;
    auto &sub = submasters[index];
    sub.level = level; sub.phase = Submaster::Phase::Idle;
    sub.pressed = false; sub.elapsed = 0;
    return true;
}

bool Engine::bumpSubmaster(int index, bool pressed) {
    if (index < 0 || index >= submasters.size()) return false;
    auto &sub = submasters[index];
    if (sub.pressed == pressed) return true;
    sub.pressed = pressed;
    auto start = [&](Submaster::Phase phase) {
        sub.phase = phase; sub.start = sub.level; sub.elapsed = 0;
    };
    if (pressed) {
        if (sub.dwell == Submaster::Dwell::Hold && sub.phase != Submaster::Phase::Idle &&
            sub.phase != Submaster::Phase::Down) start(Submaster::Phase::Down);
        else start(Submaster::Phase::Up);
    } else if (sub.dwell == Submaster::Dwell::Manual && sub.phase != Submaster::Phase::Idle)
        start(Submaster::Phase::Down);
    advanceSubmasters(0); // A zero-time flash must be visible without waiting for a tick.
    return true;
}

void Engine::advanceSubmasters(double dt) {
    if (!std::isfinite(dt) || dt < 0) return;
    for (auto &sub : submasters) {
        double remaining = dt;
        // At most Up -> Dwell -> Down -> Idle, including zero-time phases.
        for (int pass = 0; pass < 4 && sub.phase != Submaster::Phase::Idle; ++pass) {
            const auto phase = sub.phase;
            if (phase == Submaster::Phase::Dwell && sub.dwell != Submaster::Dwell::Timed) break;
            const double duration = phase == Submaster::Phase::Up ? sub.upTime
                : phase == Submaster::Phase::Down ? sub.downTime : sub.dwellTime;
            const double consumed = qMin(remaining, qMax(0., duration - sub.elapsed));
            sub.elapsed += consumed; remaining -= consumed;
            if (phase != Submaster::Phase::Dwell) {
                const double target = phase == Submaster::Phase::Up ? sub.target() : sub.home();
                const double fraction = duration <= 0 ? 1 : qBound(0., sub.elapsed / duration, 1.);
                sub.level = sub.start + (target - sub.start) * fraction;
            }
            if (sub.elapsed + 1e-9 < duration) break;
            sub.phase = phase == Submaster::Phase::Up ? Submaster::Phase::Dwell
                : phase == Submaster::Phase::Dwell ? Submaster::Phase::Down : Submaster::Phase::Idle;
            sub.elapsed = 0; sub.start = sub.level;
        }
    }
}
