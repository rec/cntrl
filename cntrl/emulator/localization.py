"""Desktop messages use the same locale set as the shared pedal UI."""

from functools import cache
from pathlib import Path
from re import findall
from string import Formatter

import tomlkit
from pydantic import TypeAdapter

from ..localization import catalogue


@cache
def messages(language: str) -> dict[str, str]:
    desktop = TypeAdapter(dict[str, dict[str, str]]).validate_python(
        tomlkit.parse(Path(__file__).with_name('messages.toml').read_text()).unwrap()
    )
    if set(desktop) != set(catalogue.locales()):
        raise ValueError('desktop and pedal languages differ')
    reference = desktop['en']
    for n, t in desktop.items():
        if t.keys() != reference.keys():
            raise ValueError(f'incomplete desktop translation: {n}')
        for k, v in t.items():
            if not v or sorted(
                (n, s, c) for _, n, s, c in Formatter().parse(v) if n is not None
            ) != sorted(
                (n, s, c)
                for _, n, s, c in Formatter().parse(reference[k])
                if n is not None
            ):
                raise ValueError(f'invalid desktop translation: {n}/{k}')
    shared = catalogue.translation(language).messages
    return desktop[language] | {
        n: shared[n].text
        for n in ('on', 'off', 'unknown_title', 'save', 'cancel', 'ok')
    }


@cache
def dialog_messages(language: str) -> dict[str, str]:
    desktop = TypeAdapter(dict[str, dict[str, str]]).validate_python(
        tomlkit.parse(Path(__file__).with_name('dialogs.toml').read_text()).unwrap()
    )
    for n, t in desktop.items():
        if set(t) != set(catalogue.locales()) or any(not v for v in t.values()):
            raise ValueError(f'incomplete desktop dialog translation: {n}')
        if any(
            sorted(findall(r'%[1-9]\d*', v)) != sorted(findall(r'%[1-9]\d*', n))
            for v in t.values()
        ):
            raise ValueError(f'invalid desktop dialog placeholders: {n}')
    return {n: t[language] for n, t in desktop.items()}
