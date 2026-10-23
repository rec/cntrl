// Host-only receiver for the Python PTY tests. No device access.
#include "../receiver.h"
#include "storage.h"
#include <unistd.h>

struct Wire : cntrl::splash::Replies {
    void reply(const char *line) override {
        for (const char *p = line; *p; ++p) if (::write(1, p, 1) != 1) return;
        ::write(1, "\n", 1);
    }
};

int main() {
    Memory memory;
    Wire wire;
    cntrl::splash::Receiver receiver(memory, wire);
    receiver.boot();
    while (receiver.busy()) receiver.poll(0);
    uint8_t bytes[512];
    ssize_t count;
    while ((count = ::read(0, bytes, sizeof(bytes))) > 0) {
        for (ssize_t i = 0; i < count; ++i) receiver.receive(bytes[i], 0);
        while (!receiver.accepts_input()) receiver.poll(0);
    }
}
