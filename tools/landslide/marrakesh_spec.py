"""The full Marrakesh night's Freelancer layer: what build_bricks.py changes in IOI's two Freelancer bricks of mild
Marrakesh so they play on Marrakesh's night level (A House Built on Sand's base).

The swap loads Marrakesh's story-free night scene (_scene_python, whose content is mission_marrakesh_python: the night
bazaar, the shisha café, its crowds, the night navmesh and lighting; IOI plays Contracts mode, The Entertainer and the
Hellebore escalation on it) with one brick instead of the contract's mission_mild_mancala (and its difficulty brick,
which belongs to the day level): our copy of it, fr_mission_mild_marrakesh_night, which in turn loads our copy of
override_mancala, fr_override_marrakesh_night. As for the Sapienza variants (landslide_spec.py), both copies keep every
entity id of the original, so the game's CPD keys and the mod's tables still apply; what changes is listed here, each
item with its reason. Ids are the bricks' sub-entity ids; positions are world coordinates. Both bricks go into their
own package, Runtime/chunk25patch290.rpkg (partition 25, "marrakesh").

How the night differs from Freelancer's day map, from the data:
- The night does not load the day story (mission_marrakesh_spider), its NPCs (npc_mission_spider: the consulate's
  staff and guards, the school's soldiers), the day crowds (npc_ambient_*_day, npc_ambient_plaza_spider) or the
  consulate's and school camp's dressing (dressing_embassy*, dressing_*_spider, agencymarkup_spider).
- Its navmesh (_scene_mission_python.navp, which mission_marrakesh_python sets on location's pathfinder) is half the
  day's: one walkable area over the bazaar, the shisha café, the streets, the plaza and the school's front, x -44..130,
  y -105..63. The consulate (south of y -105) and the school's east wing are closed.
- It keeps location and the three generic crowd bricks (market, streets, shisha), whose pool actors stay where the
  night bricks do not touch them.

How spots were judged: every spot, clue, safe guard, mule act, kept target act, exit and start must have night
navmesh floor within build_bricks.KEPT_FLOOR_M (EXIT_FLOOR_M for exits) on the default start's island (NAVMESH; the
build reads the NAVP from the --raw folders) and be within build_bricks.REACH_M of an act of the night's own NPCs
(mission_marrakesh_python and the two night crowd bricks); no spot or start may be inside a collision box or
pathfinder obstacle of mission_marrakesh_python. build_bricks.py prints that audit (--audit).

Design: from vanilla IOI data only.
"""

MARRAKESH = "assembly:/_pro/scenes/missions/marrakesh/"
NAME = "marrakesh"
DISPLAY = "Marrakesh at night"
MISSION_PATH = MARRAKESH + "fr_mission_mild_marrakesh_night.brick"
OVERRIDE_PATH = MARRAKESH + "fr_override_marrakesh_night.brick"

# Its package: partition 25 ("marrakesh" in packagedefinition), Runtime/chunk25patch290.rpkg.
PARTITION = (25, "marrakesh")

# Vanilla resources (TEMP, TBLU).
SRC_MISSION = (0x009613F820E477D4, 0x00A91EC7439FCD5B)   # mission_mild_mancala
SRC_OVERRIDE = (0x00048E86C447E109, 0x00D68646CD3CE448)  # override_mancala
SRC_NAMES = ("mission_mild_mancala", "override_mancala")
MISSION_PYTHON = (0x00EF1F6E111F6F08, 0x00E5855B85782261)  # mission_marrakesh_python, the night's content
MARKET_NIGHT = (0x00EEA81AEFC6CFA0, 0x00061C2C3CE245E8)    # npc_ambient_market_night
SHISHA_NIGHT = (0x00FD57A20D2FCC99, 0x00DADBE6838885AD)    # npc_ambient_shisha_night

SCENE = 0x00CDA4F4AA40871A  # _scene_python.entity, the scene the swap loads
SCENE_PATH = MARRAKESH + "_scene_python.entity"
BASE = MISSION_PYTHON

LOCATION = 0x00CD6A6795D3CFA7
EVERGREEN_SETUP = 0x006864DBF5FD7CD4
AGENCYMARKUP_LOCATION = 0x0091F30D556BAD3E
AGENCYMARKUP_LOCATION_TBLU = 0x00C982FD80FB7627
AGENCYMARKUP_PYTHON = 0x009AD0950C85E940  # the night's: hides agencymarkup_location's starts (m_bHide)
AGENCYMARKUP_SPIDER = 0x00989505092D32CC  # not loaded at night (override_mancala loaded it): the consulate's starts
MISSION_SPIDER = 0x00A0AF5FC89FF2BE       # the day story, not loaded at night
NPC_MISSION_SPIDER = 0x004868F794D35933   # the day story's NPCs, not loaded at night
MARKET_GENERIC = 0x00A323E8C06468E8
STREETS_GENERIC = 0x00981FCBD3E0F114
SHISHA_GENERIC = 0x008F0F040A328F56
DRESSING_MARKET = 0x0046200EE153A776
DRESSING_SHIHA = 0x00DBAC4206F3F536
DRESSING_STREETS = 0x00583E5802C54F24
SCENE_SPIDER_MILD_MANCALA = 0x00CFB992491F397C  # the contract's scene

# The night's navmesh (_scene_mission_python.navp, 734540 bytes in chunk25patch4): build_bricks.py finds
# *<HASH>.NAVP in its --navp folders (scripts/build-landslide-pkg.sh passes every --raw). Fingerprinted below.
NAVMESH = (0x0072DA52D90FC0AD, "_scene_mission_python.navp")

# Where a spot counts as reachable: near an act of the night's own NPCs (build_bricks.REACH_M), and on the navmesh.
REACH_SOURCES = [("mission_marrakesh_python", MISSION_PYTHON), ("npc_ambient_market_night", MARKET_NIGHT),
                 ("npc_ambient_shisha_night", SHISHA_NIGHT)]
REACH_MIN_ACTS = 300

# --- fr_mission_mild_marrakesh_night (copy of mission_mild_mancala) ---------------------------------------------

# The mission brick's external scenes (mission_mild_mancala's, without the day story's three: mission_marrakesh_spider,
# npc_mission_spider and npc_ambient_plaza_spider), the night base in the day story's place; the night crowd bricks
# follow (pool actors live there).
MISSION_SCENES = [EVERGREEN_SETUP, "OVERRIDE", MISSION_PYTHON[0], MARKET_GENERIC, STREETS_GENERIC, SHISHA_GENERIC,
                  LOCATION, DRESSING_MARKET, DRESSING_SHIHA]
POOL_BRICKS = {"market_night": MARKET_NIGHT[0], "shisha_night": SHISHA_NIGHT[0], "market_generic": MARKET_GENERIC,
               "shisha_generic": SHISHA_GENERIC}

SPAWN_TARGETS = 0x6DD94565ADF91563  # Helper_Evergreen_Spawn_Targets: m_aActors keeps its 46 slots
POOL_SIZE = 46

# The brick's own 20 dedicated targets: these 9 have every act on the night navmesh and within 11 m of a night act
# (the bazaar, the shisha café, the plaza and the square) and keep their slot; the other 11 have acts in the consulate,
# the school camp or the army post (16-98 m from every night act, off the navmesh), or one act off it, and leave the
# pool, staying in the brick, disabled.
KEEP_LOCAL_TARGETS = [
    0x67F01C850DB85E2B,  # MarketVendor
    0xC501B0A0295BEA2D,  # ArmyPatrol
    0xCDF2DBEEB5F1EA56,  # ShishaStaff
    0x6CA2C3A0FB68A8D3,  # ShishaLover
    0x0F7D69D2A8D09A2F,  # Thief
    0x347678AF7384E9A8,  # SpiceGirl
    0x2AF6A206A0AF2476,  # FortuneTeller
    0xFD429140466134CC,  # Beggar
    0x9E0CA66382E1C0A3,  # AngryLady
]
DROP_LOCAL_TARGETS = [
    0xFE7A24486C8342FE,  # Civilian_Amoosing (63-76 m, off the navmesh)
    0x2DA95E005154552D,  # Civilian_Panic (56-81 m)
    0x55AF10D2106BC827,  # ApatheticJanitor (consulate, 56-98 m)
    0xEE75CEECFF5DAB63,  # EvilClerk (consulate: 5 of 7 acts 24-71 m)
    0x2A6762BC34969496,  # ComfortEating (37-79 m)
    0xAEED08A9B5F4666C,  # FitSoldier (school camp, 58-80 m)
    0x53559788C74D733C,  # UndergroundSoldier (4 of 6 acts 24-61 m)
    0xD1A0DEA2F271DD75,  # Officer (4 of 7 acts 19-27 m)
    0xF6DCB932B7D304A7,  # SleepySoldier (32-57 m)
    0xA5C03058F3959E7E,  # ChildishSoldier (16-58 m)
    0xC1D5A79E574DB61E,  # RowdyRiot (11 of 12 acts within 6 m, but Act_MR_Stand_Piss at (31, -93, 0) is 2.5 m off the
                         # navmesh)
]

# Pool actors of the generic crowd bricks that keep their slot: 6 of mission_mild_mancala's 7. The night loads all
# three bricks and none of the night bricks read here deletes, moves or switches off these six. The seventh, Kevin
# Goertzen (npc_ambient_streets_generic 9f031200a4a2dcf9), gives up his slot: dressing_streets_python (not extracted)
# also references npc_ambient_streets_generic, so whether the night keeps him is not known offline.
KEEP_SCENE_TARGETS = [
    ("market_generic", 0xC7A44A993008A4CD),  # Riaasat Hussein
    ("shisha_generic", 0x3D816D869E89EC47),  # Mirna Camirand
    ("shisha_generic", 0xE68D8D6F0DA83AC6),  # Stearne Snelgrove
    ("market_generic", 0x07795E8F2A6B5FF6),  # Faarooq Omer
    ("shisha_generic", 0xFC5256A80E0AF85B),  # Khair al Din Yusef Sarkis
    ("market_generic", 0x8DA51DEEC01B47CA),  # Hasan Halim
]

# The 31 night NPCs that take the other slots (the 11 dropped dedicated targets, Kevin Goertzen's, and the 19 that
# lived in npc_mission_spider and npc_ambient_plaza_spider, which the night does not load), in slot order: (brick,
# entity id, name). No in-game census yet: chosen from the npcactors of mission_marrakesh_python and the two night
# crowd bricks, every one start-enabled and standing on the night navmesh, walkers first: shoppers and tourists of
# the night market, plaza café staff and police, shopkeepers, the carpet shop's guard, the antiques shop, the square's
# and the antiques shop's soldiers, the spice street, the tobacco boss, the shisha café's waiter and guests, and a
# tourist by the school.
# Left out of the pool (the target pool swap may still take some of them; only NEVER_IN_POOL never): Kong Tuo-Kwang and
# Matthieu Mendola (A House Built on Sand's targets), Kong's bodyguard Guang Tseng, the landlord, the fortune teller and
# the snail vendor (scripted), and NPCs with no place of their own (attached to a sequence or a setpiece).
POOL_ADDITIONS = [
    ("market_night", 0x35FB5AA7B96AAD8C, "Megan Banks"),
    ("base", 0xC727E26A0370DE6A, "Hayat Uwaysah Maloof"),
    ("market_night", 0xEF6B81DA3FD4C9C5, "Katie Clements"),
    ("base", 0x40A1CF6A977A0168, "Rizq Harith Shadid"),
    ("market_night", 0x5C71F7346950A99B, "Soren Palmund"),
    ("base", 0x7E97AC700AE902F1, "Mudar Al Tayyib Totah"),
    ("market_night", 0x2C9157CBD6E584DD, "Dahab Yumna Kalb"),
    ("base", 0x405E885D0E6E3E6D, "Yousef Shitrit"),
    ("shisha_night", 0x54DDCA2C669197E4, "Bert Nygaard"),
    ("base", 0x846FAD250A2DE66C, "Fakhiri Sajjad Mustafa"),
    ("market_night", 0x419DDCC5DCC29E70, "Othman Ishaq Baz"),
    ("base", 0xE856AE76E4051821, "Ziyad Jameel Gerges"),
    ("market_night", 0x13A81F008A6556AA, "Carl Johnsen"),
    ("base", 0x45D1FC0E731CEBAF, "Aziz Shamoon"),
    ("market_night", 0x9BF3133597C9A6ED, "Alexander Ege"),
    ("base", 0xDD041606A68E2B4D, "Jett Fairbridge"),
    ("market_night", 0x654FD3CCD44BFDF7, "Jakob Lystbak"),
    ("base", 0x6D3EB5A4A4FD03B3, "Connor Taber"),
    ("shisha_night", 0x60C91C8098E6120B, "Michael Zimmer"),
    ("base", 0xCD89DCD871477898, "Mandhur Fakhir Hadad"),
    ("market_night", 0x434F098B07F5DC77, "Koby Darbyshire"),
    ("base", 0x78D9D8A30A8E65B4, "Azhar Mashhur Aswad"),
    ("market_night", 0x471A28D945ACB456, "Nizar Raji Koury"),
    ("base", 0x5BBB01921A006B00, "Naji Sayyid Baba"),
    ("market_night", 0xDE126AB2091C770A, "Hayley Chomley"),
    ("base", 0x7B8E8D1A84607545, "Adilah 'Adawiyah Botros"),
    ("base", 0xC8633C91687025F2, "Ramzi Baligh Shamon"),
    ("base", 0x6D837C54F7968BD4, "Noorah Saladin"),
    ("base", 0xFA3825C0092ACC63, "Nasir Hamid Kattan"),
    ("base", 0x99AC77576065B544, "Clemens Koch"),
    ("base", 0x0F044BE8A42BF90F, "Mazin Sadaqat Handal"),
]

# A House Built on Sand's two targets never enter the pool (fr::MarrakeshFull().neverTargets too): Kong Tuo-Kwang, the
# Chinese contractor, and Matthieu Mendola, the architect. Both live in mission_marrakesh_python, the night's base.
NEVER_IN_POOL = [
    0x57E229B4E7543CAA,  # Kong Tuo-Kwang (repository id 23415c1d-6ee8-41a8-9975-523f3eede581)
    0x67F6C22407232163,  # Matthieu Mendola (4d0953f0-7f19-4130-abae-610a8f5dddca)
]

# Acts whose item key (the drink they hold) is an entity of npc_mission_spider: re-pointed to mission_marrakesh_python's
# key of the same item (same repository id).
#   (act, property, (old scene, old key), (new scene, new key), why)
_KEY_COFFEE = (MISSION_PYTHON[0], 0x23E6D5EF57A7ED64)  # ItemKey_Poisonable_CoffeCup
_KEY_WATER = (MISSION_PYTHON[0], 0xA7CD5C4387806741)   # ItemKey_Poisonable_Water Bottle
_KEY_WINE = (MISSION_PYTHON[0], 0x210A163646D77BC2)    # ItemKey_Poisonable_RedwineGlass
MISSION_REPOINTS = [
    (0x9E06BE9705BADEAB, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0xAB294AF5767A908B), _KEY_COFFEE,
     "the coffee cup key"),
    (0x4A45357ED8973B99, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0x3BA7DDDE5F0416C8), _KEY_WATER,
     "the water bottle key"),
    (0xA930B791551C3813, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0x5F6F762EE6C68104), _KEY_WATER,
     "the water bottle key"),
    (0xEF8D7D81AAC5927D, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0x3BA7DDDE5F0416C8), _KEY_WATER,
     "the water bottle key"),
    (0x4D986590E822B89F, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0x8CC366C20C2B96C2), _KEY_WINE,
     "the red wine glass key"),
    (0x897A52BC461EFA66, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0xDCC6F88801B837D1), _KEY_WINE,
     "the red wine glass key"),
]

# --- fr_override_marrakesh_night (copy of override_mancala) -------------------------------------------------------

TERRITORY = 0xB5E79F6AE1042212  # Helper_Evergreen_CPD_TerritorySetupRandomizer

# The override brick's external scenes: override_mancala's without the day story's and the consulate's (its deletes,
# property and pin overrides into mission_marrakesh_spider, npc_mission_spider, dressing_school_spider,
# dressing_embassy and agencymarkup_spider go with them), plus the night base (an act's item key).
OVERRIDE_SCENES = [EVERGREEN_SETUP, LOCATION, DRESSING_STREETS, AGENCYMARKUP_LOCATION, MISSION_PYTHON[0]]
# Those overrides go before anything is deleted (some name the deleted exits' volumes).
DROP_SCENES_FIRST = True

OVERRIDE_REPOINTS = [
    (0x9E97972364A0F113, "m_rRepositoryKeyToFind", (NPC_MISSION_SPIDER, 0x318F7CD312231ABD), _KEY_WATER,
     "the water bottle key"),
]

# Freelancer's own navmesh: override_mancala sets location's pathfinder to scene_spider_hot_mancala.navp and its AI
# grid (the day map with Freelancer's changes). At night it would fight the night's own, which
# mission_marrakesh_python sets on the same two entities: our copy leaves both alone, and no longer references the two
# resources (so the day navmesh is not loaded either).
REMOVE_PROPERTY_OVERRIDES = [
    (LOCATION, 0xC71DCD0AA0EFC0C7, "m_NavpowerResourceID",
     "PathfinderConfiguration: Freelancer's day navmesh; the night's (mission_marrakesh_python) stays"),
    (LOCATION, 0x35E6837AFF8A599C, "m_pGrid", "ReasoningGridConfig: Freelancer's day AI grid; the night's stays"),
]
DROP_REFS = [
    (0x00DD58F010FF8035, "scene_spider_hot_mancala.navp, Freelancer's day navmesh"),
    (0x0072CD9E3B974338, "scene_spider_hot_mancala_scene.airg, its AI grid"),
]

# Entities filed under an entity of a brick the night does not load (mission_marrakesh_spider's exit folder): the
# kept Streets gate exit's volume.
OVERRIDE_REPARENT = [
    (0xCBDB679CC1AB2F1C, "VolumeBox_Exit_Market_03, the volume of Exit_Evergreen_PushButton_01 (kept)"),
]

# Deleted with everything under them: the two deleted exits' volumes and cutscene origin (they hang off those exits),
# and Freelancer's three extra item spawners (an emetic grenade, a letterbomb and its detonator) in the school's head
# master's office (131, -22, 5) and the consulate's ambassador's office (24, -110, 9), both closed at night, with their
# item keys (filed under npc_mission_spider).
DELETE_OVERRIDE = [
    (0x81A9585B2A191EB5, "VolumeBox_Exit_ArmyTruck_01"),
    (0x39693EF6822B6117, "SeqOrig_APV_exit_01"),
    (0xEE4C85D1D6FD19AA, "VolumeBox_Exit_EmbassyCar_01"),
    (0x254CF131929E33F8, "ItemSpawner 2"),
    (0x16DFABD72AD8AC1C, "ItemSpawner"),
    (0x3A5E6B3FCDE1F289, "ItemSpawner"),
    (0x2C45BBAD0C4C67F0, "ItemKey__Grenade_ Emetic"),
    (0xFC7BF31454A8DAB2, "ItemKey_Detonator Letterbomb"),
    (0x5D593AA29B2813BA, "ItemKey_Letterbomb Remote Explosive"),
]

# Spots the night cannot reach (no night navmesh floor near them: the consulate, the school's east wing and the
# army post beyond it). The slot (its CPD index) keeps meaning a usable place: it now names a reachable spot of the
# same kind. The unreachable spot entities stay (no slot spawns them).
#   slot property: (old entity, new entity, why)
SLOT_REPOINTS = {
    "availableSupplier01": (0x188BF239675D297F, 0x961B687BEBFDF786,
                            "Helper_Evergreen_SupplierPackage (-26, -173, 2), the consulate -> SupplierPackage_01 "
                            "(-37, -3, 0), by the marketplace gate"),
    "availableSafe04": (0x53BD1170EBF10820, 0x16D02671904D86B6,
                        "Safe_Unique_04 (133, -24, 4), the school -> Safe_Unique_03 (84, -6, 10)"),
    "availableSafe05": (0x8A482BCB0F48779B, 0x16D02671904D86B6,
                        "Safe_Unique_05 (92, -40, 0), a closed room 2 m off the navmesh -> Safe_Unique_03"),
    "availableSafe06": (0x7758C7A71324826E, 0xE118DF5EABBBC457,
                        "Safe_Unique_08 (-22, -107, 0), the consulate -> Safe_Unique_01 (17, -42, 0)"),
    "availableSafe07": (0xE5225CD50C33B5E7, 0xE118DF5EABBBC457,
                        "Safe_Unique_06 (24, -113, 7), the consulate -> Safe_Unique_01"),
    "availableSafe08": (0x2EC3ED111E544DF0, 0x67A412864B27488E,
                        "Safe_Unique_07 (-6, -152, -4), the consulate basement -> Safe_Unique_02 (14, 42, 0)"),
    "availableStash06": (0xED1CA6C425BD3CB0, 0x6FCCCCBE01D21A2D,
                         "Stash_06 (97, 3, 12), a roof 6 m off the navmesh -> Stash_05 (82, 11, 0)"),
    "availableStash07": (0x95A5CC36A8AEDBB3, 0x0C1277969833152D,
                         "Stash_07 (162, 12, -1), the school -> Stash_04 (33, 23, 0)"),
    "availableStash08": (0xE6A90DB16D8ABB3A, 0x2280C2ECDB1D96D7,
                         "Stash_08 (168, -32, 4), the school -> Stash_10 (69, -64, 0)"),
    "availableStash09": (0x07C97854F93252CD, 0x45D8E80161F979B3,
                         "Stash_09 (178, -46, -1), the school -> Stash_11 (53, -93, 0)"),
    "availableStash12": (0x25439B25E5575399, 0xC65398A773175F29,
                         "Stash_12 (9, -95, 0), the consulate's front, 4 m off the navmesh -> Stash_01 (-26, -38, 12)"),
    "availableStash13": (0x3B8BEA1A23E26DD9, 0x3361127139292D9D,
                         "Stash_13 (61, -90, -12), a cellar -> Stash_02 (-38, 8, 0)"),
    "availableStash14": (0x41BA395CB98F4461, 0x634D3445BE943DFA,
                         "Stash_14 (5, -161, -4), the consulate basement -> Stash_03 (-30, 37, 0)"),
    "availableStash15": (0xCF9B3AA45176E4AE, 0x2280C2ECDB1D96D7,
                         "Stash_15 (-27, -139, 7), the consulate -> Stash_10"),
    "availableStash16": (0x44D753CD9380CF30, 0x45D8E80161F979B3,
                         "Stash_16 (-27, -180, 2), the consulate -> Stash_11"),
    "availableMule01": (0x30B1F0E0F24B506F, 0x8B42468348725CA0,
                        "MoneyMuleSetup_Unique (26, -140, 2), the consulate -> MoneyMuleSetup_Unique_01 (-21, -51, 0)"),
    "availableMule07": (0xBFE527131361967B, 0x0B7CE280A2505B78,
                        "MoneyMuleSetup_Unique_07 (119, -23, -1): one of its three acts (Act_MR_Guard_Stand_ArmsBack, "
                        "147, -14, -1) is in the school -> MoneyMuleSetup_Unique_05 (6, 52, 0)"),
    "availableMule08": (0x6B3FCEB5836C5EF5, 0x3048D498C5D6408D,
                        "MoneyMuleSetup_Unique_06 (-2, -113, 7), the consulate -> MoneyMuleSetup_Unique_02 "
                        "(-13, -30, 0)"),
}

# Spawnable spots farther than build_bricks.REACH_M from the night's acts that are judged reachable anyway: each
# still has night navmesh floor under it (checked; the night navmesh is one walkable area), on an upper floor where
# none of the night's NPCs stands.
_UPSTAIRS = "on the night navmesh, upstairs in the house east of the square (no night NPC stands there)"
REACH_EXCEPTIONS = {
    0x16D02671904D86B6: "Safe_Unique_03 (84, -6, 10), 21 m: " + _UPSTAIRS,
    0x9D8984A12B827479: "Safe_Unique_03's clue Documents_Unique_01 (90, 0, 11), 21 m: " + _UPSTAIRS,
    0x297E48E7BF23587F: "Safe_Unique_03's clue Tablet_Unique_15 (86, -4, 5), 15 m: " + _UPSTAIRS,
    0xFC8C8C67C67CC170: "Safe_Unique_03's clue Tablet_Unique_03 (87, 5, 11), 24 m: " + _UPSTAIRS,
    0xDC2B1F97075A7DA5: "Safe_Unique_03's clue Tablet_Unique_05 (90, -10, 10), 18 m: " + _UPSTAIRS,
    0x84FC34683664CAFA: "MoneyMuleSetup_Unique_01's act Act_FR_Stand_Phone_Text_Pacing (-31, -55, 0), 16 m: on the "
                        "night navmesh, in the street west of the plaza",
}

# Freelancer's four exits ("Any exit used" in the mission brick). Under the night:
# - Exit_Evergreen_PushButton (the marketplace gate, -42, -2, 0) and Exit_Evergreen_PushButton_01 (the streets gate,
#   76, 45, 0) stay: their exit sequences (location 91cfc2122a263a8a, dc3309c54cbc7d57) are location's, and no night
#   brick deletes them. These two gates are the night's own exits too (location's Marketplace_Gate and Streets_Gate,
#   which override_mancala already deletes in favor of the buttons).
# - Exit_Evergreen_UseKey (the army APV, 119, -44, 0) is deleted with its pin overrides and leaves "Any exit used":
#   its exit sequence and its key are mission_marrakesh_spider's (the day story), which the night does not load, and
#   the night has no exit of its own to move it to.
# - Exit_Evergreen_UseKey_01 (the consulate car, -11, -141, -4) is deleted likewise: the consulate is closed and its
#   sequence and key are the day story's too.
EXIT_MOVES = {}
UNMOVED_EXITS = [0xCA59D5794141000B, 0x227B000AF5DD552B]
DELETE_EXITS = {
    0xF6BEE40262DA7FE9: "Exit_Evergreen_UseKey (the army APV, 119, -44, 0): its exit sequence and key are the day "
                        "story's (mission_marrakesh_spider), which the night does not load",
    0xC1108923F33E008E: "Exit_Evergreen_UseKey_01 (the consulate car, -11, -141, -4): the consulate is closed at night, "
                        "and its sequence and key are the day story's",
}
MISSION_ANY_EXIT = 0x81233E3F0E44959C  # mission_mild_mancala's "Any exit used"
DELETE_BASE_EXITS = []  # the night's exits are location's two gates, which override_mancala already deletes

# --- Starting locations (hero spawns) --------------------------------------------------------------------------
#
# Under the swap the scene is _scene_python plus our two bricks; build_bricks.py walks the reference tables of
# everything that loads and finds hero spawns in mission_marrakesh_python (HeroSpawn, the night's own start at the
# plaza and the default: entrance a20e45b9) and agencymarkup_location (four; agencymarkup_python hides all of them,
# m_bHide: untested whether a hidden start can still be picked; if not, the default plays). agencymarkup_spider (the
# consulate's and the school's starts) and mission_marrakesh_spider (the bazaar entrance) are not loaded, so an
# entrance id of theirs lands on the default through the mod's runtime start fix (src/mod/HeroStart.h; the game does
# NOT fall back to the default itself, observed in game).
DEFAULT_HERO_SPAWN = 0xDFCF4632F0362256  # mission_marrakesh_python HeroSpawn (13, -72, 0)
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189
HERO_SPAWN_REQUIRED = [MISSION_PYTHON[0], AGENCYMARKUP_LOCATION, AGENCYMARKUP_PYTHON]
HERO_SPAWN_FORBIDDEN = [AGENCYMARKUP_SPIDER, MISSION_SPIDER]
# mission_marrakesh_python's collision boxes and pathfinder obstacles ("solid": its pathfinder boxes are navmesh
# generation hints).
BLOCKER_BRICKS = [(MISSION_PYTHON, "solid")]
SPOT_BLOCKER_CHECK = True

DELETE_HERO_SPAWNS = {
    AGENCYMARKUP_LOCATION: [
        (0x0BAFC27F84D6B473, "HeroSpawn_SniperRoofHandyman",
         "(-32, -20, 12): a roof with no night navmesh floor"),
        (0x2E67C9127B9127FE, "HeroSpawnSequenceDirector", "the SniperRoofHandyman intro's sequence director"),
    ],
}
HERO_SPAWN_EXCEPTIONS = {}
LOCATION_WORLD_FOLDERS = set()

# --- The package's checks ----------------------------------------------------------------------------------------

# The vanilla resources the night's bricks were built against (gen_package_inc.py adds each TEMP's blueprint): the
# scene, its base and the navmesh the places were checked on, the bricks our copies reference by entity id, and the
# Freelancer sources they copy.
FINGERPRINTS = [
    (SCENE, "_scene_python.entity"),
    (MISSION_PYTHON[0], "mission_marrakesh_python.brick"),
    (NAVMESH[0], NAVMESH[1]),
    (MARKET_NIGHT[0], "npc_ambient_market_night.brick"),
    (SHISHA_NIGHT[0], "npc_ambient_shisha_night.brick"),
    (AGENCYMARKUP_PYTHON, "agencymarkup_python.brick"),
    (EVERGREEN_SETUP, "evergreen_setup.brick"),
    (LOCATION, "location.brick"),
    (AGENCYMARKUP_LOCATION, "agencymarkup_location.brick"),
    (MARKET_GENERIC, "npc_ambient_market_generic.brick"),
    (STREETS_GENERIC, "npc_ambient_streets_generic.brick"),
    (SHISHA_GENERIC, "npc_ambient_shisha_generic.brick"),
    (DRESSING_MARKET, "dressing_market.brick"),
    (DRESSING_SHIHA, "dressing_shiha.brick"),
    (DRESSING_STREETS, "dressing_streets.brick"),
    (SRC_MISSION[0], "mission_mild_mancala.brick"),
    (SRC_OVERRIDE[0], "override_mancala.brick"),
    (SCENE_SPIDER_MILD_MANCALA, "scene_spider_mild_mancala.entity"),
]

# Not copied: scene_spider_mild_mancala's glue entities (as for Sapienza's scene_costaltown_mild_bingo); _scene_python
# is used as it is.
