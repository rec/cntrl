#pragma once
#include "filesystem.h"
#include <cntrl/midi_files.h>

// File objects outlive the portable playback and recording policy.
struct MidiFileStorage {
    cntrl::device::SdFileSystem filesystem;
    cntrl::device::SdFile source, index, recording;
    cntrl::device::SdDirectory entries;
};
