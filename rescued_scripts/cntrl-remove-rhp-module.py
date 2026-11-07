from pathlib import Path
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('#include "../original_firmware.h"\n','').replace('    stock_confirm = 151, stock_back, switches_return,','    switches_return = 153,')
for block in (
'''#if CNTRL_ORIGINAL_FIRMWARE
    original_firmware_module,
#endif
''',
'''#if CNTRL_ORIGINAL_FIRMWARE
    "Original firmware",
#endif
''',
'''#if CNTRL_ORIGINAL_FIRMWARE
        else if (selected == original_firmware_module) {
            original_firmware.enter(); module_controls_locked = true;
        }
#endif
''',
'''#if CNTRL_ORIGINAL_FIRMWARE
    case UiOperation::stock_confirm:
        if (!original_firmware.open()) return false;
        original_firmware.start(); return true;
    case UiOperation::stock_back:
        if (!original_firmware.open()) return false;
        original_firmware.leave(); module_menu.enter(); module_controls_locked = true; return true;
#endif
''',
'''#if CNTRL_ORIGINAL_FIRMWARE
        || original_firmware.open()
#endif
''',
'''#if CNTRL_ORIGINAL_FIRMWARE
                } else if (original_firmware.open()) {
                    if (events[i] & press_event) {
                        if (i == 2) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::stock_confirm)});
                        else if (i == 5) controller.executor().invoke(cntrl::StatePatch{unsigned(UiOperation::stock_back)});
                    }
#endif
''',
'''OriginalFirmware original_firmware(board, start_original_firmware);
'''):
 assert block in s, block
 s=s.replace(block,'')
p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/Makefile');s=p.read_text();a=s.index('\t$(CXX) -std=c++17 -Wall -Wextra -Werror -I"$(abspath $(ROOT)/../../../../..)/firmware/include" \\\n\t\t-fsanitize=address,undefined "$(ROOT)/test_original_firmware.cpp"');b=s.index('\tmkdir -p',a);s=s[:a]+s[b:];p.write_text(s)
p=Path('doc/feature-modules.md');s=p.read_text().replace('the MIDI cable tester and MIDI Interface use the right-hand page cycle. Firmware-specific modules are\nregistered by their target.', 'the MIDI cable tester, MIDI Interface and stock-firmware switching use the\nright-hand page cycle. The menu contains Setups, Songs and setlists, and Switches.');p.write_text(s)
p=Path('firmware/README.md');s=p.read_text().replace('This uses the same persistent boot selection and navigation\ncontinuation as the Original Firmware module.', 'The handoff preserves boot selection and navigation continuation.');p.write_text(s)
