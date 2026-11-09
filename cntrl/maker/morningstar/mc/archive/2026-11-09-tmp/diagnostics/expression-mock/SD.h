#pragma once
#include <vector>
#include <algorithm>
#include <cstring>
#include <cstddef>
constexpr int O_RDONLY=0, O_CREAT=1, O_RDWR=2;
struct File {
    std::vector<unsigned char>* data;
    unsigned pos = 0;
    explicit operator bool() const { return data; }
    bool seekSet(unsigned n) { pos=n; return data && pos <= data->size(); }
    size_t read(void *out, size_t n) {
        if (!data || pos+n>data->size()) return 0;
        std::memcpy(out,data->data()+pos,n); pos+=n; return n;
    }
    size_t write(const void* from, size_t n) {
        if (data->size()<pos+n) data->resize(pos+n);
        std::memcpy(data->data()+pos,from,n);pos+=n;return n;
    }
    bool sync() { return true; }
};
inline struct { struct {
    std::vector<unsigned char> data;
    File open(const char *name, int flags) {
        return {std::strcmp(name,"/cntrl/expression-1.cal") || (data.empty() && !flags) ? nullptr : &data};
    }
} sdfs; } SD;
