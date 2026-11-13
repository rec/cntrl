#include <cntrl/midi_processing.h>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <vector>

using namespace cntrl::midi;
namespace {
const void *charged_existing = nullptr;
unsigned charged_old = 0, charged_new = 0, charge_queries = 0;
unsigned fixture_actual_charge(const void *data) {
    ++charge_queries;
    return data == charged_existing ? charged_old : charged_new;
}
void allocator_charge_refusal_preserves_the_running_buffer() {
    const auto callback = cntrl::memory_actual_charge;
    cntrl::memory_actual_charge = fixture_actual_charge;
    charged_existing = nullptr; charged_new = 64; charge_queries = 0;
    cntrl::memory_budget = 192;
    {
        cntrl::MemoryBuffer buffer;
        assert(buffer.resize(9));
        buffer.data[0] = 73;
        charged_existing = buffer.data; charged_old = 64; charged_new = 128;
        assert(cntrl::memory_used == 64);
        cntrl::memory_budget = 128;
        const unsigned allocations = cntrl::memory_allocations, failures = cntrl::memory_failures;
        assert(!buffer.resize(17));
        assert(cntrl::memory_allocations == allocations + 1 && cntrl::memory_failures == failures + 1);
        assert(buffer.data == charged_existing && buffer.size == 9 && buffer.data[0] == 73 && cntrl::memory_used == 64);
        cntrl::memory_budget = 192; cntrl::memory_peak = 64;
        assert(buffer.resize(17));
        assert(buffer.data[0] == 73 && cntrl::memory_used == 128 && cntrl::memory_peak == 192);
        charged_existing = buffer.data; charged_old = 128;
    }
    assert(charge_queries >= 5 && cntrl::memory_used == 0 && !cntrl::memory_checked_operations);
    cntrl::memory_actual_charge = callback;
}
void list_nodes_keep_addresses_and_credits_through_transfer() {
    cntrl::memory_budget = 64 * 1024;
    const unsigned baseline = cntrl::memory_used;
    {
        cntrl::CheckedList<unsigned> owner, borrowed, published;
        assert(owner.reserve(5));
        const unsigned bytes = owner.memory_bytes();
        assert(bytes == cntrl::memory_used - baseline);
        assert(borrowed.take_reserved(owner, 2) && owner.loans() == 2);
        auto *value = borrowed.append(71); assert(value);
        const auto item = published.splice(published.end(), borrowed, borrowed.begin());
        assert(&*item == value && *item == 71 && borrowed.empty());
        assert(owner.loans() == 2);
        assert(owner.memory_bytes() + borrowed.memory_bytes() + published.memory_bytes() == bytes);
        assert(published.transfer_free(borrowed, 1) == 1 && borrowed.capacity() == 0);
        assert(borrowed.take_reserved(published, 1) && owner.loans() == 2 && !published.loans());
        assert(published.transfer_free(borrowed, 1) == 1);
        published.return_reserved();
        assert(owner.loans() == 1);
        published.erase(item);
        assert(owner.loans() == 0 && owner.capacity() == 5 && owner.memory_bytes() == bytes);
        assert(published.empty() && published.capacity() == 0);
        assert(borrowed.transfer_free(owner, 3) == 3 && owner.loans() == 0);
        value = borrowed.append(23); assert(value);
        cntrl::CheckedList<unsigned> moved(std::move(borrowed));
        assert(&moved.front() == value && borrowed.empty() && !borrowed.capacity());
        cntrl::memory_fail_after = 0;
        assert(moved.append(24) && moved.append(25) && !moved.append(26));
        const auto next = owner.splice(owner.end(), moved, moved.begin());
        assert(&*next == value && *next == 23);
        assert(owner.trim(1) == 1);
        assert(owner.memory_bytes() + moved.memory_bytes() == cntrl::memory_used - baseline);
        cntrl::memory_fail_after = -1;
    }
    assert(cntrl::memory_used == baseline);
}
void list_reservation_failure_keeps_owned_and_borrowed_nodes() {
    cntrl::memory_budget = 64 * 1024;
    const unsigned baseline = cntrl::memory_used;
    {
        cntrl::CheckedList<unsigned> donor, receiver;
        assert(donor.reserve(3) && receiver.take_reserved(donor, 2) && receiver.reserve(5));
        const unsigned bytes = cntrl::memory_used;
        cntrl::memory_fail_after = 1;
        assert(!receiver.reserve(7));
        assert(receiver.capacity() == 5 && donor.loans() == 2 && cntrl::memory_used == bytes);
        cntrl::memory_fail_after = 0;
        receiver.return_reserved();
        assert(receiver.capacity() == 3 && donor.capacity() == 3 && !donor.loans());
        assert(receiver.memory_bytes() + donor.memory_bytes() == bytes - baseline);
        assert(receiver.append(11) && receiver.append(33));
        assert(donor.append(22));
        auto position = receiver.begin(); ++position;
        auto *address = &donor.front();
        const auto inserted = receiver.splice(position, donor, donor.begin());
        assert(&*inserted == address && receiver.size() == 3 && donor.empty());
        auto item = receiver.begin(); assert(*item++ == 11); assert(*item++ == 22); assert(*item++ == 33);
        cntrl::memory_fail_after = -1;
    }
    assert(cntrl::memory_used == baseline);
}
void list_transfer_preserves_nested_payload_lifetime() {
    struct Counts { unsigned moves = 0, destructions = 0; } counts;
    struct Payload {
        Counts *counts;
        cntrl::CheckedList<unsigned> nested;
        explicit Payload(Counts &value) : counts(&value) {
            assert(nested.reserve(2)); assert(nested.append(7) && nested.append(8));
        }
        Payload(const Payload &) = delete;
        Payload(Payload &&other) noexcept : counts(other.counts), nested(std::move(other.nested)) { ++counts->moves; }
        ~Payload() { ++counts->destructions; }
    };
    cntrl::memory_budget = 64 * 1024;
    const unsigned baseline = cntrl::memory_used;
    unsigned destroyed = 0;
    {
        cntrl::CheckedList<Payload> donor, borrowed, published;
        assert(donor.reserve(2) && borrowed.take_reserved(donor,2));
        auto *payload = borrowed.append(Payload(counts)); assert(payload);
        auto *nested = &payload->nested.front();
        const unsigned moves = counts.moves, allocations = cntrl::memory_allocations;
        const unsigned failures = cntrl::memory_failures, bytes = cntrl::memory_used;
        destroyed = counts.destructions;
        cntrl::memory_fail_after = 0;
        assert(published.transfer_free(borrowed,1) == 1);
        const auto item = published.splice(published.end(),borrowed,borrowed.begin());
        assert(&*item == payload && &item->nested.front() == nested);
        assert(item->nested.size() == 2 && item->nested.front() == 7 && item->nested.back() == 8);
        assert(counts.moves == moves && counts.destructions == destroyed);
        assert(cntrl::memory_used == bytes && cntrl::memory_allocations == allocations && cntrl::memory_failures == failures);
        published.erase(item);
        assert(counts.destructions == destroyed + 1 && donor.loans() == 1);
        published.return_reserved();
        assert(!donor.loans() && donor.capacity() == 2 && !published.capacity());
        cntrl::memory_fail_after = -1;
    }
    assert(counts.destructions == destroyed + 1 && cntrl::memory_used == baseline);
}
void buffer_growth_preserves_running_data_until_replacement_fits() {
    using cntrl::memory_charge;
    assert(memory_charge(0) == 0);
    assert(memory_charge(1) == memory_charge(8));
    assert(memory_charge(9) > memory_charge(8));
    assert(memory_charge(UINT32_MAX) > UINT32_MAX);
    cntrl::MemoryBuffer buffer;
    cntrl::memory_budget = unsigned(memory_charge(9) + memory_charge(17));
    assert(buffer.resize(9));
    for (unsigned i = 0; i < buffer.size; ++i) buffer.data[i] = uint8_t(i + 41);
    auto *original = buffer.data;
    const unsigned used = cntrl::memory_used;
    auto intact = [&] {
        assert(buffer.data == original && buffer.size == 9 && cntrl::memory_used == used);
        for (unsigned i = 0; i < buffer.size; ++i) assert(buffer.data[i] == i + 41);
    };
    cntrl::memory_fail_after = 0;
    assert(!buffer.resize(17)); intact();
    cntrl::memory_fail_after = -1;
    // The replacement alone fits, but the running buffer must coexist with it
    // until its data is copied. Refusal keeps the current data usable.
    --cntrl::memory_budget;
    assert(!buffer.resize(17)); intact();
    assert(!buffer.resize(UINT32_MAX)); intact();
    ++cntrl::memory_budget;
    cntrl::memory_peak = used;
    assert(buffer.resize(17));
    assert(buffer.size == 17 && cntrl::memory_used == memory_charge(17));
    assert(cntrl::memory_peak == memory_charge(9) + memory_charge(17));
    for (unsigned i = 0; i < 9; ++i) assert(buffer.data[i] == i + 41);
    buffer.clear();
    assert(cntrl::memory_used == 0);
}
const OutputConnection endpoints[] = {
    {"in", "Input", 0, OutputTransport::physical, true, true},
    {"out", "Output", 1, OutputTransport::physical, true, true}
};
const Outputs outputs{endpoints,2};
struct Output : Sink {
    std::array<std::array<bool,128>,2> sounding{};
    unsigned sent = 0;
    Output() { pickup(false); }
    bool write_packet(unsigned output,const Packet &p) override {
        ++sent;
        if (p.bytes[0] >> 4 == 9) sounding[output][p.bytes[1]] = p.bytes[2] != 0;
        if (p.bytes[0] >> 4 == 8) sounding[output][p.bytes[1]] = false;
        return true;
    }
    bool quiet() const {
        for (const auto &port : sounding) for (bool note : port) if (note) return false;
        return true;
    }
};
void integer(std::vector<uint8_t> &bytes,uint64_t value,unsigned count) {
    for (unsigned i = 0; i < count; ++i) bytes.push_back(uint8_t(value >> (8 * i)));
}
std::vector<uint8_t> graph(unsigned routes,bool chain = false) {
    std::vector<uint8_t> bytes{'C','S','T','R',0,0,0,0};
    integer(bytes,routes,2); integer(bytes,1,2);
    for (unsigned route = 0; route < routes; ++route) {
        integer(bytes,65535,2); integer(bytes,1,2); integer(bytes,route ? 2 : 1,2); integer(bytes,16,2);
        bytes.push_back(uint8_t('a' + route)); bytes.insert(bytes.end(),63,0);
        bytes.push_back(uint8_t(route != 0)); integer(bytes,chain && route ? route - 1 : 0,2);
        // Sixteen identity transforms exercise the real graph traversal and
        // processor path while preserving an easily checked note lifecycle.
        for (unsigned step = 0; step < 16; ++step) {
            bytes.insert(bytes.end(),{2,0,4,0}); integer(bytes,0,4);
        }
    }
    for (unsigned i = 0; i < 4; ++i) bytes[4+i] = uint8_t(bytes.size() >> (8 * i));
    return bytes;
}
struct Timing {
    uint64_t calls = 0, total_ns = 0, max_ns = 0;
    uint64_t inputs = 0, input_total_ns = 0, input_max_ns = 0;
    std::vector<uint64_t> service_samples, input_samples;
    void service(Streams &streams,Output &output,uint64_t now) {
        const auto begin = std::chrono::steady_clock::now();
        streams.service(output,now);
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
        ++calls; total_ns += uint64_t(elapsed); max_ns = std::max(max_ns,uint64_t(elapsed));
        service_samples.push_back(uint64_t(elapsed));
    }
    void drain(Streams &streams,Output &output,uint64_t now) {
        for (unsigned i = 0; i < 4096 && (!i || streams.pending() || streams.recovery()); ++i) service(streams,output,now);
    }
    void report(const char *scenario,unsigned peak,unsigned allocations,unsigned failures) const {
        std::printf("dynamic-memory %s: service_calls=%llu mean_ns=%llu max_ns=%llu input_calls=%llu input_mean_ns=%llu input_max_ns=%llu ledger_peak=%u allocations=%u failures=%u\n",
            scenario,static_cast<unsigned long long>(calls),static_cast<unsigned long long>(total_ns / std::max(uint64_t(1),calls)),
            static_cast<unsigned long long>(max_ns),static_cast<unsigned long long>(inputs),
            static_cast<unsigned long long>(input_total_ns / std::max(uint64_t(1),inputs)),
            static_cast<unsigned long long>(input_max_ns),peak,allocations,failures);
        auto percentiles = [&](const char *kind,std::vector<uint64_t> samples) {
            std::sort(samples.begin(),samples.end());
            assert(!samples.empty());
            std::printf("  %s p50_ns=%llu p95_ns=%llu p99_ns=%llu\n",kind,
                static_cast<unsigned long long>(samples[(samples.size() - 1) * 50 / 100]),
                static_cast<unsigned long long>(samples[(samples.size() - 1) * 95 / 100]),
                static_cast<unsigned long long>(samples[(samples.size() - 1) * 99 / 100]));
        };
        percentiles("service",service_samples); percentiles("input",input_samples);
    }
};
void packet(Streams &streams,uint8_t status,unsigned note,uint64_t now,Timing &timing) {
    const auto begin = std::chrono::steady_clock::now();
    streams.receive(0,{uint8_t(status >> 4),{status,uint8_t(note),uint8_t(status == 0x90 ? 100 : 0)}},now);
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - begin).count();
    ++timing.inputs; timing.input_total_ns += uint64_t(elapsed); timing.input_max_ns = std::max(timing.input_max_ns,uint64_t(elapsed));
    timing.input_samples.push_back(uint64_t(elapsed));
}
void dense_input_and_service(unsigned routes,bool chain,const char *name) {
    const auto bytes = graph(routes,chain);
    Streams streams; Output output; Timing timing;
    assert(streams.load(bytes.data(),unsigned(bytes.size()),outputs,1));
    cntrl::memory_peak = cntrl::memory_used;
    const unsigned allocations = cntrl::memory_allocations, failures = cntrl::memory_failures;
    for (unsigned round = 0; round < 2; ++round) {
        packet(streams,0x90,47,round * 1000,timing);
        timing.drain(streams,output,round * 1000);
        for (unsigned note = 48; note < 64; ++note) packet(streams,0x90,note,round * 1000,timing);
        timing.drain(streams,output,round * 1000);
        assert(!output.quiet() || cntrl::memory_failures > failures);
        for (unsigned note = 47; note < 64; ++note) packet(streams,0x80,note,round * 1000 + 500,timing);
        timing.drain(streams,output,round * 1000 + 500);
        assert(output.quiet() && !streams.voices() && !streams.pending() && !streams.recovery());
    }
    assert(!streams.diagnostics().failed);
    std::printf("  packets=%u refused=%u overloaded=%u\n",output.sent,streams.diagnostics().dropped,streams.diagnostics().overloaded);
    timing.report(name,cntrl::memory_peak,cntrl::memory_allocations - allocations,cntrl::memory_failures - failures);
}
void deterministic_refusal_preserves_releases() {
    const auto bytes = graph(8);
    Timing timing;
    const unsigned allocations = cntrl::memory_allocations, failures = cntrl::memory_failures;
    cntrl::memory_peak = cntrl::memory_used;
    for (int fault = 0; fault < 24; ++fault) {
        Streams streams; Output output;
        assert(streams.load(bytes.data(),unsigned(bytes.size()),outputs,1));
        packet(streams,0x90,60,0,timing); timing.drain(streams,output,0);
        assert(!output.quiet());
        cntrl::memory_fail_after = fault;
        for (unsigned note = 61; note < 77; ++note) packet(streams,0x90,note,1,timing);
        timing.drain(streams,output,1);
        // Even an exhausted allocator must permit release of the accepted take.
        cntrl::memory_fail_after = 0;
        for (unsigned note = 60; note < 77; ++note) packet(streams,0x80,note,2,timing);
        timing.drain(streams,output,2);
        if (!output.quiet() || streams.voices() || streams.pending())
            std::fprintf(stderr,"fault=%d quiet=%u voices=%u pending=%u recovery=%u dropped=%u overloaded=%u failures=%u\n",
                fault,unsigned(output.quiet()),streams.voices(),streams.pending(),streams.recovery(),streams.diagnostics().dropped,
                streams.diagnostics().overloaded,cntrl::memory_failures);
        assert(output.quiet() && !streams.voices() && !streams.pending());
        cntrl::memory_fail_after = -1;
        streams.connection(0,false,1); timing.drain(streams,output,3);
        assert(output.quiet() && !streams.recovery());
    }
    assert(cntrl::memory_failures > failures);
    timing.report("fault-sweep-24-points",cntrl::memory_peak,cntrl::memory_allocations - allocations,cntrl::memory_failures - failures);
}
}
int main() {
    allocator_charge_refusal_preserves_the_running_buffer();
    list_nodes_keep_addresses_and_credits_through_transfer();
    list_reservation_failure_keeps_owned_and_borrowed_nodes();
    list_transfer_preserves_nested_payload_lifetime();
    buffer_growth_preserves_running_data_until_replacement_fits();
    cntrl::memory_budget = 430112;
    dense_input_and_service(24,false,"wide24-16steps-16notes");
    dense_input_and_service(32,false,"wide32-16steps-16notes");
    dense_input_and_service(48,false,"wide48-16steps-16notes");
    dense_input_and_service(64,false,"wide64-16steps-16notes");
    dense_input_and_service(64,true,"deep64-16steps-16notes");
    deterministic_refusal_preserves_releases();
    assert(cntrl::memory_used == 0);
}
