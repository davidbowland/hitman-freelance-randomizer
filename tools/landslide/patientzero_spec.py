"""The full Patient Zero's Freelancer layer: what build_bricks.py changes in IOI's two Freelancer bricks of mild
Hokkaido so they play on Patient Zero's own scene, the hotel under the outbreak.

The swap loads Patient Zero's scene (_scene_flu, which IOI's Patient Zero campaign mission and Contracts mode's "flu"
level both use) with one brick instead of the contract's mission_mild_shogi (and its difficulty brick, made for the
normal hospital): our copy of it, fr_mission_mild_patientzero, which in turn loads our copy of override_shogi,
fr_override_patientzero. As for the other full variants (landslide_spec.py, marrakesh_spec.py), both copies keep every
entity id of the original, so the game's CPD keys (MildShogi_*) and the mod's tables still apply. Both bricks go into
Hokkaido's own package, Runtime/chunk22patch290.rpkg (partition 22, "hokkaido").

How the outbreak differs from Freelancer's Hokkaido, from the decoded bricks:
- _scene_flu is Freelancer Hokkaido's level plus the outbreak: it loads mission_snowcrane, npc_mission_snowcrane and
  location (what scene_snowcrane_mild_shogi and the two Freelancer bricks load), then mission_flu (the outbreak's 27
  people, its story and its navmesh), tod_flu and gfx_flu; mission_flu loads ea_flu (the quarantine dressing),
  outfits_flu and agencymarkup_snowcrane. So every brick our copies name is still loaded: unlike the Marrakesh night,
  nothing needs re-pointing into a missing brick.
- mission_flu sets its own navmesh (_scene_flu.navp) on mission_snowcrane's pathfinder; override_shogi sets none.
- mission_flu deletes 94 entities of npc_mission_snowcrane (two of Freelancer's pool actors among them), moves 19 of
  its people, deletes agencymarkup_snowcrane's start intros (SnowCrane_StartingPoints) and makes its own start
  (HeroSpawn_Flu, at the helipad) the default.
- The outbreak's story that a Freelancer mission must not run is in two folders of mission_flu: "Virus" (the infection
  spreading from person to person, 47's infection and cough, the infected icon and counter, the contract events
  Event_47Infected, Event_47Cured, Event_AntidoteLost) and "Vaccine" (the antidote). IOI's own Contracts-mode switch,
  contractcreation_flu, deletes exactly those two, leaves an empty antidote case in place, and loads
  override_constantjeff (Jeff's item chest spawns at the start). Our override loads contractcreation_flu as one of
  its external scenes (STORY_SWITCH), so the swap stays one brick and the story is switched off IOI's way. Diana's
  Patient Zero lines and the campaign's objectives, exit and guidance live in mission_guidance_flu, the campaign's
  contract brick, which is not loaded. mission_flu has no exit of its own (its exit_point entities are where guards
  escort trespassers to).

How spots were judged: every spot, clue, safe guard, mule act, kept target act, exit and start must have outbreak
navmesh floor within build_bricks.KEPT_FLOOR_M (EXIT_FLOOR_M for exits) on the default start's island (NAVMESH), be
within build_bricks.REACH_M of an act of mission_flu or npc_mission_snowcrane, and lie outside every collision box and
pathfinder obstacle of ea_flu and mission_flu. All of override_shogi's spots, exits and dedicated targets pass as they
are (one clue on a high shelf: KEPT_RAISED) but one supply crate, which ea_flu's morgue table hems in
(SPOT_MOVES); no slot needs re-pointing. Spots near one of ea_flu's two keycard
(RFID) doors are listed in the audit (KEYCARD_DOORS): which side of a door a spot is on cannot be read offline, so they
are in-game checks.

Rules: a slot (its CPD index) keeps meaning a usable place of its kind (re-point, never delete); only spot entities
are re-pointed, never the supplier's choice conditions.

Design: from vanilla IOI data only.
"""

HOKKAIDO = "assembly:/_pro/scenes/missions/hokkaido/"
NAME = "patientzero"
DISPLAY = "Patient Zero"
MISSION_PATH = HOKKAIDO + "fr_mission_mild_patientzero.brick"
OVERRIDE_PATH = HOKKAIDO + "fr_override_patientzero.brick"
# Their resource ids (gen_package_inc.rid of "[path].pc_entitytype" / ".pc_entityblueprint"; in no IOI package and not
# in the hash list).
MISSION_IDS = (0x00D2065FDB6203F1, 0x0063CD03E0332494)
OVERRIDE_IDS = (0x008B1DA60A11D489, 0x00B3F39546275DFE)

# Its package: partition 22 ("hokkaido" in packagedefinition, patch level 4), Runtime/chunk22patch290.rpkg.
PARTITION = (22, "hokkaido")

# Vanilla resources (TEMP, TBLU), all in chunk22.
SRC_MISSION = (0x00D6C83B2487913C, 0x007069AA5A274642)   # mission_mild_shogi (104496 / 86611 bytes)
SRC_OVERRIDE = (0x001B8721449A41C9, 0x00F4631DF8921D40)  # override_shogi (94198 / 76493)
SRC_NAMES = ("mission_mild_shogi", "override_shogi")
MISSION_FLU = (0x007DF361D5B7606D, 0x003EEA6770CDD15B)    # the outbreak (873049 / 703442)
EA_FLU = (0x0030C60BD2E68D98, 0x00A878DD5212157D)         # the quarantine dressing
NPC_MISSION_SNOWCRANE = (0x009AAFAC5EA2ABC8, 0x003032578D699567)  # the hotel's people (22 of the 42 pool actors)
MISSION_SNOWCRANE = (0x00B465A0A9523F64, 0x0096E8C5FA5494F2)      # the normal level's base
CONTRACTCREATION_FLU = (0x00235078C9000F24, 0x002E57B42A5A1535)   # Contracts mode's story switch (897 bytes)
OVERRIDE_CONSTANTJEFF = (0x0096E183A97D24AF, 0x0030EE92D627F58E)  # loaded by contractcreation_flu (328 bytes)

SCENE = 0x001722CB49DCFF66  # _scene_flu.entity, the scene the swap loads
SCENE_PATH = HOKKAIDO + "_scene_flu.entity"
BASE = MISSION_FLU

LOCATION = 0x00DC179DAB61D86D
LOCATION_TBLU = 0x00FB4EFEB6BCC8AD
EVERGREEN_SETUP = 0x006864DBF5FD7CD4
AGENCYMARKUP_LOCATION = 0x001ECC9279457547  # agencymarkup_snowcrane: Hokkaido's nine starts
AGENCYMARKUP_LOCATION_TBLU = 0x0047BA95BA849796
TOD_FLU = 0x0025DD895C349D17
GFX_FLU = 0x001B58DA1120083B
OUTFITS_FLU = 0x007FCDAE29917724
SCENE_SNOWCRANE_MILD_SHOGI = 0x00DAC05E7AF4B6E0  # the contract's scene
DIFFICULTY_EASY = 0x003C1FD2AA30B41C  # difficulty_easy_hokkaido_snowcrane (left out by the swap)
DIFFICULTY_PRO1 = 0x00B668E642AB275B  # difficulty_pro1_hokkaido_snowcrane (left out by the swap)

# The outbreak's navmesh (_scene_flu.navp, 757180 bytes in chunk22patch4; 3421 areas, 2 islands): build_bricks.py
# finds *<HASH>.NAVP in its --navp folders.
NAVMESH = (0x00BA2346CFA4AE72, "_scene_flu.navp")
# The normal level's navmesh (_scene_mission_snowcrane.navp, the second Deck read), for comparison only: when its NAVP is
# in a --navp folder, the audit says which checked places also stand on the normal hotel's walkable floor; without it
# the build warns that the comparison did not run (nothing else depends on it).
COMPARE_NAVMESH = (0x00988E23351E39C6, "_scene_mission_snowcrane.navp")
# It is Freelancer's Hokkaido navmesh too: the crate check (build_bricks.check_crates) needs it, so for that check it
# is required (the first Hokkaido read's folder has it).
FREELANCER_NAVMESH = COMPARE_NAVMESH

# Where a spot counts as reachable: near an act of the outbreak's people or the hotel's (build_bricks.REACH_M), and on
# the navmesh. npc_mission_snowcrane is the second Deck read: without its JSON the reach check is skipped with a
# warning (OPTIONAL_REACH; the navmesh and blocker checks still run).
REACH_SOURCES = [("mission_flu", MISSION_FLU), ("npc_mission_snowcrane", NPC_MISSION_SNOWCRANE)]
OPTIONAL_REACH = {NPC_MISSION_SNOWCRANE[0]}
REACH_MIN_ACTS = 200

# --- fr_mission_mild_patientzero (copy of mission_mild_shogi) ----------------------------------------------------

# mission_mild_shogi's own external scenes (all loaded under the outbreak), plus mission_flu, whose people join the
# pool.
MISSION_SCENES = [EVERGREEN_SETUP, "OVERRIDE", AGENCYMARKUP_LOCATION, LOCATION, MISSION_SNOWCRANE[0],
                  NPC_MISSION_SNOWCRANE[0], MISSION_FLU[0]]
POOL_BRICKS = {"hotel": NPC_MISSION_SNOWCRANE[0]}  # POOL_ADDITIONS name these by key ("base": mission_flu)

SPAWN_TARGETS = 0x1CD639BF97C91B8F  # Helper_Evergreen_Spawn_Targets (src/core/SceneKinds.cpp kKnownHelpers)
POOL_SIZE = 42  # 22 start-enabled actors of npc_mission_snowcrane and 20 dedicated targets

# The brick's own 20 dedicated targets: every act of each is on the outbreak's navmesh (floor within 1.5 m, on the
# start's island) and within 15 m of an act of mission_flu or npc_mission_snowcrane. All keep their slot.
KEEP_LOCAL_TARGETS = [
    0xFED8C5753341AEF3,  # EG_Target_Custom_01 (Taro Ishida)
    0x23DFC40E2F5AADCC,  # EG_Target_Custom_02 (Shoji Okamoto)
    0x7C2349323B97E94A,  # EG_Target_Custom_03 (Kenji Samurakami)
    0xE27C190FCBFD5EC9,  # EG_Target_Custom_04 (Mr. Borch)
    0x0E80CAE0F48ABA0A,  # EG_Target_Custom_05 (Kaede Matsuda)
    0x4817E3ACF9A6FC32,  # EG_Target_Custom_10 (Hilda van der Baas)
    0x8F2374341952DF66,  # EG_Target_Custom_09 (Tatsuya Rin)
    0xD5C9BE264E4049A1,  # EG_Target_Custom_08 (Orochi Ohayashi)
    0x230DEAD1CCF61762,  # EG_Target_Custom_07 (Hiroki Kato)
    0x9A0C4B361162E0CA,  # EG_Target_Custom_06 (Tadao Sakei)
    0x4850604B0AAA0CE2,  # EG_Target_Custom_11 (Elise Katherine)
    0xDC545AE410B8FB46,  # EG_Target_Custom_12 (Don Ritter)
    0x4E878D95F63854EF,  # EG_Target_Custom_14 (Johnny Armstrong)
    0x271ECE453922CA52,  # EG_Target_Custom_13 (Sami El-Hazhid)
    0xED6199C4413ECD82,  # EG_Target_Custom_15 (Nori Morita)
    0x23C76D40C6169B43,  # EG_Target_Custom_16 (Midori Oono)
    0x11E8A31D4BEA8F91,  # EG_Target_Custom_17 (Melissa Redman)
    0xF9CB02EC231C996C,  # EG_Target_Custom_18 (Kyo Ikeda)
    0x0F504AD25557989E,  # EG_Target_Custom_19 (Diana Tarenton)
    0x85A8203EC25FD385,  # EG_Target_Custom_20 (John Danielson)
]
DROP_LOCAL_TARGETS = []

# The hotel's pool actors (npc_mission_snowcrane) that keep their slot: 8 of 22. mission_flu deletes two of the 22
# (Genta Kakuta 417e9c421706004e, the spa patient, and Kouki Shimizu 48ec73cde8309fab, a hospital guard); of the other
# 20, the 12 standing nearest the outbreak's people and acts (the hospital wing, where the outbreak's staff take over,
# and the restaurant's and helipad's side; Akira Nakamura is one mission_flu moves and gives items to) hand their slot
# to the outbreak's people, so a third of the pool (14 slots) is the outbreak's own. These 8 are checked not deleted by
# any loaded brick, and their names against npc_mission_snowcrane when it is read.
KEEP_SCENE_NAMES = {
    0x501DE56A8BB0A34A: "Justin Speer",
    0x2E530B7677ABF9E9: "Masashi Morioka",
    0x3F0844161E9EA3EC: "Nails",
    0x326D9C91B365B230: "J. Brooke",
    0x8DDEE5795CAAAF9E: "Jason Portman",
    0xB81E955443DE38F4: "Nikica Pranji?",  # sic: the game's own name ("?" in the data)
    0x1F8FCBE34EB6A5EC: "Yuuto Saiki",
    0x2D8CA40E60668D2E: "Nokadota Chiba",
}
KEEP_SCENE_TARGETS = [("hotel", eid) for eid in KEEP_SCENE_NAMES]

# The 14 outbreak people that take the other slots (the two deleted hotel actors' and the 12 handed over), in slot
# order: (brick, entity id, name), all npcactors of mission_flu, every one start-enabled (none switches it off) and
# standing on the outbreak's navmesh: the biosuit workers of the surgery room and the intensive care ward, the doctor
# and the surgeon, the quarantine's guards in the corridors, a patient and the resort staff member among the patients.
# Left out of the pool (the target pool swap may still take some of them; only NEVER_IN_POOL never): Owen Cage and
# Klaus Liebleid (Patient Zero's targets), Jeff (Contracts mode's item chest, STORY_SWITCH), Kokoro the heart patient
# (on a part of the navmesh the start cannot reach), and, for depth, the other biosuit workers, the two helipad guards
# and four patients.
POOL_ADDITIONS = [
    ("base", 0xA2D2824364B037DE, "Ichiro Kasai"),
    ("base", 0xB673EF605398FAEF, "Kei Nagai"),
    ("base", 0x3A2AF7E8DB9A3F19, "R M Steelman"),
    ("base", 0xB1D1963E7E324886, "Nami Yamazaki"),
    ("base", 0xDE992770C9C4484C, "Ishii Yoshihito"),
    ("base", 0xDD23D7056CAE8EFC, "Ikuya Ogura"),
    ("base", 0x23A05E40B8CD2353, "Hayato Shi"),
    ("base", 0xD7C10AF24D648D4D, "Yui Kawarasaki"),
    ("base", 0x8B1D0BB785DE79A4, "Juurou Matsuoka"),
    ("base", 0x02A5ADCB792A97E8, "Tarou Kaoru"),
    ("base", 0x20729B2B38931AB2, "Kazuhiko Ekirei"),
    ("base", 0x71384448BA6F8D09, "Yamato Hideki"),
    ("base", 0xA9F95DED7D534272, "Georg Kron"),
    ("base", 0xE0ECE3ADB75643BF, "Oscar Himura"),
]

# Patient Zero's two named targets never enter the pool (fr::PatientZeroFull().neverTargets too): (entity id in
# mission_flu, repository id, name). The infected are picked by the outbreak's story at run time; that story is
# switched off (STORY_SWITCH), so nobody is infected.
NEVER_TARGETS = [
    (0x1C3B8BC862DB9082, "3798fcf3-4e33-40db-a8a2-c160a3ec55bf", "Owen Cage"),
    (0x01432E3710A37DA0, "01822ba2-7b3b-4bbd-a9f3-cf8006ba945a", "Klaus Liebleid"),
]
NEVER_IN_POOL = [entity for entity, _, _ in NEVER_TARGETS if entity]

# --- fr_override_patientzero (copy of override_shogi) ------------------------------------------------------------

TERRITORY = 0x2EB66BBF858C3AA6  # Helper_Evergreen_CPD_TerritorySetupRandomizer

# override_shogi's own external scenes, plus mission_flu (an exit's acts and blockers are judged against it) and IOI's
# Contracts-mode story switch (STORY_SWITCH), which loads with our override.
OVERRIDE_SCENES = [EVERGREEN_SETUP, LOCATION, NPC_MISSION_SNOWCRANE[0], MISSION_SNOWCRANE[0], AGENCYMARKUP_LOCATION,
                   MISSION_FLU[0], CONTRACTCREATION_FLU[0]]

# The outbreak's story is switched off by loading IOI's own Contracts-mode switch, contractcreation_flu, from our
# override (it loads override_constantjeff itself). The build checks that it deletes exactly these entities of
# mission_flu (by name) and nothing else, and counts everything under them as deleted for the pool check:
#   (brick, deletes {entity id: name})
STORY_SWITCH = (CONTRACTCREATION_FLU, {
    0x7DC77D48E899BD88: "Virus",    # the infection: spread, 47 infected, cough, icon, counter, contract events
    0x05116B4D5A06C9DE: "Vaccine",  # the antidote and its timer
})
APPEND_BRICKS = []  # nothing added to the contract's brick list: the swap stays our one mission brick

# Property overrides our override adds (build_bricks checks each owner's name and old value, and the new value's name
# when its brick is read):
#   {"owner": (scene, entity id, name), "property", "type", "was": (scene, entity id, name) of the old reference,
#    "value": (scene, entity id, name), "why"}
# 47's suit: Patient Zero starts 47 in his suit and makes the suit illegal everywhere. mission_flu adds
# Hero_IsFluStartingOutfit (a ZHeroKeywordCondition whose m_rRequirements is its own Prof_Agent47, any 47 suit) and
# overrides location's Hero_GloballyIllegalProfession m_aValues to add it, which turns on location's zone "All"
# (9d9cb64fbe1e07b6, the whole hotel, EO_HIGH; rule "Illegal disguises": hostile) for the suit. Pointing the condition at
# location's Prof_Hero_NinjaSuit (what location's own Hero_IsNinja checks) makes it a copy of the vanilla ninja check:
# the suit follows the normal map's rules, and the outbreak's quarantine and surgery rules stay.
PROPERTY_OVERRIDES = [
    {"owner": (MISSION_FLU[0], 0x581AB55C286067A1, "Hero_IsFluStartingOutfit"),
     "property": "m_rRequirements", "type": "SEntityTemplateReference",
     "was": (MISSION_FLU[0], 0x37EBC9610259A196, "Prof_Agent47"),
     "value": (LOCATION, 0x2253E821FAD8F621, "Prof_Hero_NinjaSuit"),
     "why": "47's suit is no longer illegal across the whole map (zone All); the condition now matches the ninja "
            "suit, as location's Hero_IsNinja does"},
]

SLOT_REPOINTS = {}
# Spots that move (build_bricks.apply_spot_moves; icon_spec.py's SPOT_MOVES has the fields; snapped on NAVMESH). Stash
# Spawnable_15 (9a5fcfbb55e00d63, availableStash16) stands at (-64.82, 78.07, -0.53) with its open side (local -Y, yaw
# -45) toward the morgue table ea_flu puts beside it (Hospital_Morgue_Table_A 3ad4c4e04f30cef1, 1.16 m, with a donor
# body on it): the outbreak's navmesh has no floor in front of it (0.00 m; 3.00 m under Freelancer's), nor on its half
# turn, and its quarter turns do not fit (the crate check, build_bricks.check_crates). It moves 2.5 m south
# into the open floor of the same room: clearance 1.4 m, 3 m of floor in front on its vanilla rotation, no act within
# 2 m (mission_flu's Act_MR_Stand_Clipboard02 f1055439c01d8406, 2.9 m, the anchor).
SPOT_MOVES = {
    0x9A5FCFBB55E00D63: dict(kind="stash", to=(-64.82, 75.57, -0.53), anchor=0xF1055439C01D8406,
                             why="Stash Spawnable_15 faced ea_flu's morgue table -> 2.5 m south, open floor"),
}
REACH_EXCEPTIONS = {}
# A kept clue on high furniture: navmesh floor within the spec's height below it (in plan within KEPT_FLOOR_M) instead
# of build_bricks' usual 1.5 m:   {entity id: (meters below, why)}
KEPT_RAISED = {
    0x4561C701EDEA91F8: (2.0, "Safe_Unique_04's clue Laptop_Unique_02 (-81, 30, -3), in the staff quarters, on a "
                              "shelf 1.6 m above the floor (floor 1.0 m away in plan)"),
}
# ea_flu's keycard doors (RFID): Freelancer spots within KEYCARD_NEAR_M of one on its floor are listed in the audit
# (an in-game check: is the spot on the open side, or is a keycard reachable?).
KEYCARD_DOORS = [
    (EA_FLU[0], 0xF0F666DD09C6051A, "DoubleDoor_Hokkaido_D_Tall_RFID (-90, 67, 4), the hospital's upper floor"),
    (EA_FLU[0], 0xB9392CC5106FA3D3, "DoubleDoor_Hokkaido_D_Tall_RFID (-67, 87, 4), the intensive care ward"),
]
KEYCARD_NEAR_M = 12.0

# override_shogi's five exits ("Any exit used" in the mission brick) all stay: each is on the outbreak's navmesh, near
# its people's acts and outside its blockers: the garage (-69, -67, -9), the north path (-18, 47, -7), the cliff top
# (-11, 101, 24) and the helipad's two (-108, 86, 4 and -108, 89, 4).
EXIT_MOVES = {}
UNMOVED_EXITS = [0x88C6DBAC409FFE6A, 0x1464774A68528B34, 0xA64BA5D053FD8C7E, 0x26B240F303F218F7, 0x55BD6CCDAD588E93]
DELETE_EXITS = {}
MISSION_ANY_EXIT = 0xD6555539BC2D5732  # mission_mild_shogi's "Any exit used"
DELETE_BASE_EXITS = []   # mission_flu has no mission exit of its own
DELETE_OVERRIDE = []     # override_shogi's dramas name no hotel person mission_flu deletes that matters

# --- Starting locations (hero spawns) --------------------------------------------------------------------------
#
# Under the swap: mission_flu's HeroSpawn_Flu (Patient Zero's start at the helipad, entrance faf13436, the default:
# mission_flu sets agencymarkup_snowcrane's HeroSpawn_PatientRoom's m_bDefaultHeroSpawn false) and
# agencymarkup_snowcrane's nine (Freelancer's starts; their intro sequences are gone with SnowCrane_StartingPoints,
# which mission_flu deletes). Most of the nine hang off an entity of location or npc_mission_snowcrane (a bed, a
# chair): their places need those bricks' JSON (PARENT_BRICKS; the second Deck read); a start whose parent brick
# is not read is not checked, with a warning.
DEFAULT_HERO_SPAWN = 0x896C372A50983183  # mission_flu HeroSpawn_Flu (-109, 96, -0.5)
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189
HERO_SPAWN_REQUIRED = [MISSION_FLU[0], AGENCYMARKUP_LOCATION]
HERO_SPAWN_FORBIDDEN = []
PARENT_BRICKS = [("location", (LOCATION, LOCATION_TBLU)), ("npc_mission_snowcrane", NPC_MISSION_SNOWCRANE)]
BLOCKER_BRICKS = [(EA_FLU, "solid"), (MISSION_FLU, "solid")]
SPOT_BLOCKER_CHECK = True
DELETE_HERO_SPAWNS = {}
HERO_SPAWN_EXCEPTIONS = {}
LOCATION_WORLD_FOLDERS = set()

# --- The package's checks ----------------------------------------------------------------------------------------

# The vanilla resources the bricks are built against (gen_package_inc.py adds each TEMP's blueprint): the scene, the
# outbreak, its story switch and the navmesh places are checked on, the bricks our copies reference by entity id, and
# the Freelancer sources they copy.
FINGERPRINTS = [
    (SCENE, "_scene_flu.entity"),
    (MISSION_FLU[0], "mission_flu.brick"),
    (NAVMESH[0], NAVMESH[1]),
    (CONTRACTCREATION_FLU[0], "contractcreation_flu.brick"),
    (OVERRIDE_CONSTANTJEFF[0], "override_constantjeff.brick"),
    (EA_FLU[0], "ea_flu.brick"),
    (TOD_FLU, "tod_flu.brick"),
    (GFX_FLU, "gfx_flu.brick"),
    (OUTFITS_FLU, "outfits_flu.brick"),
    (EVERGREEN_SETUP, "evergreen_setup.brick"),
    (LOCATION, "location.brick"),
    (MISSION_SNOWCRANE[0], "mission_snowcrane.brick"),
    (NPC_MISSION_SNOWCRANE[0], "npc_mission_snowcrane.brick"),
    (AGENCYMARKUP_LOCATION, "agencymarkup_snowcrane.brick"),
    (SRC_MISSION[0], "mission_mild_shogi.brick"),
    (SRC_OVERRIDE[0], "override_shogi.brick"),
    (SCENE_SNOWCRANE_MILD_SHOGI, "scene_snowcrane_mild_shogi.entity"),
]

# The Deck reads this spec was built from (TEMP and TBLU pairs, plus the navmeshes), chunk 22, vanilla only;
# scripts/build-landslide-pkg.sh --help lists them. The second read's (the last four) are optional for the build:
# without them it warns and skips the checks that need them.
READS = [
    SRC_MISSION, SRC_OVERRIDE, (SCENE_SNOWCRANE_MILD_SHOGI, 0x00EF82CF66F4D7EF), (SCENE, 0x00F8203D8FF20933),
    MISSION_FLU, CONTRACTCREATION_FLU, OVERRIDE_CONSTANTJEFF, (0x00CED47600DD6BD0, 0x00A6559D267DF29E),
    (AGENCYMARKUP_LOCATION, AGENCYMARKUP_LOCATION_TBLU), (OUTFITS_FLU, 0x003B4FF6599E810D), (TOD_FLU,),
    (DIFFICULTY_EASY, 0x0078B43BE11EBA32), (DIFFICULTY_PRO1, 0x00EC296EF416B1B3), (NAVMESH[0],),
    # the second read
    NPC_MISSION_SNOWCRANE, MISSION_SNOWCRANE, (LOCATION, LOCATION_TBLU), (COMPARE_NAVMESH[0],),
]
