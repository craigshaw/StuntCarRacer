# Native launcher

`app/launcher/native_launcher.*` owns asset readiness and staged preferences;
`app/platform/windows/launcher.*` owns the Windows surface and focus handling.
The launcher exists before `NativeGameSession`, so setup and initialization
failures remain recoverable. See [playing](../playing.md) for controls and
[asset import](native-startup.md) for disk identity, format and extraction.

## Lifetime and setup

Every normal launch opens the launcher windowed. Play has initial focus when a
valid asset pack is available; otherwise Choose ADF has focus and Play is disabled.
Settings remain usable before import. Play enters the mode chooser in the same
window and applies the selected game display mode. In-game Settings remains
available; there is no suspended-session return to the launcher.

Choose/Change ADF validates and imports immediately. The source disk remains
read-only, and successful import survives closing the launcher. Unsupported or
unreadable disks, damaged packs and failed replacements show an error and retry
route. A failed replacement preserves an existing valid pack and its Play route.
Details contains the supported hash and pack path; ordinary status shows source
filename and readiness. A retained pack works without the source disk present.

## Preferences

Launcher and in-game Settings share `NativeDisplayPreferences`. The seven rows
cover display mode, image sizing, integer scale/filtering, smoothing, driving
controls, menu navigation and aspect ratio. Aspect choices are Original 4:3,
Extended 4:3, 16:9 and 32:9; their rendering contract is in
[views](native-extended-view.md).

| Action | Effect |
| --- | --- |
| Edit launcher settings | Changes pending values only |
| Play | Saves edited preferences, then constructs the game session |
| Close launcher | Discards pending edits; retains completed imports |
| Restore original defaults | Windowed, Integer Auto/nearest neighbour, smoothing Off, Original driving/navigation/view; assets and saves stay intact |
| Preference-write failure | Retains pending values and offers retry without starting |
| Initialization failure | Retains assets/preferences and returns an error to the launcher |

New, missing or invalid preferences use Responsive navigation; restoring
original defaults deliberately selects Original. Integer scale and filtering
are remembered independently. Launch-only fullscreen/interpolation overrides
are not persisted by Play unless the player explicitly edits those settings.
In-game Settings previews immediately and retains its own Apply/Cancel behaviour.

## Host and presentation boundaries

Normal paths use per-user Windows application data, as recorded in
[ADR 0003](../adr/0003-per-user-player-data.md). Assets, settings, manual saves
and careers/Hall have explicit development overrides. Ordinary play creates no
driving recordings; `--recordings-dir` opts in. `Drive Native.cmd` remains a
provisional build/check helper with checkout-local data, independent of the app.

Mouse, keyboard and controller operate the launcher; the Windows file picker
retains normal keyboard/mouse operation. Focus gain and loss repaint both old
and new controls. Play releases held launcher input before the game accepts it.
The approved logo is a subdued, proportional header background, faded into the
dialog without cropping. Its hash is pinned in `setup/branding.json`; it remains
outside Git. Builds without it use a text header. No extra native/Amiga subtitle
is drawn. The original game screens retain their own artwork after Play.

## Verification

`native_launcher_tests` checks supported/unsupported import, source preservation,
retained-pack startup, failed replacement, shared settings, defaults, transient
overrides, failed saves/startup, navigation and rendering at normal/compact sizes.
It also compares recorded and unrecorded sessions to check simulation isolation.
`native_display_tests` covers settings, window/fullscreen and pause boundaries;
DPI/layout and physical device behaviour also need manual inspection.
Run the game wrapper selected in [development](../development.md#choose-verification-by-change)
for source/fixture/executable receipts. Native asset ownership is recorded in
[ADR 0002](../adr/0002-native-application-owns-asset-import.md).
