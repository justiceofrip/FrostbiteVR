#include "fvr/ui/ModMenu.h"

namespace fvr::ui {

MenuItem& ModMenuModel::Add(MenuItem item) {
    items_.push_back(std::move(item));
    return items_.back();
}

MenuItem* ModMenuModel::Find(const std::string& id) {
    for (auto& item : items_)
        if (item.id == id) return &item;
    return nullptr;
}

bool ModMenuModel::Activate(std::size_t index) {
    if (index >= items_.size()) return false;
    MenuItem& item = items_[index];
    if (!item.enabled || item.kind == MenuItemKind::Label) return false;
    switch (item.kind) {
    case MenuItemKind::Toggle: item.value = item.value ? 0 : 1; break;
    case MenuItemKind::Choice:
        if (!item.choices.empty()) item.value = (item.value + 1) % static_cast<int>(item.choices.size());
        break;
    default: break;
    }
    if (onChange_) onChange_(item);
    return true;
}

std::string ModMenuModel::ValueText(const MenuItem& item) {
    if (item.kind == MenuItemKind::Toggle) return item.value ? "On" : "Off";
    if (item.kind == MenuItemKind::Choice && item.value >= 0 && item.value < static_cast<int>(item.choices.size()))
        return item.choices[static_cast<std::size_t>(item.value)];
    return {};
}

} // namespace fvr::ui
