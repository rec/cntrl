#pragma once
#include "ui/messages.h"
#include "ui/fonts.h"
#include "ui/presentation.h"
#include "ui/prompts.h"
#include "hardware.h"
#include <cstdio>

namespace cntrl {
// The target supplies the installed module names and handles the selection.
class ModuleMenu {
public:
    ModuleMenu(Hardware &hardware, const char *const *names, unsigned count)
        : hardware_(hardware), names_(names), count_(count) {}
    bool open() const { return open_; }
    void configure(const char *const *names, unsigned count) { names_ = names; count_ = count; selected_ = std::min(selected_, count_ ? count_ - 1 : 0); }
    void refresh() { if (open_) render(); }
    void leave() { open_ = false; }
    void enter() { open_ = locked_ = true; selected_ = 0; render(); }
    void poll(uint8_t held) { if (!held) locked_ = false; }
    // -1: no selection; -2: return to performance; otherwise installed index.
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(module_menu_button)
#endif
    int button(unsigned button, bool down) {
        if (!open_ || !down || locked_) return -1;
        if (button == 5) {
            open_ = false; return -2;
        }
        if (button == 2 && count_) {
            open_ = false; return int(selected_);
        }
        if (button < 2 && count_) {
            selected_ = (selected_ + count_ + (button == 0 ? -1 : 1)) % count_;
            render();
        }
        return -1;
    }
private:
    Hardware &hardware_;
    const char *const *names_;
    unsigned count_, selected_ = 0;
    bool open_ = false, locked_ = false;
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(module_menu_render)
#endif
    void render() {
        ui::Prompt prompt(hardware_);
        using Role = ControlRole;
        Pane views[3];
        for (auto &view : views) for (auto &line : view.lines) {
            line.graphics.line_height = line.graphics.size = 16;
            line.graphics.font = ui::localized_face;
        }
        std::snprintf(views[0].lines[0].text, 64, "%s", prompt.pair(Role::previous, Role::next, ui::i18n::text(ui::i18n::Message::browse)).text());
        std::snprintf(views[0].lines[1].text, 64, "%s", prompt.clear().add(Role::confirm, ui::i18n::text(ui::i18n::Message::ok)).add(Role::back, ui::i18n::text(ui::i18n::Message::back), ", ").text());
        const auto heading = ui::i18n::Message::pages_and_tools;
        ui::utf8::copy(views[1].lines[0].text, 64, ui::i18n::text(heading));
        views[1].lines[0].graphics.asset = unsigned(heading) + 1;
        if (count_) ui::utf8::copy(views[1].lines[1].text, 64, names_[selected_]);
        for (unsigned i = 0; i < 3; ++i) {
            for (auto &line : views[i].lines) line.wrap = true;
        }
        ui::present(hardware_, views, 1);
    }
};
}
