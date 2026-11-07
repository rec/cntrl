from pathlib import Path
p=Path('firmware/include/cntrl/midi_file.h');s=p.read_text().replace('        events_ = loop_ = position_ = 0;', '        events_ = loop_ = position_ = group_last_ = 0;')
s=s.replace('            Event event;\n            if (!load_event', '            Event event;\n            if (!load_event',1)
s=s.replace('''            if (event.status == 0xf0 || event.status == 0xf7) {
                if (settings_.reverse && !event.complete_sysex) { fail(); return; }
                stream_ = event; stream_offset_ = 0; event_pending_ = true;
                if (event.status == 0xf0) parser_.receive(0xf0, packet_);
                return;
            }''','''            if (event.status == 0xf0 || event.status == 0xf7) {
                if (settings_.reverse && event.next) return;
                if (settings_.reverse && !event.complete_sysex) { fail(); return; }
                stream_ = event; stream_offset_ = 0; event_pending_ = true;
                seeking_group_ = settings_.reverse && event.previous;
                if (!seeking_group_ && event.status == 0xf0) parser_.receive(0xf0, packet_);
                return;
            }''')
s=s.replace('uint32_t offset = 0, length = 0;', 'uint32_t offset = 0, length = 0, previous = 0, next = 0;')
s=s.replace('    unsigned pending_outputs_ = 0;', '    unsigned pending_outputs_ = 0;\n    uint32_t group_last_ = 0;\n    bool seeking_group_ = false;')
s=s.replace('if (status == 0xf0 && event.length)', 'if ((status == 0xf0 || status == 0xf7) && event.length)')
s=s.replace('''        uint8_t record[24] = {};''','''        if (event.status == 0xf0) {
            if (group_last_) { fail(); return; }
            if (!event.complete_sysex) group_last_ = events_ + 1;
        } else if (event.status == 0xf7 && group_last_) {
            event.previous = group_last_;
            uint32_t next = events_ + 1;
            uint8_t link[] = {uint8_t(next), uint8_t(next >> 8), uint8_t(next >> 16), uint8_t(next >> 24)};
            if (!index_.write((group_last_ - 1) * 32 + 28, link, 4)) { fail(); return; }
            group_last_ = event.complete_sysex ? 0 : next;
        }
        uint8_t record[32] = {};''')
s=s.replace('if (events_ == UINT32_MAX / 24 || !index_.write(events_ * 24, record, 24))','''for (unsigned i = 0; i < 4; ++i) record[24 + i] = uint8_t(event.previous >> (i * 8));
        if (events_ == UINT32_MAX / 32 || !index_.write(events_ * 32, record, 32))''')
s=s.replace('uint8_t record[24];\n        if (!index_.read(position * 24, record, 24))', 'uint8_t record[32];\n        if (!index_.read(position * 32, record, 32))')
s=s.replace('        event.status = record[16];', '        for (unsigned i = 0; i < 4; ++i) { event.previous |= uint32_t(record[24 + i]) << (i * 8); event.next |= uint32_t(record[28 + i]) << (i * 8); }\n        event.status = record[16];')
s=s.replace('''    void stream_event() {
        for''','''    void stream_event() {
        if (seeking_group_) {
            Event first;
            if (!load_event(stream_.previous - 1, first)) { fail(); return; }
            stream_ = first;
            seeking_group_ = stream_.previous != 0;
            if (!seeking_group_) parser_.receive(0xf0, packet_);
            return;
        }
        for''')
s=s.replace('''        if (stream_offset_ == stream_.length) event_pending_ = false;''','''        if (stream_offset_ == stream_.length) {
            if (settings_.reverse && stream_.next) {
                Event next;
                if (!load_event(stream_.next - 1, next)) { fail(); return; }
                stream_ = next; stream_offset_ = 0;
            } else event_pending_ = false;
        }''')
s=s.replace('''        if (!chosen) {
            if (!index_.sync())''', '''        if (!chosen) {
            if (group_last_) { fail(); return; }
            if (!index_.sync())''')
p.write_text(s)
