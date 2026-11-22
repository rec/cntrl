# CNTRL user glossary

[Set mode](#set-mode) · [Patches and controls](#patches-and-controls) ·
[Pages and display](#pages-and-display) · [MIDI and connections](#midi-and-connections) ·
[Timing and playback tools](#timing-and-playback-tools) ·
[Setups and files](#setups-and-files) · [Emulator](#emulator)

## Set mode

| Term | Meaning |
| --- | --- |
| **Set mode**, **set performance** | Performing a setlist in **Songs and setlists**. CNTRL keeps your place in the set and shows what's current and what's next. |
| **Songs and setlists** | The menu in **Pages and tools** where you perform, create, and edit songs and setlists. |
| **Song library** | Your saved collection of songs and setlists. The **Songs** picker browses songs alphabetically. **Jump** moves between initial letters. |
| **Setlist**, **set** | An ordered list of songs to play. The same song can appear more than once. |
| **Active setlist** | The set currently selected for performance. Choose it under **Setlists**. The song highlighted for editing can change while the playback position stays put. |
| **Setlist entry**, **song occurrence** | One place in a particular setlist where a song appears. If you play the same song twice, those are two entries that can be moved or removed independently. Editing the shared song affects both entries. |
| **Song** | A named item with one or more patch steps in a fixed order. Think of one step for each change you want to recall during the song. A song can also have its own control bindings and enter and exit actions. |
| **Step**, **song step** | One position in a song's patch list. Advancing to it recalls its patch. A patch sequence runs all its actions at that one step. |
| **Recall** | Run a step's patch to apply its MIDI messages, controller actions, or control assignments. Browsing the setlist moves the highlight only. |
| **Current** | The song and step at the playback position. The highlighted entry in the middle of the set view is the one selected for editing. |
| **Next**, **Next step** | The upcoming playback position. It can be another step in the same song, an empty position, or the start of the next song. On the MC Pro, release F to advance or use C+F to go back. |
| **Empty position**, **song boundary** | A playback position before the first song, between two songs, or after the last song. Leaving a song releases its controls and runs its optional exit action. From an empty position with a song ahead, Next enters that song and recalls its first step. |
| **Enter / exit actions** | Optional patches that run when crossing into or out of a song, in either direction. On entry, the enter action runs before the step's patch. |
| **Stock firmware visit** | A patch can temporarily switch to the manufacturer's firmware. Returning with the configured buttons resumes the saved performance if its source is still available and unchanged. |
| **Perform setlist / Perform song** | **Perform setlist** opens the selected set for performance. **Perform song** starts a single song from the library at its leading empty position. |
| **New setlist** | Creates an empty set with a unique default name and opens the song picker so you can add its entries. |
| **Restart setlist** | Returns playback to the opening empty position so the first entry is next. The first recall happens when you advance. |
| **Toggle startup set** | Saves the active setlist as the one to open at power-on. Choosing the action again for that set clears the preference. Startup opens at the first empty position and waits for you to advance. |
| **End of set** | There are no more steps or entries to recall. Pressing Next again sends no MIDI. |
| **Save as song**, **inline patch** | **Save as song** creates a song from the patch highlighted in Patch Chooser. The song stores a copy of that patch with its own step. Later edits to the original patch leave the copy intact. References inside the copy can still resolve live. |
| **Save reference**, **live reference** | Creates a song step that looks up a saved patch each time you recall it. The reference uses the setup name and the patch's key. If either is missing, recall fails and playback stays in place. |
| **Use selected patch** | Replaces the song's first step with an inline copy of the selection in Patch Chooser. The song keeps its other steps, name, and places in setlists. |
| **Edit steps** | Opens the song's ordered steps. You can replace a step with the Patch Chooser selection, insert that selection after a step, remove a step, or move it earlier or later. |
| **Add next / Add to end** | Insert songs at the next entry position or append them to the end of the set. Successive additions in the picker keep the order in which you choose them. |
| **Move** | Changes the position of the highlighted occurrence in the setlist. |
| **Remove from setlist**, **Current (removed)** | Removal takes one occurrence out of the setlist. If it was the current occurrence, its sound stays active and the status reads **Current (removed)** until playback moves on. |
| **Missing song** | The setlist names a song that the library cannot find. CNTRL leaves playback in place if you try to recall it. |
| **Undo edit** | Reverses the most recent saved song-library or setlist edit. It restores the saved contents and playback position without recalling a sound. |

[Songs and setlists on the pedal](../cntrl/maker/morningstar/mc/songs-and-setlists.md) ·
[Authoring patches and songs](patches.md) · [Patch bindings](control-bindings.md)

## Patches and controls

| Term | Meaning |
| --- | --- |
| **Patch** | A named or inline action you can run from a page, a song step, or a switch. It can select an instrument sound, send MIDI, change a CNTRL setting, or run a sequence. It can also assign controls and display text. |
| **Patch key / display name** | The key is the name used in a file to look up a named patch, for example `clean` in `[patches.clean]`. The display name is the label you see on the pedal. |
| **Derived patch**, **parent patch** | A patch that starts with another patch's settings and overrides fields you specify. The patch named in `parent` supplies those starting settings. |
| **Patch sequence** | An ordered list of actions run by a single patch. A delay can leave time between actions, for example while an instrument loads a sound. |
| **Patch cycle** | A named list of patches you can browse in Patch Chooser. Browsing within a cycle selects another candidate. Changing cycles selects a different list. |
| **Patch Chooser**, **patch mode** | The page for browsing and sending patches directly. It shows the next candidate, the last sent patch, and an Undo preview as space permits. With one screen it shows the last sent patch. Each Patch Chooser page keeps its own browsing and recall history. |
| **Selected patch**, **candidate** | The highlighted patch ready to send. **Activate selection** runs it. **Save as song** and **Use selected patch** use this highlight even if you have sent a different patch. |
| **Sent patch**, **current patch** | In Patch Chooser, the last patch that completed successfully. MIDI delivery here means CNTRL handed the messages to its output, with no confirmation expected from the receiving instrument. |
| **Pending**, **unknown remote state** | Pending means CNTRL is still working on an action. Unknown remote state means CNTRL cannot tell which values the receiving instrument now has, for example after an output disconnects during a send. |
| **Undo** | In Patch Chooser, recalls a previous known program selection when available. It cannot reverse arbitrary actions in a patch. The looper and setlist editor have their own Undo actions. |
| **Binding**, **assignment** | What a control does in a setup, song, or patch. A binding can run a patch on a switch gesture or send MIDI as an expression pedal moves. |
| **Scene** | A patch that takes over all the pedal's windows and supplies the control bindings for that part of a song. For example, a song can use a verse scene, a chorus scene, then the verse scene again. |
| **Binding map** | The assignments that determine what the pedal's controls do while a scene is active. |
| **Binding scope**, **inherits** | A scope is where an assignment is defined. Song assignments can override shared ones. A patch's `inherits` setting chooses whether it keeps enclosing switch assignments, expression assignments, both, or neither. |
| **Control role / control label** | A role is what a switch does in the current view, such as Confirm or Back. A label is what is printed on the pedal, such as C or F. Follow the labels shown in the current view. |
| **Gesture**, **switch chord** | A gesture is a way of using a switch, such as tapping, double-tapping, holding, or releasing it. A switch chord uses multiple switches together, such as C+F. |
| **Press / momentary / latched** | A press assignment runs once per press. Momentary runs an On action when pressed and an Off action when released. Latched alternates between On and Off on successive presses. |
| **Shift layer** | An alternate set of assignments you can bring into effect with a configured Shift action. Unshift restores the ordinary layer. |
| **Switches** | A tool for using configured press, momentary, and latched assignments. Its indicators reflect the MIDI values CNTRL knows it has sent. |
| **Expression pedal** | A continuous foot control often used for volume, wah, or another MIDI parameter. **Calibration** records the pedal's heel and toe positions so CNTRL can map the full range. |
| **Pickup** | Waits for a live control to reach or cross a destination's last known value before sending changes. This avoids an abrupt jump when the physical control is at a different position. |
| **Auxiliary switch**, **aux** | An external footswitch plugged into a supported input and assigned its own actions. |

[Patch types](patches.md#patch-types) · [Patch bindings](control-bindings.md) ·
[Switch assignments](momentary-latched.md) · [Pickup](pickup.md) ·
[Auxiliary switches](auxiliary-switches.md)

## Pages and display

| Term | Meaning |
| --- | --- |
| **Page**, **page type** | A named view in a setup. Its type determines what it does, for example Patch Chooser, Status, or Time. A setup can have more than one page of a type. |
| **Region** | A group of pages that you can browse independently. One page is selected per region. A multi-display setup can show a performance region and a tools region at the same time. |
| **Focus** | The region currently receiving page-specific controls. A border marks its visible windows. Changing focus leaves the other regions on their selected pages. |
| **Window**, **display** | A display is a physical screen. A window is a page's use of one screen. As pages change, the layout can give a page more or fewer windows. |
| **Pages and tools / Menu** | **Pages and tools** opens configured pages, regions, and tools such as Setups and Songs and setlists. The optional **Menu** page provides another way to open it. |
| **Banked page** | A performance page that groups control assignments into banks and pages within each bank. Navigating changes which assignments the same switches operate. |
| **None** | A page choice that uses no windows, freeing screens for other regions. |
| **Status** | The page for checking such details as the running firmware, memory and storage, setup patch counts, and clock status. |
| **Language**, **language pack** | The page for choosing the interface language. A language pack supplies translated captions and prompts. Names you give songs, patches, or setups stay as written. |
| **Patch view**, **field** | A patch view defines what a patch window shows. Each field supplies a piece of content, such as the display name, program, or volume. |
| **Style**, **tag** | A style controls appearance, such as font and colors. Tags are reusable labels you can assign to patches or catalogue entries and use to apply shared styling. |
| **Splash**, **screensaver** | The startup animation and the animation shown after a configured period of inactivity. |
| **Text message**, **cue**, **warning** | A short caption sent to CNTRL over USB serial or MIDI SysEx. A cue can show a prompt from another device, and a warning draws attention to a problem. |

[Pages and regions](pages.md) · [Display formatting](formatting.md) ·
[UI languages](languages.md) · [Incoming text messages](text-messages.md)

## MIDI and connections

| Term | Meaning |
| --- | --- |
| **MIDI** | Messages that tell instruments or software what to do, such as play a note, load a program, or change volume. MIDI carries control data. Audio travels separately. |
| **Input / output**, **endpoint** | Where CNTRL receives or sends MIDI. An endpoint is a named connection you can use in a setup. Its alias, such as `din`, is the name you put in the file. |
| **MIDI channel** | One of 16 logical channels on a MIDI connection. Select the receive channel of the instrument you want to control. |
| **DIN / TRS** | Common physical MIDI connections. DIN uses a round five-pin plug. TRS uses a tip-ring-sleeve jack. On configurable jacks, choose the intended role and the wiring type supported by your gear. |
| **Omniport**, **configurable port** | A jack whose role is set in the setup. Depending on the controller, it can serve as MIDI, expression, auxiliary switch, or relay. |
| **USB host / USB device** | The host manages the USB connection. A controller's host port can connect directly to compatible USB gear. Its device port plugs into a computer or another host. |
| **USB MIDI interface**, **forwarding** | Passes MIDI between enabled connections so attached devices can communicate through CNTRL. A forwarding rule sends received messages on to another endpoint. |
| **MIDI route** | Connects one or more inputs to one or more outputs, with optional processing such as filtering, transposing, or delaying messages. A system route belongs to the setup. Patch routes follow the selected patch. |
| **Instrument**, **instrument alias** | A named destination for program selections. Its alias in a setup links patches to an output, channel, and the instrument definition to use. |
| **Instrument definition**, **profile**, **program catalogue** | A definition describes an instrument's known programs and bank mappings. A profile identifies a particular version of that information, for example a rig's expansion-bank arrangement. A catalogue lists the program numbers and names to choose from. |
| **Program**, **Program Change (PC)** | A program is a sound or preset stored on an instrument. Program Change selects it. CNTRL numbers programs from 1 to 128. |
| **Bank**, **Bank Select**, **MSB / LSB** | A bank is a group of programs. Bank Select uses upper and lower value components, called MSB and LSB, to choose the group before a Program Change. CNTRL can map a bank name or number to the values your instrument expects. |
| **Control Change (CC)** | A MIDI message that changes a numbered control, such as CC 7 for volume or CC 64 for sustain. A standard CC value ranges from 0 to 127. |
| **Fine CC**, **RPN / NRPN** | Fine CC combines two messages for higher-resolution control. Registered and Non-Registered Parameter Numbers address additional MIDI settings, such as standard fine tuning or instrument-specific parameters. |
| **Note On / Note Off**, **velocity** | Note On starts a note and Note Off releases it. Velocity usually describes how hard a note is played. |
| **Pitch bend**, **aftertouch (pressure)** | Pitch bend changes pitch continuously. Aftertouch communicates pressure applied after a note starts, either for the channel or for a particular note. |
| **SysEx**, **System Exclusive** | A MIDI message format used for device-specific commands and data. Its contents depend on what the receiving equipment supports. |
| **Relay** | An electrical contact CNTRL can open or close to switch an external device, for example an amplifier's channel. |
| **USB keyboard input / output** | Input lets you type supported text into CNTRL, such as a song or setlist name. Output lets a patch send keystrokes to a connected USB host. |

[Instrument definitions](instrument-definitions.md) · [MIDI routes](midi-processing.md) ·
[USB MIDI forwarding](midi-interface.md) · [Relays](relay-outputs.md) ·
[USB keyboards](keyboard.md)

## Timing and playback tools

| Term | Meaning |
| --- | --- |
| **Time**, **Tap Tempo**, **BPM** | **Time** selects the shared timing source and controls outgoing clock. **Tap Tempo** measures the interval between taps and expresses the resulting tempo in beats per minute (BPM). |
| **MIDI Clock**, **transport** | Clock ticks keep devices at the same tempo. Transport messages such as Start, Stop, and Continue tell a device when to run or pause. |
| **MIDI looper**, **overdub** | The looper captures and repeats a beat-synchronized MIDI phrase. An overdub adds a new take while the existing loop plays. |
| **Reverse phrase / Toggle direction** | **Reverse phrase** edits the stored looper phrase. **Toggle direction** plays it forward or backward without changing the saved phrase. |
| **MIDI file**, **SMF**, **MIDI file patch** | A Standard MIDI File (`.mid`) stores timed MIDI messages. A MIDI file patch can trigger playback or recording. |
| **Arpeggiator**, **poly / mono**, **gate** | The arpeggiator plays notes on a beat grid. Poly uses a held chord. Mono uses a recorded sequence of note gestures. Gate sets the sounding fraction of a grid step. |
| **Generator**, **LFO** | A generator creates messages, for example from a clock, a MIDI file, or a low-frequency oscillator (LFO). An LFO changes a MIDI value over time, for example to sweep a filter. |
| **Counter** | A stored value you can step up or down with controls. A MIDI counter sends its new value as a Program Change or Control Change and can show that value on screen. |
| **MIDI Monitor**, **Freeze** | The monitor shows MIDI traffic. Freeze holds the captured view so you can inspect recent messages. |
| **MIDI Cable Test** | A diagnostic page that sends test messages and shows which inputs receive them, so you can check a MIDI connection. |
| **Panic** | An emergency tool for stuck notes or controls. It sends MIDI cleanup messages such as Note Off and releases pedal holds. |
| **Pong** | An optional game page on controllers with enough displays. |

[MIDI clock](midi-clock.md) · [Looper](midi-looper.md) · [MIDI files](midi-files.md) ·
[Arpeggiator](arpeggiator.md) · [LFOs](lfos.md) · [Counters](counters.md) ·
[Monitor](midi-monitor.md) · [Cable test](midi-cable-test.md) · [Panic](panic.md)

## Setups and files

| Term | Meaning |
| --- | --- |
| **CNTRL**, **CLI** | CNTRL includes the software on the pedal and tools on the computer. The command-line interface (CLI) is the `cntrl` command you run in a terminal to create, transfer, and manage setups and song libraries. |
| **Controller**, **pedal** | The device you operate. Its controls, screens, and ports are described by its controller model. |
| **Firmware**, **original firmware**, **stock firmware** | Firmware is the software running on the pedal. The `stock-firmware` patch action switches from CNTRL to the manufacturer's stock firmware for one boot on supported controllers. |
| **Flash**, **firmware image**, **bootloader** | To flash is to install a firmware image on the controller. The bootloader is the controller's update mode or program that accepts the image. |
| **Setup** | A saved configuration for a controller, including its pages, patches, control assignments, instrument connections, and playback tool settings. **Setups** is the tool for choosing and managing these configurations. |
| **Active setup** | The setup currently in use. Switching setups starts the new setup with its own defaults and stops the previous setup's running activities. |
| **TOML**, **source file** | TOML is the editable text format for setups and song libraries. A source file contains the settings you write. |
| **Target**, **compile**, **compiled setup** | The target describes what your controller supports. Compiling checks your source against that target and produces the data the pedal runs. |
| **Setup bundle**, **asset** | A bundle packages one or more compiled setups. Assets are supporting files a setup uses, such as fonts, animations, and MIDI files. |
| **Upload / download**, **serial port** | Upload (for example `cntrl setup copy`) sends data from the computer to the pedal. Download gets data from the pedal. The serial port identifies the connection the computer uses for these commands. |
| **Global rig defaults**, **device-wide preferences** | Global rig defaults in `global.toml` supply authoring settings shared by setups. Device-wide preferences, such as UI language and startup set, are saved on the pedal and apply across setups. |
| **Save / Discard** | Save commits edits on the pedal. Discard drops those unsaved changes. Confirmed edits in the song and setlist editor save automatically. |
| **Revision**, **snapshot**, **restore** | A revision is an earlier saved version of one setup. You can also take a snapshot to keep a particular version. Restore makes a chosen version current again. |
| **Backup** | A saved copy of pedal data on the computer for recovery. Setup history is kept on the pedal and concerns individual setups. |
| **SD card**, **RAM** | The card is persistent storage for saved items such as setups, song libraries, and recordings. RAM is the pedal's working memory for activities such as running a setup or capturing a loop. |
| **Setup sync**, **Base / Host / Pedal** | Sync brings saved setup edits on the pedal back into editable source files. Its comparison uses **Base** for the last common state, **Host** for today's computer files, and **Pedal** for the saved device state. Upload is a separate step. |

[Creating a setup](instrument-definitions.md) · [Global defaults and preferences](global-config.md) ·
[Setup history](setup-revision-history.md) · [Setup sync](setup-sync.md) ·
[Firmware upload helper](upload_helper.md)

## Emulator

| Term | Meaning |
| --- | --- |
| **Emulator** | The desktop app that runs CNTRL's controls, displays, and MIDI behavior without requiring you to operate a physical pedal. |
| **Controller profile**, **layout** | A profile selects a supported controller model. An emulator layout places that model's controls and displays on screen. |
| **Setup preview** | An emulator session started from a setup on your computer. |
| **Pedal mirror** | A local emulator session started from a snapshot of a connected pedal, including its saved setups, current selection, and device preferences. Edits in the mirror affect the local session. |
| **Inspector**, **virtual MIDI**, **host MIDI** | The inspector lets you examine the simulated controller and inject test MIDI. Virtual MIDI stays within the simulated connections. Host MIDI lets you connect to MIDI ports on your computer. |
| **Record replay / Save replay** | Record captures inputs during an emulator session. Save writes them to a replay file so you can run the same inputs again from the same starting state. |

[Desktop emulator guide](emulator.md)
