from pathlib import Path
import re
import subprocess

paths = [Path(x) for x in subprocess.check_output(['git', 'ls-files'], text=True).splitlines()]
for p in paths:
    if p.suffix not in {'.py', '.toml', '.json', '.md'}:
        continue
    s = old = p.read_text()
    if p.suffix == '.py':
        s = s.replace('models.Setup', 'models.InstrumentProfile')
        s = re.sub(r'\bsetups(?=\s*=)', 'profiles', s)
        s = s.replace('.setups', '.profiles').replace("'setups'", "'profiles'")
        s = s.replace("'setup':", "'profile':").replace("'setup', 'default'", "'profile', 'default'")
        s = re.sub(r'\bsetup(?=\s*=)', 'profile', s)
        s = s.replace('\\nsetup=', '\\nprofile=')
        if p.name == 'models.py':
            s = s.replace('class Setup(', 'class InstrumentProfile(').replace('setup: str', 'profile: str').replace('setups: list[Setup]', 'profiles: list[InstrumentProfile]')
        if p.name == 'compiler.py':
            s = re.sub(r'\bsetups\b', 'profiles', s)
            s = re.sub(r'\bsetup\b', 'profile', s)
            s = s.replace('document: profile {value.document', 'document: setup {value.document').replace('"this profile"', '"this setup"')
        if p.name == 'resolved.py':
            s = re.sub(r'\bsetups\b', 'profiles', s)
            s = s.replace('instrument, setup =', 'instrument, profile =').replace("'profile': setup", "'profile': profile").replace("'name': setup", "'name': profile")
        if p.name == 'songs.py':
            s = re.sub(r'\bsetups\b', 'profiles', s)
        if p.name == 'import-old-instruments.py':
            s = s.replace("'instrument', 'setup',", "'instrument', 'profile',")
    elif p.suffix in {'.toml', '.json'}:
        s = re.sub(r'\bsetup(?=\s*=)', 'profile', s)
        s = s.replace('definitions.setups', 'definitions.profiles').replace('[[setups]]', '[[profiles]]').replace('[setups.', '[profiles.')
        s = s.replace('"setup":', '"profile":').replace('"setups":', '"profiles":')
    elif p.suffix == '.md':
        s = s.replace('definitions.setups', 'definitions.profiles').replace('[[setups]]', '[[profiles]]').replace('[setups.', '[profiles.')
        s = re.sub(r'\bsetup(?=\s*=)', 'profile', s)
        s = s.replace('"setups":', '"profiles":').replace("'setups':", "'profiles':")
        s = s.replace('instrument/setup', 'instrument/profile').replace('instrument-level setup', 'instrument profile').replace('`instrument`, `setup`, `program`', '`instrument`, `profile`, `program`')
    if s != old:
        p.write_text(s)
