from urllib.parse import parse_qs, urlsplit

import tomlkit

from cntrl.emulator.corrections import (
    Correction,
    CorrectionBatch,
    TranslationField,
    TranslationRegion,
    issue_body,
)


def test_status_labels_edit_independently_and_export_with_placeholders() -> None:
    original = 'Cycles:{cycles:u}  Patches:{patches:u}'
    cycles = TranslationRegion(
        message='cycles_patches',
        field='text',
        original=original,
        index=0,
        screen=0,
        left=0,
        top=0,
        right=7,
        bottom=1,
        literal=0,
    )
    patches = cycles.model_copy(update={'literal': 1})
    assert cycles.editor_text(original) == 'Cycles:'
    assert patches.editor_text(original) == 'Patches:'
    replacement = cycles.replace_literal(original, 'Loops:')
    replacement = patches.replace_literal(replacement, 'Sounds:')
    assert replacement == 'Loops:{cycles:u}  Sounds:{patches:u}'
    assert cycles.valid_replacement(replacement)
    assert not cycles.valid_replacement(replacement.replace('{patches:u}', '0'))
    batch = CorrectionBatch('en', 'English')
    batch.accept(cycles, replacement)
    assert tomlkit.loads(batch.report())['corrections'][0]['replacement'] == replacement


def test_report_counts_fields_independently_and_keeps_draft_separate() -> None:
    batch = CorrectionBatch('fr', 'Français')
    full = TranslationField(message='browse', field='text', original='parcourir')
    short = TranslationField(message='browse', field='short', original='voir')
    batch.accept(full, 'Parcourir')
    batch.accept(full, 'Afficher')
    batch.accept(short, 'Voir')

    draft = Correction(
        message='browse', field='text', original='parcourir', replacement='parcourir'
    )
    pending = tomlkit.loads(batch.report(draft))
    assert [c['replacement'] for c in pending['corrections']] == ['Voir']
    accepted = tomlkit.loads(batch.report())
    assert [c['replacement'] for c in accepted['corrections']] == ['Voir', 'Afficher']


def test_issue_prefill_preserves_the_toml_and_native_language_name() -> None:
    batch = CorrectionBatch('fr', 'Français')
    field = TranslationField(message='browse', field='text', original='parcourir')
    batch.accept(field, 'Affiché ```')
    report = batch.report()
    body = issue_body(report)
    query = parse_qs(urlsplit(batch.issue_url(body)).query)
    assert query == {'title': ['Translation correction for Français'], 'body': [body]}
    assert body == f'````toml\n{report}````\n'
