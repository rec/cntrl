#include <cntrl/asset_commands.h>
#include "counter_file.h"
#include <cassert>
#include <string>

struct Hardware {
    bool locked = false;
    bool configuration_locked() const { return locked; }
    unsigned memory_capacity() const { return 400000; }
    unsigned memory_runtime() const { return 240000; }
};
struct Replies {
    std::string response;
    void reply(const char *text) { response = text; }
};
struct Recorder {
    bool recording = false;
    bool active() const { return recording; }
};
struct History {
    bool transferring = false;
    unsigned calls = 0;
    bool command(const char *line, uint32_t) { ++calls; return !std::strcmp(line, "CNTRL HISTORY END"); }
    bool busy() const { return transferring; }
};
struct Transfer {
    unsigned calls = 0;
    uint32_t last = 0;
    bool command(const char *, uint32_t now) { ++calls; last = now; return true; }
};
struct Listing {
    unsigned calls = 0;
    void next(char *response, unsigned size) { ++calls; std::snprintf(response, size, "CNTRL END 0"); }
};
struct Editor {
    bool visible = false;
    bool open() const { return visible; }
};
struct Fonts {
    unsigned calls = 0;
    bool command(const char *line, Replies &, uint32_t) { ++calls; return !std::strcmp(line, "CNTRL FONT LIST"); }
};
unsigned native_calls = 0;
bool native_command(const char *line) { ++native_calls; return !std::strcmp(line, "CNTRL NATIVE"); }
struct Services {
    Hardware hardware;
    Replies replies;
    cntrl::StartupCounter counter;
    CounterFile counter_file;
    Recorder recorder;
    History history;
    Transfer bundle, disk;
    Listing listing;
    Editor song_editor;
    Transfer songs;
    Fonts fonts;
    bool (*native_extension)(const char *) = native_command;
};

int main() {
    Services services;
    cntrl::AssetCommands<Services> commands(services);
    services.hardware.locked = true;
    services.history.transferring = true;
    assert(commands.command("CNTRL HISTORY END", 1));
    assert(commands.command("CNTRL MEMORY", 2));
    assert(services.replies.response == "CNTRL ERROR history-import-busy");
    assert(native_calls == 0);
    services.history.transferring = false;
    assert(commands.command("CNTRL MEMORY", 3));
    assert(services.replies.response == "CNTRL MEMORY 400000 240000");
    assert(commands.command("CNTRL BUNDLE LIST", 4));
    assert(services.bundle.calls == 1 && services.bundle.last == 4);
    assert(commands.command("CNTRL STORAGE NEXT", 5));
    assert(services.listing.calls == 1 && services.replies.response == "CNTRL END 0");
    assert(commands.command("CNTRL STATUS", 6));
    assert(services.replies.response == "CNTRL ERROR close-switches-first");
    assert(native_calls == 0 && services.fonts.calls == 0);

    services.recorder.recording = true;
    assert(commands.command("CNTRL STARTS RESET", 7));
    assert(services.replies.response == "CNTRL ERROR starts-recording");
    assert(services.counter_file.writes == 0);
    services.recorder.recording = false;
    assert(commands.command("CNTRL STARTS RESET", 8));
    assert(services.replies.response == "CNTRL OK");
    assert(services.counter_file.writes != 0 && !services.counter_file.opened());
    assert(commands.command("CNTRL STARTS READ 0", 9));
    assert(services.replies.response.find("CNTRL DATA ") == 0);
    assert(!services.counter_file.opened());

    services.hardware.locked = false;
    assert(commands.command("CNTRL NATIVE", 10));
    assert(native_calls == 1 && services.fonts.calls == 0);
    services.song_editor.visible = true;
    assert(commands.command("CNTRL SONGS LIST", 11));
    assert(services.replies.response == "CNTRL ERROR close-song-editor" && services.songs.calls == 0);
    services.song_editor.visible = false;
    assert(commands.command("CNTRL SONGS LIST", 12));
    assert(services.songs.calls == 1 && services.songs.last == 12);
    assert(commands.command("CNTRL DISK BEGIN 0", 13));
    assert(services.disk.calls == 1 && services.disk.last == 13);
    assert(commands.command("CNTRL FONT LIST", 13));
    assert(!commands.command("CNTRL STATUS", 14));
    assert(services.fonts.calls == 2);
    services.native_extension = nullptr;
    assert(!commands.command("CNTRL UNKNOWN", 15));
}
