# Freelancer Randomizer

A [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) mod that makes every Freelancer mission in HITMAN World of Assassination a little different.

## Installation

1. Install [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK).
2. Download `FreelancerRandomizer-<version>.zip` from [Releases](../../releases).
3. Close the game. Open the game's folder (in Steam: right-click the game, then **Manage** > **Browse local files**; on a Steam Deck, in Desktop Mode) and copy the zip's `Retail` folder into it. If asked whether to replace files, choose **Replace**.
4. Start the game, press `~` (the key under Esc), turn on **FreelancerRandomizer** in the mod list, and restart the game.
5. Press `~` and click **FL RANDOMIZER** for the settings. Changes apply from the next mission start, restart or safehouse visit.

Works on Windows, and on Linux and the Steam Deck through Proton. On a Steam Deck, press Steam+X for the keyboard to type `~`, or bind `~` to a back button.

To update, repeat step 3.

To uninstall:

1. Turn off **Full map variants** (Settings tab, Maps > Map variants).
2. Restart the game once. This removes the map files the mod made in the game's `Runtime` folder.
3. Turn FreelancerRandomizer off in the SDK's mod list, or delete `Retail/mods/FreelancerRandomizer.dll`.
4. To also remove its settings and logs, delete every file in `Retail/mods` whose name starts with `FreelancerRandomizer`, in upper or lower case.

If you already removed the mod before step 1, its map files are still there: put the DLL back (or turn it back on in the SDK's mod list), do steps 1 and 2, then remove it again.

## Features

Every feature can be turned on or off in the in-game menu, and most can be tuned there. The **Settings** tab has help on each setting; the **Status** tab shows what this mission picked at random, the objective payouts and each campaign's common objectives.

The mod changes Freelancer missions and the Freelancer safehouse only. Most random picks are fixed for the whole campaign, so restarting a mission gives the same picks.

In showdowns, the NPCs who might be the target aren't shuffled, and the map variants marked `*` ("Not in showdowns") aren't offered.

Some map variants, such as Sapienza: Landslide, need map files that the mod builds from your own game files. It builds them the first time **Full map variants** is on, and again after a game or mod update; this takes a few seconds. If a game update changed the files a variant needs, that variant is marked (unavailable) in the settings and never comes up until a mod update brings it back.

- empty gear slots are filled with a random loadout of items you own; gear wall items are only lent and go back on the wall after the mission
  - it packs your lockpick first, then what the chosen prestige objective needs, then what this mission's common objectives need (the best-paying objective first), then random items, new kinds of item before repeats
  - the lockpick and the objective items can each be turned off
- a free lockpick (rusty nails or a slow lockpick), a makeshift explosive, a lethal poison and a coin when you bring none
- the supplier offers guns and gear you don't own, with even odds
- supply crates offer new items, items found around the map, guns and Freelancer tools you don't own
- weapon rarities are shuffled each campaign, and a gun's gear slot cost follows its rarity
- sniper rifles are legal wherever guns are, and the mace can be holstered
- guards carry random weapons their outfit allows
- NPCs sick from emetic poison tend to throw up where 47 can kill them
- missions may take on the look of one of the game's bonus missions, special assignments, escalations or elusive target missions as often as you choose for each map
- door locks are shuffled
- the NPCs who might be the target are shuffled with other NPCs on the map
- enforcer roles move to other NPCs
- the AI's difficulty is randomized around the game's values, leaning harder; guards' senses vary, and guards can hear footsteps
- another guard checks on a guard who never reported back: when 47 takes down a guard who radioed in a noise before the guard finishes checking, the mod plays sounds of a struggle at the spot a little later, and a guard who hears them comes to look
- common objectives and prestige cards are shuffled per campaign
  - in the safehouse, before you choose a campaign, the Status tab lists the common objectives the missions of each campaign type pick from; once you choose, it shows the objectives each mission got
- objective payouts vary, and sometimes a jackpot gives a common objective a much higher payout
- money in safes, supply crates, money mules and New York's bank vault varies
- the safehouse bird's nest coin always appears, and the first fishing catches are scripted

## Troubleshooting

- **Logs**: `Retail/mods/FreelancerRandomizer.log`, and `FreelancerRandomizer.prev.log` for the session before. The switches under **Advanced** > **Logs (for troubleshooting)** only change what goes in them.
- **Reporting a bug**: [open an issue](../../issues), attach both logs, and say what you did and roughly when. The logs hold your items, campaign seeds and file paths on your PC, so check them before you share them.
- **The mod does nothing**: it can't change missions until you restart the game after turning it on in the SDK's mod list. The same goes for **Mod enabled** in the Settings tab; when it's off, the Status tab says so.

## Building from source

This part is for developers. The build runs in Docker (clang-cl and the Windows SDK through xwin), so no Windows machine is needed. It works on x86_64 and arm64 hosts.

```sh
scripts/test.sh           # build the Docker image if needed, then build and run the host tests
scripts/build.sh          # cross-compile build/FreelancerRandomizer.dll and build/FreelancerRandomizer.pdb
scripts/format.sh         # format src/ and tests/ with clang-format (--check: change nothing, fail on a diff, as CI does)
scripts/package.sh        # pack build/FreelancerRandomizer.dll into build/FreelancerRandomizer-<version>.zip, the players' download
```

`scripts/test.sh` and `scripts/build.sh` download and verify the ZHMModSDK DevPkg into `third_party/devpkg` when it's missing or out of date. The first run builds the Docker image, which downloads the Windows SDK and takes a while; building the image accepts Microsoft's license for the SDK and CRT (through `xwin --accept-license`).

Extra arguments to `scripts/test.sh` go to the doctest runner. `FR_SANITIZE=address,undefined scripts/test.sh` or `FR_SANITIZE=thread scripts/test.sh` runs the tests under those sanitizers. `scripts/build.sh` also embeds DWARF debug info in the DLL, for debugging under Proton with GDB; `FR_DWARF=OFF scripts/build.sh` leaves it out, as the release build does.

The container gets two thirds of Docker's CPUs; set `DOCKER_CPUS` to change that. To run builds from several checkouts or worktrees one at a time, wrap them in `scripts/locked.sh`, for example `scripts/locked.sh scripts/build.sh`.

### The map package

The full map variants' packages are rebuilt at game start from the player's own game files. The DLL carries only recipes, in the committed `src/mod/LandslideRecipes.inc`: instructions to copy pieces of vanilla resources, plus literal bytes that hold no 32-byte run of any source of their package.

`scripts/build-landslide-pkg.sh --help` says how to make the recipes from your own extracted game files (it runs `tools/landslide/gen_package_inc.py --recipes`). Its intermediates hold game data, so they stay in its gitignored work folder (`build-landslide/` by default). `scripts/package.sh` refuses a source that includes anything from the gitignored `src/mod/generated/`.

At game start the DLL finds each recipe's sources in `Runtime` (the newest copy in the game's own packages), checks them, writes `Runtime/chunk<N>patch290.rpkg` and records the result in `freelancerrandomizer-packages.txt` next to the DLL. Later starts decode nothing until a source or the mod changes.

A variant whose sources changed is left out and marked (unavailable) in the settings. Turning off Full map variants removes the mod's packages. If a variant from the package fails to load, the Settings tab turns it off and offers to try it again.

`FR_RECIPE_E2E=<folder> scripts/test.sh` checks the recipes end to end on real game files: the folder (a gitignored `build-*/` folder of the repository, given relative to it) holds `runtime/` (the game's `Runtime` packages, or hard links to them) and `reference/` (known-good `chunk<N>patch290.rpkg` files), and every rebuilt package must equal its reference.

### Deploying to a test machine

The deploy scripts copy files over ssh to a Linux machine running the game through Proton, such as a Steam Deck with ssh enabled. Copy `scripts/local.env.example` to `scripts/local.env` (it's gitignored) and set:

- `DECK_HOST`: the ssh target, for example `deck@steamdeck.local`;
- `GAME_DIR` (optional): the game folder, if it isn't in the default Steam library location.

Values already set in your environment win over `local.env`.

- `scripts/deploy.sh` copies `build/FreelancerRandomizer.dll` into the game's `Retail/mods/` folder and enables it in `mods.ini`. It refuses to run while the game is running.
- `scripts/fetch-log.sh` copies `FreelancerRandomizer.log` and `FreelancerRandomizer.prev.log`, if it exists, into `logs/`, with a timestamp in their names.
- `scripts/ini-set.sh <ini name> <section> key=value ...` changes settings in an ini file in the mods folder (it also refuses while the game is running).

### Continuous integration

GitHub Actions (`.github/workflows/pipeline.yaml`) runs on every push. It runs the Python tool tests and the unit tests (plain, and under the sanitizers), builds and packages the DLL, and checks the formatting. It uploads `FreelancerRandomizer.dll`, `FreelancerRandomizer.pdb` and the player package (`FreelancerRandomizer-<version>.zip`) as build artifacts. A newer push cancels an older run, except on `main`.

A push to `main` also makes a release. It publishes the version `CMakeLists.txt` sets (`scripts/version.sh`) as a GitHub Release tagged `v<version>` with all three files, then commits the next minor version to `main`. Set a `GIT_EMAIL` repository secret to choose the email its version commits use (default: the GitHub Actions bot's).

## License

Freelancer Randomizer is free software under the GNU General Public License, version 3 (see [LICENSE](LICENSE)).

Freelancer Randomizer is fan-made and not affiliated with or endorsed by IO Interactive A/S; HITMAN is IO Interactive's trademark. You install and use it of your own free will and at your own risk, with no warranty of any kind.

Freelancer Randomizer includes code from other projects: `cmake/clang-cl.cmake` comes from [ZHMModSDK](https://github.com/OrfeasZ/ZHMModSDK) (GPL-3.0), and `tests/doctest.h` is [doctest](https://github.com/doctest/doctest) (MIT; see [tests/doctest.LICENSE.txt](tests/doctest.LICENSE.txt)).
