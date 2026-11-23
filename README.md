# `cntrl`: custom firmware for MIDI controllers, with a ripping example

## What is it?

It's a toolkit to write new firmware for MIDI controllers.

As a demonstration, I have fancy new firmware for the excellent Morningstar Engineering
(ME) MC6 Pro footpedal with many features, some deep and broad, some simple and useful,
some outright stupid, most never before seen in a footpedal, and it's ready to install
today.

FYI: all of the text on this page was written by a human.

## Video: to come

[amazing demonstration of old and new pedal functionality]

## How to install

See [installation and host qualification](doc/cross-platform.md) for macOS,
Windows and Linux development setup.

Use the [desktop emulator](doc/emulator.md) to exercise the shared application
with a controller layout, virtual MIDI and local session storage without a pedal.

## What is `cntrl`?


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

* One press to original software and back without rebooting

* Easy to create simple setups, but deep customization is also possible

* Easy editing, reuse, combination, backup and sharing of patches, setups, songs, and
  sets in friendly .toml text files with a documented format

* Automatic backups of previous configs

* Fast response to button presses: usually 1 ms, always below 3.5ms from button
  to first MIDI byte

* A really good setlist/song/scene manager which you can set up in advance
  or put together a set on the fly on the pedal iteself

* A really good Patch Chooser to let you dial in patches from categories and then
  trigger them separately

* Many pedal pages to choose from in your setup, add one, several or all:
  * Patch Chooser (with undo)
  * Setlist Manager
  * MIDI
    * recorder
    * playback
    * looper
    * monitor
    * cable tester
    * routing (these last two are only available from the CLI)
    * filter
    * USB hosting and forwarding
  * Tap tempo
  * Arpeggiator for polyphonic or monophonic(!) instruments
  * Panic
  * Boring general status page
  * Exciting Pong and Doom (because I can)
  * Setup/setlist/song/scene/patch editor pages

* Pedal UI is available in 17 European languages
  * Take that, Behringer

* An emulator, so you can experiment with patches when the pedal is elsewhere.

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
  * Tap, double-tap, triple-tap
  * Hold/release for each of those three
  * Combinations of multiple switches
  * Tap while hold

* All the controllers
 * MIDI in/out
 * relays
 * Omniports
 * expression pedal
 * etc
 * USB device and host MIDI
 * USB typing keyboard triggers

## How does it work?

Right now, it only works on the Morningstar MC6 Pro.

If you have another Morningstar pedal, hit me up immediately by filing an issue, we can
make it work for you.

CNTRL is designed to be hardware agnostic and I intend to port this to other controllers
in the future.

`cntrl` is a command line application that runs on your computer and allows you to
install the new firmware in parallel to the old stock firmware onto a Morningstar MC6
Pro pedal.

There is no GUI. It's all set up in human-friendly .toml files, which you can
combine in all sorts of different ways.

You can develop all sorts of files with different sets, songs, patches, instruments and
combine them on the fly, from the command line or from the pedal.

I have installed and run variations on this new firmware hundreds of times without
problems, but there are no guarantees, actual or implicit, as to whether this will work
for you. **In the very worst case, Morningstar Engineering, in their wisdom, have a
hardware reset button, where you can unscrew the bottom, press a button, and go back
to the old software with no trace of the new.**

## How to set up

In stage one, you upload new firmware to the pedal in parallel to the stock firmware.

You can choose to power up with either the stock firmware, or CNTRL, and you can
switch that choice from the pedal, or the command line.

In stage two, you represent your setups, your instruments, your songs, your setlists in
easy-to-edit and read text files in the .toml format.


There is a wizard to walk you though this, and some examples.

The pedal has a complete copy of your source files, so you can work from any computer.
There's also a system to allow you to undo changes back to the start, in case you do
something horrible at the last minute.

(How is that even possible? It turns out that the pedal has a 32 gig SD card in it, most
of which is empty. It's a really nice piece of gear.)
