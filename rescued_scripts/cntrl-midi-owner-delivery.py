from pathlib import Path
p=Path('firmware/include/cntrl/midi_file.h');s=p.read_text();s=s.replace('    unsigned clock_outputs() const {','''    unsigned owner() const { return owner_; }
#ifdef CNTRL_FLASH_ROUTINE
    CNTRL_FLASH_ROUTINE(midi_file_delivery)
#endif
    Bridge::Delivery delivery(unsigned owner) const {
        if (owner >= Bridge::owners) return Bridge::Delivery::cancelled;
        if (owner != owner_) return deliveries_[owner];
        if (active() || cleaning()) return Bridge::Delivery::pending;
        return status_ == Status::complete ? Bridge::Delivery::complete : Bridge::Delivery::cancelled;
    }
    unsigned clock_outputs() const {''')
s=s.replace('''    bool start(const PlayMidiFilePatch &settings) {
        if (active() || cleaning()) return false;
        cancel();''','''    bool start(const PlayMidiFilePatch &settings, unsigned owner = 0) {
        if (owner >= Bridge::owners || active() || cleaning()) return false;
        deliveries_[owner_] = delivery(owner_); owner_ = owner;
        cancel();''')
s=s.replace('    Status status_ = Status::idle;', '    Status status_ = Status::idle;\n    unsigned owner_ = 0;\n    std::array<Bridge::Delivery, Bridge::owners> deliveries_{};');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/midi_files.h');s=p.read_text().replace('    unsigned owner = 0;\n','').replace('        deliveries_[owner] = delivery(owner);\n','').replace('        owner = invocation;\n        return player.start(settings);','        return player.start(settings, invocation);');i=s.index('    cntrl::midi::Bridge::Delivery delivery(');j=s.index('    void receive(unsigned input, const',i);s=s[:i]+s[j:];s=s.replace('    std::array<cntrl::midi::Bridge::Delivery, cntrl::midi::Bridge::owners> deliveries_{};\n','');p.write_text(s)
p=Path('cntrl/maker/morningstar/mc/firmware/patch_switcher/target.cpp');s=p.read_text().replace('return midi_files.delivery(owner);','return midi_files.player.delivery(owner);').replace('owner == midi_files.owner','owner == midi_files.player.owner()');p.write_text(s)
p=Path('firmware/test/test_midi_file.cpp');s=p.read_text();i=s.index('int main(');s=s[:i]+'''void test_a_new_invocation_keeps_the_previous_delivery_result() {
    auto source = smf({{0,0xc0,12,0,0xff,0x2f,0}});
    File index; Sink sink; cntrl::midi::MidiFilePlayer player(source,index);
    assert(player.start({},5));
    for (unsigned i = 0; i < 50; ++i) player.poll(0,1,sink);
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
    assert(player.start({},0));
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
    assert(player.delivery(0) == cntrl::midi::Bridge::Delivery::pending);
    player.cancel(); assert(player.start({},1));
    assert(player.delivery(0) == cntrl::midi::Bridge::Delivery::cancelled);
    assert(player.delivery(5) == cntrl::midi::Bridge::Delivery::complete);
}

'''+s[i:];s=s.replace('    test_recording_serializes_independent_input_sysex_streams();','    test_a_new_invocation_keeps_the_previous_delivery_result();\n    test_recording_serializes_independent_input_sysex_streams();');p.write_text(s)
