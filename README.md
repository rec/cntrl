
FYI: all of the text below was written by a human.

## Video: to come

[amazing demonstration of old and new pedal functionality]

## How to install

See [installation and host qualification](doc/cross-platform.md) for macOS,
Windows and Linux development setup.

Use the [desktop emulator](doc/emulator.md) to exercise the shared application
with a controller layout, virtual MIDI and local session storage without a pedal.

## What is `cntrl`?

It's a toolkit to write new firmware for MIDI controllers.

As a demonstration, I have fancy new firmware for the excellent Morningstar Engineering
(ME) MC6 Pro footpedal with many features never before seen in a footpedal, and it's ready
to install today!

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
and I have spent a couple of hundred hours carefully controlling it for clarity and ease
of use, reliability, and speed of response.

## Is this legal?

It is my belief that this is. I do not distribute any of ME's intellectual property and
I was very careful to not analyze ME's firmware beyond the very minimum I meeded to
understand to control the hardware. This new firmware is completely independently
developed, and based on a formal model of MIDI controllers, and an abstract API.

I hope that Morningstar Engineering accepts this project in the positive spirit in which
it is offered!

## Features

* Free and open source

* One-press switch to original software and back without rebooting

* Easy to create simple setups, but deep customization is also possible

* Easy editing, reuse, combination, backup and sharing of patches, setups, songs, and
  sets in friendly .toml text files with a documented format.

* Automatic backups of previous configs; even an undo, when possible.

* Fast response to button presses: usually 1 ms, always below 3.5ms from button
  down to first MIDI byte

* Many pages to choose from
  * Patch switcher
  * MIDI recorder/playback
  * MIDI looper
  * Tap tempo
  * Arpeggiator for polyphonic or monophonic instruments
  * MIDI cable tester
  * MIDI filtering, merging, splitting, etc
  * USB host and device services
  * Panic
  * MIDI monitor
  * More to come
  * One setup might have all of these; another might have two - and you can switch live.

* Graphics
 * Use any fonts
 * Autostyling
 * Smooth scrolling
 * Pulsating "action" key

* Customizable animations for splash and screen saver
  * Because I can

* Display text messages received over MIDI or USB
  * i.e. cues or warnings from your computer or other device

* All the gestures:
  * tap
  * double-tap
  * triple-tap
  * hold/release for each of those three
  * combinations of multiple switches
  * tap while hold

* All the controllers
 * MIDI in/out
 * relays
 * Omniports
 * expression pedal
 * etc
 * USB device and host MIDI
 * USB typing keyboard triggers

## How does it work?

`cntrl` is a command line application that runs on your computer and allows you to
install the new firmware in parallel to the old stock firmware onto a Morningstar MC6
Pro pedal.

If you have another Morningstar pedal, hit me up immediately by filing an issue,
we can make it work for you. It is all designed to be hardware agnostic.

There are two stages.

In stage one, you upload new firmware in parallel to the stock firmware.


You represent your setups in easy-to-edit and read .toml files with things like program
names and banks for your instruments, or `cntrl` patches you have created, or tags
you've added to programs or banks.

You can have one .toml file with all your configurations, or you can develop all sorts
of files with different sets, songs, patches, instruments and combine them on the fly,
from cntrl, or even right from the pedal.

The pedal has a complete copy of your source files, so you can work from any computer.
There's also a system to allow you to undo changes back to the start, in case you do
something horrible at the last minute.

(How is that even possible? It turns out that the pedal has a 32 gig SD card in it, most
of which is empty. It's a really nice piece of gear.)
