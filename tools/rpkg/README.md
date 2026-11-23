# tools/rpkg: reading HITMAN 3's resource packages

Standard-library Python (3.9 or later), no Docker. Two modules and their self-test:

- `rpkg.py`: reads the game's packages (`Runtime/chunk<N>.rpkg`, `chunk<N>patch<K>.rpkg`), resolves which copy of a
  resource the game uses, lists resources and their references, and extracts resources decompressed. It also writes
  the package metadata cache that `scripts/build-landslide-pkg.sh --cache`, `tools/landslide/build_bricks.py` and
  `gen_package_inc.py` read.
- `entity.py`: reads entity templates and blueprints (TEMP, TBLU) from their binary form into dicts shaped like
  ZHMTools ResourceTool's JSON: entities, names, ids, parents, positions (`m_mTransform`), the common property types,
  overrides, deletes and pins. Read only: editing still goes through ResourceTool's JSON, converted under Docker by
  `scripts/build-landslide-pkg.sh`.
- `sparse_parts.py`: rebuilds byte ranges copied out of the Deck's packages (`<package>.<offset>.part` files) as
  sparse files of the packages' full sizes (sizes from the metadata cache), so `rpkg.py --cache` extracts every
  resource lying wholly inside the ranges. `python3 -I tools/rpkg/sparse_parts.py --parts build-landslide/deckcopy/chunk0
  --cache build-landslide/cache --out build-landslide/deckcopy/chunk0sparse`, then `rpkg.py extract --dir
  build-landslide/deckcopy/chunk0sparse --dir build-landslide/deckcopy/chunk0 --chunks 0 --vanilla --cache
  build-landslide/cache ...`.
- `test_rpkg.py`: `python3 -I tools/rpkg/test_rpkg.py` (synthetic packages and resources; no game data).
- `crate_poses.py`: where the game lays each item in a Freelancer supply crate and the selector box over it, from
  chunk0's crate option templates (`chunk0patch3` and `4` hold them all; `chunk0.rpkg` is not needed), and with
  `--twins` the rows of `kCrateTwins` in `src/core/CrateItems.cpp` (items with no crate option of their own, laid as
  the crate item with the same box), and with `--boxes src/core/CrateItemBoxes.inc` every template's box center from
  its spawner (`kCrateItemBoxes`: items with neither a crate option nor a twin are centered in their slot by it), and
  with `--sizes src/core/CrateItemSizes.inc` the model size of each item with no option template (`kCrateItemModelSizes`:
  the bounding boxes of the PRIMs its `pro.repo` Runtime template's item art places; only the models the packages
  given hold, so add the sparse chunk0 copy and the cache). It also prints the tool slots' room (how long an item the
  game lays in a slot: `kCrateSlotLengthMeters`, the size rule in `JudgeCrateItemFit`).
  `python3 -I tools/rpkg/crate_poses.py --dir build-landslide/deckcopy/chunk0 --dir build-landslide/deckcopy/chunk0sparse
  --cache build-landslide/cache --names third_party/zhmtools/hash_list.txt --twins --boxes src/core/CrateItemBoxes.inc
  --sizes src/core/CrateItemSizes.inc`.
- `level_items.py`: the items a supply crate may take from around the level (`crate_level_items`). `--inc
  src/core/NotPickupItems.inc` writes the items the player gets no pick-up for (pro.repo items whose runtime
  template is built on a poisonable item core, from the packages' reference tables, and those spawned with
  `EnablePickUpOnSpawn` false); `--walk` approximates the mod's level item walk map by map from the bricks (templates in chunk0.rpkg, which the Deck copy lacks, are counted, not read). It
  imports tools/landslide's `build_bricks`, `gen_package_inc` and `variant_audit` (read only). `python3 -I
  tools/rpkg/level_items.py --deck build-landslide/deckcopy --cache build-landslide/cache --names
  third_party/zhmtools/hash_list.txt --inc src/core/NotPickupItems.inc --walk`.

- `template_dump.py`: one entity template as text: each sub-entity's index, sub-entity id, name, type, parent and
  properties, the root's property aliases and exposed entities, and the pins (the source of the sick spot templates'
  ids in `src/core/SickSpots.h`). `python3 -I tools/rpkg/template_dump.py --dir build-landslide/deckcopy/chunk0 --dir
  build-landslide/deckcopy/chunk0sparse --chunks 0 --cache build-landslide/cache --names
  third_party/zhmtools/hash_list.txt 0098C7BD51E1C742`.
- `entity_dump.py`: one entity template in full: every sub-entity with its type and property values, then its
  blueprint's pin connections and input/output pin forwardings (a template's pin names). The Deck copy of
  `chunk0.rpkg` is sparse, so most base blueprints are unreadable there: the TEMP is still printed, with its
  references. `python3 -I tools/rpkg/entity_dump.py --deck build-landslide/deckcopy --chunks 0 --cache
  build-landslide/cache --names third_party/zhmtools/hash_list.txt 00A824E80BF70DFE`.
- `template_users.py`: the templates in the map chunks (whole on the Deck copy) that use a resource as a
  sub-entity, with the pins their blueprints wire into and out of it: how the game drives a template whose own
  blueprint is unreadable (`setpiecehelper_investigatecurious`: `EnablePerception`, `DisablePerception` in,
  `OnInvestigated` out). `python3 -I tools/rpkg/template_users.py --deck build-landslide/deckcopy --cache
  build-landslide/cache 00A824E80BF70DFE,00E8BB33DEDD9125` (`--chunks 0,...` adds chunk0 with its sparse rebuild).

Game files are IOI's: keep extracted resources and caches out of the repository (`build-landslide/` and `third_party/`
are gitignored).

## Commands

```
python3 -I tools/rpkg/rpkg.py info    <package.rpkg> [...] [--deletions]
python3 -I tools/rpkg/rpkg.py list    --dir <rpkg dir> --chunks 6 [--type TEMP] [--package chunk6patch300.rpkg]
python3 -I tools/rpkg/rpkg.py refs    --dir <rpkg dir> --chunks 6,0 [--reverse] HASH
python3 -I tools/rpkg/rpkg.py extract --dir <rpkg dir> --chunks 6 [--out <dir>] HASH [HASH ...]
python3 -I tools/rpkg/rpkg.py meta    --dir <rpkg dir> --chunks 6 --cache <cache dir>
```

Options every command but `info` takes:

- `--dir`: a folder of packages (the game's `Runtime` folder, or a copy of part of it); repeatable.
- `--chunks`: chunk numbers, comma separated. Each is resolved on its own; across them the first that holds a hash
  wins (`--chunks 6,0`: Chongqing, then the base game).
- `--vanilla`: IOI's own patches only (numbered below 100). Mods number theirs from 100 up (`chunk6patch300.rpkg`), and
  FreelancerRandomizer writes `chunk<N>patch290.rpkg`. Use it for anything the mod builds on.
- `--patch-level K`: only patches up to K (without either, every patch present counts).
- `--cache`: the metadata cache. With package files, a cached head is read instead of the file's (when its size
  matches) and written when missing; without `--dir`, `list` and `refs` work from the cache alone.
- `--names`: a hash list (`hash.TYPE,path` lines, ZHMTools' `hash_list.txt`) to print resource paths.

HASH is 16 hex digits (`00CAFC0C93568981`) or a resource path in brackets
(`"[assembly:/_pro/scenes/missions/wet/scenario_azalea.brick].pc_entitytype"`), hashed as the game does (`00` and
bytes 1 to 7 of the MD5 of the lower-case path).

`extract` writes `<package stem>_<HASH>.<TYPE>` (`chunk6patch4_00CAFC0C93568981.TEMP`), the newest copy, decompressed:
the names `scripts/build-landslide-pkg.sh --raw` and `--bin` and `tools/landslide/gluttony_audit.py --raw` expect.

## How the game resolves a resource

A partition's packages load in patch order: `chunk6.rpkg`, then `chunk6patch1.rpkg`, `chunk6patch2.rpkg`, and so on by
number, up to the partition's patch level (4 for IOI's partitions today; FreelancerRandomizer raises the ones it has a
package for to 290). Each patch first applies its deletion list (hashes whose older copies stop counting), then adds
its own resources: a hash's newest copy is the one used.

## Package format (RPKG v1 "GKPR" and v2 "2KPR")

- Header: the magic; v2 adds 9 bytes (a version and chunk/patch fields); then resource count, index size and info
  size (u32 each). A patch (a `patch<K>` file name) follows with its deletion list: a u32 count and that many u64
  hashes.
- Index, 20 bytes per resource: hash (u64), data offset in the file (u64), and a u32 whose bit 31 says the data is
  XOR-obfuscated (the cyclic 8-byte key `DC 45 A6 9C D3 72 4C AB`) and whose low 30 bits are the compressed size (0:
  stored uncompressed).
- Info, per resource: its type (4 bytes, reversed: `PMET` is TEMP), the size of its reference table, a zero, the final
  (decompressed) size, the memory and video memory sizes (u32 each), then the reference table: a u32 count (top bits
  flags), one flag byte per reference, then the referenced hashes (u64 each).
- Data: LZ4 block format (no frame), after the XOR when flagged.

The metadata cache holds a package's head (header, deletion list, index and info), named
`<package>.rpkg.<file size>.meta`: a package whose size changed gets a new cache name, so a stale head is never used.

## Entity templates (BIN1)

TEMP and TBLU are ResourceLib "BIN1" resources: `BIN1`, alignment and segment count bytes, the data size (big-endian
u32), four zero bytes, the data, then segments (u32 id, u32 size, body). Pointers in the data are data offsets (-1 for
none); a TArray is (begin, end, capacity) and its count is the u32 just before begin. Segment `0x3989BF9F` lists the
type slots and type names: each property value (a ZVariant) starts with an index into that list. Property names are
kept only as their CRC-32 (`entity.PROPERTY_NAMES` names the ones the tools read; others print as `0x<crc>`).

## Notes on the Deck's chunk6

`chunk6.rpkg` and IOI's `chunk6patch1` to `4` are the game's. `chunk6patch300.rpkg` is another mod's (a v1 "GKPR"
package, as mod tools write them; 349 resources): it replaces both of Freelancer's Chongqing scene entities
(`scene_wet_mild_mahjong` and `scene_wet_hot_mahjong`: their copies load `evergreen_setup` only), and adds a navmesh,
an AI grid and props and outfits from other maps (Berlin's DJ set, The Stiletto's outfit). The game mounts it only when
Chongqing's partition is raised to 300 or above (the SDK's MaxPatchLevel, or a mod manager); FreelancerRandomizer's
`chunk6patch290.rpkg` stays below it, and `--vanilla` leaves it out.
