# How CNTRL works - the big picture

All of the rest of the documents in this directory were AI written and even if
I edit them, they'll be mostly clanker, but this is entirely written by me, a person.

I tried to make everything as simple and musically useful as possible. There are a huge
number of poential choices, but most things can be expressed very simply.

## The UI on the pedal itself

The MC6 Pro has three displays, and I hope soon to cover the MC8 Pro with four displays.

On the left displays, CNTRL shows one of two views:

* **setlist view**, to step between scenes in songs in setlists
* **browser view**, for browsing patches and instruments

On the right there are **utility pages** or just **pages**, that you can cycle through.

The page uses zero or more displays starting from the right, and the view gets the rest.


## Dramatis personae

Some key concepts.

A **patch** does things! Often a patch sends one or more MIDI messages, but it can do
all sort of things, like navigate, switch relays, or controlling MIDI recording and
playback.

An **instrument** is a thing that receives MIDI, and we're particularly interested in
things like banks and program names for instruments so you can get good names in the
foot pedal.
