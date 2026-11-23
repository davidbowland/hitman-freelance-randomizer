"""The full Entertainer: the elusive target The Entertainer (mission_marrakesh_maitai) on the full Marrakesh night.

The Entertainer's contract (IOI's contract data, MARRAKESH/ELUSIVE/THEENTERTAINER.json) loads Marrakesh's night level
(_scene_python) with one brick, mission_marrakesh_maitai: the elusive target, his bodyguards, the shisha café's extra
staff and guests, and his story. It has no navmesh of its own (it uses the night's), and besides the night's bricks
it pulls in only maitai_outfits (its other externals, mission_marrakesh_python, the dressing_*_python bricks,
npc_ambient_streets_generic and globaldata, are all loaded by the night level already).

So the full Entertainer is a layer, as the full Bad Boy is on the full Icon: the swap loads the full Marrakesh night
(marrakesh_spec.py: _scene_python with the package's fr_mission_mild_marrakesh_night, which brings
fr_override_marrakesh_night) and appends the vanilla mission_marrakesh_maitai after it. Nothing of it is copied or
edited, so the package holds no new resource; what the build adds are the variant's tables (gen_package_inc.py): the
night's own resources, the night's swap bricks plus LAYER_BRICKS, the night's fingerprints plus FINGERPRINTS below, and
the night's dependencies plus every reference of those.

The layer check (build_bricks.check_layer) needs mission_marrakesh_maitai's TEMP and TBLU decoded: both were read with
the Marrakesh night's files (chunk25patch4_007A11AAC4947561.TEMP and _00F9B5789EBE9F2A.TBLU, in the
Marrakesh --raw folder), so the next package build runs it; maitai_outfits' TEMP and TBLU (for PLACEMENT_BRICKS) are
optional, as caipirinha_outfits' are for the Bad Boy. As read from that JSON: 7 NPCs, the target "Philip Giggles"
(CHAR_ElusiveTarget_Unique_MaiTai, repository id e63259c4-..., entity 28433e998f0f6321), his three bodyguards
(Donatien Richer, Leal Covillon, Fabrice Charpentier), a café worker and two tourists (one of them also named "Philip
Giggles", repository id ad18d05c-...: not the contract's target). The layer check itself has not run yet (no package
build since): it is what says whether the brick leaves the night's pool, spots and exits alone.

Design: from IOI's rpkg metadata and contract data only (The Entertainer is a class-A layer
on the full night).
"""
import marrakesh_spec

MARRAKESH = marrakesh_spec.MARRAKESH
NAME = "entertainer"
DISPLAY = "The Entertainer"

# The full variant it is a layer on (its NAME): its scene, partition and bricks.
LAYER_OF = marrakesh_spec.NAME
SCENE = marrakesh_spec.SCENE
SCENE_PATH = marrakesh_spec.SCENE_PATH

# Vanilla resources (TEMP, TBLU), chunk25patch4.
MISSION_MAITAI = (0x007A11AAC4947561, 0x00F9B5789EBE9F2A)  # 134799 / 129422 bytes
MAITAI_OUTFITS = (0x002CF3D0E31919A7, 0x0009AF5B3184BC1E)  # mission_marrakesh_maitai loads it

# Appended after the night's bricks, in this order: (path, TEMP, TBLU).
LAYER_BRICKS = [
    (MARRAKESH + "mission_marrakesh_maitai.brick",) + MISSION_MAITAI,
]

# The bricks whose acts the night's spot placement steps past (build_bricks.layer_acts, placement.ACT_GAP_M) when their
# TEMP and TBLU are in a --raw folder, so no moved spot lands on one of the layer's NPCs (the layer check's act rule).
PLACEMENT_BRICKS = LAYER_BRICKS + [
    (MARRAKESH + "maitai_outfits.brick",) + MAITAI_OUTFITS,
]

# The vanilla resources the layer adds, beyond the night's fingerprints (each TEMP with its blueprint): a game update
# that re-emits one turns the full version off.
FINGERPRINTS = [
    (MISSION_MAITAI[0], "mission_marrakesh_maitai.brick"),
    (MAITAI_OUTFITS[0], "maitai_outfits.brick"),
]

# Never targets under the full Entertainer (fr::EntertainerFull().neverTargets): the night's two
# (marrakesh_spec.NEVER_IN_POOL, still loaded) and the elusive target (the contract's one Kill objective), whose story
# keeps running. (name, repository id, entity id in mission_marrakesh_maitai, as its JSON has it: the check fails if it
# is not one of the brick's NPCs.) The contract's other objective (f8b7e382-...) is an item to pick up, not a person.
NEVER_TARGETS = [
    ("The Entertainer", "e63259c4-1a82-4299-9633-6ac54991e1e9", 0x28433E998F0F6321),
]

# The layer check's distances: a prop of the layer (furniture, a vehicle) within PROP_GAP_M in plan and PROP_GAP_DZ
# in height of a Freelancer spot or exit blocks it; an act of the layer within placement.ACT_GAP_M puts one of its
# NPCs on the spot.
PROP_GAP_M = 1.5
PROP_GAP_DZ = 2.5
