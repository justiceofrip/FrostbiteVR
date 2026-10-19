#pragma once
// Data model for FrostbiteVR's own in-headset menu (settings, recenter, feedback).
// Rendering- and pointer-agnostic: ModMenuPanel.h lays it out as a VrPanel (layout, hit
// test, click rules), GdiPanelRaster draws that, and the XR host's laser pointer drives it
// through the menu lane like any other panel.
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fvr::ui {

enum class MenuItemKind : std::uint8_t { Action, Toggle, Choice, Label };

struct MenuItem {
    std::string id;     // stable settings key, e.g. "reload_mode"
    std::string label;  // shown to the player
    MenuItemKind kind = MenuItemKind::Action;
    std::vector<std::string> choices; // Choice only
    int value = 0;      // Toggle: 0/1, Choice: index
    bool enabled = true;
    std::string note;   // secondary line, e.g. "Not available yet; uses button press"
};

class ModMenuModel {
public:
    using ChangeHandler = std::function<void(const MenuItem&)>;

    explicit ModMenuModel(std::string title) : title_(std::move(title)) {}

    MenuItem& Add(MenuItem item);
    MenuItem* Find(const std::string& id);
    const std::vector<MenuItem>& Items() const { return items_; }
    const std::string& Title() const { return title_; }

    // One activation of item `index` (a completed click): toggles flip, choices advance,
    // actions fire. Disabled items and labels never change. Returns true if it changed.
    bool Activate(std::size_t index);

    // Current value as text: the choice name, "On"/"Off", or empty.
    static std::string ValueText(const MenuItem& item);

    void OnChange(ChangeHandler handler) { onChange_ = std::move(handler); }

private:
    std::string title_;
    std::vector<MenuItem> items_;
    ChangeHandler onChange_;
};

} // namespace fvr::ui
