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
    bool ready = true, revision = false;
    std::map<std::string, std::map<uint64_t, std::string>> revisions;
    void choose_revision(const char *name, uint64_t number) override {
        directory = std::string("history/") + name + "/" + std::to_string(number);
        revisions[name][number] = directory; revision = true;
    }
    std::map<uint64_t, std::string>::const_iterator scan_, scan_end_;
    uint64_t scan_before_ = 0;
    bool scan_records_ = false;
    void revision_scan(const char *name, uint64_t before, bool records_only) override {
        scan_ = revisions[name].begin(); scan_end_ = revisions[name].end(); scan_before_ = before; scan_records_ = records_only;
    }
    bool revision_scan_step(uint64_t &best) override {
        if (scan_ == scan_end_) return true;
        if (scan_->first < scan_before_ && scan_->first > best
            && (!scan_records_ || files[scan_->second][0].size() >= 208)) best = scan_->first;
        ++scan_; return false;
    }
    bool remove_revision(const char *name, uint64_t number) override {
        auto &r = revisions[name]; auto i = r.find(number); if (i == r.end()) return false;
        files.erase(i->second); return true;
    }
    bool available() const override { return ready; }
    void choose(const char *name) override {
        revision = false; directory = name;
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
    bool begin(unsigned s) override { if (revision && !files[directory][s].empty()) return false; slot = s; files[directory][s].clear(); return ready; }
    bool write(const uint8_t *data, unsigned n) override {
        unsigned amount = remaining < 0 ? n : std::min(n, unsigned(remaining));
        auto &bytes = files[directory][slot]; bytes.insert(bytes.end(), data, data + amount);
        if (remaining >= 0) remaining -= int(amount);
        return amount == n;
    }
    bool finish() override { return true; }
};
