"""Internal supervised worker for the public cntrl qualification flow."""

from build_entry import load_target  # ty: ignore[unresolved-import]

if __name__ == '__main__':
    load_target('usb_reliability').worker_main()
