"""Bind flashing to the selected MC implementation, as for its build entry."""

# Direct execution puts this selected target's directory on sys.path.
from build_entry import load_target  # ty: ignore[unresolved-import]

if __name__ == '__main__':
    load_target('flash_target').main()
