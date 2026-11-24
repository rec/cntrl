"""Format validation failures, with full diagnostics available to the CLI."""

from contextvars import ContextVar

from pydantic import ValidationError

verbose_validation = ContextVar('verbose_validation', default=True)


class CompilationError(ValueError):
    def __init__(self, errors: list[str]) -> None:
        self.errors = list(dict.fromkeys(errors))
        super().__init__(
            'Configuration errors:\n' + '\n'.join(f'- {e}' for e in self.errors)
        )


def validation_errors(context: str, error: ValidationError) -> list[str]:
    verbose = verbose_validation.get()
    messages = []
    for entry in error.errors(
        include_url=False, include_context=False, include_input=False
    ):
        location = '.'.join(str(p) for p in entry['loc'])
        path = (
            f'{context}.{location}'
            if context and (location or verbose)
            else context or location
        )
        message = entry['msg']
        if not verbose and entry['type'] == 'value_error':
            message = message.removeprefix('Value error, ')
        messages.append(f'{path}: {message}' if path else message)
    return messages


def validation_message(error: ValidationError, context: str = '') -> str:
    if verbose_validation.get():
        return f'{context}: {error}' if context else str(error)
    return '\n'.join(validation_errors(context, error))
