from pathlib import Path
from shutil import copyfile

from cntrl.maker.morningstar.mc.firmware import read_hex
from cntrl.maker.morningstar.mc.upload_helper import validate_firmware

backup = Path(Path('/private/tmp/cntrl-latest-backup-path.txt').read_text())
build = Path('cntrl/maker/morningstar/mc/firmware/build')
image = read_hex(build / 'boot-switch/combined.hex')
validate_firmware(image)
copyfile(build / 'patch-switcher.hex', backup / 'previous-upload-image.hex')
copyfile(build / 'boot-switch/combined.hex', backup / 'new-image.hex')
copyfile(build / 'boot-switch/combined.hex', build / 'patch-switcher.hex')
print(image.sha256)
