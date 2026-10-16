#pragma once

#include "input/touch_action.hpp"

#include <array>
#include <cstdint>
#include <optional>

// The touch screen in the game's 2D scenes (menus, dialogues, the world
// map): no pad on the screen, the screen itself is the pad. A tap confirms
// (×, the European confirm button), a drag holds the D-pad its way, a tap of
// two fingers goes back (○). Three small buttons stay in the corners: Start,
// the port's menu, and one that brings the full PSP pad back for a screen
// that needs another button.
namespace mhp3rd::input::touch {

class Gestures {
public:
    // A tap: down and up within this long, moving less than kTapSlop.
    static constexpr std::uint64_t kTapMs = 300u;
    static constexpr float kTapSlop = 0.04f; // heights of the safe area
    // How far a drag goes before it holds the D-pad.
    static constexpr float kDragDeadZone = 0.06f;
    // How long a tap presses its button, so the game reads it.
    static constexpr std::uint64_t kPressMs = 100u;

    enum class Corner : std::uint8_t { Start, Menu, Pad, Count };

    void set_area(const Area &area, float scale);
    [[nodiscard]] Placed corner(Corner which) const { return corners_[static_cast<std::size_t>(which)]; }

    void finger_down(std::uint64_t id, Point at, std::uint64_t ms);
    void finger_move(std::uint64_t id, Point at, std::uint64_t ms);
    void finger_up(std::uint64_t id, std::uint64_t ms);
    void release_all();

    // The PSP buttons pressed now (SceCtrlButtons bits).
    [[nodiscard]] std::uint16_t buttons(std::uint64_t ms) const;
    [[nodiscard]] bool held(Corner which) const;
    // Taken once: the menu button, and the button that shows the full pad.
    [[nodiscard]] bool take_menu();
    [[nodiscard]] bool take_show_pad();
    [[nodiscard]] int take_haptics();
    // Where the last tap landed and when, for a ripple.
    [[nodiscard]] std::optional<Point> last_tap(std::uint64_t ms, std::uint64_t within) const;

private:
    struct Finger {
        std::uint64_t id{};
        bool used{};
        std::optional<Corner> corner;
        Point start;
        Point last;
        std::uint64_t start_ms{};
        bool dragged{};
        bool multi{}; // part of a two-finger tap
    };
    Finger *find(std::uint64_t id);

    Area area_{};
    std::array<Placed, static_cast<std::size_t>(Corner::Count)> corners_{};
    std::array<Finger, 10> fingers_{};
    std::uint64_t cross_until_{};
    std::uint64_t circle_until_{};
    Point tap_at_{};
    std::uint64_t tap_ms_{};
    bool tapped_{};
    bool menu_{};
    bool show_pad_{};
    int haptics_{};
};

} // namespace mhp3rd::input::touch
