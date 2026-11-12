"""Bind qualification to the selected MC implementation."""

from build_entry import load_target  # ty: ignore[unresolved-import]

if __name__ == '__main__':
    load_target('qualify_usb_midi').main()
