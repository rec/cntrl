#include <cntrl/startup_counter.h>
using cntrl::StartupCounter;
#include "counter_file.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>


void test_counter_text_preserves_values_above_32_bits() {
    CounterFile file; file.data.assign(512, ' '); file.data.back() = '\n';
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

void test_explicit_reset_replaces_both_slots_and_restarts_at_one() {
    CounterFile file; StartupCounter counter;
    for (unsigned i = 0; i < 10; ++i) counter.start(file);
    assert(counter.count == 10 && file.size() == 1024);
    assert(counter.reset(file) && counter.count == 1 && file.size() == 512);
    StartupCounter reboot; reboot.start(file);
    assert(reboot.status == StartupCounter::Status::ready && reboot.count == 2);
    reboot.start(file);
    assert(reboot.status == StartupCounter::Status::ready && reboot.count == 3);
    file.data.assign(512, '?');
    assert(counter.reset(file) && counter.count == 1);
    auto before = file.data; file.truncatable = false;
    assert(!counter.reset(file) && !std::strcmp(counter.error, "truncate") && file.data == before);
    file.active = false;
    assert(!counter.reset(file) && !std::strcmp(counter.error, "open") && file.data == before);
    file.active = file.truncatable = true; file.writable = false;
    assert(!counter.reset(file) && !std::strcmp(counter.error, "write s0 0/512"));
}

int main() {
    test_counter_text_preserves_values_above_32_bits();
    test_explicit_reset_replaces_both_slots_and_restarts_at_one();
    struct Replies { std::string value; void reply(const char *text) { value = text; } } replies;
    StartupCounter counter; counter.count = 10; counter.status = StartupCounter::Status::ready;
    counter.reply("CNTRL READY 35 123 aabbccdd", replies);
    assert(replies.value == "CNTRL READY 35 123 aabbccdd starts=10 starts_status=ready");
    counter.reply("CNTRL DATA 0001", replies); assert(replies.value == "CNTRL DATA 0001");
    counter.reply("CNTRL ERROR test", replies); assert(replies.value == "CNTRL ERROR test");
    char response[160];
    StartupCounter absent; absent.usb_status("CNTRL READY 33 0 00000000", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 0 00000000 starts=0 starts_status=unavailable"));
    CounterFile file;
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
    CounterFile torn; StartupCounter first; first.start(torn);
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
    CounterFile corrupt; corrupt.data.assign(1024, '?'); StartupCounter invalid; invalid.start(corrupt);
    assert(invalid.status == StartupCounter::Status::error && !corrupt.writes);
    assert(!std::strcmp(invalid.error, "decode s0"));
    CounterFile missing; missing.active = false; StartupCounter unavailable;
    assert(unavailable.status == StartupCounter::Status::unavailable);
    unavailable.start(missing); assert(unavailable.status == StartupCounter::Status::error);
    assert(!std::strcmp(unavailable.error, "open"));
    CounterFile failed_sync; failed_sync.synced = false; StartupCounter unsaved; unsaved.start(failed_sync);
    assert(unsaved.status == StartupCounter::Status::error);
    assert(!std::strcmp(unsaved.error, "sync s0"));
    unsaved.usb_status("CNTRL READY 33 123 aabbccdd", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 123 aabbccdd starts=0 starts_status=error starts_error=sync s0"));
    recovered.usb_status("CNTRL READY 33 123 aabbccdd", response, sizeof(response));
    assert(!std::strcmp(response, "CNTRL READY 33 123 aabbccdd starts=10 starts_status=ready"));
    // A correct saved value does not imply that read-back verification succeeded.
    for (int maximum : {17, -1}) {
        CounterFile short_read; short_read.verify_limit = maximum;
        StartupCounter counter; counter.start(short_read);
        assert(counter.status == StartupCounter::Status::error && short_read.data[17] == '1');
        assert(!std::strcmp(counter.error, maximum == 17 ? "readback s0 17/512" : "readback s0 -1/512"));
    }
    CounterFile mismatch; mismatch.mismatch = 37;
    StartupCounter compared; compared.start(mismatch);
    assert(compared.status == StartupCounter::Status::error && !std::strcmp(compared.error, "mismatch s0 @37"));
    for (unsigned seek : {1u, 3u, 4u}) {
        CounterFile failed; failed.data = failed_sync.data; failed.fail_seek = seek;
        StartupCounter counter; counter.start(failed);
        assert(counter.status == StartupCounter::Status::error);
        assert(!std::strcmp(counter.error, seek == 1 ? "seek-read s0" : seek == 3 ? "seek-write s1" : "seek-verify s1"));
    }
    CounterFile short_read; short_read.data.assign(100, '?');
    StartupCounter truncated; truncated.start(short_read);
    assert(truncated.status == StartupCounter::Status::error && !std::strcmp(truncated.error, "read s0 100/512"));
    CounterFile failed_read; failed_read.data = failed_sync.data; failed_read.read_limit = -1;
    StartupCounter unread; unread.start(failed_read);
    assert(unread.status == StartupCounter::Status::error && !std::strcmp(unread.error, "read s0 -1/512") && !failed_read.writes);
    CounterFile full; full.data.assign(StartupCounter::record_size, ' ');
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
