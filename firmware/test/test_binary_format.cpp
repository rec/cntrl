#include <cntrl/binary_format.h>
#include <cassert>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

struct Reader {
    const std::vector<uint8_t> &bytes;
    bool read(unsigned offset, uint8_t *out, unsigned n) {
        assert(n <= 512);
        if (offset > bytes.size() || n > bytes.size() - offset) return false;
        std::memcpy(out, bytes.data() + offset, n); return true;
    }
};

int main(int argc, char **argv) {
    assert(argc >= 2);
    for (int i = 1; i < argc; ++i) {
        std::ifstream input(argv[i], std::ios::binary);
        std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(input), {});
        assert(bytes.size() >= 28);
        const unsigned size = unsigned(bytes.size());
        auto kind = cntrl::binary::Kind(cntrl::binary::read16(bytes.data() + 6));
        Reader reader{bytes}; cntrl::binary::View memory, streamed;
        uint32_t crc = 0;
        bool valid = cntrl::binary::open(bytes.data(), size, kind, memory);
        assert(cntrl::binary::open_reader(reader, size, kind, streamed, &crc) == valid);
        std::string name = argv[i]; bool rejected = name.substr(name.size() - 4) == ".bad";
        assert(valid != rejected);
        if (!valid) continue;
        assert(memory.offset == streamed.offset && memory.length == streamed.length && memory.opaque == streamed.opaque);
        assert(crc == cntrl::binary::crc(bytes.data(), size));
        uint8_t prefix[28];
        unsigned extensions = size - memory.offset - memory.length;
        assert(cntrl::binary::prefix(prefix, kind, memory.length, extensions));
        auto encoded = bytes;
        std::memcpy(encoded.data(), prefix, 28);
        cntrl::binary::seal(encoded.data(), size);
        assert(encoded == bytes);
    }
}
