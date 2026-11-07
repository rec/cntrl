from pathlib import Path
import subprocess,difflib
p=Path('cntrl/maker/morningstar/mc/firmware/test_renderer.cpp')
def edit(s):
    s=s.replace('    PerformanceStatus status; status.cycles', '    PerformanceStatus status; status.cycles') # anchor below targets the page fixture
    s=s.replace('    UtilityPages pages; pages.configure(settings);', '''    settings.words[cntrl::UtilityPageSettings::clock_outputs] = 1;
    settings.words[cntrl::UtilityPageSettings::slot1] = 90;
    settings.words[cntrl::UtilityPageSettings::slot2] = 144;
    UtilityPages pages; pages.configure(settings);''',1)
    s=s.replace('    assert(!std::strcmp(view.lines[0].text, "MIDI Clock"));','''    assert(!std::strcmp(view.lines[0].text, "MIDI Clock"));
    assert(pages.generator.bpm() == 120); // Two taps changed the initial 90 BPM.
    pages.clock_button(0, 4005000, 1);
    assert(pages.generator.running() && pages.clock_owner() == 1);
    pages.clock_button(4, 4006000, 1); // External mode stops the internal stream.
    assert(!pages.generator.running());
    pages.clock_button(0, 4007000, 1);
    assert(!pages.generator.running());''',1)
    s=s.replace('    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::clock_tempo);','''    pages.browse(false, "ignored"); assert(pages.page() == UtilityPages::Page::clock_tempo);
    pages.clock_button(2, 4008000, 1);
    assert(pages.generator.bpm() == 144);
    pages.clock_button(0, 4009000, 1);
    assert(pages.generator.bpm() == 143);''',1)
    return s
base=subprocess.check_output(['git','show',':'+str(p)],text=True)
p.write_text(edit(p.read_text()))
Path('/private/tmp/cntrl-clock-ui-test.patch').write_text(''.join(difflib.unified_diff(base.splitlines(True),edit(base).splitlines(True),f'a/{p}', f'b/{p}')))
