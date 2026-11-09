#include <cntrl/startup_counter.h>
using cntrl::StartupCounter;
#include "counter_file.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>

struct Response {
    char text[80] = {};
    std::vector<char> bytes;
    operator const char *() const { return text; }
    void reply(const char *value) { std::strcpy(text, value); bytes.clear(); }
    void data(const uint8_t *value, unsigned n) { text[0] = 0; bytes.assign(value, value + n); }
};

void test_raw_reads_preserve_every_byte() {
    CounterFile file; file.data.resize(1031);
    for (unsigned i = 0; i < file.data.size(); ++i) file.data[i] = char(i % 256);
    const auto original = file.data;
    Response response;
    StartupCounter::read_command("CNTRL STARTS SIZE", file, response);
    assert(!std::strcmp(response, "CNTRL STARTS SIZE 1031"));
    std::vector<char> received;
    for (unsigned offset = 0; offset < original.size(); offset += 128) {
        char command[64]; std::snprintf(command, sizeof(command), "CNTRL STARTS READ %u", offset);
        StartupCounter::read_command(command, file, response);
        unsigned n = std::min(128u, unsigned(original.size()) - offset);
        assert(response.bytes.size() == n);
        received.insert(received.end(), response.bytes.begin(), response.bytes.end());
    }
    assert(received == original && file.data == original && !file.writes);
    StartupCounter::read_command("CNTRL STARTS READ 1031", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-offset"));
    StartupCounter::read_command("CNTRL STARTS READ 0 extra", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-command"));
    file.fail_seek = file.seeks + 1;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-seek"));
    file.read_limit = 17;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-read 17/128"));
    file.read_limit = -1;
    StartupCounter::read_command("CNTRL STARTS READ 0", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-read -1/128"));
    file.active = false;
    StartupCounter::read_command("CNTRL STARTS SIZE", file, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-open"));
    assert(file.data == original && !file.writes);
    CounterFile empty;
    StartupCounter::read_command("CNTRL STARTS SIZE", empty, response);
    assert(!std::strcmp(response, "CNTRL STARTS SIZE 0") && !empty.writes);
    StartupCounter::read_command("CNTRL STARTS READ 0", empty, response);
    assert(!std::strcmp(response, "CNTRL ERROR starts-offset") && !empty.writes);
}


int main() { test_raw_reads_preserve_every_byte(); }
