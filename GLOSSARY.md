# Stunt Car Racer native port

Language for the native port, its setup experience and the original game.

## Language

**Launcher**:
The port's pre-game entry menu, shown on each normal launch, for selecting the source disk and configuring game options.

**Source disk**:
An original Stunt Car Racer ADF supplied by the player as the source of the game's assets.
_Avoid_: ROM

**Asset pack**:
The locally extracted game data used by the native port.

**Game settings**:
The player's shared choices for display mode, image scaling, motion presentation, driving controls and game-menu navigation.

**Faithful baseline**:
The port's original game behaviour against which optional enhancements are defined.

**Game menus**:
The original game's menu journey, including mode selection, player-name entry, save/load and pre-race screens.

**Responsive navigation**:
The enhanced game-menu behaviour, with immediate input response and a dedicated Back action while retaining the original appearance and high-level structure.

**Original navigation**:
The faithful baseline's game-menu behaviour, selectable independently of the driving control preset.

**Back**:
The game-menu action that returns one logical level and restores the previous selection, subject to the screen's cancellation and competition-progression rules.

**Career**:
A persistent single-player driver identity and its progress across seasons. Multiple careers may use the same driver name.

**Driver name**:
The name shown for a player in the original game's rosters, races and record tables. It does not uniquely identify a career.

**Career selection menu**:
The Single Player entry menu for opening the last-used career, creating a career or browsing saved careers.

**Single Player main menu**:
An open career's home, containing Hall of Fame, Practise and either Start Season or Resume Season.

**Continue**:
The action that opens the last-used career's Single Player main menu. An active career retains its in-memory progress after a save failure until it is saved or explicitly discarded.

**Resume Season**:
The action that returns an open career to its suspended season stage, including pending matchups, finish messages and reports.

**Career autosave**:
The automatically retained continuation point for a career.

**Career checkpoint**:
A saved point in a career containing its progression and continuation stage. It represents a pre-race position or committed progress between races, including pending finish/results screens.

**Career recovery copy**:
An earlier valid career checkpoint retained for restoring a damaged current autosave.

**Hall of Fame**:
The shared collection of eligible best-lap and three-lap race records, with their holder names, independent of any one career.

**Personal-best lap**:
The planned career-owned fastest eligible completed lap for one circuit and Normal/Super league, paired with its ghost regardless of whether it was set in Practice or a league race.

**Lap ghost**:
The planned visual playback of a recorded lap, used as a non-colliding target for another attempt. The first version plays the personal-best lap in Practice.

**Online Hall of Fame**:
The future collection of ranked records shared across players online, distinct from the existing local Hall of Fame.
_Avoid_: global Hall of Fame (ambiguous with the locally shared Hall)

**Online ghost**:
A future ghost recording shared across players online.
