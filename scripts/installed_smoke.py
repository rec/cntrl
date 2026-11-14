"""Check installed resources and offline artifacts from outside the checkout."""

from pathlib import Path

import tyro
from PIL import Image
from pydantic import BaseModel

import cntrl
from cntrl.animation import asset
from cntrl.fonts import assets
from cntrl.fonts.outline import Outline
from cntrl.patch_switcher import binary_format, compiler, models, resolved, setup_bundle


class Smoke(BaseModel, frozen=True):
    fixtures: Path
    output: Path


def run(options: Smoke) -> None:
    assert (
        not Path(cntrl.__file__)
        .resolve()
        .is_relative_to(Path(__file__).resolve().parents[1])
    )
    options.output.mkdir(parents=True, exist_ok=True)
    target = models.Target.model_validate(
        compiler.read_document(options.fixtures / 'target.json')
    )
    source = compiler.read_document(options.fixtures / 'show.json')
    definitions = compiler.read_document(options.fixtures / 'definitions.json')
    data = compiler.compile_show(source, definitions, [], target)
    document = options.output / 'show.bin'
    document.write_bytes(data)
    bundle = setup_bundle.build({'show': data}, {'show': source}, target.memory).pack()
    assert {
        n: binary_format.core(d) for n, d in setup_bundle.unpack(bundle).items()
    } == {'show': data}
    (options.output / 'setups.bin').write_bytes(bundle)
    import tomlkit

    assert (
        compiler.compile_show(
            tomlkit.parse(resolved.export(data)).unwrap(), {}, [], target
        )
        == data
    )
    font = Path(cntrl.__file__).parent / 'fonts/courier-prime/CourierPrime-Regular.ttf'
    (options.output / 'font.cfont').write_bytes(
        assets.pack(Outline(font), {24: ['Café', *assets.NUMBERS]})
    )
    geometry = asset.Geometry(width=16, height=16)
    image = options.output / 'café movie.gif'
    Image.new('RGB', (16, 16), 'red').save(image)
    movie = asset.prepare_movie(image, geometry)
    assert asset.inspect_asset(movie).frames == 1
    (options.output / 'movie.cnsa').write_bytes(movie)


if __name__ == '__main__':
    run(tyro.cli(Smoke))
