#!/usr/bin/env bash
# Builds src/mod/LandslideRecipes.inc, the full map variants' package recipes, from game files
# extracted on the Deck. The .inc is committed: it holds no IOI data, only instructions to rebuild each
# package resource from the player's own game files plus literal bytes, with no run of 32 or more bytes
# of any candidate source of the same package (gen_package_inc.py checks). Everything else it writes
# (--work) holds IOI data and stays local. It uses Docker, so run it under scripts/locked.sh:
#
#   scripts/locked.sh scripts/build-landslide-pkg.sh --rt <ResourceTool dir> --cache <rpkg metadata dir> \
#       --raw <extracted resources dir> [--raw <another> ...] [--bin <dir> ...] --names <hash list> [--work <dir>]
#
# Options:
#   --rt     ZHMTools ResourceTool (linux-x64) with its libResourceLib_*.so; run in debian:bookworm-slim (amd64).
#   --cache  rpkg metadata (<package>.rpkg.<size>.meta, as tools/rpkg/rpkg.py meta writes them) of chunks 0, 3,
#            6, 16, 17, 18, 19, 22, 25, 26 and 27. Leave out the mod's own chunk<N>patch290.rpkg metadata.
#   --raw    extracted vanilla resources, <package stem>_<HASH>.<TYPE> (tools/rpkg/rpkg.py extract);
#            repeatable, one folder per extraction.
#   --bin    extracted resources read in binary only (tools/rpkg/entity.py), never converted to JSON; repeatable.
#            Together, the --raw and --bin folders must hold every source the recipes may copy from: each
#            built resource's source and its variant's fingerprinted resources.
#   --names  a hash list (hash.TYPE,path lines) for template and source names.
#   --work   scratch folder (default: build-landslide/ in the repository, gitignored).
#   -h, --help  print this text.
#
# What the packages hold:
#   Sapienza     the full Landslide, the full Author, the full Icon, and the full Bad Boy's tables (a layer
#                on the full Icon)
#   Marrakesh    the full Marrakesh night (the full Entertainer's base) and the full Entertainer's tables
#                (a layer on it)
#   Hokkaido     the full Patient Zero
#   Chongqing    the full Gluttony's fix brick
#   Spot fixes   tools/landslide/spotfix_spec.py's bricks for Santa Fortuna, Hokkaido, Dartmoor, Mumbai,
#                Miami, Paris and Whittleton Creek
#
# Resources the build reads (missing optional ones make it warn and skip a check):
#   --raw, Landslide   mission_mild_bingo, override_bingo, mission_mamba, agencymarkup_location,
#                      blockedoff_mansion, dressing_mission_mamba (TEMP and TBLU each); night ambient bricks
#                      optional (their pool ids are then checked too).
#   --raw, Author      scenario_ebola (TEMP and TBLU); optional: its 15 day crowd bricks (their acts join the
#                      reach check and the kept townspeople's names are checked).
#   --raw, Icon        mission_copperhead and npc_ambient_streets_night (TEMP and TBLU); optional: the Icon's
#                      navmesh chunk26_00E920F30DEF45C1.NAVP (without it the moved spots keep their nominal
#                      places).
#   --raw, Bad Boy     optional: mission_caipirinha (chunk26patch4_005246A08E78F16F.TEMP, _006179542A627268.TBLU)
#                      for the layer check, and caipirinha_outfits (_000F296CD5178932.TEMP,
#                      _001A4052A24BFC2B.TBLU) so the Icon's moved spots also step past their acts.
#   --raw, Marrakesh   tools/landslide/marrakesh_spec.py's chunk25 resources (mission_mild_mancala,
#                      override_mancala, mission_marrakesh_python, the night and generic crowd bricks,
#                      agencymarkup_location and _python, location, evergreen_setup) and the navmesh
#                      chunk25patch4_0072DA52D90FC0AD.NAVP (required).
#   --raw, Entertainer optional: mission_marrakesh_maitai (chunk25patch4_007A11AAC4947561.TEMP,
#                      _00F9B5789EBE9F2A.TBLU) for the layer check, and maitai_outfits
#                      (_002CF3D0E31919A7.TEMP, _0009AF5B3184BC1E.TBLU).
#   --raw, Patient Zero tools/landslide/patientzero_spec.py's READS: the first Hokkaido read is required
#                      (mission_mild_shogi, override_shogi, mission_flu, contractcreation_flu,
#                      agencymarkup_snowcrane, the outbreak navmesh chunk22patch4_00BA2346CFA4AE72.NAVP); the
#                      second (npc_mission_snowcrane, mission_snowcrane, location, the normal navmesh) is
#                      optional and enables the reach, kept-actor, start, keycard-door and navmesh checks.
#   --raw, Gluttony    difficulty_easy_rat (chunk6_00F7BC60EB538D23.TEMP, chunk6patch4_002F43930709372E.TBLU),
#                      in a folder of its own; keep scenario_rat out of --raw.
#   --bin, Gluttony    tools/landslide/gluttony_spec.py's READS (scenario_rat, scenario_azalea, commentary_rat,
#                      scene_wet_azalea, mission_mild_mahjong, mission_hot_mahjong, override_mahjong and the two
#                      difficulty bricks; TEMP and TBLU each, all required).
#   --bin, spot fixes  every brick and navmesh the fixed scenes load (Freelancer's mild and showdown scene with
#                      the option's bricks, as variant_audit.py --deck fills build-landslide/audit/<map>; all
#                      required except chunk0's gameessentials and ui_hitman_s2/s3), and each fix brick's source
#                      (spotfix_spec.py FIX_SOURCE).
#   --bin, crates      the supply-crate check's navmeshes (required): build-landslide/audit/sapienza holds
#                      Freelancer's mission01.navp, Landslide's and The Author's.
#
# Steps: ResourceTool convert of every raw resource without JSON yet (cached in <work>/json); the tool
# self-tests; build_bricks.py (the edits of tools/landslide/*_spec.py) into <work>/built; ResourceTool
# generate; convert the generated binaries back and require JSON identical to what build_bricks.py wrote;
# gen_package_inc.py --recipes src/mod/LandslideRecipes.inc (it prints each resource's copied and literal
# bytes). Stops at the first failure.
set -euo pipefail
cd "$(dirname "$0")/.."

usage() { sed -n '2,/^set /{/^#/{s/^# \{0,1\}//;p;};}' "$0"; }

RT="" CACHE="" NAMES="" WORK="build-landslide"
RAWS=()
BINS=()
BIN_DIRS=()
while [ $# -gt 0 ]; do
    case "$1" in
        --rt) RT="$2"; shift 2 ;;
        --cache) CACHE="$2"; shift 2 ;;
        --raw) RAWS+=("$2"); shift 2 ;;
        --bin) BINS+=(--bin "$(cd "$2" && pwd)"); BIN_DIRS+=("$(cd "$2" && pwd)"); shift 2 ;;
        --names) NAMES="$2"; shift 2 ;;
        --work) WORK="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "unknown argument $1" >&2; exit 2 ;;
    esac
done
for v in RT CACHE NAMES; do
    [ -n "${!v}" ] || { echo "--$(echo "$v" | tr '[:upper:]' '[:lower:]') is required" >&2; exit 2; }
done
[ ${#RAWS[@]} -gt 0 ] || { echo "--raw is required" >&2; exit 2; }

abs() { (cd "$1" && pwd); }
RT=$(abs "$RT")
MOUNTS=()
for i in "${!RAWS[@]}"; do
    RAWS[$i]=$(abs "${RAWS[$i]}")
    MOUNTS+=(-v "${RAWS[$i]}:/raw$i:ro")
done
mkdir -p "$WORK/json" "$WORK/built" "$WORK/back"
WORK=$(abs "$WORK")

resourcetool() { # <script run in the container with /rt, /raw0 /raw1 ... and /work mounted>
    docker run --rm --platform linux/amd64 -v "$RT":/rt:ro "${MOUNTS[@]}" -v "$WORK":/work \
        debian:bookworm-slim bash -c "export LD_LIBRARY_PATH=/rt; set -e; $1"
}

echo "== ResourceTool convert (cached)"
resourcetool 'for f in /raw*/*.TEMP /raw*/*.TBLU; do
    b=$(basename "$f"); h=${b#*_}; j=/work/json/$h.json
    [ -s "$j" ] || /rt/ResourceTool HM3 convert "${h##*.}" "$f" "$j"
done'

echo "== build_bricks.py (after the self-tests of brick_edit.py, navp.py, placement.py, the spot moves and the layer check)"
python3 -I tools/landslide/test_brick_edit.py
python3 -I tools/landslide/test_placement.py
python3 -I tools/landslide/test_spot_moves.py
python3 -I tools/landslide/test_layer.py
python3 -I tools/landslide/test_patientzero_spec.py  # the full Patient Zero's spec (its tables' shape)
python3 -I tools/landslide/test_gluttony.py  # the full Gluttony's spec, its clash audit and the fix brick
python3 -I tools/landslide/test_scene_props.py  # the full Landslide's and Author's prop deletes (DELETE_SCENE_PROPS), the Sapienza clash audit
python3 -I tools/rpkg/test_rpkg.py  # the package and binary readers the Gluttony checks use
python3 -I tools/landslide/test_variant_audit.py  # the every-map clash audit's checks (variant_audit.py)
python3 -I tools/landslide/test_spot_fix.py  # the spot fixes' bricks, their checks and specs (spot_fix.py)
python3 -I tools/landslide/test_package_inc.py  # gen_package_inc.py's scene rules (several appends on one scene)
python3 -I tools/landslide/test_recipes.py  # gen_package_inc.py's recipes and their 32-byte check
rm -f "$WORK"/built/*
NAVPS=()
for r in "${RAWS[@]}"; do NAVPS+=(--navp "$r"); done
python3 -I tools/landslide/build_bricks.py --cache "$CACHE" --json "$WORK/json" --names "$NAMES" \
    --out "$WORK/built" --audit "${NAVPS[@]}" ${BINS[@]+"${BINS[@]}"}

echo "== ResourceTool generate, and convert back"
rm -f "$WORK"/back/*
resourcetool 'cd /work/built; for f in 00*.json; do
    t=${f%.json}; /rt/ResourceTool HM3 generate "${t##*.}" "$f" "$t"
    /rt/ResourceTool HM3 convert "${t##*.}" "$t" "/work/back/$f"
done'
python3 -I - "$WORK" <<'EOF'
import json, os, sys
work = sys.argv[1]
names = sorted(f for f in os.listdir(os.path.join(work, "built")) if f.startswith("00") and f.endswith(".json"))
for f in names:
    with open(os.path.join(work, "built", f)) as a, open(os.path.join(work, "back", f)) as b:
        if json.load(a) != json.load(b):
            sys.exit("generate round trip: %s differs from what build_bricks.py wrote" % f)
print("generate round trip: %d resources identical" % len(names))
EOF

echo "== gen_package_inc.py"
SOURCES=()
for r in "${RAWS[@]}" ${BIN_DIRS[@]+"${BIN_DIRS[@]}"}; do SOURCES+=(--raw "$r"); done
python3 -I tools/landslide/gen_package_inc.py --cache "$CACHE" "${SOURCES[@]}" --built "$WORK/built" \
    --names "$NAMES" --recipes src/mod/LandslideRecipes.inc | grep -v "^not in any package"
python3 -I - "$WORK" <<'EOF'
import json, os, sys
with open(os.path.join(sys.argv[1], "built", "package_bricks.json")) as f:
    for v in json.load(f)["variants"]:
        if v.get("navmesh") == "not checked":
            print("WARNING: %s: the navmesh check did not run (its NAVP is in no --raw folder): its moved spots are "
                  "the design's nominal places, not checked on the navmesh. Extract the NAVP and rebuild before "
                  "shipping." % v["name"])
        for check, state in sorted(v.get("checks", {}).items()):
            print("WARNING: %s: the %s check did not run (%s): a Deck read is missing (see the build_bricks warnings "
                  "above and scripts/build-landslide-pkg.sh --help).%s" % (v["name"], check, state, "" if "information only" in state else
                                                " Extract it and rebuild before shipping."))
        if v.get("layerCheck") == "not checked":
            print("WARNING: %s: the layer check did not run (its layer bricks' TEMP and TBLU are in no --raw folder): "
                  "nothing says %s leaves %s's target pool, spots and exits alone. Extract them and rebuild before "
                  "shipping." % (v["name"], ", ".join(b["path"].rsplit("/", 1)[-1] for b in v["layerBricks"]),
                                 v["layerOf"]))
EOF
