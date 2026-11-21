"""Report user-facing validation failures during setup compilation."""

from pydantic import ValidationError


class CompilationError(ValueError):
    def __init__(self, errors: list[str]) -> None:
        self.errors = list(dict.fromkeys(errors))
        super().__init__(
            'Configuration errors:\n' + '\n'.join(f'- {e}' for e in self.errors)
        )


def validation_errors(context: str, error: ValidationError) -> list[str]:
    return [
        f'{context}.{".".join(str(p) for p in e["loc"])}: {e["msg"]}'
        for e in error.errors()
    ]
