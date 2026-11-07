from pathlib import Path
p=Path('firmware/include/cntrl/usb_session.h');s=p.read_text().replace('unsigned long long session;', 'unsigned long session;').replace('%llx','%lx').replace('(unsigned long long)session_', '(unsigned long)session_').replace('uint64_t session_', 'uint32_t session_');p.write_text(s)
p=Path('cntrl/patch_switcher/transport.py');s=p.read_text().replace('secrets.randbits(64)', 'secrets.randbits(32)');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('#include <cntrl/songs_transfer.h>', '#include <cntrl/songs_transfer.h>\n#include <cntrl/usb_session.h>');pos=s.index('class Board :');s=s[:pos]+'DMAMEM cntrl::UsbSession usb_session;\n'+s[pos:]
a=s.index('    bool reply_ready() const override');b=s.index('    void poll()',a)
s=s[:a]+'''    bool reply_ready() const override { return usb_session.reply_ready(); }
    void reply(const char *text) override { usb_session.reply(text); }
'''+s[b:]
s=s.replace('if (response_[response_offset_] && Serial.availableForWrite() > 0)', 'if (usb_session.output_pending() && Serial.availableForWrite() > 0)').replace('std::strlen(response_ + response_offset_)','std::strlen(usb_session.output())').replace('response_offset_ += Serial.write(response_ + response_offset_, n);','usb_session.sent(Serial.write(usb_session.output(), n));').replace('    unsigned response_offset_ = 0;\n    char response_[600] = {};\n','')
a=s.index('    for (unsigned i = 0; i < 256 && board.reply_ready() && Serial.available()');b=s.index('    // Preserve the current serial frame',a)
s=s[:a]+'''    for (unsigned i = 0; i < 512 && !usb_session.output_pending() && Serial.available(); ++i)
        usb_session.receive(Serial.read());
    if (usb_session.resetting() && serial_router.accepts_input() && !songs_transfer.processing()) {
        serial_router.cancel_transfer(); songs_transfer.cancel_transfer(); font_files.cancel_transfer();
        usb_session.reset_done();
    }
    for (unsigned i = 0; i < 128 && usb_session.input_size() && board.reply_ready()
#if CNTRL_ORIGINAL_FIRMWARE
        && !firmware_visit_stage
#endif
        && serial_router.accepts_input(); ++i) {
        serial_router.receive(usb_session.input(), now);
        usb_session.consumed();
    }
    // A resumable session owns its unfinished transfers until a new session
    // replaces it. USB outages must not expire staged bytes or list cursors.
    if (usb_session.active()) {
        serial_router.keepalive(now); songs_transfer.keepalive(now); font_files.keepalive(now);
    }
'''+s[b:];p.write_text(s)
p=Path('cntrl/patch_switcher/cli.py');s=p.read_text().replace('    except (OSError, ValueError) as error:\n        sys.exit(str(error))', '    except KeyboardInterrupt:\n        sys.exit(130)\n    except (OSError, ValueError) as error:\n        sys.exit(str(error))');p.write_text(s)
