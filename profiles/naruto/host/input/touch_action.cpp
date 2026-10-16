#include "input/touch_action.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace mhp3rd::input::touch {
namespace {

constexpr std::uint16_t kTriangle = 0x1000u, kCircle = 0x2000u, kCross = 0x4000u, kSquare = 0x8000u;
constexpr std::uint16_t kL = 0x0100u, kR = 0x0200u, kStart = 0x0008u, kSelect = 0x0001u;
constexpr std::uint16_t kUp = 0x0010u, kRight = 0x0020u, kDown = 0x0040u, kLeft = 0x0080u;

constexpr ElementInfo kInfo[kElements] = {
    {"stick", "Stick"},
    {"item", "L"},
    {"dpad_up", "D-pad Up"},
    {"dpad_down", "D-pad Down"},
    {"attack", "Main attack"},
    {"secondary", "Square"},
    {"combo", "Special (R + attack)"},
    {"evade", "Cross"},
    {"use", "Triangle"},
    {"guard", "Guard"},
    {"special", "Guard (second)"},
    {"pause", "Pause (menu)"},
    {"start", "Start"},
    {"select", "Select"},
    {"swipe", "Swipe area"},
    {"r3", "R3"},
};

ActionLayout make_default() {
    ActionLayout l;
    // To the thousandth, as settings.ini writes them, so a saved layout reads
    // back equal to this one.
    const auto set = [&](Element e, Anchor anchor, float x, float y, float size, std::uint16_t buttons) {
        const auto q = [](float v) { return std::round(v * 1000.0f) / 1000.0f; };
        l.at(e) = {anchor, q(x), q(y), q(size), buttons, true};
    };
    // Laid out for Naruto's fights, from a design for an 844x390 phone in
    // landscape: positions there, in pixels, divided by its height (390).
    // Left anchored from the left edge, right anchored from the right one
    // (844 - x). Every element is at least 44 px across.
    constexpr float kHeight = 390.0f, kWidth = 844.0f;
    // A design element: its box's top-left corner and size, in pixels.
    const auto left = [&](Element e, float x, float y, float size, std::uint16_t buttons) {
        set(e, Anchor::Left, (x + size * 0.5f) / kHeight, (y + size * 0.5f) / kHeight, size * 0.5f / kHeight, buttons);
    };
    const auto right = [&](Element e, float x, float y, float size, std::uint16_t buttons) {
        set(e, Anchor::Right, (kWidth - x - size * 0.5f) / kHeight, (y + size * 0.5f) / kHeight,
            size * 0.5f / kHeight, buttons);
    };
    // The left thumb: Start at the top corner (a 60x40 pill, touched as a
    // 44 px circle), L and R by the edge, the D-pad (the stick, drawn as a
    // cross) low and inward.
    set(Element::Start, Anchor::Left, 33.0f / kHeight, 60.0f / kHeight, 22.5f / kHeight, kStart);
    left(Element::Item, 50.0f, 230.0f, 56.0f, kL);
    left(Element::Guard, 90.0f, 300.0f, 56.0f, kR);
    left(Element::Stick, 150.0f, 202.0f, 128.0f, 0u);
    // The right thumb: ○ large in the middle, the main attack; □ above it, ×
    // to its right, △ below that.
    right(Element::Attack, 584.0f, 194.0f, 112.0f, kCircle);
    right(Element::Secondary, 650.0f, 127.0f, 48.0f, kSquare);
    right(Element::Evade, 727.0f, 169.0f, 56.0f, kCross);
    right(Element::Use, 717.0f, 278.0f, 56.0f, kTriangle);
    // Not in the design; kept in the editor, hidden. The menu opens with
    // Android's Back as well.
    set(Element::DpadUp, Anchor::Left, 0.30f, 0.10f, 0.04f, kUp);
    set(Element::DpadDown, Anchor::Left, 0.42f, 0.10f, 0.04f, kDown);
    set(Element::Combo, Anchor::Right, 0.42f, 0.10f, 0.055f, static_cast<std::uint16_t>(kR | kSquare));
    set(Element::Special, Anchor::Right, 0.30f, 0.10f, 0.05f, kR);
    set(Element::Pause, Anchor::Right, 0.055f, 0.08f, 0.035f, 0u);
    set(Element::Select, Anchor::Right, 0.055f, 0.20f, 0.035f, kSelect);
    set(Element::Swipe, Anchor::Centre, 0.0f, 0.30f, 0.09f, 0u);
    set(Element::R3, Anchor::Right, 0.075f, 0.925f, 0.04f, 0u);
    for (const Element e : {Element::DpadUp, Element::DpadDown, Element::Combo, Element::Special, Element::Pause,
             Element::Select, Element::Swipe, Element::R3})
        l.at(e).shown = false;
    return l;
}

bool is_button(Element e) {
    return e != Element::Stick && e != Element::Swipe;
}

// The right thumb's buttons, which a thumb may slide across.
bool in_cluster(Element e) {
    switch (e) {
    case Element::Attack:
    case Element::Secondary:
    case Element::Combo:
    case Element::Evade:
    case Element::Use:
    case Element::Guard:
    case Element::Special:
        return true;
    default:
        return false;
    }
}

const char *anchor_name(Anchor a) {
    switch (a) {
    case Anchor::Left:
        return "left";
    case Anchor::Centre:
        return "centre";
    case Anchor::Right:
        return "right";
    }
    return "left";
}

} // namespace

const ActionLayout &default_action_layout() {
    static const ActionLayout value = make_default();
    return value;
}

const ElementInfo &info(Element element) {
    return kInfo[static_cast<std::size_t>(element)];
}

bool rebindable(Element element) {
    return is_button(element) && element != Element::Pause && element != Element::R3;
}

const std::vector<ButtonChoice> &button_choices() {
    static const std::vector<ButtonChoice> choices = {
        {kTriangle, "△"},
        {kCircle, "○"},
        {kCross, "×"},
        {kSquare, "□"},
        {kL, "L"},
        {kR, "R"},
        {kStart, "START"},
        {kSelect, "SELECT"},
        {kUp, "D-pad Up"},
        {kDown, "D-pad Down"},
        {kLeft, "D-pad Left"},
        {kRight, "D-pad Right"},
        {static_cast<std::uint16_t>(kTriangle | kCircle), "△ + ○"},
        {static_cast<std::uint16_t>(kR | kTriangle), "R + △"},
        {static_cast<std::uint16_t>(kR | kCircle), "R + ○"},
        {static_cast<std::uint16_t>(kR | kCross), "R + ×"},
        {static_cast<std::uint16_t>(kR | kSquare), "R + □"},
        {static_cast<std::uint16_t>(kL | kCircle), "L + ○"},
        {static_cast<std::uint16_t>(kL | kSquare), "L + □"},
        {static_cast<std::uint16_t>(kTriangle | kCross), "△ + ×"},
    };
    return choices;
}

std::string buttons_label(std::uint16_t buttons) {
    for (const ButtonChoice &c : button_choices())
        if (c.buttons == buttons) return c.label;
    char text[16];
    std::snprintf(text, sizeof(text), "0x%04X", buttons);
    return text;
}

std::string format(const Placement &p) {
    char text[96];
    std::snprintf(text, sizeof(text), "%s %.3f %.3f %.3f 0x%04X %d", anchor_name(p.anchor), static_cast<double>(p.x),
        static_cast<double>(p.y), static_cast<double>(p.size), p.buttons, p.shown ? 1 : 0);
    return text;
}

bool parse(std::string_view text, Placement &out) {
    std::istringstream in{std::string(text)};
    std::string anchor, buttons;
    Placement p;
    int shown = 1;
    if (!(in >> anchor >> p.x >> p.y >> p.size >> buttons >> shown)) return false;
    if (anchor == "left")
        p.anchor = Anchor::Left;
    else if (anchor == "centre")
        p.anchor = Anchor::Centre;
    else if (anchor == "right")
        p.anchor = Anchor::Right;
    else
        return false;
    char *end = nullptr;
    const unsigned long bits = std::strtoul(buttons.c_str(), &end, 0);
    if (end == buttons.c_str() || *end != '\0' || bits > 0xFFFFu) return false;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.size)) return false;
    p.buttons = static_cast<std::uint16_t>(bits);
    p.x = std::clamp(p.x, -4.0f, 4.0f);
    p.y = std::clamp(p.y, 0.0f, 1.0f);
    p.size = std::clamp(p.size, kMinElementSize, kMaxElementSize);
    p.shown = shown != 0;
    out = p;
    return true;
}

Area safe_area(float width, float height, Insets insets) {
    Area a;
    a.left = insets.left;
    a.top = insets.top;
    a.width = std::max(1.0f, width - insets.left - insets.right);
    a.height = std::max(1.0f, height - insets.top - insets.bottom);
    return a;
}

Placed place(const Placement &p, Element element, const Area &area, float scale) {
    const float unit = area.height;
    Placed placed;
    switch (p.anchor) {
    case Anchor::Left:
        placed.centre.x = area.left + p.x * unit;
        break;
    case Anchor::Centre:
        placed.centre.x = area.left + area.width * 0.5f + p.x * unit;
        break;
    case Anchor::Right:
        placed.centre.x = area.left + area.width - p.x * unit;
        break;
    }
    placed.centre.y = area.top + p.y * unit;
    placed.radius = p.size * std::clamp(scale, 0.5f, 2.0f) * unit;
    placed.half_width = element == Element::Swipe ? placed.radius * kSwipeAspect : placed.radius;
    return placed;
}

Placement move_to(const Placement &placement, Element element, Point centre, const Area &area) {
    Placement p = placement;
    const float unit = area.height;
    centre.x = std::clamp(centre.x, area.left, area.left + area.width);
    centre.y = std::clamp(centre.y, area.top, area.top + area.height);
    if (element == Element::Swipe)
        p.anchor = Anchor::Centre;
    else
        p.anchor = centre.x < area.left + area.width * 0.5f ? Anchor::Left : Anchor::Right;
    switch (p.anchor) {
    case Anchor::Left:
        p.x = (centre.x - area.left) / unit;
        break;
    case Anchor::Centre:
        p.x = (centre.x - area.left - area.width * 0.5f) / unit;
        break;
    case Anchor::Right:
        p.x = (area.left + area.width - centre.x) / unit;
        break;
    }
    p.y = (centre.y - area.top) / unit;
    return p;
}

void ActionControls::set_layout(const ActionLayout &layout, const Area &area, float scale) {
    layout_ = layout;
    layout_.at(Element::R3).buttons = 0u;
    area_ = area;
    scale_ = scale;
    for (std::size_t i = 0; i < kElements; ++i)
        placed_[i] = place(layout_.elements[i], static_cast<Element>(i), area_, scale_);
}

std::optional<Element> ActionControls::button_at(Point at, bool cluster_only) const {
    std::optional<Element> best;
    float best_distance = 0.0f;
    for (std::size_t i = 0; i < kElements; ++i) {
        const auto e = static_cast<Element>(i);
        if (!is_button(e) || !layout_.elements[i].shown || (cluster_only && !in_cluster(e))) continue;
        const Placed &p = placed_[i];
        // A little slack: a thumb is wider than its aim.
        const float distance = std::hypot(at.x - p.centre.x, at.y - p.centre.y) / std::max(p.radius, 1.0f);
        if (distance > 1.2f) continue;
        if (!best || distance < best_distance) {
            best = e;
            best_distance = distance;
        }
    }
    return best;
}

bool ActionControls::in_swipe_area(Point at) const {
    if (!layout_.at(Element::Swipe).shown) return false;
    const Placed &p = placed(Element::Swipe);
    return std::fabs(at.x - p.centre.x) <= p.half_width && std::fabs(at.y - p.centre.y) <= p.radius;
}

ActionControls::Finger *ActionControls::find(std::uint64_t id) {
    for (Finger &finger : fingers_)
        if (finger.used && finger.id == id) return &finger;
    return nullptr;
}

void ActionControls::swipe(int direction, std::uint64_t ms) {
    swipe_until_[direction > 0 ? 1 : 0] = ms + kSwipePressMs;
    // The other way ends at once, so a swipe back is not both at once.
    swipe_until_[direction > 0 ? 0 : 1] = 0u;
    ++haptics_;
}

void ActionControls::finger_down(std::uint64_t id, Point at, std::uint64_t ms) {
    if (find(id) != nullptr) finger_up(id, ms);
    Finger *slot = nullptr;
    for (Finger &finger : fingers_)
        if (!finger.used) {
            slot = &finger;
            break;
        }
    if (slot == nullptr) return;
    *slot = Finger{};
    slot->id = id;
    slot->used = true;
    slot->last = slot->start = at;
    slot->start_ms = ms;
    const Placed &stick = placed(Element::Stick);
    if (const std::optional<Element> button = button_at(at, false)) {
        slot->role = Role::Button;
        slot->element = *button;
        slot->over = true;
        if (*button == Element::Pause) menu_tapped_ = true;
        ++haptics_;
    } else if (layout_.at(Element::Stick).shown && !stick_finger_ &&
        std::hypot(at.x - stick.centre.x, at.y - stick.centre.y) <= stick.radius * 1.8f) {
        slot->role = Role::Stick;
        stick_finger_ = id;
        stick_thumb_ = at;
        stick_origin_ = stick.centre;
    } else if (floating_stick_ && layout_.at(Element::Stick).shown && !stick_finger_ && !in_swipe_area(at) &&
        at.x < area_.left + area_.width * 0.45f) {
        // A floating stick, centred where the thumb landed.
        slot->role = Role::Stick;
        stick_finger_ = id;
        stick_thumb_ = at;
        stick_origin_ = at;
    } else if (in_swipe_area(at)) {
        slot->role = Role::Swipe;
    } else {
        slot->role = Role::Camera;
    }
}

void ActionControls::finger_move(std::uint64_t id, Point at, std::uint64_t ms) {
    Finger *finger = find(id);
    if (finger == nullptr) return;
    const float unit = std::max(area_.height, 1.0f);
    switch (finger->role) {
    case Role::Stick:
        stick_thumb_ = at;
        break;
    case Role::Button:
        if (in_cluster(finger->element)) {
            if (const std::optional<Element> button = button_at(at, true)) {
                if (*button != finger->element || !finger->over) ++haptics_;
                finger->element = *button;
                finger->over = true;
            } else {
                finger->over = false;
            }
        } else {
            const Placed &p = placed(finger->element);
            finger->over = std::hypot(at.x - p.centre.x, at.y - p.centre.y) <= p.radius * 1.6f;
        }
        break;
    case Role::Camera:
        camera_drag_.x += at.x - finger->last.x;
        camera_drag_.y += at.y - finger->last.y;
        break;
    case Role::Swipe: {
        const float dx = at.x - finger->start.x;
        const float dy = at.y - finger->start.y;
        const float reach = kSwipeDistance * unit;
        if (finger->swiped) {
            // Each further length of swipe, either way, presses again.
            if (std::fabs(dx) >= reach && std::fabs(dx) > std::fabs(dy)) {
                swipe(dx > 0.0f ? 1 : -1, ms);
                finger->start = at;
            }
            break;
        }
        const bool quick = ms - finger->start_ms <= kSwipeWindowMs;
        if (quick && std::fabs(dx) >= reach && std::fabs(dx) > std::fabs(dy) * 1.5f) {
            swipe(dx > 0.0f ? 1 : -1, ms);
            finger->swiped = true;
            finger->start = at;
        } else if (!quick || std::fabs(dy) >= reach) {
            // Not a swipe: the camera, with the motion made so far.
            finger->role = Role::Camera;
            camera_drag_.x += dx;
            camera_drag_.y += dy;
        }
        break;
    }
    case Role::None:
        break;
    }
    finger->last = at;
}

void ActionControls::finger_up(std::uint64_t id, std::uint64_t) {
    Finger *finger = find(id);
    if (finger == nullptr) return;
    if (finger->role == Role::Stick) {
        stick_finger_.reset();
        stick_thumb_ = {};
        stick_origin_.reset();
    }
    *finger = Finger{};
}

void ActionControls::release_all() {
    fingers_ = {};
    stick_finger_.reset();
    stick_thumb_ = {};
    stick_origin_.reset();
    camera_drag_ = {};
    menu_tapped_ = false;
    haptics_ = 0;
    swipe_until_[0] = swipe_until_[1] = 0u;
}

std::uint16_t ActionControls::buttons(std::uint64_t ms) const {
    std::uint16_t buttons = 0u;
    for (const Finger &finger : fingers_)
        if (finger.used && finger.role == Role::Button && finger.over) buttons |= layout_.at(finger.element).buttons;
    if (ms < swipe_until_[0]) buttons |= kLeft;
    if (ms < swipe_until_[1]) buttons |= kRight;
    return buttons;
}

bool ActionControls::held(Element element) const {
    if (element == Element::Stick) return stick_held();
    for (const Finger &finger : fingers_)
        if (finger.used && finger.role == Role::Button && finger.over && finger.element == element) return true;
    return false;
}

Point ActionControls::stick_centre() const {
    return stick_finger_ && stick_origin_ ? *stick_origin_ : placed(Element::Stick).centre;
}

Point ActionControls::stick() const {
    if (!stick_finger_) return {};
    const Placed &p = placed(Element::Stick);
    const Point centre = stick_centre();
    return stick_deflection({stick_thumb_.x - centre.x, stick_thumb_.y - centre.y}, p.radius);
}

Point ActionControls::take_camera_drag() {
    const Point drag = camera_drag_;
    camera_drag_ = {};
    return drag;
}

bool ActionControls::take_menu() {
    const bool tapped = menu_tapped_;
    menu_tapped_ = false;
    return tapped;
}

int ActionControls::take_haptics() {
    const int count = haptics_;
    haptics_ = 0;
    return count;
}

int ActionControls::swipe_shown(std::uint64_t ms) const {
    if (ms < swipe_until_[0]) return -1;
    if (ms < swipe_until_[1]) return 1;
    return 0;
}

} // namespace mhp3rd::input::touch
