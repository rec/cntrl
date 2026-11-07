#include "patch_switcher/startup_counter.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

struct File {
    std::vector<char> data;
    unsigned position = 0, writes = 0, limit = StartupCounter::record_size;
    unsigned seeks = 0, fail_seek = 0, mismatch = StartupCounter::record_size;
    int read_limit = StartupCounter::record_size, verify_limit = StartupCounter::record_size;
    bool open = true, writable = true, synced = true;
    explicit operator bool() const { return open; }
    unsigned size() const { return unsigned(data.size()); }
    bool seekSet(unsigned offset) { position = offset; return open && ++seeks != fail_seek; }
    int read(char *bytes, unsigned length) {
        int maximum = writes ? verify_limit : read_limit;
        if (maximum < 0) return maximum;
        length = std::min(length, unsigned(maximum));
        unsigned n = position < data.size() ? std::min(length, unsigned(data.size()) - position) : 0;
        if (n) std::memcpy(bytes, data.data() + position, n);
        if (writes && mismatch < n) bytes[mismatch] ^= 1;
        position += n; return int(n);
    }
    unsigned write(const char *bytes, unsigned length) {
        ++writes; if (!writable) return 0;
        unsigned n = std::min(length, limit);
        if (position + n > data.size()) data.resize(position + n);
        std::memcpy(data.data() + position, bytes, n); position += n; return n;
    }
    bool sync() const { return synced; }
};

void test_raw_reads_preserve_every_byte() {
    File file; file.data.resize(1031);
    for (unsigned i = 0; i < file.data.size(); ++i) file.data[i] = char(i % 256);
    const auto original = file.data;
    char response[280];
    StartupCounter::read_command("CNTRL STARTS SIZE", file, response);
    assert(!std::strcmp(response, "CNTRL STARTS SIZE 1031"));
    std::vector<char> received;
    for (unsigned offset = 0; offset < original.size(); offset += 128) {
        char command[64]; std::snprintf(command, sizeof(command), "CNTRL STARTS READ %u", offset);
        StartupCounter::read_command(command, file, response);
        assert(!std::strncmp(response, "CNTRL DATA ", 11));
        unsigned n = std::min(128u, unsigned(original.size()) - offset);
        assert(std::strlen(response) == 11 + n * 2);
        for (unsigned i = 0; i < n; ++i)
            received.push_back(char(std::stoul(std::string(response + 11 + i * 2, 2), nullptr, 16)));
    }
    assert(received == original && file.data == original && !file.writes);
    StartupCounter::read_command("CNTRL STARTS READ 1031", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-offset"));
    StartupCounter::read_command("CNTRL STARTS READ 0 extra", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-command"));
    file.fail_seek = file.seeks + 1;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-seek"));
    file.read_limit = 17;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-read 17/128"));
    file.read_limit = -1;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-read -1/128"));
    file.open = false;
    StartupCounter::read_command("CNTRL STARTS SIZE", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-open"));
    assert(file.data == original && !file.writes);
    File empty;
    StartupCounter::read_command("CNTRL STARTS SIZE", empty, response);
    assert(!std::strcmp(response, "CNTRL STARTS SIZE 0") && !empty.writes);
    StartupCounter::read_command("CNTRL STARTS READ 0", empty, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-offset") && !empty.writes);
}

void test_counter_text_preserves_values_above_32_bits() {
    File file; file.data.assign(512, ' '); file.data.back() = '\n';
    constexpr char saved[] = "[slot0]\nstarts = 4294967295\ncheck = \"ffffffff00000000\"\n";
    std::memcpy(file.data.data(), saved, sizeof(saved) - 1);
    StartupCounter counter; counter.start(file);
    assert(counter.status == StartupCounter::Status::ready && counter.count == 4294967296ull);
    constexpr char expected[] = "[slot1]\nstarts = 4294967296\ncheck = \"fffffffeffffffff\"\n";
    assert(!std::memcmp(file.data.data() + 512, expected, sizeof(expected) - 1));
    StartupCounter reboot; reboot.start(file);
    assert(reboot.status == StartupCounter::Status::ready && reboot.count == 4294967297ull);
    counter.count = UINT64_MAX;
    char response[160]; counter.usb_status("CNTRL READY 34 123 aabbccdd", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 34 123 aabbccdd starts=18446744073709551615 starts_status=ready"));
}

int main() {
    test_raw_reads_preserve_every_byte();
    test_counter_text_preserves_values_above_32_bits();
    char response[160];
    StartupCounter absent; absent.usb_status("CNTRL READY 33 0 00000000", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 0 00000000 starts=0 starts_status=unavailable"));
    File file;
    for (unsigned i = 1; i <= 10; ++i) {
        StartupCounter counter; counter.start(file);
        assert(counter.status == StartupCounter::Status::ready && counter.count == i && !counter.error[0]);
    }
    assert(file.size() == 1024 && file.writes == 10);
    // Corrupt the newest copy. The previous copy remains a usable baseline.
    file.data[StartupCounter::record_size + 1] = '!';
    StartupCounter recovered; recovered.start(file);
    assert(recovered.status == StartupCounter::Status::ready && recovered.count == 10);
    // A partial update preserves the previous sector and is detected next time.
    File torn; StartupCounter first; first.start(torn);
    torn.limit = 20; StartupCounter interrupted; interrupted.start(torn);
    assert(interrupted.status == StartupCounter::Status::error && interrupted.count == 1);
    assert(!std::strcmp(interrupted.error, "write s1 20/512"));
    torn.limit = StartupCounter::record_size;
    StartupCounter restart; restart.start(torn);
    assert(restart.status == StartupCounter::Status::ready && restart.count == 2);
    torn.writable = false; StartupCounter readonly; readonly.start(torn);
    assert(readonly.status == StartupCounter::Status::error && readonly.count == 2);
    assert(!std::strcmp(readonly.error, "write s0 0/512"));
    // An unrecognizable existing file must not silently reset the count.
    File corrupt; corrupt.data.assign(1024, '?'); StartupCounter invalid; invalid.start(corrupt);
    assert(invalid.status == StartupCounter::Status::error && !corrupt.writes);
    assert(!std::strcmp(invalid.error, "decode s0"));
    File missing; missing.open = false; StartupCounter unavailable;
    assert(unavailable.status == StartupCounter::Status::unavailable);
    unavailable.start(missing); assert(unavailable.status == StartupCounter::Status::error);
    assert(!std::strcmp(unavailable.error, "open"));
    File failed_sync; failed_sync.synced = false; StartupCounter unsaved; unsaved.start(failed_sync);
    assert(unsaved.status == StartupCounter::Status::error);
    assert(!std::strcmp(unsaved.error, "sync s0"));
    unsaved.usb_status("CNTRL READY 33 123 aabbccdd", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 123 aabbccdd starts=0 starts_status=error starts_error=sync s0"));
    recovered.usb_status("CNTRL READY 33 123 aabbccdd", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 123 aabbccdd starts=10 starts_status=ready"));
    // A correct saved value does not imply that read-back verification succeeded.
    for (int maximum : {17, -1}) {
        File short_read; short_read.verify_limit = maximum;
        StartupCounter counter; counter.start(short_read);
        assert(counter.status == StartupCounter::Status::error && short_read.data[17] == '1');
        assert(!std::strcmp(counter.error, maximum == 17 ? "readback s0 17/512" : "readback s0 -1/512"));
    }
    File mismatch; mismatch.mismatch = 37;
    StartupCounter compared; compared.start(mismatch);
    assert(compared.status == StartupCounter::Status::error && !std::strcmp(compared.error, "mismatch s0 @37"));
    for (unsigned seek : {1u, 3u, 4u}) {
        File failed; failed.data = failed_sync.data; failed.fail_seek = seek;
        StartupCounter counter; counter.start(failed);
        assert(counter.status == StartupCounter::Status::error);
        assert(!std::strcmp(counter.error, seek == 1 ? "seek-read s0" : seek == 3 ? "seek-write s1" : "seek-verify s1"));
    }
    File short_read; short_read.data.assign(100, '?');
    StartupCounter truncated; truncated.start(short_read);
    assert(truncated.status == StartupCounter::Status::error && !std::strcmp(truncated.error, "read s0 100/512"));
    File failed_read; failed_read.data = failed_sync.data; failed_read.read_limit = -1;
    StartupCounter unread; unread.start(failed_read);
    assert(unread.status == StartupCounter::Status::error && !std::strcmp(unread.error, "read s0 -1/512") && !failed_read.writes);
    File full; full.data.assign(StartupCounter::record_size, ' ');
    constexpr char near_limit[] = "[slot0]\nstarts = 9223372036854775806\ncheck = \"8000000000000001\"\n";
    std::memcpy(full.data.data(), near_limit, sizeof(near_limit) - 1); full.data.back() = '\n';
    StartupCounter penultimate; penultimate.start(full);
    assert(penultimate.status == StartupCounter::Status::ready && penultimate.count == INT64_MAX);
    constexpr char at_limit[] = "[slot1]\nstarts = 9223372036854775807\ncheck = \"8000000000000000\"\n";
    assert(!std::memcmp(full.data.data() + 512, at_limit, sizeof(at_limit) - 1));
    unsigned writes = full.writes;
    StartupCounter limit; limit.start(full);
    assert(limit.status == StartupCounter::Status::error && limit.count == INT64_MAX && full.writes == writes);
    assert(!std::strcmp(limit.error, "limit"));
}
