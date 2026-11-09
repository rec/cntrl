#include <cntrl/usb_session.h>
#include <cassert>
#include <vector>
#include <string>

using cntrl::UsbKind;
using cntrl::UsbStatus;

std::vector<uint8_t> request(UsbKind kind, uint32_t session, uint32_t sequence, uint32_t ack, const std::vector<uint8_t> &payload = {}) {
    std::vector<uint8_t> bytes(cntrl::usb_header_size + payload.size());
    cntrl::UsbFrame{kind, UsbStatus::ok, session, sequence, ack, 0, unsigned(payload.size()), 0}.encode(bytes.data(), payload.data());
    return bytes;
}
void feed(cntrl::UsbSession &usb, const std::vector<uint8_t> &bytes) { for (uint8_t byte : bytes) usb.receive(byte); }
cntrl::UsbFrame response(const cntrl::UsbSession &usb) {
    assert(cntrl::UsbFrame::valid_header(usb.output()));
    auto frame = cntrl::UsbFrame::decode(usb.output());
    assert(usb.output_size() == cntrl::usb_header_size + frame.length);
    assert(frame.checksum == cntrl::usb_crc(usb.output() + cntrl::usb_header_size, frame.length));
    return frame;
}
int main() {
    // DMA RAM retains arbitrary bytes at boot; reset must initialize the handshake.
    cntrl::UsbSession retained;
    std::memset(static_cast<void *>(&retained), 0x64, sizeof(retained));
    retained.reset();
    feed(retained, request(UsbKind::open, 0xabc, 1, 0));
    assert(retained.resetting()); retained.reset_done();
    assert(response(retained).kind == UsbKind::text && !response(retained).length);

    cntrl::UsbSession usb;
    feed(usb, request(UsbKind::open, 0xabc, 1, 0));
    assert(usb.resetting()); usb.reset_done();
    assert(response(usb).sequence == 1 && response(usb).block_size == cntrl::usb_block_size);
    auto command = request(UsbKind::command, 0xabc, 2, 0, {'A', '\n'});
    feed(usb, command);
    std::string received;
    while (usb.input_size()) { received += char(usb.input()); usb.consumed(); }
    assert(received == "A\n"); usb.reply("CNTRL OK");
    feed(usb, command);
    assert(!usb.input_size() && !usb.reply_ready());
    assert(response(usb).kind == UsbKind::text && response(usb).length == 9);
    feed(usb, request(UsbKind::poll, 0xabc, 3, 1)); assert(usb.reply_ready());

    std::vector<uint8_t> payload(cntrl::usb_block_size);
    for (unsigned i = 0; i < payload.size(); ++i) payload[i] = uint8_t(i);
    auto full = request(UsbKind::disk, 0xabc, 4, 1, payload);
    // Corrupt headers and payloads must never reach the application.
    auto corrupt = full; corrupt[36] ^= 1; feed(usb, corrupt); assert(!usb.input_size());
    corrupt = full; corrupt.back() ^= 1; feed(usb, corrupt); assert(!usb.input_size());
    // A partially written frame followed by a retry resynchronizes at a valid header.
    for (unsigned i = 0; i < 53; ++i) usb.receive(full[i]);
    feed(usb, full);
    assert(usb.input_size() == payload.size() && usb.input_kind() == UsbKind::disk);
    assert(!std::memcmp(usb.input_bytes(), payload.data(), payload.size()));
    usb.data(payload.data(), unsigned(payload.size())); usb.consumed(unsigned(payload.size()));
    feed(usb, full); assert(!usb.input_size());
    assert(response(usb).kind == UsbKind::data);
    assert(response(usb).length == payload.size());
    assert(usb.output_size() == payload.size() + cntrl::usb_header_size);
    assert(!std::memcmp(usb.output() + cntrl::usb_header_size, payload.data(), payload.size()));

    feed(usb, request(UsbKind::open, 0xdef, 1, 0)); assert(usb.resetting()); usb.reset_done();
    feed(usb, command); assert(response(usb).status == UsbStatus::lost && !usb.input_size());
    feed(usb, request(UsbKind::close, 0xdef, 0, 0)); assert(usb.resetting()); usb.reset_done();
    assert(!usb.active());
    cntrl::UsbSession rebooted;
    feed(rebooted, command); assert(response(rebooted).status == UsbStatus::lost && !rebooted.input_size());
}
