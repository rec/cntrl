 binding<expression>.channel: integer
 binding<expression>.controller: integer
 binding<expression>.curve: 'linear' | 'slow' | 'fast' = 'linear'
 binding<expression>.maximum: integer = 127
 binding<expression>.minimum: integer = 0
 binding<expression>.mirror_output: string | null = null
 binding<expression>.output: string
 binding<expression>.reverse: boolean = false
 binding<patch>: patch
 binding<switch>.display_name: string | null = null
 binding<switch>.enter: SwitchEnter | null = null
 binding<switch>.enter.action: patch
 binding<switch>.enter.override: boolean = false
 binding<switch>.exit: patch | null = null
 binding<switch>.gestures: SwitchGestures | null = null
 binding<switch>.gestures.debounce: float = 0.02
 binding<switch>.gestures.double_delay: float = 0.3
 binding<switch>.gestures.double_release: patch | null = null
 binding<switch>.gestures.double_tap: patch | null = null
 binding<switch>.gestures.long_delay: float = 0.5
 binding<switch>.gestures.long_double_release: patch | null = null
 binding<switch>.gestures.long_double_tap: patch | null = null
 binding<switch>.gestures.long_press: patch | null = null
 binding<switch>.gestures.long_release: patch | null = null
 binding<switch>.gestures.long_triple_release: patch | null = null
 binding<switch>.gestures.long_triple_tap: patch | null = null
 binding<switch>.gestures.release: patch | null = null
 binding<switch>.gestures.repeat: patch | null = null
 binding<switch>.gestures.repeat_interval: float = 0.1
 binding<switch>.gestures.triple_release: patch | null = null
 binding<switch>.gestures.triple_tap: patch | null = null
 binding<switch>.mode: 'press' | 'momentary' | 'latched' = 'press'
 binding<switch>.name: string | null = null
 binding<switch>.off: patch | null = null
 binding<switch>.on: patch
 binding<switch>.shifted: ShiftedActions | null = null
 binding<switch>.shifted.gestures: SwitchGestures | null = null
 binding<switch>.shifted.gestures.debounce: float = 0.02
 binding<switch>.shifted.gestures.double_delay: float = 0.3
 binding<switch>.shifted.gestures.double_release: patch | null = null
 binding<switch>.shifted.gestures.double_tap: patch | null = null
 binding<switch>.shifted.gestures.long_delay: float = 0.5
 binding<switch>.shifted.gestures.long_double_release: patch | null = null
 binding<switch>.shifted.gestures.long_double_tap: patch | null = null
 binding<switch>.shifted.gestures.long_press: patch | null = null
 binding<switch>.shifted.gestures.long_release: patch | null = null
 binding<switch>.shifted.gestures.long_triple_release: patch | null = null
 binding<switch>.shifted.gestures.long_triple_tap: patch | null = null
 binding<switch>.shifted.gestures.release: patch | null = null
 binding<switch>.shifted.gestures.repeat: patch | null = null
 binding<switch>.shifted.gestures.repeat_interval: float = 0.1
 binding<switch>.shifted.gestures.triple_release: patch | null = null
 binding<switch>.shifted.gestures.triple_tap: patch | null = null
 binding<switch>.shifted.off: patch | null = null
 binding<switch>.shifted.on: patch
 binding<switch>.style: Style | null = null
 binding<switch>.style.background_color: string | null = null
 binding<switch>.style.font_name: string | null = null
 binding<switch>.style.foreground_color: string | null = null
 binding<switch>.style.horizontal_scale: float | null = null
 binding<switch>.style.image: string | null = null
 binding<switch>.switch_label: string | null = null
 catalogue.<controller>.buttons: list<string>
 catalogue.<controller>.displays: list<string | display specification>
 catalogue.<controller>.displays[].color: integer | null
 catalogue.<controller>.displays[].kind: 'graphical' | 'alphanumeric'
 catalogue.<controller>.displays[].size: string
 catalogue.<controller>.name: string
 catalogue.<controller>.ports: map<string, port specification | list<port specification>>
 catalogue.<controller>.ports.<port>: USB socket | reference | port specification
 catalogue.port.direction: 'in' | 'out'
 catalogue.port.host: boolean = false
 catalogue.port.kind: string
 catalogue.port.kinds: list<string>
 catalogue.port.physical.connector: string = 'din'
 catalogue.port.physical.contacts: integer
 catalogue.port.physical.size: float
 catalogue.port.wiring: string | list<string> | null = null
 cli.animation.prepare.output: path
 cli.animation.prepare.source: path
 cli.animation.prepare.target: path
 cli.animation.upload.asset: path
 cli.animation.upload.port: path
 cli.build.diagnostics: boolean = false
 cli.build.doom_image: path | null = null
 cli.build.output: path
 cli.build.publish: boolean = false
 cli.build.source_root: path = <current directory>
 cli.build.target_dir: path
 cli.compile.avoid_confict: boolean = true
 cli.compile.definitions: list<string> = []
 cli.compile.document: list<string>
 cli.compile.global_defaults: map<string, path | null> | null = null
 cli.compile.global_defaults.<key>: path | null
 cli.compile.memory_margin: integer = 16192
 cli.compile.output: path
 cli.compile.port: path | null = null
 cli.compile.table: list<list<string>> = []
 cli.compile.target: list<string> = []
 cli.emulate.caret_blink_period: string = '1 second'
 cli.emulate.compare_status_fonts: boolean = false
 cli.emulate.controller: string | null = null
 cli.emulate.doom_host: path | null = null
 cli.emulate.interactive: boolean = false
 cli.emulate.layout: path | null = null
 cli.emulate.output: path | null = null
 cli.emulate.port: path | null = null
 cli.emulate.scenario: path | null = null
 cli.emulate.setup: path | null = null
 cli.emulate.source_root: path = <current directory>
 cli.emulate.storage: path | null = null
 cli.emulate.target: path | null = null
 cli.firmware.boot-switch.diagnostics: boolean = false
 cli.firmware.boot-switch.doom: boolean = false
 cli.firmware.boot-switch.switch_window: float = 0.05
 cli.flash.controller: string
 cli.flash.doom_assets: path | null = null
 cli.flash.editor_backup: path | null = null
 cli.flash.first_install: boolean = false
 cli.flash.legacy_usb: boolean = false
 cli.flash.output: path | null = null
 cli.flash.port: path | null = null
 cli.flash.prepare_only: boolean = false
 cli.flash.resources: path | null = null
 cli.flash.serial: string | null = null
 cli.flash.stock_version: string | null = null
 cli.flash.upload_helper: boolean = false
 cli.font-coverage.output: path = 'plan/font-coverage.md'
 cli.global.clear.port: path | null = null
 cli.global.clear.setting: string
 cli.global.list.from_pedal: boolean = false
 cli.global.list.port: path | null = null
 cli.global.list.setting: 'default_firmware' | 'name_and_contact' | null = null
 cli.global.restore.file: path
 cli.global.restore.port: path | null = null
 cli.global.set.port: path | null = null
 cli.global.set.setting: string
 cli.global.set.value: string
 cli.import.arguments: list<string> = []
 cli.import.target_dir: path
 cli.language.install.language: string
 cli.language.install.port: path | null = null
 cli.qualify.arguments: list<string> = []
 cli.qualify.target_dir: path
 cli.setup.add-cycle.setup: path
 cli.setup.add-doom.backup: path
 cli.setup.add-doom.before: string
 cli.setup.add-doom.name: string
 cli.setup.add-doom.port: path
 cli.setup.add-doom.region: string
 cli.setup.add-doom.target: path
 cli.setup.backup.directory: path
 cli.setup.backup.port: path
 cli.setup.convert.destination: path
 cli.setup.convert.directory: path
 cli.setup.convert.target: path
 cli.setup.copy.avoid_confict: boolean = true
 cli.setup.copy.definitions: list<string> = []
 cli.setup.copy.document: list<string>
 cli.setup.copy.global_defaults: map<string, path | null> | null = null
 cli.setup.copy.global_defaults.<key>: path | null
 cli.setup.copy.memory_margin: integer = 16192
 cli.setup.copy.overwrite: boolean = false
 cli.setup.copy.port: path
 cli.setup.copy.table: list<list<string>> = []
 cli.setup.copy.target: list<string> = []
 cli.setup.create.output: path | null = null
 cli.setup.del.names: list<string>
 cli.setup.del.port: path
 cli.setup.del.yes: boolean = false
 cli.setup.download.force: boolean = false
 cli.setup.download.names: list<string> = []
 cli.setup.download.output: path = '.'
 cli.setup.download.port: path
 cli.setup.history.action: 'list' | 'save' | 'download' | 'compare' | 'restore' | 'delete' | 'clear' = 'list'
 cli.setup.history.label: string = ''
 cli.setup.history.name: string
 cli.setup.history.output: path | null = null
 cli.setup.history.port: path
 cli.setup.history.revision: integer = 0
 cli.setup.history.yes: boolean = false
 cli.setup.list.port: path
 cli.setup.recover.port: path
 cli.setup.recover.root: integer
 cli.setup.restore.directory: path
 cli.setup.restore.port: path
 cli.setup.restore.preserve_memory_estimates: boolean = false
 cli.setup.restore.target: path
 cli.setup.rotate.backward: boolean = false
 cli.setup.rotate.port: path
 cli.setup.set.name: string | null = null
 cli.setup.set.port: path
 cli.setup.sync.apply: path | null = null
 cli.setup.sync.avoid_confict: boolean = true
 cli.setup.sync.definitions: list<string> = []
 cli.setup.sync.document: list<string> = []
 cli.setup.sync.global_defaults: map<string, path | null> | null = null
 cli.setup.sync.global_defaults.<key>: path | null
 cli.setup.sync.memory_margin: integer = 16192
 cli.setup.sync.port: path | null = null
 cli.setup.sync.recover: path | null = null
 cli.setup.sync.table: list<list<string>> = []
 cli.setup.sync.target: list<string> = []
 cli.setup.verify.directory: path
 cli.setup.verify.port: path
 cli.setup.verify.preserve_memory_estimates: boolean = false
 cli.setup.verify.target: path
 cli.songs.copy.document: path
 cli.songs.copy.overwrite: boolean = false
 cli.songs.copy.port: path
 cli.songs.copy.target: path | null = null
 cli.songs.download.directory: path
 cli.songs.download.port: path
 cli.songs.new.document: path
 cli.songs.new.scenes: list<string> = ['Verse']
 cli.status.port: path | null = null
 cli.status.reset_starts: boolean = false
 cli.upload-helper.bootloader.serial: string
 cli.upload-helper.controller: string
 cli.upload-helper.serve.serial: string
 cli.upload-helper.upload.image: path
 cli.upload-helper.upload.serial: string
 cli.upload-helper.upload.sha256: string
 cli.verbose: boolean = false
 emulator.layout.catalogue: path
 emulator.layout.controller: string
 emulator.layout.controls: map<string, Placement> = {}
 emulator.layout.controls.<control>.height: float
 emulator.layout.controls.<control>.width: float
 emulator.layout.controls.<control>.x: float
 emulator.layout.controls.<control>.y: float
 emulator.layout.displays: map<string, Placement> = {}
 emulator.layout.displays.<key>.height: float
 emulator.layout.displays.<key>.width: float
 emulator.layout.displays.<key>.x: float
 emulator.layout.displays.<key>.y: float
 emulator.layout.faceplate: path | null = null
 emulator.layout.height: float
 emulator.layout.ports: list<PortPlacement> = []
 emulator.layout.ports[].height: float
 emulator.layout.ports[].port: integer
 emulator.layout.ports[].width: float
 emulator.layout.ports[].x: float
 emulator.layout.ports[].y: float
 emulator.layout.provisional: boolean = false
 emulator.layout.width: float
 emulator.scenario.events: list<Event> = []
 emulator.scenario.events[].at: float
 emulator.scenario.events[].data: list<integer> = []
 emulator.scenario.events[].kind: 'button' | 'key' | 'keyboard_connection' | 'midi' | 'block' | 'connection' | 'expression' | 'auxiliary' | 'text' | 'cleanup'
 emulator.scenario.events[].target: string = ''
 emulator.scenario.events[].text: string = ''
 emulator.scenario.events[].value: integer = 0
 emulator.scenario.seed: integer = 1
 emulator.scenario.until: float
 emulator.session.correction_mode: boolean = false
 emulator.session.midi.<endpoint>.blocked: boolean = false
 emulator.session.midi.<endpoint>.connected: boolean = true
 emulator.session.midi.<endpoint>.host_port: string | null = null
 emulator.session.realtime: boolean = true
 emulator.session.record_replay: boolean = false
 emulator.session.step_ms: integer = 10
 environment.APPDATA: path
 environment.CNTRL_TYCMD: path
 environment.CXX: string
 environment.LOCALAPPDATA: path
 environment.XDG_CACHE_HOME: path
 environment.XDG_CONFIG_HOME: path
 global.animation_color_cycle: boolean = false
 global.auxiliary_switches: map<string, 'independent' | 'combined'> = {}
 global.auxiliary_switches.<jack>: 'independent' | 'combined'
 global.background_color: string = '0xFFF8D8'
 global.bindings: map<string, binding> = {}
 global.bindings.<input>: binding
 global.brightness: integer = 192
 global.contrast: float = 0.6
 global.counters: map<string, Counter> = {}
 global.counters.<counter>.boundary: 'wrap' | 'bounce' = 'wrap'
 global.counters.<counter>.channel: integer
 global.counters.<counter>.controller: integer | null = null
 global.counters.<counter>.initial: integer
 global.counters.<counter>.maximum: integer
 global.counters.<counter>.minimum: integer
 global.counters.<counter>.output: string
 global.counters.<counter>.step: integer = 1
 global.counters.<counter>.type: 'program-change' | 'control-change'
 global.definitions: Definitions | null = null
 global.definitions.profiles: list<InstrumentProfile>
 global.definitions.profiles[].banks: map<integer, Bank> = {}
 global.definitions.profiles[].banks.<bank>.lsb: integer | null = null
 global.definitions.profiles[].banks.<bank>.msb: integer | null = null
 global.definitions.profiles[].instrument: string
 global.definitions.profiles[].name: string
 global.definitions.profiles[].sysex: list<string> = []
 global.definitions.programs: list<TablePatch> = []
 global.definitions.programs[].bank: integer | null = null
 global.definitions.programs[].description: string | null = null
 global.definitions.programs[].display_name: string | null = null
 global.definitions.programs[].instrument: string
 global.definitions.programs[].name: string | null = null
 global.definitions.programs[].names: map<string, string> = {}
 global.definitions.programs[].names.<language>: string
 global.definitions.programs[].profile: string = 'default'
 global.definitions.programs[].program: integer
 global.definitions.programs[].range: string | null = null
 global.definitions.programs[].style: Style | null = null
 global.definitions.programs[].style.background_color: string | null = null
 global.definitions.programs[].style.font_name: string | null = null
 global.definitions.programs[].style.foreground_color: string | null = null
 global.definitions.programs[].style.horizontal_scale: float | null = null
 global.definitions.programs[].style.image: string | null = null
 global.definitions.programs[].tags: list<string> = []
 global.displays: Displays = {'next': {'view': 'patch', 'pad': null, 'style': null}, 'sent': {'view': 'patch', 'pad': null, 'style': null}, 'undo': {'view': 'patch', 'pad': null, 'style': null}}
 global.displays.next: Display = {'view': 'patch', 'pad': null, 'style': null}
 global.displays.next.pad: integer | null = null
 global.displays.next.style: Style | null = null
 global.displays.next.style.background_color: string | null = null
 global.displays.next.style.font_name: string | null = null
 global.displays.next.style.foreground_color: string | null = null
 global.displays.next.style.horizontal_scale: float | null = null
 global.displays.next.style.image: string | null = null
 global.displays.next.view: string = 'patch'
 global.displays.sent: Display = {'view': 'patch', 'pad': null, 'style': null}
 global.displays.sent.pad: integer | null = null
 global.displays.sent.style: Style | null = null
 global.displays.sent.style.background_color: string | null = null
 global.displays.sent.style.font_name: string | null = null
 global.displays.sent.style.foreground_color: string | null = null
 global.displays.sent.style.horizontal_scale: float | null = null
 global.displays.sent.style.image: string | null = null
 global.displays.sent.view: string = 'patch'
 global.displays.undo: Display = {'view': 'patch', 'pad': null, 'style': null}
 global.displays.undo.pad: integer | null = null
 global.displays.undo.style: Style | null = null
 global.displays.undo.style.background_color: string | null = null
 global.displays.undo.style.font_name: string | null = null
 global.displays.undo.style.foreground_color: string | null = null
 global.displays.undo.style.horizontal_scale: float | null = null
 global.displays.undo.style.image: string | null = null
 global.displays.undo.view: string = 'patch'
 global.fast_debounce: boolean = true
 global.fast_switches: string = 'A'
 global.fill_in_styles: boolean = true
 global.fonts: map<string, string> = {}
 global.fonts.<font>: string
 global.foreground_brightness_limit: float = 0.5
 global.forward_device_to_host: boolean = true
 global.frame_buffers: integer = 1
 global.idle_movie: list<string> = []
 global.inline_patches: boolean = true
 global.instruments: map<string, Instrument> = {}
 global.instruments.<instrument>.catalogues: list<path>
 global.instruments.<instrument>.channel: integer
 global.instruments.<instrument>.instrument: string = <instrument alias>
 global.instruments.<instrument>.output: string
 global.instruments.<instrument>.profile: string = 'default'
 global.instruments.<instrument>.programs: list<string | program metadata> | map<integer, list<string | program metadata>>
 global.instruments.<instrument>.programs[].description: string | null = null
 global.instruments.<instrument>.programs[].display_name: string | null = null
 global.instruments.<instrument>.programs[].name: string | null = null
 global.instruments.<instrument>.programs[].names: map<string, string> = {}
 global.instruments.<instrument>.programs[].names.<language>: string
 global.instruments.<instrument>.programs[].range: string | null = null
 global.instruments.<instrument>.programs[].style: Style | null = null
 global.instruments.<instrument>.programs[].style.background_color: string | null = null
 global.instruments.<instrument>.programs[].style.font_name: string | null = null
 global.instruments.<instrument>.programs[].style.foreground_color: string | null = null
 global.instruments.<instrument>.programs[].style.horizontal_scale: float | null = null
 global.instruments.<instrument>.programs[].style.image: string | null = null
 global.instruments.<instrument>.programs[].tags: list<string> = []
 global.instruments.<instrument>.tags: map<string, list<string>>
 global.is_midi_interface: boolean = false
 global.layout: PageLayout | null = null
 global.layout.focus: string | null = null
 global.layout.left_wins: boolean = false
 global.layout.order: list<string> = []
 global.loop_stop_neutral_values: list<ControlChange | Nrpn | Rpn> = []
 global.loop_stop_neutral_values[]<control-change>.channel: integer | null = null
 global.loop_stop_neutral_values[]<control-change>.controller: integer
 global.loop_stop_neutral_values[]<control-change>.output: string | null = null
 global.loop_stop_neutral_values[]<control-change>.resolution: 7 | 14 = 7
 global.loop_stop_neutral_values[]<control-change>.type: 'control-change'
 global.loop_stop_neutral_values[]<control-change>.value: integer
 global.loop_stop_neutral_values[]<nrpn>.channel: integer | null = null
 global.loop_stop_neutral_values[]<nrpn>.output: string | null = null
 global.loop_stop_neutral_values[]<nrpn>.parameter: integer
 global.loop_stop_neutral_values[]<nrpn>.resolution: 7 | 14 = 14
 global.loop_stop_neutral_values[]<nrpn>.type: 'nrpn'
 global.loop_stop_neutral_values[]<nrpn>.value: integer
 global.loop_stop_neutral_values[]<rpn>.channel: integer | null = null
 global.loop_stop_neutral_values[]<rpn>.output: string | null = null
 global.loop_stop_neutral_values[]<rpn>.parameter: 1 = 1
 global.loop_stop_neutral_values[]<rpn>.type: 'rpn'
 global.loop_stop_neutral_values[]<rpn>.value: integer
 global.loop_stop_reset_controls: boolean = false
 global.midi: map<string, Route> | null = null
 global.midi.<route>.from: string | list<string> | null = null
 global.midi.<route>.generate: MIDI generator | null = null
 global.midi.<route>.to: string | list<string> | null = null
 global.midi.<route>.via: list<MIDI processor> = []
 global.navigation: NavigationSettings = {'page_forward': null, 'page_back': null, 'song_forward': null, 'song_backward': null, 'hold_delay': 1.5, 'repeat_interval': 1}
 global.navigation.hold_delay: float = 1.5
 global.navigation.page_back: string | null = null
 global.navigation.page_forward: string | null = null
 global.navigation.repeat_interval: float = 1
 global.navigation.song_backward: string | null = null
 global.navigation.song_forward: string | null = null
 global.pad: integer | null = 4
 global.page_settings: PageSettings = {'memon': null, 'contact_style': null, 'arpeggiator': null, 'looper': null, 'expression': {}, 'clock_outputs': [], 'clock_bpms': [120], 'autoselect_clock': true, 'refresh_interval': 1, 'clock_timeout': 0.25, 'clocks_per_quarter': 24, 'clock_average_beats': 1, 'tap_intervals': 4, 'minimum_taps': 2, 'minimum_interval': 0.1, 'reset_gap': 3, 'flash_fraction': 0.4, 'flasher_diameter': 40, 'annulus': 5, 'title_height': 12, 'tempo_height': 40, 'padding': 2, 'tempo_font': 'CourierPrime', 'status_font': 'TamzenBold7x13', 'title_font': 'AtkinsonNextSemiBold', 'off_color': '0x600000', 'on_color': '0xFF0000', 'ring_color': 'black', 'background_color': '0xFFF085', 'digits': 3, 'decimals': 2}
 global.page_settings.annulus: integer = 5
 global.page_settings.arpeggiator: ArpeggiatorSettings | null = null
 global.page_settings.arpeggiator.channels: list<integer> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
 global.page_settings.arpeggiator.events: integer = 128
 global.page_settings.arpeggiator.gate: string = '4/5'
 global.page_settings.arpeggiator.inputs: list<string> | null = null
 global.page_settings.arpeggiator.mode: 'poly' | 'mono' = 'poly'
 global.page_settings.arpeggiator.outputs: list<string> | null = null
 global.page_settings.arpeggiator.rhythm: ArpeggiatorRhythm = {'kind': 'grid', 'step': '1/4 beat'}
 global.page_settings.arpeggiator.rhythm.kind: 'grid' = 'grid'
 global.page_settings.arpeggiator.rhythm.step: string = '1/4 beat'
 global.page_settings.arpeggiator.seed: integer = 42
 global.page_settings.arpeggiator.selection: ArpeggiatorSelection | ArpeggiatorPlayed | ArpeggiatorAlternating | ArpeggiatorWalk | ArpeggiatorMod = {'kind': 'ascending'}
 global.page_settings.arpeggiator.selection<alternating>.kind: 'alternating' = 'alternating'
 global.page_settings.arpeggiator.selection<alternating>.repeat_begin: boolean = false
 global.page_settings.arpeggiator.selection<alternating>.repeat_end: boolean = false
 global.page_settings.arpeggiator.selection<arpeggiator-selection>.kind: 'ascending' | 'descending' | 'choice' | 'shuffle' | 'outside_in' = 'ascending'
 global.page_settings.arpeggiator.selection<mod>.kind: 'mod' = 'mod'
 global.page_settings.arpeggiator.selection<mod>.modulus: integer = 2
 global.page_settings.arpeggiator.selection<played>.direction: 'forward' | 'reverse' = 'forward'
 global.page_settings.arpeggiator.selection<played>.kind: 'played' = 'played'
 global.page_settings.arpeggiator.selection<walk>.cycle: boolean = false
 global.page_settings.arpeggiator.selection<walk>.kind: 'walk' = 'walk'
 global.page_settings.arpeggiator.selection<walk>.prob_down: string = '1/2'
 global.page_settings.arpeggiator.selection<walk>.prob_up: string = '1/2'
 global.page_settings.autoselect_clock: boolean = true
 global.page_settings.background_color: string = '0xFFF085'
 global.page_settings.clock_average_beats: integer = 1
 global.page_settings.clock_bpms: list<integer> = [120]
 global.page_settings.clock_outputs: list<string> = []
 global.page_settings.clock_timeout: float = 0.25
 global.page_settings.clocks_per_quarter: integer = 24
 global.page_settings.contact_style: ContactStyle | null = null
 global.page_settings.contact_style.background_color: string | null = null
 global.page_settings.contact_style.font_name: string | null = null
 global.page_settings.contact_style.foreground_color: string | null = null
 global.page_settings.contact_style.image: string | null = null
 global.page_settings.decimals: integer = 2
 global.page_settings.digits: integer = 3
 global.page_settings.expression: map<string, ExpressionAssignment> = {}
 global.page_settings.expression.<jack>.channel: integer
 global.page_settings.expression.<jack>.controller: integer
 global.page_settings.expression.<jack>.curve: 'linear' | 'slow' | 'fast' = 'linear'
 global.page_settings.expression.<jack>.maximum: integer = 127
 global.page_settings.expression.<jack>.minimum: integer = 0
 global.page_settings.expression.<jack>.mirror_output: string | null = null
 global.page_settings.expression.<jack>.output: string
 global.page_settings.expression.<jack>.reverse: boolean = false
 global.page_settings.flash_fraction: float = 0.4
 global.page_settings.flasher_diameter: integer = 40
 global.page_settings.looper: LooperSettings | null = null
 global.page_settings.looper.channels: list<integer> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
 global.page_settings.looper.events: integer = 512
 global.page_settings.looper.inputs: list<string> | null = null
 global.page_settings.looper.outputs: list<string> | null = null
 global.page_settings.memon: Memon | null = null
 global.page_settings.memon.channel: integer = 1
 global.page_settings.memon.cooperative: boolean = false
 global.page_settings.memon.device: string = 'din'
 global.page_settings.memon.error_chord: list<integer> = []
 global.page_settings.memon.error_duration: float = 2
 global.page_settings.memon.gap_duration: float = 0.25
 global.page_settings.memon.mono: boolean = true
 global.page_settings.memon.note_duration: float = 0.25
 global.page_settings.memon.note_offset: integer = 60
 global.page_settings.memon.notes: list<integer> = [0, 4, 7, 10]
 global.page_settings.memon.repeat_requests: boolean = false
 global.page_settings.memon.show_remaining_time: boolean = true
 global.page_settings.memon.style: MemonStyle = {'font': 'AtkinsonNextSemiBold', 'foreground_color': 'white', 'background_color': 'black', 'tile_colors': ['0x884DFF', '0xFF7A1A', '0x00B8A0', '0xFFF1D6'], 'color_offset': 64}
 global.page_settings.memon.style.background_color: string = 'black'
 global.page_settings.memon.style.color_offset: integer = 64
 global.page_settings.memon.style.font: string = 'AtkinsonNextSemiBold'
 global.page_settings.memon.style.foreground_color: string = 'white'
 global.page_settings.memon.style.tile_colors: list<string> = ['0x884DFF', '0xFF7A1A', '0x00B8A0', '0xFFF1D6']
 global.page_settings.memon.timed: boolean = true
 global.page_settings.memon.timeout: float = 5
 global.page_settings.memon.velocity: integer = 100
 global.page_settings.minimum_interval: float = 0.1
 global.page_settings.minimum_taps: integer = 2
 global.page_settings.off_color: string = '0x600000'
 global.page_settings.on_color: string = '0xFF0000'
 global.page_settings.padding: integer = 2
 global.page_settings.refresh_interval: float = 1
 global.page_settings.reset_gap: float = 3
 global.page_settings.ring_color: string = 'black'
 global.page_settings.status_font: string = 'TamzenBold7x13'
 global.page_settings.tap_intervals: integer = 4
 global.page_settings.tempo_font: string = 'CourierPrime'
 global.page_settings.tempo_height: integer = 40
 global.page_settings.title_font: string = 'AtkinsonNextSemiBold'
 global.page_settings.title_height: integer = 12
 global.pages: map<string, Page> = {'status': {'type': 'status', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'cable-test': {'type': 'cable-test', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'panic': {'type': 'panic', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'midi-monitor': {'type': 'midi-monitor', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'midi-looper': {'type': 'midi-looper', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'arpeggiator': {'type': 'arpeggiator', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'time': {'type': 'time', 'ram2_bytes': 0, 'min_displays': 2, 'give_up_displays': [], 'patch': null}, 'language': {'type': 'language', 'ram2_bytes': 0, 'min_displays': 2, 'give_up_displays': [], 'patch': null}, 'setups': {'type': 'setups', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'songs-and-setlists': {'type': 'songs-and-setlists', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'switches': {'type': 'switches', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'settings': {'type': 'settings', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'calibration': {'type': 'calibrate', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'pong': {'type': 'pong', 'ram2_bytes': 0, 'min_displays': 3, 'give_up_displays': [], 'patch': null}, 'memon': {'type': 'memon', 'ram2_bytes': 0, 'min_displays': 3, 'give_up_displays': [], 'patch': null}, 'patch-chooser': {'type': 'patch-chooser', 'ram2_bytes': 0, 'min_displays': 0, 'give_up_displays': [], 'patch': null}}
 global.pages.<page>.give_up_displays: list<string> = []
 global.pages.<page>.min_displays: integer
 global.pages.<page>.patch: string | null = null
 global.pages.<page>.ram2_bytes: integer = 0
 global.pages.<page>.type: 'patch-chooser' | 'time' | 'status' | 'cable-test' | 'panic' | 'midi-monitor' | 'midi-looper' | 'arpeggiator' | 'banked' | 'pong' | 'memon' | 'doom' | 'language' | 'calibrate' | 'None' | 'setups' | 'songs-and-setlists' | 'switches' | 'settings' = 'patch-chooser'
 global.pages.<page><None>.min_displays: integer = 0
 global.pages.<page><banked>.min_displays: integer = 3
 global.pages.<page><language>.min_displays: integer = 2
 global.pages.<page><memon>.min_displays: integer = 3
 global.pages.<page><patch-chooser>.min_displays: integer = 1
 global.pages.<page><pong>.min_displays: integer = 3
 global.pages.<page><time>.min_displays: integer = 2
 global.parameters: map<string, Parameter> = {}
 global.parameters.<parameter>.style: Style | null = null
 global.parameters.<parameter>.style.background_color: string | null = null
 global.parameters.<parameter>.style.font_name: string | null = null
 global.parameters.<parameter>.style.foreground_color: string | null = null
 global.parameters.<parameter>.style.horizontal_scale: float | null = null
 global.parameters.<parameter>.style.image: string | null = null
 global.parameters.<parameter>.value: string | integer | float | boolean | null = null
 global.patch_cycle_seeds: map<string, integer> = {}
 global.patch_cycle_seeds.<cycle>: integer
 global.patch_cycles: map<string, list<patch>> = {}
 global.patch_cycles.<cycle>: list<patch> | Selector
 global.patch_cycles.<cycle><selector>.instrument: string
 global.patch_cycles.<cycle><selector>.tag: string | null = null
 global.patch_views: map<string, PatchView> = {'patch': {'fields': ['patch_cycle.name', 'display_name'], 'style': null, 'line_styles': {}}}
 global.patch_views.<view>.fields: list<string> = ['patch_cycle.name', 'display_name']
 global.patch_views.<view>.line_styles: map<string, Style> = {}
 global.patch_views.<view>.line_styles.<field>.background_color: string | null = null
 global.patch_views.<view>.line_styles.<field>.font_name: string | null = null
 global.patch_views.<view>.line_styles.<field>.foreground_color: string | null = null
 global.patch_views.<view>.line_styles.<field>.horizontal_scale: float | null = null
 global.patch_views.<view>.line_styles.<field>.image: string | null = null
 global.patch_views.<view>.style: Style | null = null
 global.patch_views.<view>.style.background_color: string | null = null
 global.patch_views.<view>.style.font_name: string | null = null
 global.patch_views.<view>.style.foreground_color: string | null = null
 global.patch_views.<view>.style.horizontal_scale: float | null = null
 global.patch_views.<view>.style.image: string | null = null
 global.patches: map<string, patch> = {}
 global.patches.<patch>: patch
 global.pickup: boolean | null = null
 global.pickup_pairing_ms: integer = 2
 global.regions: map<string, PageRegion> = {}
 global.regions.<region>.pages: list<string>
 global.relays: map<string, boolean> = {}
 global.relays.<relay>: boolean
 global.screensaver_timeout: integer = 0
 global.scrolling: Scrolling = {'horizontal_duration': 0.075, 'vertical_duration': 0.2, 'send': 'right', 'patches': 'right', 'cycles': 'right'}
 global.scrolling.cycles: 'right' | 'left' | 'up' | 'down' = 'right'
 global.scrolling.horizontal_duration: float = 0.075
 global.scrolling.patches: 'right' | 'left' | 'up' | 'down' = 'right'
 global.scrolling.send: 'right' | 'left' | 'up' | 'down' = 'right'
 global.scrolling.vertical_duration: float = 0.2
 global.seed: integer | null = 23
 global.splash_movie: list<string> = []
 global.style: Style | null = null
 global.style.background_color: string | null = null
 global.style.font_name: string | null = null
 global.style.foreground_color: string | null = null
 global.style.horizontal_scale: float | null = null
 global.style.image: string | null = null
 global.switches: map<string, SwitchAssignment> = {}
 global.switches.<switch>.button: string
 global.switches.<switch>.display_name: string | null = null
 global.switches.<switch>.enter: SwitchEnter | null = null
 global.switches.<switch>.enter.action: patch
 global.switches.<switch>.enter.override: boolean = false
 global.switches.<switch>.exit: patch | null = null
 global.switches.<switch>.gestures: SwitchGestures | null = null
 global.switches.<switch>.gestures.debounce: float = 0.02
 global.switches.<switch>.gestures.double_delay: float = 0.3
 global.switches.<switch>.gestures.double_release: patch | null = null
 global.switches.<switch>.gestures.double_tap: patch | null = null
 global.switches.<switch>.gestures.long_delay: float = 0.5
 global.switches.<switch>.gestures.long_double_release: patch | null = null
 global.switches.<switch>.gestures.long_double_tap: patch | null = null
 global.switches.<switch>.gestures.long_press: patch | null = null
 global.switches.<switch>.gestures.long_release: patch | null = null
 global.switches.<switch>.gestures.long_triple_release: patch | null = null
 global.switches.<switch>.gestures.long_triple_tap: patch | null = null
 global.switches.<switch>.gestures.release: patch | null = null
 global.switches.<switch>.gestures.repeat: patch | null = null
 global.switches.<switch>.gestures.repeat_interval: float = 0.1
 global.switches.<switch>.gestures.triple_release: patch | null = null
 global.switches.<switch>.gestures.triple_tap: patch | null = null
 global.switches.<switch>.mode: 'press' | 'momentary' | 'latched' = 'press'
 global.switches.<switch>.name: string | null = null
 global.switches.<switch>.off: patch | null = null
 global.switches.<switch>.on: patch
 global.switches.<switch>.shifted: ShiftedActions | null = null
 global.switches.<switch>.shifted.gestures: SwitchGestures | null = null
 global.switches.<switch>.shifted.gestures.debounce: float = 0.02
 global.switches.<switch>.shifted.gestures.double_delay: float = 0.3
 global.switches.<switch>.shifted.gestures.double_release: patch | null = null
 global.switches.<switch>.shifted.gestures.double_tap: patch | null = null
 global.switches.<switch>.shifted.gestures.long_delay: float = 0.5
 global.switches.<switch>.shifted.gestures.long_double_release: patch | null = null
 global.switches.<switch>.shifted.gestures.long_double_tap: patch | null = null
 global.switches.<switch>.shifted.gestures.long_press: patch | null = null
 global.switches.<switch>.shifted.gestures.long_release: patch | null = null
 global.switches.<switch>.shifted.gestures.long_triple_release: patch | null = null
 global.switches.<switch>.shifted.gestures.long_triple_tap: patch | null = null
 global.switches.<switch>.shifted.gestures.release: patch | null = null
 global.switches.<switch>.shifted.gestures.repeat: patch | null = null
 global.switches.<switch>.shifted.gestures.repeat_interval: float = 0.1
 global.switches.<switch>.shifted.gestures.triple_release: patch | null = null
 global.switches.<switch>.shifted.gestures.triple_tap: patch | null = null
 global.switches.<switch>.shifted.off: patch | null = null
 global.switches.<switch>.shifted.on: patch
 global.switches.<switch>.style: Style | null = null
 global.switches.<switch>.style.background_color: string | null = null
 global.switches.<switch>.style.font_name: string | null = null
 global.switches.<switch>.style.foreground_color: string | null = null
 global.switches.<switch>.style.horizontal_scale: float | null = null
 global.switches.<switch>.style.image: string | null = null
 global.switches.<switch>.switch_label: string | null = null
 global.tag_styles: map<string, Style> = {}
 global.tag_styles.<tag>.background_color: string | null = null
 global.tag_styles.<tag>.font_name: string | null = null
 global.tag_styles.<tag>.foreground_color: string | null = null
 global.tag_styles.<tag>.horizontal_scale: float | null = null
 global.tag_styles.<tag>.image: string | null = null
 global.tags: list<string> = []
 global.take_first_match: boolean = false
 global.target_pulse: TargetPulse = {'delay': 2, 'period': 4, 'minimum': 64, 'maximum': 128}
 global.target_pulse.delay: float = 2
 global.target_pulse.maximum: integer = 128
 global.target_pulse.minimum: integer = 64
 global.target_pulse.period: float = 4
 global.trs_ports: map<string, 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux'> = {}
 global.trs_ports.<jack>: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux'
 global.usb_device_midi: boolean = true
 global.usb_host_midi: boolean = true
 global.use_animations: boolean = true
 host.name_and_contact: string = ''
 instrument_table.instruments.<instrument>.catalogues: list<path>
 instrument_table.instruments.<instrument>.instrument: string = <instrument alias>
 instrument_table.instruments.<instrument>.profile: string = 'default'
 instrument_table.instruments.<instrument>.programs: list<string | program metadata> | map<integer, list<string | program metadata>>
 instrument_table.instruments.<instrument>.programs[].description: string | null = null
 instrument_table.instruments.<instrument>.programs[].display_name: string | null = null
 instrument_table.instruments.<instrument>.programs[].name: string | null = null
 instrument_table.instruments.<instrument>.programs[].names: map<string, string> = {}
 instrument_table.instruments.<instrument>.programs[].names.<language>: string
 instrument_table.instruments.<instrument>.programs[].range: string | null = null
 instrument_table.instruments.<instrument>.programs[].style: Style | null = null
 instrument_table.instruments.<instrument>.programs[].style.background_color: string | null = null
 instrument_table.instruments.<instrument>.programs[].style.font_name: string | null = null
 instrument_table.instruments.<instrument>.programs[].style.foreground_color: string | null = null
 instrument_table.instruments.<instrument>.programs[].style.horizontal_scale: float | null = null
 instrument_table.instruments.<instrument>.programs[].style.image: string | null = null
 instrument_table.instruments.<instrument>.programs[].tags: list<string> = []
 instrument_table.instruments.<instrument>.tags: map<string, list<string>>
 instrument_table.patches[].bank: integer | null = null
 instrument_table.patches[].description: string | null = null
 instrument_table.patches[].display_name: string | null = null
 instrument_table.patches[].instrument: string
 instrument_table.patches[].name: string | null = null
 instrument_table.patches[].names: map<string, string> = {}
 instrument_table.patches[].names.<language>: string
 instrument_table.patches[].profile: string = 'default'
 instrument_table.patches[].program: integer
 instrument_table.patches[].range: string | null = null
 instrument_table.patches[].style: Style | null = null
 instrument_table.patches[].style.background_color: string | null = null
 instrument_table.patches[].style.font_name: string | null = null
 instrument_table.patches[].style.foreground_color: string | null = null
 instrument_table.patches[].style.horizontal_scale: float | null = null
 instrument_table.patches[].style.image: string | null = null
 instrument_table.patches[].tags: list<string> = []
 localization.catalogue.messages.<message>.context: string
 localization.catalogue.messages.<message>.en: string
 localization.catalogue.messages.<message>.short_en: string = ''
 localization.translation.language: string
 localization.translation.messages.<message>.short: string = ''
 localization.translation.messages.<message>.text: string
 localization.translation.name: string
 patch.binding_view: BindingView | null = null
 patch.binding_view.align: 'switch' | 'left' = 'switch'
 patch.binding_view.detail_size: integer = 18
 patch.binding_view.font_name: string = 'Arimo'
 patch.binding_view.maximum_size: integer = 18
 patch.binding_view.show_labels: boolean = false
 patch.binding_view.status: string = ''
 patch.binding_view.status_size: integer = 18
 patch.binding_view.status_style: Style | null = null
 patch.binding_view.status_style.background_color: string | null = null
 patch.binding_view.status_style.font_name: string | null = null
 patch.binding_view.status_style.foreground_color: string | null = null
 patch.binding_view.status_style.horizontal_scale: float | null = null
 patch.binding_view.status_style.image: string | null = null
 patch.bindings: map<string, binding> | null = null
 patch.bindings.<input>: binding
 patch.display_name: string | null = null
 patch.fast_switches: string | null = null
 patch.inherits: 'none' | 'switch' | 'expression' | 'both' = 'expression'
 patch.midi: map<string, Route> | null = null
 patch.midi.<route>.from: string | list<string> | null = null
 patch.midi.<route>.generate: MIDI generator | null = null
 patch.midi.<route>.to: string | list<string> | null = null
 patch.midi.<route>.via: list<MIDI processor> = []
 patch.name: string | null = null
 patch.navigation: PageNavigation | null = null
 patch.navigation.page_back: string | null = null
 patch.navigation.page_forward: string | null = null
 patch.pickup: boolean | null = null
 patch.scene_status: string | null = null
 patch.seed: integer | null = null
 patch.style: Style | null = null
 patch.style.background_color: string | null = null
 patch.style.font_name: string | null = null
 patch.style.foreground_color: string | null = null
 patch.style.horizontal_scale: float | null = null
 patch.style.image: string | null = null
 patch.switch_label: string | null = null
 patch.tags: list<string> = []
 patch<banked>.bank_count: integer
 patch<banked>.banks: map<integer, map<integer, string>> = {}
 patch<banked>.banks.<bank>.<bank>: string
 patch<banked>.chords: map<string, list<string>> = {}
 patch<banked>.initial_bank: integer = 0
 patch<banked>.page_count: integer = 1
 patch<banked>.page_counts: map<integer, integer> = {}
 patch<banked>.page_counts.<bank>: integer
 patch<banked>.patches: map<string, patch> = {}
 patch<banked>.patches.<patch>: patch
 patch<banked>.type: 'banked' = 'banked'
 patch<brightness>.operation: 'brightness'
 patch<brightness>.value: integer
 patch<channel-pressure>.channel: integer | null = null
 patch<channel-pressure>.output: string | null = null
 patch<channel-pressure>.pressure: integer
 patch<channel-pressure>.type: 'channel-pressure'
 patch<control-change>.channel: integer | null = null
 patch<control-change>.controller: integer
 patch<control-change>.output: string | null = null
 patch<control-change>.resolution: 7 | 14 = 7
 patch<control-change>.type: 'control-change'
 patch<control-change>.value: integer
 patch<counter-message>.change: 'none' | 'increase' | 'decrease' = 'none'
 patch<counter-message>.channel: integer | null = null
 patch<counter-message>.counter: string
 patch<counter-message>.output: string | null = null
 patch<counter-message>.type: 'counter-message'
 patch<counter-message>.wrap: boolean = true
 patch<delay>.duration: float
 patch<delay>.type: 'delay' = 'delay'
 patch<derived>.parent: string
 patch<derived>.type: 'derived' = 'derived'
 patch<global-setting>.operation: 'global-setting'
 patch<global-setting>.setting: string
 patch<global-setting>.value: boolean | integer | float | string | list<string> | null = null
 patch<keyboard>.action: 'press' | 'release'
 patch<keyboard>.keys: list<integer>
 patch<keyboard>.modifiers: integer = 0
 patch<keyboard>.operation: 'keyboard'
 patch<midi-looper>.action: 'record' | 'play-stop' | 'undo' | 'clear' | 'save' | 'load' | 'reverse' | 'direction'
 patch<midi-looper>.filename: string = ''
 patch<midi-looper>.operation: 'midi-looper'
 patch<midi-stream>.enabled: boolean
 patch<midi-stream>.operation: 'midi-stream'
 patch<midi-stream>.stream: string
 patch<navigation>.operation: 'next-patch' | 'previous-patch' | 'next-patch-cycle' | 'previous-patch-cycle' | 'activate-selection' | 'undo' | 'back' | 'next-song-step' | 'previous-song-step' | 'next-bank' | 'previous-bank' | 'next-bank-page' | 'previous-bank-page'
 patch<note>.channel: integer | null = null
 patch<note>.note: integer
 patch<note>.output: string | null = null
 patch<note>.type: 'note-on' | 'note-off'
 patch<note>.velocity: integer
 patch<nrpn>.channel: integer | null = null
 patch<nrpn>.output: string | null = null
 patch<nrpn>.parameter: integer
 patch<nrpn>.resolution: 7 | 14 = 14
 patch<nrpn>.type: 'nrpn'
 patch<nrpn>.value: integer
 patch<pitch-bend>.channel: integer | null = null
 patch<pitch-bend>.output: string | null = null
 patch<pitch-bend>.type: 'pitch-bend'
 patch<pitch-bend>.value: integer
 patch<play-midi-file>.filename: string = ''
 patch<play-midi-file>.loops: integer = 1
 patch<play-midi-file>.operation: 'play-midi-file'
 patch<play-midi-file>.outputs: list<string> | null = null
 patch<play-midi-file>.post_delay: integer = 0
 patch<play-midi-file>.pre_delay: integer = 0
 patch<play-midi-file>.reverse: boolean = false
 patch<play-midi-file>.speed: float = 1
 patch<play-midi-file>.transpose: integer = 0
 patch<polyphonic-key-pressure>.channel: integer | null = null
 patch<polyphonic-key-pressure>.note: integer
 patch<polyphonic-key-pressure>.output: string | null = null
 patch<polyphonic-key-pressure>.pressure: integer
 patch<polyphonic-key-pressure>.type: 'polyphonic-key-pressure'
 patch<program-change.channel>.channel: integer | null = null
 patch<program-change.channel>.output: string | null = null
 patch<program-change.channel>.program: integer
 patch<program-change.channel>.type: 'program-change'
• patch<program-change.instrument>.bank: integer | null = null
 patch<program-change.instrument>.instrument: string | null = null
• patch<program-change.instrument>.pan: integer | null = null
• patch<program-change.instrument>.program: integer | string
 patch<program-change.instrument>.type: 'program-change'
• patch<program-change.instrument>.volume: integer | null = null
 patch<record-midi-file>.channels: list<integer> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
 patch<record-midi-file>.inputs: list<string> | null = null
 patch<record-midi-file>.operation: 'record-midi-file'
 patch<reference>.document: string | null = null
 patch<reference>.patch: string
 patch<relay>.action: 'engage' | 'disengage' | 'toggle' | 'pulse'
 patch<relay>.duration: float | null = null
 patch<relay>.operation: 'relay'
 patch<relay>.relay: string
 patch<rpn>.channel: integer | null = null
 patch<rpn>.output: string | null = null
 patch<rpn>.parameter: 1 = 1
 patch<rpn>.type: 'rpn'
 patch<rpn>.value: integer
 patch<scroll-counter>.counter: string
 patch<scroll-counter>.direction: 'forward' | 'backward' = 'forward'
 patch<scroll-counter>.operation: 'scroll-counter'
 patch<sequence>.patches: list<patch>
 patch<shift>.operation: 'shift' | 'unshift'
 patch<song-position-pointer>.output: string | null = null
 patch<song-position-pointer>.position: integer
 patch<song-position-pointer>.type: 'song-position-pointer'
 patch<song-select>.output: string | null = null
 patch<song-select>.song: integer
 patch<song-select>.type: 'song-select'
 patch<stock-firmware>.exit_buttons: string | null = null
 patch<stock-firmware>.operation: 'stock-firmware'
 patch<sysex>.data: list<integer>
 patch<sysex>.output: string | null = null
 patch<sysex>.type: 'sysex'
 patch<system-message>.output: string | null = null
 patch<system-message>.type: 'tune-request' | 'timing-clock' | 'start' | 'continue' | 'stop' | 'active-sensing' | 'system-reset'
 patch<tap-tempo>.operation: 'tap-tempo'
 patch<time-code-quarter-frame>.output: string | null = null
 patch<time-code-quarter-frame>.piece: integer
 patch<time-code-quarter-frame>.type: 'time-code-quarter-frame'
 patch<time-code-quarter-frame>.value: integer
 patch<usb-midi-interface>.operation: 'usb-midi-interface'
 patch<usb-midi-interface>.value: boolean
• pedal.animation_color_cycle: boolean | null = null
• pedal.auxiliary_switches.1: 'independent' | 'combined' | null = null
• pedal.auxiliary_switches.2: 'independent' | 'combined' | null = null
• pedal.auxiliary_switches.3: 'independent' | 'combined' | null = null
• pedal.auxiliary_switches.4: 'independent' | 'combined' | null = null
• pedal.brightness: integer | null = null
• pedal.calibration.<jack>.heel: integer
• pedal.calibration.<jack>.name: string = ''
• pedal.calibration.<jack>.toe: integer
 pedal.calibration.bits: 10 | 12 = 10
• pedal.calibration.settle_ms: integer = 2000
• pedal.default_firmware: 'cntrl' | 'stock' = 'cntrl'
• pedal.fast_debounce: boolean | null = null
• pedal.forward_device_to_host: boolean | null = null
• pedal.frame_buffers: integer | null = null
• pedal.is_midi_interface: boolean | null = null
• pedal.language: string = 'en'
• pedal.loop_stop_reset_controls: boolean | null = null
• pedal.name_and_contact: string = ''
• pedal.navigation.hold_delay: float | null = null
• pedal.navigation.repeat_interval: float | null = null
• pedal.page_settings.clock_outputs: list<string> | null = null
• pedal.pickup: boolean | null = null
• pedal.pickup_pairing_ms: integer | null = null
• pedal.screensaver_timeout: integer | null = null
• pedal.scrolling.cycles: 'right' | 'left' | 'up' | 'down' | null = null
• pedal.scrolling.horizontal_duration: float | null = null
• pedal.scrolling.patches: 'right' | 'left' | 'up' | 'down' | null = null
• pedal.scrolling.send: 'right' | 'left' | 'up' | 'down' | null = null
• pedal.scrolling.vertical_duration: float | null = null
• pedal.selected_setup: string | null = null
• pedal.startup_set: string | null = null
• pedal.target_pulse.delay: float | null = null
• pedal.target_pulse.maximum: integer | null = null
• pedal.target_pulse.minimum: integer | null = null
• pedal.target_pulse.period: float | null = null
• pedal.trs_ports.1: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux' | null = null
• pedal.trs_ports.2: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux' | null = null
• pedal.trs_ports.3: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux' | null = null
• pedal.trs_ports.4: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux' | null = null
• pedal.usb_device_midi: boolean | null = null
• pedal.usb_host_midi: boolean | null = null
• pedal.use_animations: boolean | null = null
 route.from: string | list<string> | null = null
 route.generate: MIDI generator | null = null
 route.generate.clock.bpm: float = 120
 route.generate.file.filename: string = ''
 route.generate.file.loops: integer = 1
 route.generate.file.post_delay: integer = 0
 route.generate.file.pre_delay: integer = 0
 route.generate.file.reverse: boolean = false
 route.generate.file.speed: float = 1
 route.generate.file.transpose: integer = 0
 route.generate.lfo.off: Off
 route.generate.lfo.off.action: 'hold' | 'value' | 'restore'
 route.generate.lfo.off.value: integer | null = null
 route.generate.lfo.period: float | BeatPeriod
 route.generate.lfo.period<beat-period>.beats: string
 route.generate.lfo.phase: float = 0
 route.generate.lfo.phase_mode: 'retrigger' | 'free' = 'retrigger'
 route.generate.lfo.range: list<integer>
 route.generate.lfo.run: 'repeat' | 'once' = 'repeat'
 route.generate.lfo.seed: integer = 0
 route.generate.lfo.shape: 'sine' | 'triangle' | 'saw-up' | 'saw-down' | 'square' | 'random' | 'trapezoid' | 'smooth-random'
 route.generate.lfo.target: Target
 route.generate.lfo.target.channel: integer
 route.generate.lfo.target.controller: integer | null = null
 route.generate.lfo.target.note: integer | null = null
 route.generate.lfo.target.parameter: integer | null = null
 route.generate.lfo.target.resolution: 7 | 14 | null = null
 route.generate.lfo.target.type: 'control-change' | 'pitch-bend' | 'channel-pressure' | 'polyphonic-key-pressure' | 'nrpn' | 'rpn' | 'program-change'
 route.generate.lfo.update_interval: float = 0.01
 route.to: string | list<string> | null = null
 route.via: list<MIDI processor> = []
 route.via[].delay.feedback: float = 1
 route.via[].delay.repeats: integer = 1
 route.via[].delay.time: float
 route.via[].drop.channel: integer | list<integer> | null = null
 route.via[].drop.controller: integer | list<integer> | null = null
 route.via[].drop.note: integer | list<integer> | null = null
 route.via[].drop.origin: string | list<string> | null = null
 route.via[].drop.parameter: integer | list<integer> | null = null
 route.via[].drop.pressure: integer | list<integer> | null = null
 route.via[].drop.program: integer | list<integer> | null = null
 route.via[].drop.type: string | list<string> | null = null
 route.via[].drop.value: integer | list<integer> | null = null
 route.via[].drop.velocity: integer | list<integer> | null = null
 route.via[].invert: integer
 route.via[].keep.channel: integer | list<integer> | null = null
 route.via[].keep.controller: integer | list<integer> | null = null
 route.via[].keep.note: integer | list<integer> | null = null
 route.via[].keep.origin: string | list<string> | null = null
 route.via[].keep.parameter: integer | list<integer> | null = null
 route.via[].keep.pressure: integer | list<integer> | null = null
 route.via[].keep.program: integer | list<integer> | null = null
 route.via[].keep.type: string | list<string> | null = null
 route.via[].keep.value: integer | list<integer> | null = null
 route.via[].keep.velocity: integer | list<integer> | null = null
 route.via[].map.match: Match = {'type': null, 'channel': null, 'note': null, 'velocity': null, 'controller': null, 'value': null, 'program': null, 'pressure': null, 'origin': null, 'parameter': null}
 route.via[].map.match.channel: integer | list<integer> | null = null
 route.via[].map.match.controller: integer | list<integer> | null = null
 route.via[].map.match.note: integer | list<integer> | null = null
 route.via[].map.match.origin: string | list<string> | null = null
 route.via[].map.match.parameter: integer | list<integer> | null = null
 route.via[].map.match.pressure: integer | list<integer> | null = null
 route.via[].map.match.program: integer | list<integer> | null = null
 route.via[].map.match.type: string | list<string> | null = null
 route.via[].map.match.value: integer | list<integer> | null = null
 route.via[].map.match.velocity: integer | list<integer> | null = null
 route.via[].map.set: map<string, value>
 route.via[].map.set.<key>: value
 route.via[].reverse.window: float | null = null
 route.via[].scale.field: 'note' | 'velocity' | 'value' | 'program' | 'pressure'
 route.via[].scale.input: list<integer> | null = null
 route.via[].scale.match: Match = {'type': null, 'channel': null, 'note': null, 'velocity': null, 'controller': null, 'value': null, 'program': null, 'pressure': null, 'origin': null, 'parameter': null}
 route.via[].scale.match.channel: integer | list<integer> | null = null
 route.via[].scale.match.controller: integer | list<integer> | null = null
 route.via[].scale.match.note: integer | list<integer> | null = null
 route.via[].scale.match.origin: string | list<string> | null = null
 route.via[].scale.match.parameter: integer | list<integer> | null = null
 route.via[].scale.match.pressure: integer | list<integer> | null = null
 route.via[].scale.match.program: integer | list<integer> | null = null
 route.via[].scale.match.type: string | list<string> | null = null
 route.via[].scale.match.value: integer | list<integer> | null = null
 route.via[].scale.match.velocity: integer | list<integer> | null = null
 route.via[].scale.output: list<integer>
 route.via[].transpose: integer
• setlist.display_name: string | null = null
• setlist.name: string
• setlist.songs: list<string> = []
 setlist.style: Style | null = null
• setlist.style.background_color: string | null = null
 setlist.style.font_name: string | null = null
• setlist.style.foreground_color: string | null = null
 setlist.style.horizontal_scale: float | null = null
 setlist.style.image: string | null = null
• setup.animation_color_cycle: boolean = false
 setup.auxiliary_switches: map<string, 'independent' | 'combined'> = {}
• setup.auxiliary_switches.<jack>: 'independent' | 'combined'
 setup.background_color: string = '0xFFF8D8'
 setup.bindings: map<string, binding> = {}
 setup.bindings.<input>: binding
• setup.brightness: integer = 192
 setup.contrast: float = 0.6
 setup.counters: map<string, Counter> = {}
• setup.counters.<counter>: Counter
• setup.counters.<counter>.boundary: 'wrap' | 'bounce' = 'wrap'
• setup.counters.<counter>.channel: integer
• setup.counters.<counter>.controller: integer | null = null
• setup.counters.<counter>.initial: integer
• setup.counters.<counter>.maximum: integer
• setup.counters.<counter>.minimum: integer
• setup.counters.<counter>.output: string
• setup.counters.<counter>.step: integer = 1
• setup.counters.<counter>.type: 'program-change' | 'control-change'
 setup.definitions: Definitions | null = null
 setup.definitions.profiles: list<InstrumentProfile>
 setup.definitions.profiles[].banks: map<integer, Bank> = {}
 setup.definitions.profiles[].banks.<bank>.lsb: integer | null = null
 setup.definitions.profiles[].banks.<bank>.msb: integer | null = null
 setup.definitions.profiles[].instrument: string
 setup.definitions.profiles[].name: string
 setup.definitions.profiles[].sysex: list<string> = []
 setup.definitions.programs: list<TablePatch> = []
 setup.definitions.programs[].bank: integer | null = null
 setup.definitions.programs[].description: string | null = null
 setup.definitions.programs[].display_name: string | null = null
 setup.definitions.programs[].instrument: string
 setup.definitions.programs[].name: string | null = null
 setup.definitions.programs[].names: map<string, string> = {}
 setup.definitions.programs[].names.<language>: string
 setup.definitions.programs[].profile: string = 'default'
 setup.definitions.programs[].program: integer
 setup.definitions.programs[].range: string | null = null
 setup.definitions.programs[].style: Style | null = null
 setup.definitions.programs[].style.background_color: string | null = null
 setup.definitions.programs[].style.font_name: string | null = null
 setup.definitions.programs[].style.foreground_color: string | null = null
 setup.definitions.programs[].style.horizontal_scale: float | null = null
 setup.definitions.programs[].style.image: string | null = null
 setup.definitions.programs[].tags: list<string> = []
 setup.displays: Displays = {'next': {'view': 'patch', 'pad': null, 'style': null}, 'sent': {'view': 'patch', 'pad': null, 'style': null}, 'undo': {'view': 'patch', 'pad': null, 'style': null}}
 setup.displays.next: Display = {'view': 'patch', 'pad': null, 'style': null}
 setup.displays.next.pad: integer | null = null
 setup.displays.next.style: Style | null = null
 setup.displays.next.style.background_color: string | null = null
 setup.displays.next.style.font_name: string | null = null
 setup.displays.next.style.foreground_color: string | null = null
 setup.displays.next.style.horizontal_scale: float | null = null
 setup.displays.next.style.image: string | null = null
 setup.displays.next.view: string = 'patch'
 setup.displays.sent: Display = {'view': 'patch', 'pad': null, 'style': null}
 setup.displays.sent.pad: integer | null = null
 setup.displays.sent.style: Style | null = null
 setup.displays.sent.style.background_color: string | null = null
 setup.displays.sent.style.font_name: string | null = null
 setup.displays.sent.style.foreground_color: string | null = null
 setup.displays.sent.style.horizontal_scale: float | null = null
 setup.displays.sent.style.image: string | null = null
 setup.displays.sent.view: string = 'patch'
 setup.displays.undo: Display = {'view': 'patch', 'pad': null, 'style': null}
 setup.displays.undo.pad: integer | null = null
 setup.displays.undo.style: Style | null = null
 setup.displays.undo.style.background_color: string | null = null
 setup.displays.undo.style.font_name: string | null = null
 setup.displays.undo.style.foreground_color: string | null = null
 setup.displays.undo.style.horizontal_scale: float | null = null
 setup.displays.undo.style.image: string | null = null
 setup.displays.undo.view: string = 'patch'
• setup.fast_debounce: boolean = true
 setup.fast_switches: string = 'A'
 setup.fill_in_styles: boolean = true
 setup.fonts: map<string, string> = {}
 setup.fonts.<font>: string
 setup.foreground_brightness_limit: float = 0.5
• setup.forward_device_to_host: boolean = true
• setup.frame_buffers: integer = 1
• setup.idle_movie: list<string> = []
 setup.inheritance.remove: list<list<string>> = []
 setup.inline_patches: boolean = true
 setup.instruments: map<string, Instrument> = {}
 setup.instruments.<instrument>.catalogues: list<path>
• setup.instruments.<instrument>.channel: integer
 setup.instruments.<instrument>.instrument: string = <instrument alias>
• setup.instruments.<instrument>.output: string
 setup.instruments.<instrument>.profile: string = 'default'
 setup.instruments.<instrument>.programs: list<string | program metadata> | map<integer, list<string | program metadata>>
 setup.instruments.<instrument>.programs[].description: string | null = null
 setup.instruments.<instrument>.programs[].display_name: string | null = null
 setup.instruments.<instrument>.programs[].name: string | null = null
 setup.instruments.<instrument>.programs[].names: map<string, string> = {}
 setup.instruments.<instrument>.programs[].names.<language>: string
 setup.instruments.<instrument>.programs[].range: string | null = null
 setup.instruments.<instrument>.programs[].style: Style | null = null
 setup.instruments.<instrument>.programs[].style.background_color: string | null = null
 setup.instruments.<instrument>.programs[].style.font_name: string | null = null
 setup.instruments.<instrument>.programs[].style.foreground_color: string | null = null
 setup.instruments.<instrument>.programs[].style.horizontal_scale: float | null = null
 setup.instruments.<instrument>.programs[].style.image: string | null = null
 setup.instruments.<instrument>.programs[].tags: list<string> = []
 setup.instruments.<instrument>.tags: map<string, list<string>>
• setup.is_midi_interface: boolean = false
 setup.layout: PageLayout | null = null
• setup.layout.focus: string | null = null
• setup.layout.left_wins: boolean = false
• setup.layout.order: list<string> = []
• setup.loop_stop_neutral_values: list<ControlChange | Nrpn | Rpn> = []
• setup.loop_stop_neutral_values[]<control-change>.channel: integer | null = null
• setup.loop_stop_neutral_values[]<control-change>.controller: integer
• setup.loop_stop_neutral_values[]<control-change>.output: string | null = null
• setup.loop_stop_neutral_values[]<control-change>.resolution: 7 | 14 = 7
• setup.loop_stop_neutral_values[]<control-change>.type: 'control-change'
• setup.loop_stop_neutral_values[]<control-change>.value: integer
• setup.loop_stop_neutral_values[]<nrpn>.channel: integer | null = null
• setup.loop_stop_neutral_values[]<nrpn>.output: string | null = null
• setup.loop_stop_neutral_values[]<nrpn>.parameter: integer
• setup.loop_stop_neutral_values[]<nrpn>.resolution: 7 | 14 = 14
• setup.loop_stop_neutral_values[]<nrpn>.type: 'nrpn'
• setup.loop_stop_neutral_values[]<nrpn>.value: integer
• setup.loop_stop_neutral_values[]<rpn>.channel: integer | null = null
• setup.loop_stop_neutral_values[]<rpn>.output: string | null = null
• setup.loop_stop_neutral_values[]<rpn>.parameter: 1 = 1
• setup.loop_stop_neutral_values[]<rpn>.type: 'rpn'
• setup.loop_stop_neutral_values[]<rpn>.value: integer
• setup.loop_stop_reset_controls: boolean = false
 setup.midi: map<string, Route> | null = null
 setup.midi.<route>.from: string | list<string> | null = null
 setup.midi.<route>.generate: MIDI generator | null = null
 setup.midi.<route>.to: string | list<string> | null = null
 setup.midi.<route>.via: list<MIDI processor> = []
 setup.navigation: NavigationSettings = {'page_forward': null, 'page_back': null, 'song_forward': null, 'song_backward': null, 'hold_delay': 1.5, 'repeat_interval': 1}
• setup.navigation.hold_delay: float = 1.5
• setup.navigation.page_back: string | null = null
• setup.navigation.page_forward: string | null = null
• setup.navigation.repeat_interval: float = 1
• setup.navigation.song_backward: string | null = null
• setup.navigation.song_forward: string | null = null
 setup.pad: integer | null = 4
 setup.page_settings: PageSettings = {'memon': null, 'contact_style': null, 'arpeggiator': null, 'looper': null, 'expression': {}, 'clock_outputs': [], 'clock_bpms': [120], 'autoselect_clock': true, 'refresh_interval': 1, 'clock_timeout': 0.25, 'clocks_per_quarter': 24, 'clock_average_beats': 1, 'tap_intervals': 4, 'minimum_taps': 2, 'minimum_interval': 0.1, 'reset_gap': 3, 'flash_fraction': 0.4, 'flasher_diameter': 40, 'annulus': 5, 'title_height': 12, 'tempo_height': 40, 'padding': 2, 'tempo_font': 'CourierPrime', 'status_font': 'TamzenBold7x13', 'title_font': 'AtkinsonNextSemiBold', 'off_color': '0x600000', 'on_color': '0xFF0000', 'ring_color': 'black', 'background_color': '0xFFF085', 'digits': 3, 'decimals': 2}
• setup.page_settings.annulus: integer = 5
 setup.page_settings.arpeggiator: ArpeggiatorSettings | null = null
 setup.page_settings.arpeggiator.channels: list<integer> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
 setup.page_settings.arpeggiator.events: integer = 128
 setup.page_settings.arpeggiator.gate: string = '4/5'
 setup.page_settings.arpeggiator.inputs: list<string> | null = null
 setup.page_settings.arpeggiator.mode: 'poly' | 'mono' = 'poly'
 setup.page_settings.arpeggiator.outputs: list<string> | null = null
 setup.page_settings.arpeggiator.rhythm: ArpeggiatorRhythm = {'kind': 'grid', 'step': '1/4 beat'}
 setup.page_settings.arpeggiator.rhythm.kind: 'grid' = 'grid'
 setup.page_settings.arpeggiator.rhythm.step: string = '1/4 beat'
 setup.page_settings.arpeggiator.seed: integer = 42
 setup.page_settings.arpeggiator.selection: ArpeggiatorSelection | ArpeggiatorPlayed | ArpeggiatorAlternating | ArpeggiatorWalk | ArpeggiatorMod = {'kind': 'ascending'}
 setup.page_settings.arpeggiator.selection<alternating>.kind: 'alternating' = 'alternating'
 setup.page_settings.arpeggiator.selection<alternating>.repeat_begin: boolean = false
 setup.page_settings.arpeggiator.selection<alternating>.repeat_end: boolean = false
 setup.page_settings.arpeggiator.selection<arpeggiator-selection>.kind: 'ascending' | 'descending' | 'choice' | 'shuffle' | 'outside_in' = 'ascending'
 setup.page_settings.arpeggiator.selection<mod>.kind: 'mod' = 'mod'
 setup.page_settings.arpeggiator.selection<mod>.modulus: integer = 2
 setup.page_settings.arpeggiator.selection<played>.direction: 'forward' | 'reverse' = 'forward'
 setup.page_settings.arpeggiator.selection<played>.kind: 'played' = 'played'
 setup.page_settings.arpeggiator.selection<walk>.cycle: boolean = false
 setup.page_settings.arpeggiator.selection<walk>.kind: 'walk' = 'walk'
 setup.page_settings.arpeggiator.selection<walk>.prob_down: string = '1/2'
 setup.page_settings.arpeggiator.selection<walk>.prob_up: string = '1/2'
• setup.page_settings.autoselect_clock: boolean = true
• setup.page_settings.background_color: string = '0xFFF085'
• setup.page_settings.clock_average_beats: integer = 1
• setup.page_settings.clock_bpms: list<integer> = [120]
• setup.page_settings.clock_outputs: list<string> = []
• setup.page_settings.clock_timeout: float = 0.25
• setup.page_settings.clocks_per_quarter: integer = 24
 setup.page_settings.contact_style: ContactStyle | null = null
• setup.page_settings.contact_style.background_color: string | null = null
• setup.page_settings.contact_style.font_name: string | null = null
• setup.page_settings.contact_style.foreground_color: string | null = null
 setup.page_settings.contact_style.image: string | null = null
• setup.page_settings.decimals: integer = 2
• setup.page_settings.digits: integer = 3
 setup.page_settings.expression: map<string, ExpressionAssignment> = {}
• setup.page_settings.expression.<jack>: ExpressionAssignment
• setup.page_settings.expression.<jack>.channel: integer
• setup.page_settings.expression.<jack>.controller: integer
• setup.page_settings.expression.<jack>.curve: 'linear' | 'slow' | 'fast' = 'linear'
• setup.page_settings.expression.<jack>.maximum: integer = 127
• setup.page_settings.expression.<jack>.minimum: integer = 0
• setup.page_settings.expression.<jack>.mirror_output: string | null = null
• setup.page_settings.expression.<jack>.output: string
• setup.page_settings.expression.<jack>.reverse: boolean = false
• setup.page_settings.flash_fraction: float = 0.4
• setup.page_settings.flasher_diameter: integer = 40
 setup.page_settings.looper: LooperSettings | null = null
 setup.page_settings.looper.channels: list<integer> = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16]
 setup.page_settings.looper.events: integer = 512
 setup.page_settings.looper.inputs: list<string> | null = null
 setup.page_settings.looper.outputs: list<string> | null = null
 setup.page_settings.memon: Memon | null = null
 setup.page_settings.memon.channel: integer = 1
 setup.page_settings.memon.cooperative: boolean = false
 setup.page_settings.memon.device: string = 'din'
 setup.page_settings.memon.error_chord: list<integer> = []
 setup.page_settings.memon.error_duration: float = 2
 setup.page_settings.memon.gap_duration: float = 0.25
 setup.page_settings.memon.mono: boolean = true
 setup.page_settings.memon.note_duration: float = 0.25
 setup.page_settings.memon.note_offset: integer = 60
 setup.page_settings.memon.notes: list<integer> = [0, 4, 7, 10]
 setup.page_settings.memon.repeat_requests: boolean = false
 setup.page_settings.memon.show_remaining_time: boolean = true
 setup.page_settings.memon.style: MemonStyle = {'font': 'AtkinsonNextSemiBold', 'foreground_color': 'white', 'background_color': 'black', 'tile_colors': ['0x884DFF', '0xFF7A1A', '0x00B8A0', '0xFFF1D6'], 'color_offset': 64}
 setup.page_settings.memon.style.background_color: string = 'black'
 setup.page_settings.memon.style.color_offset: integer = 64
 setup.page_settings.memon.style.font: string = 'AtkinsonNextSemiBold'
 setup.page_settings.memon.style.foreground_color: string = 'white'
 setup.page_settings.memon.style.tile_colors: list<string> = ['0x884DFF', '0xFF7A1A', '0x00B8A0', '0xFFF1D6']
 setup.page_settings.memon.timed: boolean = true
 setup.page_settings.memon.timeout: float = 5
 setup.page_settings.memon.velocity: integer = 100
• setup.page_settings.minimum_interval: float = 0.1
• setup.page_settings.minimum_taps: integer = 2
• setup.page_settings.off_color: string = '0x600000'
• setup.page_settings.on_color: string = '0xFF0000'
• setup.page_settings.padding: integer = 2
• setup.page_settings.refresh_interval: float = 1
• setup.page_settings.reset_gap: float = 3
• setup.page_settings.ring_color: string = 'black'
• setup.page_settings.status_font: string = 'TamzenBold7x13'
• setup.page_settings.tap_intervals: integer = 4
• setup.page_settings.tempo_font: string = 'CourierPrime'
• setup.page_settings.tempo_height: integer = 40
• setup.page_settings.title_font: string = 'AtkinsonNextSemiBold'
• setup.page_settings.title_height: integer = 12
 setup.pages: map<string, Page> = {'status': {'type': 'status', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'cable-test': {'type': 'cable-test', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'panic': {'type': 'panic', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'midi-monitor': {'type': 'midi-monitor', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'midi-looper': {'type': 'midi-looper', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'arpeggiator': {'type': 'arpeggiator', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'time': {'type': 'time', 'ram2_bytes': 0, 'min_displays': 2, 'give_up_displays': [], 'patch': null}, 'language': {'type': 'language', 'ram2_bytes': 0, 'min_displays': 2, 'give_up_displays': [], 'patch': null}, 'setups': {'type': 'setups', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'songs-and-setlists': {'type': 'songs-and-setlists', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'switches': {'type': 'switches', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'settings': {'type': 'settings', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'calibration': {'type': 'calibrate', 'ram2_bytes': 0, 'min_displays': 1, 'give_up_displays': [], 'patch': null}, 'pong': {'type': 'pong', 'ram2_bytes': 0, 'min_displays': 3, 'give_up_displays': [], 'patch': null}, 'memon': {'type': 'memon', 'ram2_bytes': 0, 'min_displays': 3, 'give_up_displays': [], 'patch': null}, 'patch-chooser': {'type': 'patch-chooser', 'ram2_bytes': 0, 'min_displays': 0, 'give_up_displays': [], 'patch': null}}
• setup.pages.<page>.give_up_displays: list<string> = []
• setup.pages.<page>.min_displays: integer
 setup.pages.<page>.patch: string | null = null
 setup.pages.<page>.ram2_bytes: integer = 0
 setup.pages.<page>.type: 'patch-chooser' | 'time' | 'status' | 'cable-test' | 'panic' | 'midi-monitor' | 'midi-looper' | 'arpeggiator' | 'banked' | 'pong' | 'memon' | 'doom' | 'language' | 'calibrate' | 'None' | 'setups' | 'songs-and-setlists' | 'switches' | 'settings' = 'patch-chooser'
 setup.pages.<page><None>.min_displays: integer = 0
 setup.pages.<page><banked>.min_displays: integer = 3
 setup.pages.<page><language>.min_displays: integer = 2
 setup.pages.<page><memon>.min_displays: integer = 3
• setup.pages.<page><patch-chooser>.min_displays: integer = 1
 setup.pages.<page><pong>.min_displays: integer = 3
 setup.pages.<page><time>.min_displays: integer = 2
• setup.pages.<utility-page>: Page
 setup.parameters: map<string, Parameter> = {}
 setup.parameters.<parameter>.style: Style | null = null
 setup.parameters.<parameter>.style.background_color: string | null = null
 setup.parameters.<parameter>.style.font_name: string | null = null
 setup.parameters.<parameter>.style.foreground_color: string | null = null
 setup.parameters.<parameter>.style.horizontal_scale: float | null = null
 setup.parameters.<parameter>.style.image: string | null = null
 setup.parameters.<parameter>.value: string | integer | float | boolean | null = null
 setup.patch_cycle_seeds: map<string, integer> = {}
 setup.patch_cycle_seeds.<cycle>: integer
• setup.patch_cycles: map<string, list<patch>> = {}
• setup.patch_cycles.<cycle>: list<patch> | Selector
• setup.patch_cycles.<cycle><selector>.instrument: string
• setup.patch_cycles.<cycle><selector>.tag: string | null = null
 setup.patch_views: map<string, PatchView> = {'patch': {'fields': ['patch_cycle.name', 'display_name'], 'style': null, 'line_styles': {}}}
 setup.patch_views.<view>.fields: list<string> = ['patch_cycle.name', 'display_name']
 setup.patch_views.<view>.line_styles: map<string, Style> = {}
 setup.patch_views.<view>.line_styles.<field>.background_color: string | null = null
 setup.patch_views.<view>.line_styles.<field>.font_name: string | null = null
 setup.patch_views.<view>.line_styles.<field>.foreground_color: string | null = null
 setup.patch_views.<view>.line_styles.<field>.horizontal_scale: float | null = null
 setup.patch_views.<view>.line_styles.<field>.image: string | null = null
 setup.patch_views.<view>.style: Style | null = null
 setup.patch_views.<view>.style.background_color: string | null = null
 setup.patch_views.<view>.style.font_name: string | null = null
 setup.patch_views.<view>.style.foreground_color: string | null = null
 setup.patch_views.<view>.style.horizontal_scale: float | null = null
 setup.patch_views.<view>.style.image: string | null = null
 setup.patches: map<string, patch> = {}
 setup.patches.<patch>: patch
• setup.pickup: boolean | null = null
• setup.pickup_pairing_ms: integer = 2
 setup.regions: map<string, PageRegion> = {}
• setup.regions.<region>: PageRegion
• setup.regions.<region>.pages: list<string>
 setup.relays: map<string, boolean> = {}
 setup.relays.<relay>: boolean
• setup.screensaver_timeout: integer = 0
 setup.scrolling: Scrolling = {'horizontal_duration': 0.075, 'vertical_duration': 0.2, 'send': 'right', 'patches': 'right', 'cycles': 'right'}
• setup.scrolling.cycles: 'right' | 'left' | 'up' | 'down' = 'right'
• setup.scrolling.horizontal_duration: float = 0.075
• setup.scrolling.patches: 'right' | 'left' | 'up' | 'down' = 'right'
• setup.scrolling.send: 'right' | 'left' | 'up' | 'down' = 'right'
• setup.scrolling.vertical_duration: float = 0.2
 setup.seed: integer | null = 23
• setup.splash_movie: list<string> = []
 setup.style: Style | null = null
 setup.style.background_color: string | null = null
 setup.style.font_name: string | null = null
 setup.style.foreground_color: string | null = null
 setup.style.horizontal_scale: float | null = null
 setup.style.image: string | null = null
 setup.switches: map<string, SwitchAssignment> = {}
 setup.switches.<switch>.button: string
 setup.switches.<switch>.display_name: string | null = null
 setup.switches.<switch>.enter: SwitchEnter | null = null
 setup.switches.<switch>.enter.action: patch
 setup.switches.<switch>.enter.override: boolean = false
 setup.switches.<switch>.exit: patch | null = null
 setup.switches.<switch>.gestures: SwitchGestures | null = null
 setup.switches.<switch>.gestures.debounce: float = 0.02
 setup.switches.<switch>.gestures.double_delay: float = 0.3
 setup.switches.<switch>.gestures.double_release: patch | null = null
 setup.switches.<switch>.gestures.double_tap: patch | null = null
 setup.switches.<switch>.gestures.long_delay: float = 0.5
 setup.switches.<switch>.gestures.long_double_release: patch | null = null
 setup.switches.<switch>.gestures.long_double_tap: patch | null = null
 setup.switches.<switch>.gestures.long_press: patch | null = null
 setup.switches.<switch>.gestures.long_release: patch | null = null
 setup.switches.<switch>.gestures.long_triple_release: patch | null = null
 setup.switches.<switch>.gestures.long_triple_tap: patch | null = null
 setup.switches.<switch>.gestures.release: patch | null = null
 setup.switches.<switch>.gestures.repeat: patch | null = null
 setup.switches.<switch>.gestures.repeat_interval: float = 0.1
 setup.switches.<switch>.gestures.triple_release: patch | null = null
 setup.switches.<switch>.gestures.triple_tap: patch | null = null
 setup.switches.<switch>.mode: 'press' | 'momentary' | 'latched' = 'press'
 setup.switches.<switch>.name: string | null = null
 setup.switches.<switch>.off: patch | null = null
 setup.switches.<switch>.on: patch
 setup.switches.<switch>.shifted: ShiftedActions | null = null
 setup.switches.<switch>.shifted.gestures: SwitchGestures | null = null
 setup.switches.<switch>.shifted.gestures.debounce: float = 0.02
 setup.switches.<switch>.shifted.gestures.double_delay: float = 0.3
 setup.switches.<switch>.shifted.gestures.double_release: patch | null = null
 setup.switches.<switch>.shifted.gestures.double_tap: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_delay: float = 0.5
 setup.switches.<switch>.shifted.gestures.long_double_release: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_double_tap: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_press: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_release: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_triple_release: patch | null = null
 setup.switches.<switch>.shifted.gestures.long_triple_tap: patch | null = null
 setup.switches.<switch>.shifted.gestures.release: patch | null = null
 setup.switches.<switch>.shifted.gestures.repeat: patch | null = null
 setup.switches.<switch>.shifted.gestures.repeat_interval: float = 0.1
 setup.switches.<switch>.shifted.gestures.triple_release: patch | null = null
 setup.switches.<switch>.shifted.gestures.triple_tap: patch | null = null
 setup.switches.<switch>.shifted.off: patch | null = null
 setup.switches.<switch>.shifted.on: patch
 setup.switches.<switch>.style: Style | null = null
 setup.switches.<switch>.style.background_color: string | null = null
 setup.switches.<switch>.style.font_name: string | null = null
 setup.switches.<switch>.style.foreground_color: string | null = null
 setup.switches.<switch>.style.horizontal_scale: float | null = null
 setup.switches.<switch>.style.image: string | null = null
 setup.switches.<switch>.switch_label: string | null = null
 setup.tag_styles: map<string, Style> = {}
 setup.tag_styles.<tag>.background_color: string | null = null
 setup.tag_styles.<tag>.font_name: string | null = null
 setup.tag_styles.<tag>.foreground_color: string | null = null
 setup.tag_styles.<tag>.horizontal_scale: float | null = null
 setup.tag_styles.<tag>.image: string | null = null
 setup.tags: list<string> = []
 setup.take_first_match: boolean = false
 setup.target_pulse: TargetPulse = {'delay': 2, 'period': 4, 'minimum': 64, 'maximum': 128}
• setup.target_pulse.delay: float = 2
• setup.target_pulse.maximum: integer = 128
• setup.target_pulse.minimum: integer = 64
• setup.target_pulse.period: float = 4
 setup.trs_ports: map<string, 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux'> = {}
• setup.trs_ports.<jack>: 'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux'
• setup.usb_device_midi: boolean = true
• setup.usb_host_midi: boolean = true
• setup.use_animations: boolean = true
 song.bindings: map<string, binding> = {}
 song.bindings.<input>: binding
• song.display_name: string | null = null
 song.enter: patch | null = null
 song.exit: patch | null = null
 song.fast_switches: string | null = null
 song.midi: map<string, Route> | null = null
 song.midi.<route>.from: string | list<string> | null = null
 song.midi.<route>.generate: MIDI generator | null = null
 song.midi.<route>.to: string | list<string> | null = null
 song.midi.<route>.via: list<MIDI processor> = []
• song.name: string
 song.navigation: PageNavigation | null = null
 song.navigation.page_back: string | null = null
 song.navigation.page_forward: string | null = null
• song.patches: list<patch>
• song.patches[]<control-change>.channel: integer | null = null
• song.patches[]<control-change>.controller: integer
• song.patches[]<control-change>.value: integer
• song.patches[]<program-change.channel>.channel: integer | null = null
• song.patches[]<program-change.channel>.program: integer
• song.patches[]<program-change.instrument>.bank: integer | null = null
• song.patches[]<program-change.instrument>.pan: integer | null = null
• song.patches[]<program-change.instrument>.program: integer | string
• song.patches[]<program-change.instrument>.volume: integer | null = null
 song.seed: integer | null = null
 song.style: Style | null = null
• song.style.background_color: string | null = null
• song.style.font_name: string | null = null
• song.style.foreground_color: string | null = null
 song.style.image: string | null = null
 song_library.background_color: string = '0xFFF8D8'
 song_library.bindings: map<string, binding> = {}
 song_library.bindings.<input>: binding
 song_library.definitions: Definitions | null = null
 song_library.definitions.profiles: list<InstrumentProfile>
 song_library.definitions.profiles[].banks: map<integer, Bank> = {}
 song_library.definitions.profiles[].banks.<bank>.lsb: integer | null = null
 song_library.definitions.profiles[].banks.<bank>.msb: integer | null = null
 song_library.definitions.profiles[].instrument: string
 song_library.definitions.profiles[].name: string
 song_library.definitions.profiles[].sysex: list<string> = []
 song_library.definitions.programs: list<TablePatch> = []
 song_library.definitions.programs[].bank: integer | null = null
 song_library.definitions.programs[].description: string | null = null
 song_library.definitions.programs[].display_name: string | null = null
 song_library.definitions.programs[].instrument: string
 song_library.definitions.programs[].name: string | null = null
 song_library.definitions.programs[].names: map<string, string> = {}
 song_library.definitions.programs[].names.<language>: string
 song_library.definitions.programs[].profile: string = 'default'
 song_library.definitions.programs[].program: integer
 song_library.definitions.programs[].range: string | null = null
 song_library.definitions.programs[].style: Style | null = null
 song_library.definitions.programs[].style.background_color: string | null = null
 song_library.definitions.programs[].style.font_name: string | null = null
 song_library.definitions.programs[].style.foreground_color: string | null = null
 song_library.definitions.programs[].style.horizontal_scale: float | null = null
 song_library.definitions.programs[].style.image: string | null = null
 song_library.definitions.programs[].tags: list<string> = []
 song_library.fast_switches: string | null = null
 song_library.fill_in_styles: boolean = true
 song_library.fonts: map<string, string> = {}
 song_library.fonts.<font>: string
 song_library.instruments: map<string, Instrument> = {}
 song_library.instruments.<instrument>.catalogues: list<path>
 song_library.instruments.<instrument>.channel: integer
 song_library.instruments.<instrument>.instrument: string = <instrument alias>
 song_library.instruments.<instrument>.output: string
 song_library.instruments.<instrument>.profile: string = 'default'
 song_library.instruments.<instrument>.programs: list<string | program metadata> | map<integer, list<string | program metadata>>
 song_library.instruments.<instrument>.programs[].description: string | null = null
 song_library.instruments.<instrument>.programs[].display_name: string | null = null
 song_library.instruments.<instrument>.programs[].name: string | null = null
 song_library.instruments.<instrument>.programs[].names: map<string, string> = {}
 song_library.instruments.<instrument>.programs[].names.<language>: string
 song_library.instruments.<instrument>.programs[].range: string | null = null
 song_library.instruments.<instrument>.programs[].style: Style | null = null
 song_library.instruments.<instrument>.programs[].style.background_color: string | null = null
 song_library.instruments.<instrument>.programs[].style.font_name: string | null = null
 song_library.instruments.<instrument>.programs[].style.foreground_color: string | null = null
 song_library.instruments.<instrument>.programs[].style.horizontal_scale: float | null = null
 song_library.instruments.<instrument>.programs[].style.image: string | null = null
 song_library.instruments.<instrument>.programs[].tags: list<string> = []
 song_library.instruments.<instrument>.tags: map<string, list<string>>
 song_library.midi: map<string, Route> | null = null
 song_library.midi.<route>.from: string | list<string> | null = null
 song_library.midi.<route>.generate: MIDI generator | null = null
 song_library.midi.<route>.to: string | list<string> | null = null
 song_library.midi.<route>.via: list<MIDI processor> = []
 song_library.patches: map<string, patch> = {}
 song_library.patches.<patch>: patch
 song_library.seed: integer | null = 23
 song_library.setlists: list<Setlist> = []
 song_library.songs: list<Song> = []
 song_library.tag_styles: map<string, Style> = {}
 song_library.tag_styles.<tag>.background_color: string | null = null
 song_library.tag_styles.<tag>.font_name: string | null = null
 song_library.tag_styles.<tag>.foreground_color: string | null = null
 song_library.tag_styles.<tag>.horizontal_scale: float | null = null
 song_library.tag_styles.<tag>.image: string | null = null
 target.animation: Geometry | null = null
 target.animation.height: integer
 target.animation.streams: list<Stream> = [{'mirror_x': false, 'mirror_y': false}]
 target.animation.streams[].mirror_x: boolean = false
 target.animation.streams[].mirror_y: boolean = false
 target.animation.width: integer
 target.animation_display_streams: list<integer> = []
 target.button_labels: map<string, string> = {}
 target.button_labels.<button>: string
 target.buttons: list<string> = []
 target.characters: string
 target.compiled_button_ids: map<string, string> = {}
 target.compiled_button_ids.<button>: string
 target.compiled_display_ids: map<string, string> = {}
 target.compiled_display_ids.<display>: string
 target.controls: TargetControls = {'settings_shortcut': false, 'previous': null, 'next': null, 'confirm': null, 'previous_section': null, 'next_section': null, 'back': null, 'next_page': null, 'previous_page': null, 'reverse_page': null, 'tap': null, 'app_actions': {}}
 target.controls.app_actions: map<string, 'previous-patch' | 'next-patch' | 'activate-selection' | 'previous-group' | 'next-group' | 'undo'> = {}
 target.controls.app_actions.<key>: 'previous-patch' | 'next-patch' | 'activate-selection' | 'previous-group' | 'next-group' | 'undo'
 target.controls.back: string | null = null
 target.controls.confirm: string | null = null
 target.controls.next: string | null = null
 target.controls.next_page: string | null = null
 target.controls.next_section: string | null = null
 target.controls.previous: string | null = null
 target.controls.previous_page: string | null = null
 target.controls.previous_section: string | null = null
 target.controls.reverse_page: string | null = null
 target.controls.settings_shortcut: boolean = false
 target.controls.tap: string | null = null
 target.default_midi_output: string
 target.displays: list<TargetDisplay>
 target.displays[].corner_radius: integer = 0
 target.displays[].gap: integer = 0
 target.displays[].height: integer
 target.displays[].id: string
 target.displays[].label: string
 target.displays[].width: integer
 target.font_sources: map<string, string> = {}
 target.font_sources.<font>: string
 target.fonts: list<FontFace>
 target.fonts[].asset: integer = 0
 target.fonts[].horizontal_scale: float = 1
 target.fonts[].id: integer
 target.fonts[].name: string
 target.fonts[].sizes: map<integer, FontMetrics>
 target.fonts[].sizes.<size>.characters: map<string, list<integer>> = {}
 target.fonts[].sizes.<size>.glyphs: string = ''
 target.fonts[].sizes.<size>.line_height: integer
 target.fonts[].sizes.<size>.runs: map<string, list<integer>> = {}
 target.fonts[].sizes.<size>.top: integer = 0
 target.functions: list<string> = []
 target.history_depth: integer
 target.max_instruments: integer
 target.max_patch_cycles: integer
 target.max_patches: integer
 target.max_switches: integer = 5
 target.memory: TargetMemory
 target.memory.capacity: integer
 target.memory.runtime: integer
 target.midi_endpoints: list<MidiEndpoint>
 target.midi_endpoints[].input: boolean = false
 target.midi_endpoints[].label: string
 target.midi_endpoints[].name: string
 target.midi_endpoints[].output: boolean = true
 target.midi_endpoints[].port: integer | null = null
 target.midi_endpoints[].route: integer
 target.midi_endpoints[].transport: 'physical' | 'usb-device' | 'usb-host' = 'physical'
 target.relay_outputs: list<string> = []
 target.scan_interval: float = 0.001
 target.trs_ports: map<string, list<'off' | 'midi-a' | 'midi-b' | 'expression' | 'aux'>> = {}
 target.ui_fonts: list<string>
 target.usb_block_size: integer = 4096
• time.send: boolean = false
• time.source: 'tap' | 'din' | 'trs' = 'tap'
• time.tempo: float | null = null
