"""The full Gluttony: Chongqing's Seven Deadly Sins set (scenario_azalea) on Freelancer's Chongqing, without the props
that clash with it.

The gluttony map variant appends scenario_azalea (the escalation's shared brick: the food stalls, the pig statue, the
restaurant's mess and their story) to Freelancer's Chongqing scene. IOI built that set for its own Gluttony scene,
scene_wet_azalea: scenario_rat + commentary_rat + scenario_azalea, where the scene entity itself also deletes three
entities of scenario_rat. Freelancer's scenes (scene_wet_mild_mahjong, scene_wet_hot_mahjong) load scenario_rat with
their own two bricks instead (mission_mild_mahjong or mission_hot_mahjong, and override_mahjong), so those deletes
never happen, and Freelancer's own props were never placed with the set in mind. The clash audit (gluttony_audit.py,
run by build_bricks.py on every build) finds two clashes:

1. Old_Truck_A_01, the old truck on the street of food stalls at (-85.0, 70.0, 96.9), with its load (four rigid-body
   water containers and a plastic military can, all physics objects). scene_wet_azalea deletes it; Freelancer's scenes
   do not. scenario_azalea puts its canopy tent canopy_tent_b_02 at (-85.3, 69.9, 96.9), 0.3 m from it, and a fruit
   stall's crates on it: the tent spawns into the truck, and the truck and its load are thrown about (the jumping car
   and propane tank seen in game). The fix deletes the truck, as the Gluttony scene does (its load goes
   with it: they are its children). The scene's two other deletes are ambient sounds (distant traffic, horns and dogs),
   not props, and stay as they are.
2. A safe clue of override_mahjong (Setpiece_Evergreen_Scannable_SafeClue_Tablet_Unique_20, a tablet on the low table
   at (-96.1, 49.2, 101.5), in the room behind the food court) lies where scenario_azalea stacks a column of seven beer
   crates (Container_Cardboard_Box_B_Beer_00_12 at (-96.3, 49.1, 101.5), 0.2 m from it, with six more on top, up to
   103.5): with that clue picked, the tablet spawns inside the crates. The fix deletes that column (only it: the crate
   under it, _09, stays, and the tablet lies on it at the table's height). The set's story uses none of them.

Nothing else Freelancer's bricks add or move stands on one of the set's solid props, and the set names nothing
Freelancer's bricks delete. The audit's notes (near, not on: a clue 0.9 m from a chair, a safe 1.3 m from a crate, a
clue 1.4 m from a wall decoration) are left as they are.

The fix is a small brick of ours, fr_gluttony, whose only content is those deletes (DELETES). The variant appends it
after scenario_azalea, which stays IOI's file, unchanged. Freelancer's bricks name none of the deleted entities nor
anything under them (build_bricks.py checks), so Freelancer's targets, spots and exits are untouched. Deleting the
truck rather than moving or dropping the tent keeps Gluttony's set as IOI placed it, as its own scene does; deleting
the crates rather than moving the clue keeps Freelancer's safe-clue slot where the game's map tables expect it.

The brick is built from IOI's smallest scenario_rat brick, difficulty_easy_rat (4 entities): its three child entities
and its three property overrides go, its root ("Scene") and its external scene (scenario_rat) stay, scenario_azalea
is added as a second external scene, and the deletes are added. It goes into Chongqing's own package,
Runtime/chunk6patch290.rpkg (partition 6, "wet").

Design: from IOI's rpkg data only (the decoded bricks, read from the Deck copy of chunk6).
"""

WET = "assembly:/_pro/scenes/missions/wet/"
NAME = "gluttony"
DISPLAY = "Gluttony"

# A fix brick appended to the contract's own scene (FIX_OF: no scene swap), after the vanilla APPEND_BRICKS.
FIX_PATH = WET + "fr_gluttony.brick"
# Its resource ids (gen_package_inc.rid of "[path].pc_entitytype" / ".pc_entityblueprint"; in no IOI package and not in
# the hash list).
FIX_IDS = (0x004A22FBEC6ACD93, 0x00A87038CB0C2EEA)

# Its package: partition 6 ("wet" in packagedefinition, patch level 4), Runtime/chunk6patch290.rpkg.
PARTITION = (6, "wet")

# The scene the variant applies to (its identity in the package's tables; the swap keeps the contract's scene). The
# variant is offered on Chongqing's showdown scene too (scene_wet_hot_mahjong): the same fix applies there.
SCENE = 0x00E4691BD97E440C  # scene_wet_mild_mahjong.entity
SCENE_PATH = WET + "scene_wet_mild_mahjong.entity"

# Vanilla resources (TEMP, TBLU), chunk6.
SCENARIO_RAT = (0x0005C60C0B3C7977, 0x002E69256922F8F6)          # Chongqing's level (23705520 / 16097811 bytes)
SCENARIO_AZALEA = (0x00CAFC0C93568981, 0x0056E209A17CDC54)       # the Gluttony set (4104067 / 3507243)
COMMENTARY_RAT = (0x0026CC287D51640C, 0x002D215C333DEFF2)        # Diana's commentary (the Gluttony scene loads it)
SCENE_WET_AZALEA = (0x009A5828EB0E63C1, 0x00336BA8BCCD2BF5)      # IOI's Gluttony scene entity (17733 bytes)
MISSION_MILD_MAHJONG = (0x00EC3CEC3F43C631, 0x005A4DD0A9941469)  # Freelancer's mild mission brick
MISSION_HOT_MAHJONG = (0x003EE41DA2F6A0DA, 0x00181D13FF5DFBD8)   # Freelancer's showdown mission brick
OVERRIDE_MAHJONG = (0x00F90288DCFD71D6, 0x00F0F42E2D1A504B)      # Freelancer's override (both scenes)
DIFFICULTY_EASY_RAT = (0x00F7BC60EB538D23, 0x002F43930709372E)   # the fix brick's source (1265 / 1796 bytes)
DIFFICULTY_HARD_RAT = (0x00E8615EE7857833, 0x00614C361884ECFD)   # the hard contract's extra cameras

# The fix brick's source (copied, then stripped to its root).
FIX_SOURCE = DIFFICULTY_EASY_RAT
FIX_SOURCE_NAME = "difficulty_easy_rat"

# Appended to the contract's bricks, in this order, before the fix brick: (path, TEMP, TBLU).
APPEND_BRICKS = [
    (WET + "scenario_azalea.brick",) + SCENARIO_AZALEA,
]

# The fix brick's deletes: (brick, entity id, its name, why). Each must be an entity of that brick with that name, and
# named by no Freelancer brick nor by another entity of its own brick (build_bricks.py checks), and with them the audit
# must find no clash left.
DELETES = [
    (SCENARIO_RAT[0], 0x9249724A74D21096, "Old_Truck_A_01",
     "scenario_azalea's canopy_tent_b_02 stands on it (0.3 m apart); scene_wet_azalea deletes it"),
] + [
    (SCENARIO_AZALEA[0], eid, "Container_Cardboard_Box_B_Beer_00_%s" % n,
     "the crate column at (-96.3, 49.1) stands on the safe clue Tablet_Unique_20 of override_mahjong")
    for eid, n in ((0xEFA0B03F512AA326, "12"), (0xEA458CD7A3FA4A6F, "13"), (0xB7705632A0414B13, "14"),
                   (0xA1BF8C25A2842EE3, "15"), (0xAC23652904008C9B, "29"), (0x272A863D0929B6AC, "30"),
                   (0x9DA964CD6320B4A7, "28"))
]

# Read in binary (tools/rpkg/entity.py, from the build's --bin folders; never converted: scenario_rat's TEMP alone is
# 23 MB) for the clash audit and the delete checks: (name, (TEMP, TBLU)). The fix brick's source is read as JSON (it
# is edited and generated), from --raw.
READS = [
    ("scenario_rat", SCENARIO_RAT),
    ("scenario_azalea", SCENARIO_AZALEA),
    ("commentary_rat", COMMENTARY_RAT),
    ("scene_wet_azalea", SCENE_WET_AZALEA),
    ("mission_mild_mahjong", MISSION_MILD_MAHJONG),
    ("mission_hot_mahjong", MISSION_HOT_MAHJONG),
    ("override_mahjong", OVERRIDE_MAHJONG),
    ("difficulty_easy_rat", DIFFICULTY_EASY_RAT),
    ("difficulty_hard_rat", DIFFICULTY_HARD_RAT),
]
BASE_NAME = "scenario_rat"
LAYER_NAME = "scenario_azalea"
# IOI's Gluttony scene, as loaded (scene_wet_azalea's own entities, overrides and deletes included).
OFFICIAL_SCENE = ["scenario_rat", "commentary_rat", "scenario_azalea", "scene_wet_azalea"]
# Freelancer's Chongqing scenes the variant applies to, as loaded (each difficulty's brick on its own).
FREELANCER_SCENES = [
    ("mild", ["scenario_rat", "mission_mild_mahjong", "override_mahjong"]),
    ("mild easy", ["scenario_rat", "mission_mild_mahjong", "override_mahjong", "difficulty_easy_rat"]),
    ("mild hard", ["scenario_rat", "mission_mild_mahjong", "override_mahjong", "difficulty_hard_rat"]),
    ("showdown", ["scenario_rat", "mission_hot_mahjong", "override_mahjong"]),
]
# The Freelancer bricks that must not name a deleted entity (DELETES, and everything logically under it).
FREELANCER_BRICKS = ["mission_mild_mahjong", "mission_hot_mahjong", "override_mahjong", "difficulty_easy_rat",
                     "difficulty_hard_rat"]

# The audit's distances (gluttony_audit.py), to the set's solid props (not its loose food, litter, tableware and
# carpets): an entity the Gluttony scene removes or moves within LARGE_GAP_M in plan and PROP_GAP_DZ in height (a
# canopy tent is about 3 m across), or a prop that Freelancer's bricks add or move within ON_GAP_M in plan and ON_GAP_DZ
# in height (the two in the same place; props stand on their origin, so a clue 0.5 m above a crate's lies on it), is a
# clash; a Freelancer prop within PROP_GAP_M, or a Freelancer act (an NPC's place) within ACT_GAP_M, is noted.
LARGE_GAP_M = 3.0
PROP_GAP_M = 1.5
PROP_GAP_DZ = 2.5
ON_GAP_M = 0.3
ON_GAP_DZ = 0.4
ACT_GAP_M = 0.5

# The vanilla resources it was built against (each TEMP with its blueprint): a game update that re-emits one turns
# the full version off.
FINGERPRINTS = [
    (SCENARIO_RAT[0], "scenario_rat.brick"),
    (SCENARIO_AZALEA[0], "scenario_azalea.brick"),
    (COMMENTARY_RAT[0], "commentary_rat.brick"),
    (SCENE_WET_AZALEA[0], "scene_wet_azalea.entity"),
    (MISSION_MILD_MAHJONG[0], "mission_mild_mahjong.brick"),
    (MISSION_HOT_MAHJONG[0], "mission_hot_mahjong.brick"),
    (OVERRIDE_MAHJONG[0], "override_mahjong.brick"),
    (DIFFICULTY_EASY_RAT[0], "difficulty_easy_rat.brick"),
    (DIFFICULTY_HARD_RAT[0], "difficulty_hard_rat.brick"),
]
