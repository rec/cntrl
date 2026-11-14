"""Bind the selected MC importer without importing another installation."""

import tyro
from build_entry import load_target  # ty: ignore[unresolved-import]

if __name__ == '__main__':
    importer = load_target('import_backup')
    importer.run(tyro.cli(importer.ImportBackup))
