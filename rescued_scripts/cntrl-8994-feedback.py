from pathlib import Path
import subprocess,difflib
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/utility_pages.h')
def change(s):
    s=s.replace('#include <cntrl/clock_generator.h>', '#include <cntrl/clock_generator.h>\n#include <cntrl/midi_outputs.h>')
    s=s.replace('    void clock_button(','''    void clock_connections(unsigned available) {
        available &= clock_outputs();
        if (available != available_outputs_) {
            available_outputs_ = available;
            if (clock_page()) dirty_ = true;
        }
    }
    void clock_button(''')
    s=s.replace('!clock_outputs() ? "No outputs configured" : generator.running()', '!clock_outputs() ? "No outputs configured" : !available_outputs_ ? "Outputs offline" : generator.running()')
    s=s.replace('                line(pane, 2, tempo, 52, 20, font, true);', '''                line(pane, 2, tempo, 48, 19, font, true);
                detail[0] = 0;
                auto output = [&](const cntrl::midi::OutputConnection &port) {
                    if (!(clock_outputs() & (1u << port.route))) return;
                    auto used = std::strlen(detail);
                    std::snprintf(detail + used, sizeof(detail) - used, "%s%s%s", used ? " " : "",
                        port.label, available_outputs_ & (1u << port.route) ? "" : "*");
                };
                for (const auto &port : cntrl::midi::output_connections) output(port);
                for (const auto &port : cntrl::midi::usb_output_connections) output(port);
                line(pane, 3, detail, 67, 14, font, true);''').replace('line(pane, 3, "A Start', 'line(pane, 4, "A Start').replace('line(pane, 4, "C Stop','line(pane, 5, "C Stop')
    return s.replace('    unsigned slot_ = 0;', '    unsigned available_outputs_ = 0, slot_ = 0;')
staged=subprocess.check_output(['git','show',':'+str(p)],text=True)
p.write_text(change(p.read_text()))
Path('/private/tmp/cntrl-8994-feedback.patch').write_text(''.join(difflib.unified_diff(staged.splitlines(True),change(staged).splitlines(True),f'a/{p}',f'b/{p}')))
