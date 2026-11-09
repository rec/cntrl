#define main existing_switcher_tests
#include "test_switcher.cpp"
#undef main
struct Board : Fake {
    bool supports_font(unsigned, unsigned) const override { return true; }
    bool supports_font_asset(uint32_t, unsigned) const override { return true; }
};
int main(int argc, char **argv) {
    assert(argc == 2);
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(f), {}};
    Board board;
    cntrl::Document d;
    assert(d.load(bytes.data(), unsigned(bytes.size()), board));
    auto a = d.utility_pages().expression[1];
    assert(!std::strcmp(a.output, "din") && a.channel == 7 && a.controller == 11);
    assert(a.minimum == 10 && a.maximum == 100 && a.reverse && a.curve == cntrl::ExpressionCurve::slow);
    assert(!d.utility_pages().expression[0].channel && d.patches() == 59);
    bytes[d.bank(d.patches()) - d.data() + 38 + 32] = 17;
    cntrl::put32(bytes.data() + 20, cntrl::checksum(bytes.data() + 28, unsigned(bytes.size()) - 28));
    assert(!d.load(bytes.data(), unsigned(bytes.size()), board));
}
