#include "input/touch_gestures.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mhp3rd::input::touch {
namespace {

constexpr std::uint16_t kCircle = 0x2000u, kCross = 0x4000u, kStart = 0x0008u;
constexpr std::uint16_t kUp = 0x0010u, kRight = 0x0020u, kDown = 0x0040u, kLeft = 0x0080u;

// A two-finger tap: both fingers down and up within this long.
constexpr std::uint64_t kTwoFingerMs = 500u;

float distance(Point a, Point b) {
    return std::hypot(a.x - b.x, a.y - b.y);
}

} // namespace

void Gestures::set_area(const Area &area, float scale) {
    area_ = area;
    // As the fight layout's: drawn for an 844x390 phone, in its heights.
    const float h = area.height;
    const float r = 22.5f / 390.0f * h * scale;
    const float y = area.top + 60.0f / 390.0f * h;
    corners_[static_cast<std::size_t>(Corner::Start)] = {{area.left + 33.0f / 390.0f * h, y}, r, r * 1.5f};
    corners_[static_cast<std::size_t>(Corner::Menu)] = {{area.left + area.width * 0.5f, area.top + 26.0f / 390.0f * h}, r, r};
    // At the bottom middle: clear of the full pad's own buttons, which it
    // stays over to go back to gestures.
    corners_[static_cast<std::size_t>(Corner::Pad)] = {
        {area.left + area.width * 0.5f, area.top + area.height - 30.0f / 390.0f * h}, r, r};
}

Gestures::Finger *Gestures::find(std::uint64_t id) {
    for (Finger &f : fingers_)
        if (f.used && f.id == id) return &f;
    return nullptr;
}

void Gestures::finger_down(std::uint64_t id, Point at, std::uint64_t ms) {
    if (find(id) != nullptr) finger_up(id, ms);
    Finger *slot = nullptr;
    for (Finger &f : fingers_)
        if (!f.used) {
            slot = &f;
            break;
        }
    if (slot == nullptr) return;
    *slot = Finger{};
    slot->id = id;
    slot->used = true;
    slot->start = slot->last = at;
    slot->start_ms = ms;
    for (std::size_t i = 0; i < corners_.size(); ++i) {
        const Placed &c = corners_[i];
        if (distance(at, c.centre) <= std::max(c.radius, c.half_width)) {
            slot->corner = static_cast<Corner>(i);
            ++haptics_;
            if (*slot->corner == Corner::Menu) menu_ = true;
            if (*slot->corner == Corner::Pad) show_pad_ = true;
            return;
        }
    }
    // A second finger on the screen, both still: the start of a two-finger tap.
    for (Finger &other : fingers_) {
        if (&other == slot || !other.used || other.corner || other.dragged) continue;
        if (ms - other.start_ms > kTwoFingerMs) continue;
        other.multi = slot->multi = true;
    }
}

void Gestures::finger_move(std::uint64_t id, Point at, std::uint64_t) {
    Finger *f = find(id);
    if (f == nullptr) return;
    f->last = at;
    if (!f->corner && distance(at, f->start) > kTapSlop * area_.height) f->dragged = true;
}

void Gestures::finger_up(std::uint64_t id, std::uint64_t ms) {
    Finger *f = find(id);
    if (f == nullptr) return;
    const Finger done = *f;
    *f = Finger{};
    if (done.corner || done.dragged) {
        // A drag in a two-finger group spoils the tap for the other finger too.
        if (done.multi)
            for (Finger &other : fingers_)
                if (other.used && other.multi) other.dragged = true;
        return;
    }
    const bool quick = ms - done.start_ms <= (done.multi ? kTwoFingerMs : kTapMs);
    if (done.multi) {
        // The two-finger tap goes off when its last finger lifts.
        bool others = false;
        for (Finger &other : fingers_)
            if (other.used && other.multi) {
                others = true;
                if (!quick) other.dragged = true;
            }
        if (!others && quick) {
            circle_until_ = ms + kPressMs;
            ++haptics_;
        }
        return;
    }
    if (!quick) return;
    cross_until_ = ms + kPressMs;
    tap_at_ = done.start;
    tap_ms_ = ms;
    tapped_ = true;
    ++haptics_;
}

void Gestures::release_all() {
    fingers_ = {};
    cross_until_ = circle_until_ = 0u;
    tapped_ = menu_ = show_pad_ = false;
    haptics_ = 0;
}

std::uint16_t Gestures::buttons(std::uint64_t ms) const {
    std::uint16_t buttons = 0u;
    if (ms < cross_until_) buttons |= kCross;
    if (ms < circle_until_) buttons |= kCircle;
    const float dead = kDragDeadZone * area_.height;
    for (const Finger &f : fingers_) {
        if (!f.used) continue;
        if (f.corner == Corner::Start) buttons |= kStart;
        if (f.corner || f.multi || !f.dragged) continue;
        // One way at a time, the one the drag goes most: menus move along
        // one axis, and a slanted drag must not move the cursor twice.
        const float dx = f.last.x - f.start.x;
        const float dy = f.last.y - f.start.y;
        if (std::max(std::fabs(dx), std::fabs(dy)) <= dead) continue;
        if (std::fabs(dx) >= std::fabs(dy))
            buttons |= dx > 0.0f ? kRight : kLeft;
        else
            buttons |= dy > 0.0f ? kDown : kUp;
    }
    return buttons;
}

bool Gestures::held(Corner which) const {
    for (const Finger &f : fingers_)
        if (f.used && f.corner == which) return true;
    return false;
}

bool Gestures::take_menu() {
    return std::exchange(menu_, false);
}

bool Gestures::take_show_pad() {
    return std::exchange(show_pad_, false);
}

int Gestures::take_haptics() {
    return std::exchange(haptics_, 0);
}

std::optional<Point> Gestures::last_tap(std::uint64_t ms, std::uint64_t within) const {
    if (!tapped_ || ms - tap_ms_ > within) return std::nullopt;
    return tap_at_;
}

} // namespace mhp3rd::input::touch
