# Native load and save screens

`NativeSaveMenu` reproduces the original `showLoadGameMenu` flow beginning at
`0x62748`. It draws the local asset image and its seven-pixel font into a
private planar presentation buffer. No host font or verification CPU is used by
the game. The initial insert-disc message, directory warning, thirty slots,
selection marker and editing area use the original strings, colours and cursor
positions.

The slots occupy three columns of ten rows. Up and down wrap across the thirty
slots; left and right move ten slots while retaining the original column
boundaries. Direction controls must be released before the selected slot accepts
input. Return or fire selects a nonblank load slot. Escape cancels.

Save names retain the original eight-character editing rules. New letters are
uppercase, Backspace erases the preceding character, the first character cannot
be a space, and a blank name retries. Navigation discards an unfinished edit and
restores the directory name. Names beginning with `HALL` select record data;
names beginning with `MP` select multiplayer data. The original compatibility
message rejects a name unsuitable for the current player mode.

The caller supplies the original 512-byte directory at `0x7a21a` after a
`ReadDirectory` action, then calls `directoryLoaded()`. A `Select` action carries
the selected index, eight name bytes and the data kind. Storage and game-state
encoding remain separate from the menu. `commitNativeSaveDirectory` reproduces
the name, selected/next-save index and validation-word updates.

`operationFailed()` displays the original I/O message and retry/Escape route.
The original fifty-unit busy delay precedes a retry. `invalidSaveData()` uses the
original incorrect-data message and release/press wait. Successful operations
finish through `complete()` so directional input is restored.

`native_save_menu_tests` compares screen pixels, slot navigation, name editing
and directory commits with the untouched 68000 routines. Generated images and
verification output stay under `analysis/save-menu/`.
