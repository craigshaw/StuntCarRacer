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
The player's shared choices for display, motion presentation, driving controls, menu navigation, timing category and ghost visibility.

**Faithful baseline**:
The port's original game behaviour against which optional enhancements are defined.

**Game menus**:
The port's in-game menu journey, including mode and saved-game selection, player-name entry, pre-race screens and competition reports.

**Responsive navigation**:
The enhanced game-menu behaviour, with immediate input response and a dedicated Back action while retaining the original appearance and high-level structure.

**Original navigation**:
The faithful baseline's game-menu behaviour, selectable independently of the driving control preset.

**Back**:
The game-menu action that returns one logical level and restores the previous selection, subject to the screen's cancellation and competition-progression rules.

**Career**:
A persistent single-player driver identity and its progress across seasons. Multiple careers may use the same driver name.

**Multiplayer game**:
A named, persistent local competition for two to eight human drivers, including its roster and progress across championships. It is independent of any single-player career.

**Driver name**:
The name shown for a player in the original game's rosters, races and record tables. It does not uniquely identify a career.

**Career selection menu**:
The Single Player entry menu for opening the last-used career, creating a career or browsing saved careers.

**Single Player main menu**:
An open career's home, containing Hall of Fame, Practise and either Start Season or Resume Season.

**Continue**:
The action that opens the main menu of the last-used career or saved multiplayer game in the selected mode. Active progress survives a save failure in memory until it is saved or explicitly discarded.

**Resume Season**:
The action that returns an open career to its suspended season stage, including pending matchups, finish messages and reports.

**Resume Championship**:
The action that returns an open multiplayer game to its suspended competition stage, including pending matchups, finish messages and reports.

**Career autosave**:
The automatically retained continuation point for a career.

**Career checkpoint**:
A saved point in a career containing its progression and continuation stage. It represents a pre-race position or committed progress between races, including pending finish/results screens.

**Career recovery copy**:
An earlier valid career checkpoint retained for restoring a damaged current autosave.

**Multiplayer checkpoint**:
A saved continuation point for a multiplayer game, retaining completed turns and the current competition stage. An unfinished race is represented by its pre-race position.

**Hall of Fame**:
The installation-wide rankings of eligible human lap and three-lap race attempts, shared by all players using that installation and independent of any one career.

**Personal-best lap**:
The career-owned fastest eligible completed lap for one circuit, Normal/Super league and timing category, whether set in Practice or a league race. Its ghost is retained when available.

**Lap ghost**:
The visual playback of a recorded lap, used as a non-colliding target in Practice. Its source is the career's personal best or a selected Hall rival.

**Hall rival**:
A specific Hall lap ghost selected as the fixed target for a Practice challenge.

**Hall challenge**:
A Practice session against a chosen Hall rival on its track, league and timing category, separate from earned career progression.

**Online Hall of Fame**:
The future collection of ranked records shared across players online, distinct from the existing local Hall of Fame.
_Avoid_: global Hall of Fame (ambiguous with the locally shared Hall)

**Online ghost**:
A future ghost recording shared across players online.
