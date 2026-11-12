"""Exercise qualification dispatch without importing a hardware implementation."""

import sys

if __name__ == '__main__':
    for argument in sys.argv[1:]:
        print(argument)
    sys.exit(7)
