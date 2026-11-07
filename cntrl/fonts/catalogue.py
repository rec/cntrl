"""Bundled outline sources and prepared patch label sizes."""

from pathlib import Path


def bundled_sources() -> dict[str, Path]:
    return {n: Path(__file__).parent / p for n, p in OUTLINE_SOURCES.items()}


SIZES = [12, 16, 20, 24, 28, 32, 40, 48]
UI_FONTS = {
    'classic': 0,
    'Arial': 1,
    'ComicSansMS': 2,
    'StillTime': 5,
    'CourierPrime': 8,
    'CourierPrimeBold': 9,
    'AtkinsonNextSemiBold': 16,
}
OUTLINE_SOURCES = {
    'Circuit': 'orbitron/Orbitron-Medium.ttf',
    'CircuitBold': 'orbitron/Orbitron-Bold.ttf',
    'StillTime': 'still-time/StillTime.otf',
    'MetalLord': 'metal-lord/MetalLord.otf',
    'CaveatBold': 'caveat/Caveat-Bold.ttf',
    'CourierPrime': 'courier-prime/CourierPrime-Regular.ttf',
    'CourierPrimeBold': 'courier-prime/CourierPrime-Bold.ttf',
    'DecoRound': 'righteous/Righteous-Regular.ttf',
    'DMSerifDisplay': 'dm-serif-display/DMSerifDisplay-Regular.ttf',
    'CollegeBlock': 'graduate/Graduate-Regular.ttf',
    'OrnamentalSlab': 'ewert/Ewert-Regular.ttf',
    'RubikDistressed': 'rubik-distressed/RubikDistressed-Regular.ttf',
    'SpecialElite': 'special-elite/SpecialElite-Regular.ttf',
    'AtkinsonNextSemiBold': (
        'atkinson-hyperlegible-next/AtkinsonHyperlegibleNext-SemiBold.ttf'
    ),
}
