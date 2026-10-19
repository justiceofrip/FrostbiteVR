#pragma once
// Lays the ModMenuModel out as a VrPanel and routes the laser pointer into it. VrPanel
// owns hit testing and the click rule (fresh press + release on the same widget, so a
// trigger held while the menu opens never activates anything). Platform-neutral.
#include "fvr/ui/ModMenu.h"
#include "fvr/ui/VrPanel.h"

#include <string>

namespace fvr::ui {

inline std::wstring WidenAscii(const std::string& s) { return std::wstring(s.begin(), s.end()); }

// Widget ids are item index + 1 (0 = none). Rebuilding every frame is cheap: VrPanel only
// bumps its revision (and the platform only re-rasterises) when something changed.
inline void LayoutModMenu(const ModMenuModel& menu, VrPanel& panel) {
    const float w = static_cast<float>(panel.Width()), h = static_cast<float>(panel.Height());
    const float margin = w * 0.05f, header = h * 0.14f, gap = h * 0.012f;
    float row = h * 0.105f;
    // More items than one column holds: two columns (filled top to bottom, then right), and
    // shorter rows only if even that is not enough. Every item stays on the panel.
    const std::size_t count = menu.Items().size();
    const auto fit = static_cast<std::size_t>((h - header) / row);
    const bool twoColumns = count > fit;
    const std::size_t perColumn = twoColumns ? (count + 1) / 2 : count;
    if (perColumn > fit && perColumn) row = (h - header) / static_cast<float>(perColumn);
    const float columnWidth = twoColumns ? (w - 3 * margin) / 2 : w - 2 * margin;
    panel.Begin();
    Widget title;
    title.kind = WidgetKind::Title;
    title.rect = {margin, 0, w - 2 * margin, header};
    title.text = WidenAscii(menu.Title());
    title.detail = L"Menu button closes";
    panel.Add(title);
    const auto& items = menu.Items();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const MenuItem& item = items[i];
        Widget wd;
        wd.id = static_cast<std::uint32_t>(i + 1);
        const std::size_t column = twoColumns ? i / perColumn : 0, line = i - column * perColumn;
        wd.rect = {margin + static_cast<float>(column) * (columnWidth + margin), header + static_cast<float>(line) * row,
                   columnWidth, row - gap};
        wd.kind = item.kind == MenuItemKind::Label ? WidgetKind::Label
                  : item.kind == MenuItemKind::Toggle ? WidgetKind::Toggle
                  : item.kind == MenuItemKind::Choice ? WidgetKind::Choice : WidgetKind::Button;
        const std::string value = ModMenuModel::ValueText(item);
        wd.text = WidenAscii(value.empty() ? item.label : item.label + ": " + value);
        wd.detail = WidenAscii(item.note);
        wd.enabled = item.enabled;
        wd.active = item.kind == MenuItemKind::Toggle && item.value != 0;
        panel.Add(wd);
    }
    panel.End();
}

// One pointer frame. Returns true when an item was activated (its OnChange handler ran).
inline bool PointModMenu(ModMenuModel& menu, VrPanel& panel, const PointerSample& sample) {
    const PanelEvent e = panel.Update(sample);
    return e.clicked != 0 && menu.Activate(e.clicked - 1);
}

} // namespace fvr::ui
