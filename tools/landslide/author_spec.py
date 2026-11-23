"""The full Author's Freelancer layer: what build_bricks.py changes in IOI's two Freelancer bricks of mild Sapienza so
they play on The Author's own scene.

The swap loads The Author's scene (scene_ebola, whose content is scenario_ebola: Craig Black's book signing by the
church, at night) with one brick instead of the contract's mission_mild_bingo: our copy of it, fr_mission_mild_author,
which in turn loads our copy of override_bingo, fr_override_author. As for the full Landslide (landslide_spec.py),
both copies keep every entity id of the original, so the game's CPD keys (MildBingo_*) and the mod's tables still
apply; what changes is listed here, each item with its reason. Ids are the bricks' sub-entity ids; positions are world
coordinates.

How The Author differs from Landslide, from the data:
- scenario_ebola keeps Freelancer Sapienza's 15 day crowd bricks (npc_ambient_*_day), location, agencymarkup_location
  and the same blockedoff_mansion that closes Villa Caruso and its caves under Landslide; it has no story brick of its
  own beyond itself (no mission01, no npc_mission_octopus: those are what our copies stop loading).
- It deletes 340 entities of the day crowd bricks (127 in npc_ambient_beachsquare_day alone: a quieter night town) and
  594 of location, among them the cave seaplane exit (647d5b5203ea01ea) and the ruins boat's exit sequence
  (332b67e5fc48813a). Its own exits are location's: the harbor speedboat, the road (car) and the old-town gate, which
  it moves to (62, 67, 7), the place Landslide uses too ("Any exit activated" reads those three).
- Its 56 NPCs (scenario_ebola npcactors): Craig Black and his entourage, Brother Akram and his cult guards, the
  reading's audience, waiters, church staff, a photo crew and about twenty bodyguards.

How spots were judged: scenario_ebola's own NPCs stand at 253 acts, which cover the church, the square and the town
hall but not every street. So a spot counts as reachable when it is near an act of scenario_ebola (direct evidence
that The Author's navmesh reaches it), of the day crowd (REACH_CROWD: the 15 day crowd bricks' acts that The Author
keeps, 479 of 621, leaving out those it or blockedoff_mansion deletes, themselves or through a parent:
also direct evidence, as The Author's own townspeople stand there) or of mission_mamba: Landslide's scene closes the
mansion block with the very same blockedoff_mansion and nothing else of the town that The Author opens, so its 836
acts stand for "outside the closed mansion". The audit names the first of the three each spot is near. The day crowd
bricks are optional reads (their TEMP and TBLU in a --raw folder): without them the build says so and judges by the
other two, and the kept scene actors' names (KEEP_SCENE_NAMES) are checked only when they are there. On top of that
no spot or start may be inside a collision box or pathfinder obstacle of scenario_ebola (its door blockers, the
church tower, the graveyard obstacles) or blockedoff_mansion. build_bricks.py prints that audit (--audit).
"""

COASTALTOWN = "assembly:/_pro/scenes/missions/coastaltown/"
NAME = "author"
DISPLAY = "The Author"
MISSION_PATH = COASTALTOWN + "fr_mission_mild_author.brick"
OVERRIDE_PATH = COASTALTOWN + "fr_override_author.brick"

# Vanilla resources (TEMP, TBLU).
MISSION_MILD_BINGO = (0x00D15B6A8159D00D, 0x00F23DCB39B5FD0D)
OVERRIDE_BINGO = (0x00AC32D2CCC449A7, 0x00C63AC62C8E65C9)
SCENARIO_EBOLA = (0x00F84465C5960E4B, 0x000468209FECFCEE)
MISSION_MAMBA = (0x006026A87FB981F5, 0x000A254D361DC059)  # only as a reach reference (see the docstring)

SCENE = 0x00E35B73AB0A3FCD  # scene_ebola.entity, the scene the swap loads (its one entity loads scenario_ebola)
SCENE_PATH = COASTALTOWN + "scene_ebola.entity"
BASE = SCENARIO_EBOLA

# The Author's navmesh (scene_ebola.navp, 1162620 bytes in chunk26patch4), for its one moved spot (SPOT_MOVES) and the
# floor checks of the places that stay, and Freelancer's (mission01.navp, the one scene_costaltown_mild_bingo loads),
# for the crate check of the places that stay (build_bricks.check_crates: a vanilla supply crate The Author's dressing
# leaves under placement.CRATE_FRONT_M of floor in front, with that much under Freelancer's, fails the build).
# build_bricks.py finds *<HASH>.NAVP in its --navp and --bin folders; both are in build-landslide/audit/sapienza
# (landslide_audit.py --missing). The Author's is fingerprinted below: the moved spot depends on it.
NAVMESH = (0x00B7A403A6517E2A, "scene_ebola.navp")
FREELANCER_NAVMESH = (0x008980F97BE736B2, "mission01.navp")

LOCATION = 0x00E9F09C3B030590
EVERGREEN_SETUP = 0x006864DBF5FD7CD4
AGENCYMARKUP_LOCATION = 0x00B6D38769189EA2
AGENCYMARKUP_LOCATION_TBLU = 0x0025F7D01875A6CA
AGENCYMARKUP_OCTOPUS = 0x00199C55C9BC50D6  # not loaded under The Author (override_bingo loaded it)
BLOCKEDOFF_MANSION = (0x0085BEC4E9BBCCF9, 0x007129A21F0B0DB1)

# Freelancer Sapienza's day crowd bricks; scenario_ebola loads all 15, and both Freelancer bricks reference them (pool
# actors, the override's deletes and property overrides), so they stay external scenes of our copies.
DAY = {
    "bakery": 0x002F6CE381654804,
    "beach": 0x001BD8E79D29BB17,
    "beachshop": 0x009EA6DF9138FEB4,
    "beachsquare": 0x003D280D592C5059,
    "cafe": 0x00A6BAA96FA6CCF0,
    "ceramicsstore": 0x00216649976CD8C7,
    "graveyard": 0x0014DABDACAFB70C,
    "mainsquare": 0x0082AC5F977077A0,
    "morgue": 0x007BD2ECA943A0D1,
    "pier": 0x007C7C060444F32D,
    "sewers": 0x00818FD61C6F798D,
    "smallshops": 0x00DD9B6FC3AD67C8,
    "streets": 0x00BF51EF1CC61BF4,
    "town_apartments": 0x00916FCA49E7A124,
    "townhall": 0x005981882CF928A6,
}
DAY_ORDER = ["bakery", "beach", "beachshop", "beachsquare", "cafe", "ceramicsstore", "graveyard", "mainsquare",
             "morgue", "pier", "sewers", "smallshops", "streets", "town_apartments", "townhall"]

# Where a spot counts as reachable: near an act of one of these bricks (build_bricks.REACH_M), the base first.
REACH_SOURCES = [("scenario_ebola", SCENARIO_EBOLA), ("mission_mamba", MISSION_MAMBA)]
REACH_MIN_ACTS = 1000  # both together; either alone would be too few to stand for the town
# The day crowd's acts, between the two (build_bricks.crowd_reach; optional reads, see the docstring). Checked
# with the bricks extracted: of the 15 spots, exits, starts and kept targets that were reachable only via
# mission_mamba's acts, 13 now have a day crowd act within reach; the canteen-roof stash (c28246124b27b84a) and the
# Cafe Tower start, both on the café roof (z 33), still rest on mission_mamba's acts there.
REACH_CROWD = ("the day crowd", DAY)

# --- fr_mission_mild_author (copy of mission_mild_bingo) ---------------------------------------------------------

# The mission brick's external scenes (before the bricks pool actors come from): location, the Freelancer setup, our
# override brick ("OVERRIDE"), the base, then the day crowd bricks (the kept pool actors live there).
MISSION_SCENES = [LOCATION, EVERGREEN_SETUP, "OVERRIDE", SCENARIO_EBOLA[0]] + [DAY[n] for n in DAY_ORDER]

SPAWN_TARGETS = 0x98CA7BAED06A1211  # Helper_Evergreen_Spawn_Targets: m_aActors keeps its 59 slots

# The brick's own 21 dedicated targets: the same 9 as under Landslide keep their slot (every act within 15 m of
# scenario_ebola's or mission_mamba's acts); the other 12 have acts inside the closed mansion block (blockedoff_mansion,
# as under Landslide: 22-132 m from both scenes' acts) and leave the pool, staying in the brick, disabled.
KEEP_LOCAL_TARGETS = [
    0x913F4F2C2DF2FEB7,  # Martina Spagna
    0xA0A21A86D45CA1D1,  # Rosita Masi
    0x579753E9E5091542,  # Miriam Tasca
    0x484C149BBBF34747,  # Egeo Carini
    0xA7337D652D8E1478,  # Beltramo Ruisi
    0x8A73C205C4795263,  # Zeno Pasquarelli
    0x6AFA1C50E26198B4,  # Dom Bonano
    0x578582BDCD3C4407,  # Massimo Dugo
    0x4BF4C25D1A6651AE,  # Gregorio Fricano
]
DROP_LOCAL_TARGETS = [
    0xFD6E378953306ABA,  # Tristano Fiorini
    0x3CCC05C0250A9972,  # Efrem Paolini
    0x753E250C0D7CE218,  # Paulo Coletta, mansion guard
    0xB210DCA38ACC9B1A,  # Alessandro Malbera, mansion guard
    0x3F1FCA22325F2AF5,  # Monica Camara, lab technician
    0x05ACF075E3BA7603,  # Iolanda Baldacci, lab technician
    0xEAE902AF0AFC8C4B,  # Rino Triano, lab technician
    0x7590B4599EFD4704,  # Imelda Meccia, mansion staff
    0x1D4EE2599E324045,  # Gisella Bartoletii, mansion staff
    0x2CFE39EDDB8A52EF,  # Otto Agosta, mansion staff
    0x155BB4BABFE2D68C,  # Elmo Barilla, mansion staff
    0x2CA3F83B0F2E97A9,  # Gustavo Paci, mansion staff
]

# Pool actors in the day crowd bricks that keep their slot: the 9 of mission_mild_bingo's 16 that scenario_ebola does
# not delete (three of them it moves: b43d59c9b2d47f03, adb3033a8a5dcf9c, 6b59153df63484c3; they stay its NPCs).
# The other 7 (278ff646f512b53b graveyard, 04e8beb0f47c8dff pier, 24d63639d2bb4d31 cafe, 10cec2c65f72fee8
# mainsquare, 48889f4bdcf8f346 / c5900c4507df86d1 / 0dbbc35ec8949506 beachsquare) are deleted by The Author;
# build_bricks.py refuses a kept one that a loaded brick deletes.
# Their names (m_sActorName), checked against the day crowd bricks when they are read (build_bricks.crowd_reach): each
# is an npcactor there that no loaded brick deletes.
KEEP_SCENE_NAMES = {
    0x825A733C81C3716A: "Paolo Fellegara",
    0x376149E1C0B42105: "Torres Piombo",
    0x70F4EE0223221360: "Orlando Siciliano",
    0xEE847883F9AA15C6: "Tommaso Selvaggio",
    0x5426D6555B47959F: "Davide Di Meo",
    0xB43D59C9B2D47F03: "Rinaldo Cattaneo",
    0xADB3033A8A5DCF9C: "Gina Bellucci",
    0x750E5D96306D9353: "Maria Delaurentis",
    0x6B59153DF63484C3: "Marco Vienna",
}
KEEP_SCENE_TARGETS = [
    ("smallshops", 0x825A733C81C3716A),
    ("townhall", 0x376149E1C0B42105),
    ("mainsquare", 0x70F4EE0223221360),
    ("beachsquare", 0xEE847883F9AA15C6),
    ("cafe", 0x5426D6555B47959F),
    ("beachsquare", 0xB43D59C9B2D47F03),
    ("beachsquare", 0xADB3033A8A5DCF9C),
    ("mainsquare", 0x750E5D96306D9353),
    ("beachsquare", 0x6B59153DF63484C3),
]

# The 41 Author NPCs that take the other slots (the 12 dropped dedicated targets, the 7 deleted day actors, and the
# 22 that lived in npc_mission_octopus and octopus_pro1, which our copy no longer loads), in slot order: (brick, entity
# id, name), all in scenario_ebola. Chosen from its npcactors, every one start-enabled, no in-game census yet: the
# reading's audience, the café's critic and waiters, the photo crew, church staff, the lawyer at the town hall and his
# bodyguard, and 14 of the bodyguards about the church and the square.
# Left out of the pool (the target pool swap may still take some of them; only Craig Black is never a target): Craig
# Black (The Author's target, NEVER_IN_POOL), Brother Akram (The Author's other target: the virus sample and his own
# evacuation), Craig's manager Catherine Lang, his bodyguard James Wolfe (an escort that follows him) and his fan James
# King, the photographer Lorenzo Venturi (the photo opportunity), Jeff, Brother Akram's four cult guards, the two
# guards up the church tower (Douglas Tucker at 28 m and Fabrizio Biondi at 10 m above the square, behind the tower's
# collision boxes), and the plumbers Mario and Luigi Saltatore in the sewers.
POOL_ADDITIONS = [
    ("base", 0xE53048D16A2CF91E, "Costanzo Baresi"),
    ("base", 0x18C4E92C99C8861B, "Mitchell Melton"),
    ("base", 0xA31B4D241E462F1E, "Chelsey Parker"),
    ("base", 0xDCA878D506ABD12F, "Leah Livingston"),
    ("base", 0x5618B98A47B679D5, "Milena Fiorentino"),
    ("base", 0x25360AD604716D91, "Ernest Maddox"),
    ("base", 0x5BB0C43C67238E37, "Gregory Walker"),
    ("base", 0x4BA1E516C4A62148, "Basilio Genovesi"),
    ("base", 0x4D15C3C3C7D64492, "Valentino Sagese"),
    ("base", 0x367DF8A90CB8F0E6, "Enzo Angelo"),
    ("base", 0x99BD91CCF6352EC2, "Daria Boni"),
    ("base", 0xBF927239C9304A80, "Vittore Bianchi"),
    ("base", 0x132C4D3BBBF74E7E, "Piero Milani"),
    ("base", 0x900CE5A8AED0C2E2, "David Warren"),
    ("base", 0xF7C056A95B7A6D83, "Franco Luoni"),
    ("base", 0x721C10E2130A9B4F, "Marcello Schiavone"),
    ("base", 0x17D22D2CB9DE7F2E, "Benedetto Schiavone"),
    ("base", 0xAA12FDC9459C7614, "Hunter McDowell"),
    ("base", 0xFFB2D56C917069CD, "Sofia Bergamaschi"),
    ("base", 0x7F015D8AA4E25BF4, "Lorenzo Nucci"),
    ("base", 0xE3100867139A30FB, "Lanfranco Li Fonti"),
    ("base", 0xA18C17C2F3650A02, "Erik Carson"),
    ("base", 0x82F52FDEB5639B00, "Muzio Lori"),
    ("base", 0x5BBE9E328F6C84A9, "Ettore De Marco"),
    ("base", 0x2800D6A34DF78EA4, "Davide Rizzo"),
    ("base", 0x15E50AA11A3CE08E, "Gastone Davide"),
    ("base", 0x72A58FDC4349A688, "Lavinia Di Traglia"),
    ("base", 0xB60BF8D6E8DA3BD2, "Riccardo Beneventi"),
    ("base", 0x0A3307BC8B789CD6, "Mark Barrett"),
    ("base", 0x0FA8EFCA25D0FF04, "Robert Lester"),
    ("base", 0x778C1A60F9026544, "Alessandro Zito"),
    ("base", 0x03FFEB89F5393FCA, "Beppe Calabresi"),
    ("base", 0xE128FB79319288EB, "Pietro Mazzi"),
    ("base", 0x664F4054565565E2, "Brady Grant"),
    ("base", 0xC568C53CF47E64B6, "Salvatore Bravuomo"),
    ("base", 0x23BA422C7A581BD2, "William Pearce"),
    ("base", 0x3A2F35814788EE0E, "Marco Pizzaiolo"),
    ("base", 0x3A033CFE230061E7, "Frank Wilcox"),
    ("base", 0x1E2B62AF8A5A557D, "Dario Buccio"),
    ("base", 0xB9E81471E543CC20, "Cesare Alessi"),
    ("base", 0x4F903D4028BFA50C, "Raymond Owen"),
]

# Craig Black never enters the pool (fr::AuthorNeverTargets: The Author's own target, whose story ends in his escape,
# Event_Target_Escape after "Contemplating moon").
NEVER_IN_POOL = [0xC7CB1D860F5AB7BE]

# --- fr_override_author (copy of override_bingo) -----------------------------------------------------------------

TERRITORY = 0x8926D401C9249780  # Helper_Evergreen_CPD_TerritorySetupRandomizer

# The override brick's external scenes: what is left of override_bingo's once octopus_pro1, mission01,
# agencymarkup_octopus, dressing_caves_biolab, mission_guidance and npc_mission_octopus go (their deletes and overrides
# go with them), in the same order.
OVERRIDE_SCENES = [LOCATION, EVERGREEN_SETUP] + [DAY[n] for n in ("cafe", "bakery", "beach", "beachshop",
                                                                   "beachsquare", "ceramicsstore", "graveyard",
                                                                   "mainsquare", "morgue", "pier", "sewers",
                                                                   "smallshops", "streets", "town_apartments",
                                                                   "townhall")] + [AGENCYMARKUP_LOCATION,
                                                                                   SCENARIO_EBOLA[0]]
# (scenario_ebola last: for DELETE_SCENE_PROPS.)

# The Author's props that stand where a Freelancer spot spawns, deleted by our override brick (build_bricks
# delete_scene_props, as for the full Landslide; tools/landslide/landslide_audit.py runs the clash audit). Deleted, not moved: the spots are Freelancer's design, and these are
# decoration (nothing of scenario_ebola, of our bricks or of anything else loaded names them; nothing stands on them).
#   brick: [(entity id, name, why)]
DELETE_SCENE_PROPS = {
    SCENARIO_EBOLA[0]: [
        (0xF1441CF7494072CC, "Bed_Masters_Italy_C_00",
         "(15.95, 14.34, 16.74), the double bed The Author puts in the delivery man's room: 0.62 m from "
         "Helper_Evergreen_SupplierPackage_02 (48adf8d57aeb75fc, 16.37, 14.79, 16.74; availableSupplier01 and 02), "
         "whose supplier then stood on it (Alessio Supplier at z 17.51, 0.77 m above the floor he stands "
         "on under Landslide; The Author's navmesh has no floor at the spot). Freelancer's version of the room has a "
         "mattress on the floor there (location's Mattress_A_00_01, which The Author deletes)"),
    ],
}

# Scripted moments of override_bingo whose day NPC The Author deletes: the street performer's water break
# (PerformBreak 2: its Role casts npc_ambient_beachsquare_day 1c9188ccd0c0ae9c) and a screenplay that hangs off
# npc_ambient_streets_day 5ec5945d756b7497. Deleted with everything under them (Landslide deletes the same two, for
# lack of the day bricks).
DELETE_OVERRIDE = [
    (0x074F4A8AC6157131, "PerformBreak 2"),
    (0xF197401C57515F26, "Screenplay 2"),
]

# Spots The Author cannot reach (all in the closed mansion block, as under Landslide). The slot (its CPD index) keeps
# meaning a usable place: it now names a reachable spot of the same kind, Landslide's choice where that one is
# reachable here too. The unreachable spot entities stay (no slot spawns them).
#   slot property: (old entity, new entity, why)
SLOT_REPOINTS = {
    "availableSupplier01": (0xFADD3FE97A972ADA, 0x48ADF8D57AEB75FC,
                            "Helper_Evergreen_SupplierPackage_01 (-11, -53, 14): 40 m from both scenes' acts; its "
                            "supplier is mansion staff -> SupplierPackage_02 (16, 15, 17), the delivery man"),
    "availableSafe02": (0xA25C9DAB8056FB44, 0x403F20A003A43FB7,
                        "Safe_Unique_04 (-36, -24, 27), 37 m -> Safe_Unique (church, -60, 91, 5)"),
    "availableSafe04": (0x6AC0794557E04B59, 0xC71204AF550BE863,
                        "Safe_Unique_05 (-42, -52, 27), 52 m -> Safe_Unique_03 (-44, -17, 7)"),
    "availableSafe05": (0xE38D51621482F60C, 0x0F267931BF1E8322,
                        "Safe_Unique_01 (-127, -119, 18), 102 m -> Safe_Unique_07 (town hall, 73, -24, 26)"),
    "availableSafe06": (0x3CC72354DD5E4666, 0x403F20A003A43FB7,
                        "Safe_Unique_06 (-69, -19, 18), 25 m -> Safe_Unique (church)"),
    "availableSafe07": (0x172B7EDB9B82FF99, 0xC71204AF550BE863,
                        "Safe_Unique_02 (-56, -17, 23), 32 m -> Safe_Unique_03"),
    "availableStash04": (0xCECC62420EA3E886, 0x039D7058A2FD4843,
                         "Stash Attic (-42, -43, 33), 55 m -> Stash Office (70, -19, 26)"),
    "availableStash05": (0xE180060CA8D5AF5A, 0xF4BFBC679E57741E,
                         "Stash Fortress (-93, -114, 1), 90 m -> Stash Toilet (74, -37, 12)"),
    "availableStash06": (0x61801C45B2D2D9A8, 0xC28246124B27B84A,
                         "Stash Observatory (-73, -11, 18), 23 m -> Stash PlaneArea_02 (71, -7, 33)"),
    "availableStash07": (0xC8728B2DCBEB839D, 0xAC7078F3BA0A6BC2,
                         "Stash PlaneArea_01 (-56, -25, 23), 32 m -> Stash InsideRadioTowerFence (the church "
                         "basement; moved next door, SPOT_MOVES)"),
    "availableStash08": (0x458497EC4C5B41ED, 0x87879189C4CD8E40,
                         "Stash Cavern (-95, -51, 8), 31 m -> Stash PlaneArea_06 (-65, -8, 7)"),
    "availableStash09": (0x61AB6CD8DFE23AEA, 0x49461E39C9EB2D3A,
                         "Stash PlaneArea (-52, -79, -1), 58 m -> Stash GlassStore (45, 15, 13)"),
    "availableStash10": (0x083ED9CFC66A0B45, 0x9509F27C773BE8A1,
                         "Stash PlaneArea_03 (-57, -34, 8), 16 m, toward the caves -> Stash Streets (48, 56, 7)"),
    "availableMule07": (0x7163D40B618973C5, 0xF92BE591CE991B6C,
                        "MoneyMuleSetup_Unique_06: its acts are 18-21 m away, inside the mansion -> "
                        "MoneyMuleSetup_Unique (church)"),
}
# Not repointed although Landslide repoints it: availableStash16, Stash PlaneArea_05 (-70, -25, 0), 2 m from The
# Author's VIPEvac_SafeRoom_Tunnels act (Brother Akram's evacuation room in the tunnels, which The Author keeps open).

# Spots that move (build_bricks.apply_spot_moves, as for the full Icon: icon_spec.py's SPOT_MOVES has the fields; the
# place is snapped on NAVMESH). Stash InsideRadioTowerFence (ac7078f3ba0a6bc2, availableStash07 and availableStash11)
# stands at (-57.01, 83.11, -1.51) in a store room of the church basement that The Author turns into a corner of
# Brother Akram's gas works: its smoke machine (Setpiece_Italy_Ebola_Smokemachine 99899558cb6a11f6, the poison-gas set
# piece, which stays) stands 0.58 m from the crate's origin, 0.2 m off its open side (local -Y, yaw -46), a 1 m wooden
# crate (Crate_Wooden_1x1x1_00 38004144ac840f39) 0.31 m from it, inside it, and a flight-case work table
# (Movieset_Flightcases_C_Case02/04, 2.7-3.2 m) fills the rest of the room's floor. The Author's navmesh has no floor
# in front of the open side (0.00 m; 3.00 m under Freelancer's) on any of its four quarter turns, and the room has no
# floor with 1 m of clearance. So it moves 7 m north, through the arched door, to the next basement room
# (504_Basement_A, the room of benches, bookshelves and coin boxes), which The Author leaves as Freelancer has it: the
# middle of its floor (clearance 1.4 m), 5.6 m from the nearest act of The Author (the guard Act_MR_Guard_Stand_ArmsSide
# 1de1016e8b4292bb in the corridor by the door, its anchor) and none of The Author's people within 2 m; its vanilla
# rotation kept, its open side to the door with 2.45 m of floor in front. The wooden crate is no longer deleted.
#   entity: dict(kind, to=nominal world position, anchor=the scenario_ebola entity whose place it borrows, why)
SPOT_MOVES = {
    0xAC7078F3BA0A6BC2: dict(kind="stash", to=(-57.26, 90.11, -1.51), anchor=0x1DE1016E8B4292BB,
                             why="Stash InsideRadioTowerFence, hemmed in by The Author's smoke machine and flight "
                                 "cases -> the next basement room north (504_Basement_A)"),
}

# Spawnable spots farther than build_bricks.REACH_M from both scenes' acts that are judged reachable anyway.
REACH_EXCEPTIONS = {}

# Freelancer's four exits ("Any exit used" in the mission brick): push buttons whose Exit pin starts an exit sequence
# in location, whose end triggers the button. Under The Author:
# - Exit_Evergreen_PushButton_01 (the harbor speedboat, -93, 67, -1; sequence 8c6e2d48c6b60954) and
#   Exit_Evergreen_PushButton_03 (the road, 29, -66, 13; sequence 44724f00f7782bb3, enable condition
#   66e646f0508fa5f0) stay: The Author keeps both sequences and both exits (its VolumeBox_Exit_Harbour_speedboat and
#   VolumeBox_Exit_Road hang off them).
# - Exit_Evergreen_PushButton (the ruins boat, -144, -138, -1, below the closed mansion) lost its sequence: The Author
#   deletes 332b67e5fc48813a. It moves to The Author's third exit, the old-town gate: location's exit fb8dfcd7a113e19b,
#   which The Author places at (62, 67, 7) with a property override (taken as the button's transform) and which
#   override_bingo deletes, wired as location wires it: its sequence c821c3e0ff6c3888 ends in fb8d's Trigger (the pin
#   connection override_bingo deletes, checked), and is started by Play (as mission_mamba's own copy of that exit,
#   OldTown_Exit, is: Exit -> Play, OnStop -> Trigger).
# - Exit_Evergreen_PushButton_02 (the cave seaplane, -60, -77, -1, in the caves under the closed mansion; The Author
#   deletes the seaplane exit 647d5b5203ea01ea) is deleted with its pin overrides, and leaves "Any exit used": The
#   Author has no fourth exit to move it to.
#   button: dict(place=("override", scene, entity) | ("entity", scene, entity), old=(scene, sequence, start, end),
#                new=(scene, sequence, start, end), wired_by=(check), drop=[properties], why=...)
EXIT_MOVES = {
    0x08DB2FE9103A73CF: dict(
        place=("override", LOCATION, 0xFB8DFCD7A113E19B),
        old=(LOCATION, 0x332B67E5FC48813A, "StartExitSequence", "ExitSequenceOver"),
        new=(LOCATION, 0xC821C3E0FF6C3888, "Play", "OnStop"),
        wired_by="vanilla", drop=[],
        # mission_mamba's own copy of the old-town gate exit (world placed) stands where the override puts fb8d.
        confirm=(MISSION_MAMBA[0], 0x6D437E5C6DCE641A),
        why="Exit_Evergreen_PushButton (the ruins boat, -144, -138, -1, 116 m from both scenes' acts; The Author deletes "
            "its sequence) -> the old-town gate (location's exit fb8dfcd7a113e19b where The Author puts it)"),
}
UNMOVED_EXITS = [0xC0632396F86D316E, 0xD5AA959458203ABF]
DELETE_EXITS = {
    0x6721EBE4C0FDED96: "Exit_Evergreen_PushButton_02 (the cave seaplane, -60, -77, -1, 54 m from The Author's acts, "
                        "in the caves blockedoff_mansion closes; The Author deletes the seaplane exit 647d5b5203ea01ea)",
}
MISSION_ANY_EXIT = 0xE320A6EE11DFC348  # mission_mild_bingo's "Any exit used" (m_aValues: the buttons' exitActivated)
DELETE_BASE_EXITS = []  # The Author's exits are location's, which override_bingo already deletes

# --- Starting locations (hero spawns) --------------------------------------------------------------------------
#
# Under the swap the scene is scene_ebola plus our two bricks; build_bricks.py walks the reference tables of everything
# that loads and finds hero spawns in scenario_ebola (HeroSpawn_Ebola, The Author's own start and the default) and
# agencymarkup_location (its six). agencymarkup_octopus is not loaded, so its five starts (the mansion's and
# MainSquare) do not exist here: an entrance id of theirs lands on HeroSpawn_Ebola through the mod's start fix (the
# game does NOT fall back to the default spawn; observed in game; src/mod/HeroStart.h). Every start left must be near
# the scenes' acts and outside every collision box / pathfinder obstacle of blockedoff_mansion and scenario_ebola.
DEFAULT_HERO_SPAWN = 0x9C77302F4FBAA307  # scenario_ebola HeroSpawn_Ebola (-53, 9, 0)
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189
HERO_SPAWN_REQUIRED = [SCENARIO_EBOLA[0], AGENCYMARKUP_LOCATION, BLOCKEDOFF_MANSION[0]]
HERO_SPAWN_FORBIDDEN = [AGENCYMARKUP_OCTOPUS]
# Bricks whose blockers no start or spot may be inside: (TEMP, TBLU, what counts). "boxes": collision boxes and every
# pathfinder box (blockedoff_mansion's two close the mansion's gates); "solid": collision boxes and pathfinder
# obstacles only (scenario_ebola's pathfinder boxes are navmesh generation hints, two of them as big as the map), and
# a blocker hung off an entity of another brick is skipped (logged).
BLOCKER_BRICKS = [(BLOCKEDOFF_MANSION, "boxes"), (SCENARIO_EBOLA, "solid")]
SPOT_BLOCKER_CHECK = True

# Deleted in agencymarkup_location, as for the full Landslide: the ruins start below the closed mansion. Its entrance
# id then has no spawn: the game does NOT fall back to the default (observed in game); the mod's runtime start fix
# (src/mod/HeroStart.h) re-points HeroSpawn_Ebola to it.
DELETE_HERO_SPAWNS = {
    AGENCYMARKUP_LOCATION: [
        (0xF9084ED3B4CEFBF6, "Ruins_HeroSpawn",
         "(-143, -138, -1), 116 m from both scenes' acts: the castle ruins on the shore below the mansion, reached only "
         "through the mansion grounds and caves that blockedoff_mansion seals"),
        (0xC12DEFBE5378DC32, "HeroSpawnSeqDir_Ruins",
         "the Ruins intro's sequence director; its spawner is Ruins_HeroSpawn"),
    ],
}
HERO_SPAWN_EXCEPTIONS = {}
# location's 000_Outside (34.17, -35.76, 12.84), composed by build_bricks.folder_matrices (landslide_spec.py).
LOCATION_WORLD_FOLDERS = {0xA3ABFC17113FA981}

# --- The package's checks ----------------------------------------------------------------------------------------

# The vanilla resources the Author's bricks were built against (gen_package_inc.py adds each TEMP's blueprint): the
# scene and its brick, the bricks our copies reference by entity id, and the Freelancer sources they copy.
FINGERPRINTS = [
    (SCENE, "scene_ebola.entity"),
    (SCENARIO_EBOLA[0], "scenario_ebola.brick"),
    (NAVMESH[0], NAVMESH[1]),
    (EVERGREEN_SETUP, "evergreen_setup.brick"),
    (LOCATION, "location.brick"),
    (AGENCYMARKUP_LOCATION, "agencymarkup_location.brick"),
    (BLOCKEDOFF_MANSION[0], "blockedoff_mansion.brick"),
    (MISSION_MILD_BINGO[0], "mission_mild_bingo.brick"),
    (OVERRIDE_BINGO[0], "override_bingo.brick"),
    (0x004B6755A8F753FA, "scene_costaltown_mild_bingo.entity"),
] + [(DAY[n], "npc_ambient_%s_day.brick" % n) for n in DAY_ORDER]

# Not copied, as for Landslide: scene_costaltown_mild_bingo's 20 vestigial entities; scene_ebola is used as it is, and
# The Author contract's own extra brick (mission_guidance_ebola, guidance only) is not loaded.
