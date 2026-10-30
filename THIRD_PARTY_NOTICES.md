# Third-party notices

The [MIT licence](LICENSE) covers original contributions to this port. It does
not relicense the original game or third-party material.

| Material | Use and provenance |
| --- | --- |
| **Stunt Car Racer (1989), Geoff Crammond / MicroStyle** | Original game being ported. Players supply their own supported ADF; disk images, ROMs and extracted game-data packs are not distributed. Original rights remain with their respective holders. |
| **Launcher logo** | Separately supplied artwork retained as an explicit branding exception. Its exact hash and resource identity are pinned in `setup/branding.json`. The artwork is excluded from the MIT licence. |
| **[Vesuri's Stunt Car Racer disassembly](https://github.com/Vesuri/stuntcarracer/tree/3cce78d193c3d0208be34d0bbfc3155e787cd406)** | Research reference for labels, routines and the extraction recipe in `tools/research/reference/extract_reference_payload.py`. Its loader also credits Codetapper/Action! and StingRay. The enhanced source is not bundled or executed by the player. No upstream licence is stated at this pinned revision; this notice does not grant rights to that work. |
| **[Musashi, Karl Stenerud](https://github.com/kstenerud/Musashi/tree/313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd)** | Optional, locally downloaded test-only CPU oracle. Its upstream permission notices are retained in the downloaded files. It is not linked into or distributed with the player. The file hashes are pinned in `setup/musashi-source-lock.json`. |

The supported disk identity and extraction offsets are recorded in
[the asset guide](https://github.com/craigshaw/StuntCarRacer/blob/main/docs/components/native-startup.md).
Local reference hashes and evidence locations are described in
[the research guide](https://github.com/craigshaw/StuntCarRacer/blob/main/docs/research/reverse-engineering.md).

This is an unofficial project, unaffiliated with the original game's rights holders.
