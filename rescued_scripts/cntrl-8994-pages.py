from pathlib import Path
import subprocess
import difflib

def change(p, s):
    if p.name == 'test_renderer.cpp':
        marker = '    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::tap);\n    pages.browse(true, "ignored");'
        return s.replace(marker, '''    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::clock_transport);
    view = pages.view(4005, 4005000, status);
    assert(!std::strcmp(view.lines[0].text, "MIDI Clock"));
    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::clock_tempo);
    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::tap);
    pages.browse(true, "ignored"); assert(pages.page() == UtilityPages::Page::clock_tempo);
    pages.browse(true, "ignored"); assert(pages.page() == UtilityPages::Page::clock_transport);
    pages.browse(true, "ignored");''')
    s=s.replace('#include <cntrl/tempo.h>', '#include <cntrl/tempo.h>\n#include <cntrl/clock_generator.h>')
    s=s.replace(', confirm_firmware };', ', clock_transport, clock_tempo, confirm_firmware };')
    s=s.replace('        settings_ = settings;', '        generator.stop();\n        slot_ = 0;\n        settings_ = settings;')
    s=s.replace('        tap_.configure', '''        external_ = settings[R::clock_external];
        recall_slot();
        tap_.configure''')
    for n in (5,6):
        s=s.replace(f'page_ = Page((unsigned(page_) + (reverse ? {n-1} : 1)) % {n});', '''constexpr unsigned count = unsigned(Page::confirm_firmware);
            page_ = Page((unsigned(page_) + (reverse ? count - 1 : 1)) % count);''')
    s=s.replace('    Page page() const', '''    cntrl::ClockGenerator generator;
    unsigned clock_outputs() const { return settings_[cntrl::UtilityPageSettings::clock_outputs]; }
    unsigned clock_owner() const { return (external_ ? 0 : clock_outputs()) | generator.stopping(); }
    bool clock_page() const { return page_ == Page::clock_transport || page_ == Page::clock_tempo; }
    void clock_button(unsigned button, uint32_t now_us, unsigned available) {
        using R = cntrl::UtilityPageSettings;
        if (page_ == Page::clock_transport) {
            if (button == 2) generator.stop();
            if (!external_ && button < 2)
                generator.start(now_us, clock_outputs() & available, button == 1);
            if (button == 4) { generator.stop(); external_ = !external_; }
        } else if (page_ == Page::clock_tempo) {
            if (button < 2) {
                unsigned bpm = std::clamp(int(generator.bpm()) + (button ? 1 : -1), 20, 600);
                generator.set_tempo((60000000u + bpm / 2) / bpm);
            } else if (button == 2) {
                if (++slot_ == 4 || !settings_.words[R::slot1 + slot_]) slot_ = 0;
                recall_slot();
            }
        }
        dirty_ = true;
    }
    Page page() const''')
    s=s.replace('if (page_ == Page::tap) tap_.tap(now);', '''if (page_ == Page::tap) {
            if (tap_.tap(now)) generator.set_tempo(tap_.period() * 1000);
        }''')
    s=s.replace('if (page_ == Page::status) return uint32_t', 'if (page_ == Page::status || clock_page()) return uint32_t')
    s=s.replace('const char *heading = page_', 'const char *heading = page_ == Page::clock_transport ? "MIDI Clock" : page_ == Page::clock_tempo ? "Clock Tempo" : page_')
    s=s.replace('        if (page_ == Page::cable) {\n            const bool active', '''        if (clock_page()) {
            const unsigned font = settings_[R::status_font];
            char tempo[32], detail[64];
            std::snprintf(tempo, sizeof(tempo), "%u BPM", generator.bpm());
            if (page_ == Page::clock_transport) {
                const char *state = external_ ? (clock_.active() ? "External: receiving" : "External: waiting")
                    : !clock_outputs() ? "No outputs configured" : generator.running() ? "Internal: running" : "Internal: stopped";
                line(pane, 1, state, 30, 19, font, true);
                if (external_) number_text(tempo, sizeof(tempo), clock_.period_us(), true, true);
                line(pane, 2, tempo, 52, 20, font, true);
                line(pane, 3, "A Start  B Continue", 82, 16, font, true);
                line(pane, 4, "C Stop  E Source", 102, 16, font, true);
            } else {
                std::snprintf(detail, sizeof(detail), "Slot %u: %u BPM", slot_ + 1, settings_.words[R::slot1 + slot_]);
                line(pane, 1, detail, 28, 18, font, true);
                line(pane, 2, tempo, 50, 24, font, true);
                line(pane, 3, "A -1   B +1   C Slot", 80, 17, font, true);
                std::snprintf(detail, sizeof(detail), "Late %uus  Lost %u", generator.late_us, generator.skipped + generator.blocked);
                line(pane, 4, detail, 102, 16, font, true);
            }
            return pane;
        }
        if (page_ == Page::cable) {
            const bool active''')
    s=s.replace('    void number_text(','''    unsigned slot_ = 0;
    bool external_ = false;
    void recall_slot() {
        unsigned bpm = settings_.words[cntrl::UtilityPageSettings::slot1 + slot_];
        if (bpm) generator.set_tempo((60000000u + bpm / 2) / bpm);
    }
    void number_text(''')
    return s

patch = ''
for p in (Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/utility_pages.h'),Path('cntrl/maker/morningstar/mc/firmware/test_renderer.cpp')):
    clean = subprocess.check_output(['git','show',f'HEAD:{p}'],text=True)
    patch += ''.join(difflib.unified_diff(clean.splitlines(True),change(p,clean).splitlines(True),f'a/{p}',f'b/{p}'))
    p.write_text(change(p,p.read_text()))
Path('/private/tmp/cntrl-8994-own-pages.patch').write_text(patch)
