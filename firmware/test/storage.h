#pragma once
#include <cntrl/documents.h>
#include <array>
#include <map>
#include <string>
#include <vector>

struct TestStorage : cntrl::SetupStorage {
    std::map<std::string, std::array<std::vector<uint8_t>, 2>> files;
    std::string directory;
    unsigned slot = 0;
    int remaining = -1;
    bool ready = true;
    bool available() const override { return ready; }
    void choose(const char *name) override {
        directory = name;
        for (const auto &f : files) if (!cntrl::name_compare(f.first.c_str(), name)) { directory = f.first; break; }
    }
    bool next(const char *after, char *name) override {
        const char *best = nullptr;
        for (const auto &f : files)
            if (cntrl::setup_name(f.first.c_str()) && cntrl::name_compare(f.first.c_str(), after) > 0
                && (!best || cntrl::name_compare(f.first.c_str(), best) < 0)) best = f.first.c_str();
        if (best) std::snprintf(name, 64, "%s", best);
        return best != nullptr;
    }
    unsigned size(unsigned s) override {
        auto f = files.find(directory); return f == files.end() ? 0 : unsigned(f->second[s].size());
    }
    bool read(unsigned s, unsigned offset, uint8_t *data, unsigned n) override {
        auto f = files.find(directory);
        if (f == files.end() || offset + n > f->second[s].size()) return false;
        std::memcpy(data, f->second[s].data() + offset, n); return true;
    }
    bool begin(unsigned s) override { slot = s; files[directory][s].clear(); return ready; }
    bool write(const uint8_t *data, unsigned n) override {
        unsigned amount = remaining < 0 ? n : std::min(n, unsigned(remaining));
        auto &bytes = files[directory][slot]; bytes.insert(bytes.end(), data, data + amount);
        if (remaining >= 0) remaining -= int(amount);
        return amount == n;
    }
    bool finish() override { return true; }
};
