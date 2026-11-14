#pragma once
#include <cntrl/ui/fonts.h>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>
#include <cstdlib>

namespace cntrl::emulator {
// Keep runtime file bytes alive behind the same descriptors used by the renderer.
class FontFiles {
public:
    FontFiles(int count, char **arguments) {
        if (!count || count % 2) throw std::runtime_error("emulator requires UI font ID/file pairs");
        data_.reserve(unsigned(count / 2)); faces_.reserve(unsigned(count / 2));
        for (int i = 0; i < count; i += 2) {
            char *end = nullptr;
            const unsigned long face = std::strtoul(arguments[i], &end, 10);
            const std::string path = arguments[i + 1];
            if (!arguments[i][0] || *end || face > 254 || !ui_font_valid(unsigned(face)))
                throw std::runtime_error("invalid UI font identity: " + path);
            for (const auto &existing : faces_) if (existing.face == face)
                throw std::runtime_error("duplicate UI font identity: " + path);
            std::ifstream input(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
            const auto length = input.tellg();
            if (!input || length < std::streampos(8) || length > std::streampos(font_assets::max_file))
                throw std::runtime_error("cannot read UI font: " + path);
            std::vector<uint8_t> bytes(static_cast<size_t>(length));
            input.seekg(0);
            if (!input.read(reinterpret_cast<char *>(bytes.data()), std::streamsize(bytes.size())) || !valid(bytes))
                throw std::runtime_error("invalid UI font asset: " + path);
            data_.push_back(std::move(bytes));
            faces_.push_back({unsigned(face), data_.back().data(), unsigned(data_.back().size())});
        }
        fonts = std::make_unique<ui::AssetFonts>(faces_.data(), unsigned(faces_.size()));
    }
    std::unique_ptr<ui::AssetFonts> fonts;
private:
    std::vector<std::vector<uint8_t>> data_;
    std::vector<ui::EmbeddedFont> faces_;
    static bool valid(const std::vector<uint8_t> &bytes) {
        if (std::memcmp(bytes.data(), "CFN2", 4)) return false;
        const unsigned count = cntrl::u32(bytes.data() + 4);
        if (!count || count > UINT8_MAX) return false;
        unsigned offset = 8; bool sizes[256] = {};
        for (unsigned i = 0; i < count; ++i) {
            font_assets::Entry entry;
            if (sizeof(entry.bytes) > bytes.size() - offset) return false;
            std::memcpy(entry.bytes, bytes.data() + offset, sizeof(entry.bytes)); offset += sizeof(entry.bytes);
            if (!entry.valid() || sizes[entry.size()] || entry.length() > bytes.size() - offset
                || !font_assets::valid_pixels(entry, bytes.data() + offset)) return false;
            sizes[entry.size()] = true; offset += entry.length();
        }
        return offset == bytes.size();
    }
};
} // namespace cntrl::emulator
