"""Prepare a private working disk and normalize the supplied mirrored Kickstart."""
from pathlib import Path
import hashlib
import json
import shutil

ROOT = Path(__file__).resolve().parents[2]
roms = list(ROOT.glob('*.rom'))
if len(roms) != 1:
    raise SystemExit('Expected exactly one supplied ROM in the project root.')
rom = roms[0]
raw = rom.read_bytes()
if len(raw) != 524288 or raw[:262144] != raw[262144:]:
    raise SystemExit('ROM is not the verified duplicated 256 KiB image; inspect it first.')
if hashlib.sha256(raw).hexdigest() != '1d68ba18412501d2a4b307a0a632b94a50b839c2c7c5ff2df6de2c38b99a921f':
    raise SystemExit('ROM differs from the supplied setup input.')
work = ROOT / 'working'
work.mkdir(exist_ok=True)
kick = work / 'kickstart-1.3.rom'
kick.write_bytes(raw[:262144])
disk = work / 'stunt-car-racer.adf'
source = ROOT / 'Stunt Car Racer (MicroStyle).adf'
if hashlib.sha256(source.read_bytes()).hexdigest() != '548fd106cd62f2d80159d48ddd5293d8b22b6b17f80c17a84a61d75f5c8a9e06':
    raise SystemExit('ADF differs from the recorded original.')
if not disk.exists():
    shutil.copyfile(source, disk)
config = f'''config_description=Stunt Car Racer - A500 PAL analysis
config_hardware=true
config_host=true
use_gui=no
kickstart_rom_file={kick}
floppy0={disk}
floppy0type=0
floppy1type=-1
floppy2type=-1
floppy3type=-1
floppy0wp=true
floppy_write_protect=true
floppy_speed=100
cpu_model=68000
cpu_speed=real
cpu_compatible=true
cpu_cycle_exact=true
cpu_memory_cycle_exact=true
blitter_cycle_exact=true
chipset=ocs
chipset_compatible=A500
chipmem_size=1
bogomem_size=2
fastmem_size=0
ntsc=false
cachesize=0
sound_output=none
gfx_width=720
gfx_height=568
gfx_fullscreen_amiga=false
'''
(work / 'stunt-car-racer.uae').write_text(config, encoding='utf-8')
manifest = {
    'original_rom': rom.name,
    'original_sha256': hashlib.sha256(raw).hexdigest(),
    'normalization': 'Two byte-identical 256 KiB halves: use first half; preserve original.',
    'normalized_sha256': hashlib.sha256(kick.read_bytes()).hexdigest(),
    'config': str(work / 'stunt-car-racer.uae'),
}
out = ROOT / 'analysis' / 'toolchain'
out.mkdir(parents=True, exist_ok=True)
(out / 'rom.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
print(json.dumps(manifest, indent=2))
