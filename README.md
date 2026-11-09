# cntrl: a general model of MIDI controllers

## Video: to come

[amazing demonstration of old and new pedal functionality]

## How to install

See [installation and host qualification](doc/cross-platform.md) for macOS,
Windows and Linux development setup.

## What is `cntrl`?

It's a toolkit in Python and C designed to help build new interfaces and firmwave for
existing MIDI controllers, with an immediate emphasis on foot pedals.

As a demonstration, I have fancy new firmware for the excellent Morningstar Engineering
MC6 footpedal with many features never before seen in a footpedal, and it's ready to
install.

## Why?

I got my first MIDI footpedal, the Lake Butler Music MIDI Mitigator, in the late 80s,
and it was my constant companion on live gigs for very many years.

But it is big, and it has limited smarts.

As a MIDI instrumentalist who does both songs and improvisations, I have had strong
ideas about what I wanted out of a MIDI footpedal for a long time, and I rigged up some
setup with a computer for a while, but it was painful.

Now we have very sophisticated hardware, and we have AI tools to adapt our software
to that hardware.

## Is this AI slop?

There was heavy use of a coding assistant, but I have been coding for half a century,
and I have spent a hundred hours carefully controlling it for clarity and ease of use,
reliability, and speed of response.

## Features

 (* means "in progress", otherwise it's complete)

* Free and open source

* One-press switch to original software and back without rebooting

* Easy to create simple setups, but deep customization is also possible

* Easy editing, reuse, combination, backup and sharing of patches, setups, songs, and
  sets in friendly .toml text files with a documented format.

* Automatic backups of previous configs; even an undo, when possible.

* Fast response to button presses: usually 1 ms, always below 3.5ms from button
  down to first MIDI byte

* Plug-in architecture with many types of active pages
  * Patch switcher
  * MIDI recorder/playback
  * MIDI looper(*)
  * Arpeggiator for polyphonic or monophonic instruments(*)
  * Drone controller(*)
  * MIDI cable tester
  * MIDI filtering, merging, splitting, etc(*)
  * USB host and device services(*)
  * Panic
  * MIDI monitor(*)

* Display text messages received over MIDI or USB(*)
  * i.e. cues or warnings from your computer

* The usual gestures: click, double-, hold, button pairs, hold/release, etc, also
  "press while held"

* The usual controllers:
  * Switches/buttons

* Graphics: use your own fonts, autostyling, scrolling, pulsating "action" key

* Customizable animations for splash and screen saver
