# Native career presentation

The season-ending screen displays the division and league context that were
active when the season finished. The original `updateLeagueStandingsAfterRace`
routine at `0x64916` draws that screen before promotion, relegation and the
Standard-to-Super-League transition change those values. Native rendering keeps
the old division and context as explicit presentation inputs while reading the
completed standings and names.

An ordinary division winner receives the original award image. This includes
promotion from Divisions 4, 3 and 2, and the Standard Division 1 championship.
The award is gated by `0x593f6`, set by the winner branch at `0x649c6`; the image
routine at `0x5933a` uses original result image index four.

Winning Super League Division 1 takes a separate original branch. It displays
the Super League victory message and leaves the award flag negative, so the
award image is skipped. That branch must retain its separate behaviour even
though the player finished first. The following roster uses the newly arranged
drivers. The Standard Division 1 championship instead resets the roster for the
new Super League context.

The season-end screen announces promotion and relegation using the completed
division's sorted names. Division 4 has no relegation row. A Division 1 result
where the player is not champion shows the relegation row; the championship
message belongs to the local player's separate victory branch.

F1 season skipping rejoins the original season-end progression. It does not
introduce a separate end-screen design or change the award-image gate. The
caller retains the original released/new-fire waits and palette transitions.
The screen checks execute the original remaining-fixture loop from `0x5d000`
before comparing endings for skips at the first, fourth and sixth fixture,
including a player whose existing lead still wins the division.

`native_competition_render_tests` compares the completed screens with the
untouched 68000 routines, including the menu, result image and roster palettes.
The career matrix covers both league contexts, all four divisions, each of the
three finishing places and four player-name styles. It also verifies that the
Super League victory skips the award bitmap and that old presentation context
is retained after the gameplay context changes. Generated evidence and images
are local files under `analysis/career-render/`.
