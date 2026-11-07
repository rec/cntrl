from pathlib import Path
p=Path('firmware/include/cntrl/panic.h');s=p.read_text().replace('void tick(uint32_t now)', 'void tick(uint32_t now, bool outputs_ready)').replace('''        for (unsigned attempt''','''        if (!outputs_ready) return;
        for (unsigned attempt''');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('if (bridge_.handoff(*this)) panic.tick(now);','panic.tick(now, bridge_.handoff(*this));');p.write_text(s)
p=Path('firmware/test/test_panic.cpp');s=p.read_text().replace('panic.tick(0);','panic.tick(0, true);');a=s.index('int main()');s=s[:a]+'''void test_blocked_panic_finishes_one_second_after_release() {
    Hardware hw; cntrl::Panic panic(hw, hw);
    hw.blocked[2] = true;
    panic.enter(77); panic.button(true, 100);
    tick(panic, hw, 1000);
    panic.tick(10000, true); assert(panic.active());
    panic.button(false, 10000);
    panic.tick(10999, true); assert(panic.status() == cntrl::Panic::Status::sending);
    panic.take_redraw();
    panic.tick(11000, true);
    assert(!panic.active() && panic.status() == cntrl::Panic::Status::incomplete && panic.take_redraw());
    for (unsigned i : {0u, 1u, 3u}) assert(hw.messages[i].size() == 48);
    assert(hw.brightness.back().first == 77);
    hw.blocked[2] = false;
    panic.button(true, 12000); panic.button(false, 12001);
    for (unsigned i = 0; i < 1000; ++i) panic.tick(12002, true);
    assert(panic.status() == cntrl::Panic::Status::ready);
    panic.button(true, 0xfffffff0); panic.button(false, 0xfffffff0);
    panic.tick(984, false);
    assert(panic.status() == cntrl::Panic::Status::incomplete);
}

int main() { test_tap_without_display_side_effects(); test_hold_full_loop_and_release(); test_backpressure_and_disconnection(); test_blocked_panic_finishes_one_second_after_release(); }
''';p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/test_renderer.cpp');s=p.read_text().replace('"Disk: 1.1/29G"', '"Disk:1.1/29G Up:0:00:01"').replace('assert(!std::strcmp(view.lines[7].text, "Uptime: 0:00:01"));','assert(!view.lines[7].text[0]);').replace('settings[cntrl::RightPageSettings::title_height] * 3 / 2);','settings[cntrl::RightPageSettings::title_height] * 3 / 2 + 4);').replace('for (unsigned i = 1; i <= 7; ++i)', 'for (unsigned i = 1; i <= 6; ++i)').replace('graphics.line_height >= 17 && view.lines[i].graphics.line_height <= 18','graphics.line_height >= 20 && view.lines[i].graphics.line_height <= 21').replace('const auto &uptime = view.lines[7];','const auto &uptime = view.lines[6];').replace('uptime.x + 9 * advance','uptime.x + 16 * advance').replace('view.lines[7].text, "Uptime: 0:20:05"','view.lines[6].text, "Disk:1.1/29G Up:0:20:05"').replace('"Disk: --/--G"', '"Disk:--/--G Up:0:20:05"').replace('view.lines[7].text, "Uptime: 1193:02:48"', 'view.lines[6].text, "Disk:--/--G Up:1193:02:48"').replace('status, {}, true)', 'status, {}, cntrl::Panic::Status::sending)').replace('status, {}, false, interface)', 'status, {}, cntrl::Panic::Status::ready, interface)')
s=s.replace('assert(screens[2][10][0] == 0 && screens[2][25][0] == 0xff90);', 'assert(screens[2][10][0] == 0 && screens[2][23][0] == 0 && screens[2][24][0] == 0xff90);')
# Exercise the newly configurable heading and the failure state.
needle='    view = pages.view(4003, 4003000, status, {}, cntrl::Panic::Status::sending);';s=s.replace(needle,'''    view = pages.view(4003, 4003000, status, {}, cntrl::Panic::Status::incomplete);
    assert(!std::strcmp(view.lines[3].text, "Incomplete"));
'''+needle)
needle='    PerformancePages pages; pages.configure(settings);';s=s.replace(needle,'''    PerformancePages custom;
    settings.words[cntrl::RightPageSettings::title_font] = 1;
    custom.configure(settings);
    assert(custom.view(0, 0, {}).lines[0].graphics.font == 1);
    settings.words[cntrl::RightPageSettings::title_font] = 0;
'''+needle,1);p.write_text(s)
p=Path('test/test_patch_switcher.py');s=p.read_text().replace("        'minimum_taps': 3,", "        'minimum_taps': 3,\n        'title_font': 'Arial',",1).replace('assert settings.clocks_per_quarter == 96 and settings.minimum_taps == 3','assert settings.clocks_per_quarter == 96 and settings.minimum_taps == 3\n    assert settings.title_font == \'Arial\'');p.write_text(s)
