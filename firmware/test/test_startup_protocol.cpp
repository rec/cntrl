#include <cntrl/startup_counter.h>
using cntrl::StartupCounter;
#include "counter_file.h"
#include <algorithm>
#include <cassert>
#include <string>
#include <vector>


void test_raw_reads_preserve_every_byte() {
    CounterFile file; file.data.resize(1031);
    for (unsigned i = 0; i < file.data.size(); ++i) file.data[i] = char(i % 256);
    const auto original = file.data;
    char response[280];
    StartupCounter::read_command("CNTRL STARTS SIZE", file, response);
    assert(!std::strcmp(response, "CNTRL STARTS SIZE 1031"));
    std::vector<char> received;
    for (unsigned offset = 0; offset < original.size(); offset += 128) {
        char command[64]; std::snprintf(command, sizeof(command), "CNTRL STARTS READ %u", offset);
        StartupCounter::read_command(command, file, response);
        assert(!std::strncmp(response, "CNTRL DATA ", 11));
        unsigned n = std::min(128u, unsigned(original.size()) - offset);
        assert(std::strlen(response) == 11 + n * 2);
        for (unsigned i = 0; i < n; ++i)
            received.push_back(char(std::stoul(std::string(response + 11 + i * 2, 2), nullptr, 16)));
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
