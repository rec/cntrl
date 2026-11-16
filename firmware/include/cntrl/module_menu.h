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
    void configure(const char *const *names, unsigned count) { names_ = names; count_ = count; selected_ = std::min(selected_, count_); }
    void refresh() { if (open_) render(); }
    void leave() { open_ = false; }
    void enter() { open_ = locked_ = true; selecting_language_ = false; selected_ = 0; render(); }
    void poll(uint8_t held) { if (!held) locked_ = false; }
    // -1: no selection; -2: return to performance; otherwise installed index.
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(module_menu_button)
#endif
    int button(unsigned button, bool down) {
        if (!open_ || !down || locked_) return -1;
        if (button == 5) {
            if (selecting_language_) { selecting_language_ = false; render(); return -1; }
            open_ = false; return -2;
        }
        if (button == 2) {
            if (selecting_language_) {
                hardware_.request_language(hardware_.language_tag(language_)); return -1;
            }
            if (selected_ == count_) {
                selecting_language_ = true;
                language_ = 0;
                const unsigned count = hardware_.language_count();
                for (unsigned i = 0; i < count; ++i)
                    if (!std::strcmp(hardware_.current_language(), hardware_.language_tag(i))) { language_ = i; break; }
                render(); return -1;
            }
            open_ = false; return int(selected_);
        }
        if (button < 2) {
            if (selecting_language_) {
                const unsigned count = hardware_.language_count();
                if (count) language_ = (language_ + count + (button == 0 ? -1 : 1)) % count;
            } else selected_ = (selected_ + count_ + 1 + (button == 0 ? -1 : 1)) % (count_ + 1);
            render();
        }
        return -1;
    }
private:
    Hardware &hardware_;
    const char *const *names_;
    unsigned count_, selected_ = 0;
    bool open_ = false, locked_ = false;
    bool selecting_language_ = false;
    unsigned language_ = 0;
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
        const auto heading = selecting_language_ ? ui::i18n::Message::language : ui::i18n::Message::pages_and_tools;
        ui::utf8::copy(views[1].lines[0].text, 64, ui::i18n::text(heading));
        views[1].lines[0].graphics.asset = unsigned(heading) + 1;
        if (selecting_language_) {
            const char *tag = hardware_.language_tag(language_);
            std::snprintf(views[1].lines[1].text, 64, "%s%s",
                          !std::strcmp(hardware_.current_language(), tag) ? "* " : "", hardware_.language_name(language_));
        } else ui::utf8::copy(views[1].lines[1].text, 64,
                            selected_ == count_ ? ui::i18n::text(ui::i18n::Message::language) : names_[selected_]);
        for (unsigned i = 0; i < 3; ++i) {
            for (auto &line : views[i].lines) line.wrap = true;
        }
        ui::present(hardware_, views, 1);
    }
};
}
