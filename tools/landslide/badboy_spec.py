"""The full Bad Boy: the elusive target The Bad Boy (mission_caipirinha) on the full Icon.

The Bad Boy's contract (IOI's contract data, SAPIENZA/ELUSIVE/THEBADBOY.json) loads The Icon's own scene
(_scene_copperhead) with one brick, mission_caipirinha: Bartholomew Argus, his people, his hangout on the movie set
(a car, a chair, a TV and a game console) and his story. It has no navmesh of its own (it uses The Icon's), and
besides The Icon's bricks it pulls in only caipirinha_outfits.

So the full Bad Boy is a layer, not a variant of its own: the swap loads the full Icon (icon_spec.py: The Icon's scene
with the package's fr_mission_mild_icon, which brings fr_override_icon) and appends the vanilla mission_caipirinha
after it. Nothing of it is copied or edited, so the package holds no new resource; what the build adds are the
variant's tables (gen_package_inc.py): the Icon's own resources, the Icon's swap bricks plus LAYER_BRICKS, the
Icon's fingerprints plus FINGERPRINTS below, and the Icon's dependencies plus every reference of those.

The layer check (build_bricks.check_layer) needs mission_caipirinha's TEMP and TBLU decoded (a --raw folder holding
them; scripts/build-landslide-pkg.sh --help lists them). With them, the build stops when
mission_caipirinha deletes or disables one of fr_mission_mild_icon's pool actors, deletes an entity either of the
Icon's bricks points at, or puts one of its props or acts on a Freelancer spot or exit; it prints its NPCs (Argus's
entity id among them, for NEVER_TARGETS and fr::BadBoyFull()), its exits, starts and overrides. Without them the
build says loudly that the layer check did not run, as it did for the Icon's navmesh. With them the Icon's spot
placement also steps past the layer's acts (PLACEMENT_BRICKS), so a moved spot does not land on one of his people.

As read: 9 NPCs (Argus, Glen Matlock, Alvaro Lombardo and six "Manfredo Esposito" movie guards) on the set's east side
(70-81, -24..-31) and by its south edge (31-33, -65..-90), two exit_point markups (his escape, not Freelancer exits),
no starts; it deletes 9 entities of mission_copperhead and 4 of location and overrides properties there, none a pool
actor, a spot or something our bricks name. Nothing to remove.

Design: from IOI's rpkg metadata and contract data only.
"""
import icon_spec

COASTALTOWN = icon_spec.COASTALTOWN
NAME = "badboy"
DISPLAY = "The Bad Boy"

# The full variant it is a layer on (its NAME): its scene, partition and bricks.
LAYER_OF = icon_spec.NAME
SCENE = icon_spec.SCENE
SCENE_PATH = icon_spec.SCENE_PATH

# Vanilla resources (TEMP, TBLU), chunk26patch4.
MISSION_CAIPIRINHA = (0x005246A08E78F16F, 0x006179542A627268)  # 228424 / 222098 bytes
CAIPIRINHA_OUTFITS = (0x000F296CD5178932, 0x001A4052A24BFC2B)  # 4930 / 4060 bytes (mission_caipirinha loads it)

# Appended after the Icon's bricks, in this order: (path, TEMP, TBLU).
LAYER_BRICKS = [
    (COASTALTOWN + "mission_caipirinha.brick",) + MISSION_CAIPIRINHA,
]

# The bricks whose acts the Icon's spot placement steps past (build_bricks.layer_acts, placement.ACT_GAP_M) when their
# TEMP and TBLU are in a --raw folder, so no moved spot lands on one of the layer's NPCs (the layer check's act rule).
# Without them the Icon builds as before.
PLACEMENT_BRICKS = LAYER_BRICKS + [
    (COASTALTOWN + "caipirinha_outfits.brick",) + CAIPIRINHA_OUTFITS,
]

# The vanilla resources the layer adds, beyond the Icon's fingerprints (each TEMP with its blueprint): a game update
# that re-emits one turns the full version off.
FINGERPRINTS = [
    (MISSION_CAIPIRINHA[0], "mission_caipirinha.brick"),
    (CAIPIRINHA_OUTFITS[0], "caipirinha_outfits.brick"),
]

# Never targets under the full Bad Boy (fr::BadBoyFull().neverTargets): The Icon's two (icon_spec.NEVER_IN_POOL, still
# loaded) and Bartholomew Argus, The Bad Boy (the contract's one Kill objective), whose story (his escape) keeps
# running. (name, repository id, entity id in mission_caipirinha, as the layer check printed it: the check fails if
# it is not one of the brick's NPCs.)
NEVER_TARGETS = [
    ("Bartholomew Argus", "ab3fe438-acd0-49a4-a0f4-690c86a10324", 0x30685A26D9C1F190),
]

# The layer check's distances: a prop of the layer (furniture, a vehicle) within PROP_GAP_M in plan and PROP_GAP_DZ
# in height of a Freelancer spot or exit blocks it; an act of the layer within placement.ACT_GAP_M puts one of its
# NPCs on the spot.
PROP_GAP_M = 1.5
PROP_GAP_DZ = 2.5
