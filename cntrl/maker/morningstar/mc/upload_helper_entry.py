"""Bind the upload helper to the explicitly selected MC implementation."""

# Direct execution puts this selected target's directory on sys.path.
from build_entry import load_target  # ty: ignore[unresolved-import]

if __name__ == '__main__':
    load_target('upload_helper').main()
