#include <cntrl/midi_bridge.h>
#include <cntrl/document.h>
#include "input_hardware.h"
#include "midi_endpoints.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <vector>

using cntrl::midi::Packet;

struct Output : cntrl::midi::Sink {
    std::vector<Packet> packets;
    unsigned allowance = 10000;
    bool fail_allocations_after_write = false;
    bool write_packet(unsigned, const Packet &p) override {
        if (!allowance) return false;
        --allowance; packets.push_back(p);
        if (fail_allocations_after_write) { cntrl::memory_fail_after = 0; fail_allocations_after_write = false; }
        return true;
    }
};

Packet cc(unsigned controller, unsigned value, unsigned source = 0) {
    return {11, {0xb0, uint8_t(controller), uint8_t(value)}, uint16_t(source)};
}
int parameter_value(const Output &output, uint8_t kind, uint16_t address = 0) {
    cntrl::midi::Parameter parameter{}; parameter.kind = kind; parameter.address = address;
    return output.state().parameter_value(0, parameter);
}
void test_crossing_rearms_on_other_sources() {
    Output out; out.pickup(true, 0);
    assert(out.submit(0, cc(74, 64)));
    assert(out.submit_live(0, cc(74, 20)));
    assert(out.state().value(0, 0, 74) == 64);
    assert(out.submit_live(0, cc(74, 70)));
    assert(out.state().value(0, 0, 74) == 70);
    assert(out.submit_live(0, cc(74, 30, 1)));
    assert(out.state().value(0, 0, 74) == 70);
    assert(out.submit_live(0, cc(74, 80, 1)));
    assert(out.state().value(0, 0, 74) == 80);
    assert(out.submit_live(0, cc(74, 60)));
    assert(out.state().value(0, 0, 74) == 80);
    assert(out.submit_live(0, cc(74, 90)));
    assert(out.state().value(0, 0, 74) == 90);
    assert(out.submit(0, cc(74, 20)));
    assert(out.submit_live(0, cc(74, 80)));
    assert(out.state().value(0, 0, 74) == 20);
    assert(out.submit_live(0, cc(74, 10)));
    assert(out.state().value(0, 0, 74) == 10);
}
void test_unknown_disabled_and_discrete_controls() {
    Output out;
    assert(out.submit_live(0, cc(74, 20)));
    assert(out.state().value(0, 0, 74) == 20);
    assert(out.submit_live(0, cc(64, 127, 1)));
    assert(out.state().value(0, 0, 64) == 127);
    out.pickup(false);
    assert(out.submit_live(0, cc(74, 120, 1)));
    assert(out.state().value(0, 0, 74) == 120);
    out.pickup(true);
    assert(out.submit_live(0, cc(74, 10, 1)));
    assert(out.state().value(0, 0, 74) == 120);
    out.forget_output(0);
    assert(out.submit_live(0, cc(74, 10, 1)));
    assert(out.state().value(0, 0, 74) == 10);
}
void test_full_precision_pair_and_coarse_timeout() {
    Output out;
    out.submit(0, cc(7, 64)); out.submit(0, cc(39, 64));
    unsigned before = unsigned(out.packets.size());
    out.submit_live(0, cc(7, 64)); out.submit_live(0, cc(39, 63));
    assert(out.packets.size() == before);
    out.submit_live(0, cc(7, 64)); out.submit_live(0, cc(39, 65));
    assert(out.packets.size() == before + 2);
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 7) == 64 * 128 + 65);
    out.submit(0, cc(1, 70));
    out.submit_live(0, cc(1, 20));
    out.pickup_poll(1999, 1);
    assert(out.state().value(0, 0, 1) == 70);
    out.pickup_poll(2000, 1);
    assert(out.state().value(0, 0, 1) == 70);
    out.submit_live(0, cc(1, 80));
    out.pickup_poll(4000, 1);
    assert(out.state().value(0, 0, 1) == 80);
}
void test_pressure_pitch_and_failed_pair_delivery() {
    Output out;
    out.submit(0, {14, {0xe0, 0, 64}});
    out.submit_live(0, {14, {0xe0, 127, 63}, 0});
    assert(out.state().value(0, 0, 129) == 8192);
    out.submit_live(0, {14, {0xe0, 1, 64}, 0});
    assert(out.state().value(0, 0, 129) == 8193);
    out.submit(0, {13, {0xd0, 70, 0}});
    out.submit_live(0, {13, {0xd0, 20, 0}, 0});
    assert(parameter_value(out, cntrl::midi::Parameter::pressure) == 70);
    out.submit_live(0, {13, {0xd0, 80, 0}, 0});
    assert(parameter_value(out, cntrl::midi::Parameter::pressure) == 80);
    out.allowance = 1;
    out.submit_live(0, cc(2, 50)); out.submit_live(0, cc(34, 60));
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 2) == cntrl::midi::Parameter::unknown);
    assert(!out.submit(0, cc(74, 10)));
    out.allowance = 10; out.pickup(true); out.pickup_poll(2000, 1);
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 2) == 50 * 128 + 60);
    out.submit_live(0, cc(2, 20)); out.submit_live(0, cc(34, 0));
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 2) == 50 * 128 + 60);
}
void test_nrpn_and_rpn_keep_source_parameter_selection() {
    for (bool nrpn : {true, false}) {
        Output out;
        unsigned high = nrpn ? 99 : 101, low = nrpn ? 98 : 100;
        unsigned key = (nrpn ? 256 : 33024) + 130;
        out.submit(0, cc(high, 1)); out.submit(0, cc(low, 2));
        out.submit(0, cc(6, 64)); out.submit(0, cc(38, 64));
        assert(out.state().value(0, 0, key) == 8256);
        out.submit_live(0, cc(high, 1)); out.submit_live(0, cc(low, 2));
        unsigned before = unsigned(out.packets.size());
        out.submit_live(0, cc(6, 64)); out.submit_live(0, cc(38, 63));
        assert(out.packets.size() == before);
        // Another source selects another parameter between samples.
        out.submit_live(0, cc(high, 1, 1)); out.submit_live(0, cc(low, 3, 1));
        out.submit_live(0, cc(6, 64)); out.submit_live(0, cc(38, 65));
        assert(out.state().value(0, 0, key) == 8257);
        assert(out.state().value(0, 0, low) == 2);
        assert(out.state().value(0, 0, key + 1) == -1);
    }
}
void test_poly_pressure_and_coarse_lsb_do_not_share_crossings() {
    Output out;
    out.submit(0, cc(99, 0)); out.submit(0, cc(98, 61));
    out.submit(0, cc(6, 64)); out.submit(0, cc(38, 0));
    assert(parameter_value(out, cntrl::midi::Parameter::key_pressure, 61) == cntrl::midi::Parameter::unknown);
    out.submit_live(0, {10, {0xa0, 61, 20}, 0});
    assert(parameter_value(out, cntrl::midi::Parameter::key_pressure, 61) == 20);
    out.submit(0, {10, {0xa0, 60, 70}});
    out.submit_live(0, {10, {0xa0, 60, 20}, 0});
    assert(parameter_value(out, cntrl::midi::Parameter::key_pressure, 60) == 70);
    out.submit_live(0, {10, {0xa0, 60, 80}, 0});
    assert(parameter_value(out, cntrl::midi::Parameter::key_pressure, 60) == 80);
    out.pickup(true, 0);
    out.submit(0, cc(7, 70)); out.submit(0, cc(39, 80));
    out.submit_live(0, cc(7, 70));
    out.submit_live(0, cc(39, 90));
    assert(out.state().value(0, 0, 39) == 80);
}
void test_forwarded_and_expression_samples_share_pickup_and_diagnostics() {
    Output out; out.pickup(true, 0);
    cntrl::midi::Bridge bridge{test_midi_outputs};
    bridge.configure(true); bridge.connected(true, 0);
    out.submit(0, cc(74, 64));
    bridge.forward_packet(0, cc(74, 20), 0); bridge.service(out, 0);
    assert(out.state().value(0, 0, 74) == 64 && !bridge.counters(0).transmitted);
    bridge.forward_packet(0, cc(74, 70), 0); bridge.service(out, 0);
    assert(out.state().value(0, 0, 74) == 70 && bridge.counters(0).transmitted == 1);
    uint8_t sample[] = {0xb0, 74, 30};
    assert(bridge.pedal(0, sample, 3, 256)); bridge.service(out, 0);
    assert(out.state().value(0, 0, 74) == 70);
    sample[2] = 80;
    assert(bridge.pedal(0, sample, 3, 256)); bridge.service(out, 0);
    assert(out.state().value(0, 0, 74) == 80 && bridge.counters(0).transmitted == 2);
}
void test_parameter_transactions_establish_targets_and_lock_out_live_writes() {
    Output out;
    cntrl::midi::ParameterTransaction transaction{};
    transaction.token = 1;
    transaction.parameter = {cntrl::midi::Parameter::cc14, 0, 7, 8256};
    assert(out.submit_parameter(0, transaction));
    out.submit_live(0, cc(7, 64)); out.submit_live(0, cc(39, 63));
    assert(out.parameter_locked(0));
    assert(out.submit_parameter(0, transaction));
    out.pickup_poll(2000, 1);
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 7) == 8256);
    out.submit_live(0, cc(7, 64)); out.submit_live(0, cc(39, 65));
    assert(parameter_value(out, cntrl::midi::Parameter::cc14, 7) == 8257);
}
void test_disconnected_sources_drop_unsent_values_and_reacquire() {
    Output out;
    out.submit_live(0, cc(7, 64, 16));
    out.forget_pickup_source(1); out.pickup_poll(2000, 1);
    assert(out.state().value(0, 0, 7) == -1);
    out.submit_live(0, cc(74, 60, 16));
    out.forget_pickup_source(1);
    out.submit_live(0, cc(74, 20, 16));
    assert(out.state().value(0, 0, 74) == 60);
}
struct PolicyHardware : TestInputHardware {
    cntrl::midi::Outputs midi_outputs() const override { return test_midi_outputs; }
    bool send(const char *, const uint8_t *, unsigned) override { return true; }
    bool supports(const char *) const override { return true; }
    bool supports_font(unsigned, unsigned) const override { return true; }
    unsigned display_width() const override { return 160; }
    unsigned display_height() const override { return 128; }
    void show(unsigned, const cntrl::Pane &) override {}
    void set_brightness(uint8_t, bool = false) override {}
    void set_splash(bool, uint32_t) override {}
    void error(const char *) override { assert(false); }
};
void test_compiled_policy(const char *path, bool global) {
    std::ifstream file(path, std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    PolicyHardware hardware; cntrl::Document document;
    assert(document.load(bytes.data(), unsigned(bytes.size()), hardware));
    assert(document.pickup() == global && document.pickup_pairing_ms() == 7);
    assert(document.pickup(unsigned(document.named_patch("inherit"))) == global);
    assert(document.pickup(unsigned(document.named_patch("acquire"))));
    assert(!document.pickup(unsigned(document.named_patch("direct"))));
    unsigned end = unsigned(document.bank(document.patches()) - bytes.data()) + document.page_section_size();
    assert(!std::memcmp(bytes.data() + end, "CNAM", 4));
    unsigned offset = 0;
    while (offset + 4 < bytes.size() && std::memcmp(bytes.data() + offset, "CPUP", 4)) ++offset;
    bytes[offset + 6] = 3;
    cntrl::put32(bytes.data() + 20, cntrl::checksum(bytes.data() + 28, unsigned(bytes.size()) - 28));
    cntrl::Document rejected; assert(!rejected.load(bytes.data(), unsigned(bytes.size()), hardware));
}
void test_pickup_parameter_admits_every_selector_and_data_message_before_output() {
    Output out;
    assert(out.submit_live(0, cc(99, 1)));
    assert(out.submit_live(0, cc(98, 2)));
    assert(out.submit_live(0, cc(6, 10)));
    const unsigned before = unsigned(out.packets.size());
    out.fail_allocations_after_write = true;
    assert(out.submit_live(0, cc(38, 11)));
    cntrl::memory_fail_after = -1;
    assert(out.packets.size() == before + 4);
    assert(out.packets[before].bytes[1] == 99 && out.packets[before + 1].bytes[1] == 98);
    assert(out.packets[before + 2].bytes[1] == 6 && out.packets[before + 3].bytes[1] == 38);
}

int main(int argc, char **argv) {
    cntrl::memory_budget = 524288;
    test_pickup_parameter_admits_every_selector_and_data_message_before_output();
    test_crossing_rearms_on_other_sources();
    test_unknown_disabled_and_discrete_controls();
    test_full_precision_pair_and_coarse_timeout();
    test_pressure_pitch_and_failed_pair_delivery();
    test_nrpn_and_rpn_keep_source_parameter_selection();
    test_poly_pressure_and_coarse_lsb_do_not_share_crossings();
    test_forwarded_and_expression_samples_share_pickup_and_diagnostics();
    test_parameter_transactions_establish_targets_and_lock_out_live_writes();
    test_disconnected_sources_drop_unsent_values_and_reacquire();
    if (argc == 3) test_compiled_policy(argv[1], argv[2][0] == '1');
}
