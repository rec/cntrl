"""Import the legacy VL70/Patchman tables and category tags.

Run from the repository root with uv run scripts/import-old-instruments.py.
Inputs are deliberately fixed; an optional positional argument selects output.

Preset 1/2 bank addressing: VL70-m MIDI Data Format, page 26:
https://data.yamaha.com/files/download/other_assets/7/314687/VL70mG2.pdf
The Patchman TURBO replaces those same two preset banks:
https://www.patchmanmusic.com/turbovl.html
"""

from pathlib import Path
from typing import Annotated

import tomlkit
import tyro
from pydantic import BaseModel, TypeAdapter

from cntrl.patch_switcher import models


class ImportOldInstruments(BaseModel, frozen=True):
    output: Annotated[Path, tyro.conf.Positional] = (
        Path(__file__).resolve().parents[1] / 'instruments'
    )


def convert_instruments(
    instruments: dict[str, dict[str, dict[str, list[str]]]],
    categories: dict[str, list[str]],
) -> dict[str, list[models.TablePatch]]:
    tags: dict[tuple[str, str, int], list[str]] = {}
    for category, references in categories.items():
        for reference in references:
            instrument, bank, program, _ = reference.split(':', 3)
            # The address, not the duplicated display name, identifies the patch.
            instruments[instrument][bank][f'{int(program):03}']
            assigned = tags.setdefault((instrument, bank, int(program)), [])
            if category not in assigned:
                assigned.append(category)
    tables: dict[str, list[models.TablePatch]] = {}
    for instrument, banks in instruments.items():
        name = INSTRUMENT_IDS[instrument]
        entries: list[models.TablePatch] = []
        for bank, patches in banks.items():
            for program, values in patches.items():
                entries.append(
                    models.TablePatch(
                        instrument=name,
                        setup='default',
                        program=int(program),
                        bank=BANK_IDS[bank],
                        name=values[0],
                        range=values[1] if len(values) > 1 else None,
                        description=values[2] if len(values) > 2 else None,
                        tags=tags.get((instrument, bank, int(program)), []),
                    )
                )
        tables[name] = entries
    return tables


def import_instruments(output: Path) -> None:
    instruments = TypeAdapter(dict[str, dict[str, dict[str, list[str]]]]).validate_json(
        INSTRUMENTS_PATH.read_bytes()
    )
    categories = TypeAdapter(dict[str, list[str]]).validate_json(
        CATEGORIES_PATH.read_bytes()
    )
    tables = convert_instruments(instruments, categories)
    definitions = models.Definitions(
        setups=[
            models.Setup(
                instrument=i,
                name='default',
                banks={
                    0: models.Bank(msb=33, lsb=0),
                    1: models.Bank(msb=33, lsb=1),
                },
            )
            for i in tables
        ]
    )
    documents: dict[str, dict[str, object]] = {
        'definitions.toml': definitions.model_dump(mode='json', exclude_none=True),
        **{
            f'{i}.toml': {'patches': [p.model_dump(exclude_none=True) for p in e]}
            for i, e in tables.items()
        },
    }
    output.mkdir(parents=True, exist_ok=True)
    for name, document in documents.items():
        path = output / name
        path.write_text(tomlkit.dumps(document))
        print(path)


INSTRUMENTS_PATH = Path('/Users/tom/code/midi/vl70/all-vl70.json')
CATEGORIES_PATH = Path('/Users/tom/code/midi/vl70/categories.json')
INSTRUMENT_IDS = {'stock': 'vl70', 'patchman': 'pman'}
BANK_IDS = {'A': 0, 'B': 1}


if __name__ == '__main__':
    import_instruments(tyro.cli(ImportOldInstruments).output)
