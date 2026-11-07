from pathlib import Path
replacements={
'README.md':[
('tag-style overrides. The current compiled document format is version 25; source data must be\nrecompiled for a format change.', 'tag-style overrides. The current compiled document format is version 25; source\ndata must be recompiled for a format change.'),
('separately. The [application guide](firmware/README.md) describes implemented behavior;\nremaining hardware qualification', 'separately. The [application guide](firmware/README.md) describes implemented\nbehavior; remaining hardware qualification')],
'doc/momentary-latched.md':[
('Live On/Off state is never persisted. This configuration uses the current compiled setup format\nand the tagged authoring schema.', 'Live On/Off state is never persisted. This configuration uses the current\ncompiled setup format and the tagged authoring schema.')],
'doc/patches.md':[
('band is reversed across its full width. Its background uses the heading\'s resolved patch\ncolor', 'band is reversed across its full width. Its background uses the heading\'s\nresolved patch color'),
('and optional pan, in that order. NRPN sends CC 99, 98, 6 and 38 as one ordered message sequence\non its output', 'and optional pan, in that order. NRPN sends CC 99, 98, 6 and 38 as one ordered\nmessage sequence on its output')],
'doc/setup-revision-history.md':[
('progress replies. The cooperative implementation has been flashed and subsequent boots have loaded\nsaved setups successfully.', 'progress replies. The cooperative implementation has been flashed and subsequent\nboots have loaded saved setups successfully.')],
'doc/formatting.md':[
('property wins. Registry declaration order has no effect. Tags have no integer `priority`.', 'property wins. Registry declaration order has no effect. Tags have no integer\n`priority`.')],
'plan/features.md':[
('## Additional work beyond the prompt\n\nNone.\n\n', '')],
'cntrl/maker/morningstar/mc/firmware/patch-switcher.md':[
('Implemented the portable [setup guide](../../../../../firmware/README.md).', 'Implemented named setups as described in the\n[application guide](../../../../../firmware/README.md).'),
('This module has not been flashed; physical forwarding and UI checks remain\npending.', 'This section records the original build-only verification. The module was later\nincluded in combined firmware uploads; dedicated physical forwarding and UI\nqualification remains pending.')],
}
for name, pairs in replacements.items():
    p=Path(name); s=p.read_text()
    for a,b in pairs: s=s.replace(a,b)
    p.write_text(s)
