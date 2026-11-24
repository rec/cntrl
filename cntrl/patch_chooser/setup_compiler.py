"""Compile setup source expressions and their companion documents."""

from pathlib import Path

from pydantic import BaseModel, ValidationError

from . import (
    compiler,
    compiler_errors,
    data_files,
    global_config,
    models,
    setup_bundle,
    source_archive,
)


class SetupInputs(BaseModel, frozen=True):
    sources: dict[str, object]
    paths: dict[str, Path]
    target: models.Target
    definitions: object
    tables: list[object]
    memory_margin: int
    errors: list[str]


def compile_inputs(
    command: data_files.Sources,
    assets: dict[str, dict[int, bytes]] | None = None,
    movies: dict[int, bytes] | None = None,
    *,
    trace: data_files.Trace | None = None,
    overrides: dict[Path, bytes] | None = None,
) -> setup_bundle.SetupBundle:
    if trace is None:
        trace = data_files.Trace()
    inputs = load_inputs(command, trace, overrides)
    return compile_loaded(inputs, trace, assets, movies)


def load_inputs(
    command: data_files.Sources,
    trace: data_files.Trace,
    overrides: dict[Path, bytes] | None = None,
) -> SetupInputs:
    errors: list[str] = []
    command = global_config.resolve_inputs(command, trace=trace, overrides=overrides)

    documents = data_files.expressions(command.document, errors)
    targets = data_files.expressions(command.target, errors)
    definitions = data_files.expressions(command.definitions, errors)
    tables = [e for t in command.table for e in data_files.expressions(t, errors)]
    _, relocations = (
        source_archive.session_context(documents[0]) if documents else ({}, {})
    )
    target_data = [
        data_files.load_target(
            e,
            command.avoid_confict,
            errors,
            trace=trace,
            overrides=overrides,
            relocations=relocations,
        )
        for e in targets
    ]
    definition_data = [
        data_files.load(
            e,
            command.avoid_confict,
            errors,
            trace=trace,
            overrides=overrides,
            relocations=relocations,
        )
        for e in definitions
    ]
    table_data: list[object] = [
        data_files.load(
            e,
            command.avoid_confict,
            errors,
            trace=trace,
            overrides=overrides,
            relocations=relocations,
        )
        for e in tables
    ]
    if len(target_data) != 1:
        errors.append('expected one target expression; use + to combine target files')
    if len(definition_data) > 1:
        errors.append(
            'expected one definitions expression; use + to combine definition files'
        )
    target: models.Target | None = None
    if len(target_data) == 1:
        try:
            target = models.Target.model_validate(target_data[0])
        except ValidationError as error:
            errors.append(f'target: {error}')
    if not documents:
        errors.append('at least one setup data-file expression is required')
    seen: set[str] = set()
    sources: dict[str, object] = {}
    # Companion documents use the same logical names on the host and SD card.
    # Load the whole dependency graph before compiling or opening a transport.
    for e in documents:
        sources[e.name] = global_config.load_setup(
            e,
            command.avoid_confict,
            errors,
            trace=trace,
            overrides=overrides,
            defaults=command.global_defaults,
        )
    index = 0
    while index < len(documents):
        expression = documents[index]
        index += 1
        for name in document_references(sources[expression.name]):
            if any(n.lower() == name.lower() for n in sources):
                continue
            try:
                data_files.validate_name(name)
            except ValueError as error:
                errors.append(f'{expression.paths[0]}: {error}')
                continue
            companion = data_files.DataFile(
                name=name, paths=[expression.paths[0].parent / f'{name}.toml']
            )
            if (expression.paths[0].resolve().parent / 'cntrl-session.toml').exists():
                recovered = source_archive.recovered_recipe(
                    expression.paths[0].resolve().parent
                )
                companion = next(
                    (
                        e
                        for e in data_files.expressions(recovered.document)
                        if e.name == name
                    ),
                    companion,
                )
            sources[name] = global_config.load_setup(
                companion,
                command.avoid_confict,
                errors,
                trace=trace,
                overrides=overrides,
                defaults=command.global_defaults,
            )
            documents.append(companion)
    for e in documents:
        try:
            data_files.validate_name(e.name)
        except ValueError as error:
            errors.append(str(error))
        if e.name.lower() in seen:
            errors.append(f'{e.name}: duplicate destination name')
        seen.add(e.name.lower())
    if trace is not None:
        global_names = {e.name for e in [*targets, *definitions, *tables]}
        for consumers in trace.consumers.values():
            if any(n in global_names for n in consumers):
                consumers[:] = list(
                    dict.fromkeys(
                        [n for n in consumers if n not in global_names]
                        + [e.name for e in documents]
                    )
                )
    if target is None:
        raise compiler_errors.CompilationError(errors)
    return SetupInputs(
        sources=sources,
        paths={e.name: e.paths[0] for e in documents},
        target=target,
        definitions=definition_data[0] if definition_data else {'profiles': []},
        tables=table_data,
        memory_margin=command.memory_margin,
        errors=errors,
    )


def compile_loaded(
    inputs: SetupInputs,
    trace: data_files.Trace,
    assets: dict[str, dict[int, bytes]] | None = None,
    movies: dict[int, bytes] | None = None,
) -> setup_bundle.SetupBundle:
    errors = list(inputs.errors)
    result: dict[str, bytes] = {}
    for name, document in inputs.sources.items():
        try:
            fonts: dict[int, bytes] = {}
            result[name] = compiler.compile_show(
                document,
                inputs.definitions,
                inputs.tables,
                inputs.target,
                fonts,
                inputs.sources,
                movies,
                source_snapshots=trace.files,
                setting_overrides=trace.setting_overrides.get(name),
                include_catalogue=True,
            )
            if assets is not None:
                assets[name] = fonts
        except compiler_errors.CompilationError as error:
            errors.extend(f'{inputs.paths[name]} ({name}): {x}' for x in error.errors)
    if errors:
        raise compiler_errors.CompilationError(errors)
    try:
        return setup_bundle.build(
            result, inputs.sources, inputs.target.memory, inputs.memory_margin
        )
    except ValidationError as error:
        raise compiler_errors.CompilationError([str(error)]) from error


def document_references(document: object) -> list[str]:
    result: list[str] = []
    pending = [document]
    while pending:
        value = pending.pop()
        if isinstance(value, dict):
            if 'patch' in value and isinstance(name := value.get('document'), str):
                if name not in result:
                    result.append(name)
            pending.extend(value.values())
        elif isinstance(value, list):
            pending.extend(value)
    return result
