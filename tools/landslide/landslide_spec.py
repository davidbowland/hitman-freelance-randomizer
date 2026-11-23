"""The full Landslide's Freelancer layer: what build_bricks.py changes in IOI's two Freelancer bricks of mild Sapienza.

The swap loads Landslide's scene (_scene_mamba, whose content is mission_mamba) with one brick instead of the
contract's mission_mild_bingo: our copy of it, fr_mission_mild_landslide, which in turn loads our copy of
override_bingo, fr_override_landslide. Both copies keep every entity id of the original, so the game's CPD keys
(MildBingo_*) and the mod's tables still apply; what changes is listed here, each item with its reason. Ids are the
bricks' sub-entity ids. Positions are world coordinates (both bricks' spots hang off the brick root).

How spots were judged: Landslide closes Villa Caruso (blockedoff_mansion) and its caves, and its navmesh has no way
in. Every spot and act of the two bricks was measured against the 831 acts of mission_mamba (which Landslide's own
NPCs use, so they stand where its navmesh reaches): a spot within ~12 m of one counts as reachable, one 20 m or more
away as inside the closed area (all of these are in the mansion block, x -140..0, y -140..-5, mostly 13 m up, or the
caves under it). build_bricks.py prints that audit (--audit); the decisions below follow it.
"""

COASTALTOWN = "assembly:/_pro/scenes/missions/coastaltown/"
NAME = "landslide"
DISPLAY = "Landslide"
MISSION_PATH = COASTALTOWN + "fr_mission_mild_landslide.brick"
OVERRIDE_PATH = COASTALTOWN + "fr_override_landslide.brick"

# Vanilla resources (TEMP, TBLU).
MISSION_MILD_BINGO = (0x00D15B6A8159D00D, 0x00F23DCB39B5FD0D)
OVERRIDE_BINGO = (0x00AC32D2CCC449A7, 0x00C63AC62C8E65C9)
MISSION_MAMBA = (0x006026A87FB981F5, 0x000A254D361DC059)

LOCATION = 0x00E9F09C3B030590
EVERGREEN_SETUP = 0x006864DBF5FD7CD4
AGENCYMARKUP_LOCATION = 0x00B6D38769189EA2
AGENCYMARKUP_LOCATION_TBLU = 0x0025F7D01875A6CA
AGENCYMARKUP_OCTOPUS = 0x00199C55C9BC50D6  # not loaded under Landslide (override_bingo loaded it)
SCENE_MAMBA = 0x00FD4FC867F311FE  # _scene_mamba.entity, the scene the swap loads
SCENE = SCENE_MAMBA
SCENE_PATH = COASTALTOWN + "_scene_mamba.entity"
BASE = MISSION_MAMBA  # the scene's content: its acts, NPCs and exits
# The crate check (build_bricks.check_crates): every supply crate a slot names opens toward floor on Landslide's
# navmesh as on Freelancer's (mission01.navp). Both NAVPs are required, in a --navp or --bin folder
# (build-landslide/audit/sapienza holds them).
CRATE_NAVMESH = (0x00AC273EDE6FDC08, "_scene_mission_mamba.navp")
FREELANCER_NAVMESH = (0x008980F97BE736B2, "mission01.navp")
# The two bricks that close the mansion block for Landslide (both loaded by mission_mamba), TEMP and TBLU.
BLOCKEDOFF_MANSION = (0x0085BEC4E9BBCCF9, 0x007129A21F0B0DB1)
DRESSING_MISSION_MAMBA = (0x000C38D90184DD2C, 0x005264CC65C8F493)

# Landslide's night ambient bricks (all loaded by mission_mamba) that pool actors come from.
NIGHT = {
    "streets": 0x00376CE096E14ECE,
    "town": 0x009782681CABAA77,
    "mainsquare": 0x000AB6C0DAC4FD26,
    "cafe": 0x000EA76F78E67454,
    "pier": 0x00B0BC80E4E6116D,
    "beachsquare": 0x00B134DFBBC53981,
    "beach": 0x006C648EE01F258A,
    "graveyard": 0x002ED25699A0E96C,
    "bakery": 0x007D6EE6AFC2171D,
}

# Where a spot counts as reachable: near an act of these bricks (build_bricks.REACH_M).
REACH_SOURCES = [("mission_mamba", MISSION_MAMBA)]
REACH_MIN_ACTS = 500

# --- fr_mission_mild_landslide (copy of mission_mild_bingo) ------------------------------------------------------

# The mission brick's external scenes, before the bricks pool actors come from ("OVERRIDE": our override brick).
MISSION_SCENES = [LOCATION, EVERGREEN_SETUP, "OVERRIDE", MISSION_MAMBA[0]]
POOL_BRICKS = NIGHT  # POOL_ADDITIONS name these by key ("base" or a hash otherwise)

# The target pool: Helper_Evergreen_Spawn_Targets.m_aActors keeps its 59 slots (a stored pick stays in range).
SPAWN_TARGETS = 0x98CA7BAED06A1211

# The brick's own 21 dedicated targets (start disabled until picked). These 9 have every act within 10 m of a
# Landslide act and keep their slot; the other 12 (mansion staff, mansion guards, lab technicians and three locals
# whose acts are all inside the mansion block) leave the pool but stay in the brick, disabled.
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
    0xFD6E378953306ABA,  # Tristano Fiorini (acts 22-29 m away, inside the mansion block)
    0x3CCC05C0250A9972,  # Efrem Paolini (23-33 m)
    0x753E250C0D7CE218,  # Paulo Coletta, mansion guard (114-118 m)
    0xB210DCA38ACC9B1A,  # Alessandro Malbera, mansion guard (18-22 m)
    0x3F1FCA22325F2AF5,  # Monica Camara, lab technician (28-43 m)
    0x05ACF075E3BA7603,  # Iolanda Baldacci, lab technician (32-50 m)
    0xEAE902AF0AFC8C4B,  # Rino Triano, lab technician (22-34 m)
    0x7590B4599EFD4704,  # Imelda Meccia, mansion staff (20-64 m)
    0x1D4EE2599E324045,  # Gisella Bartoletii, mansion staff (18-37 m)
    0x2CFE39EDDB8A52EF,  # Otto Agosta, mansion staff (32-33 m)
    0x155BB4BABFE2D68C,  # Elmo Barilla, mansion staff (14-19 m)
    0x2CA3F83B0F2E97A9,  # Gustavo Paci, mansion staff (35-42 m)
]

# Pool actors of other bricks that keep their slot: none (mission_mild_bingo's are mission01's and the day crowd's).
KEEP_SCENE_TARGETS = []

# The 50 Landslide NPCs that take the other slots, in slot order: (brick, entity id, name). Picked from the in-game
# census of the swapped scene (alive and enabled after 3 minutes, most of them
# moving about): townspeople of the night ambient bricks, and mission_mamba's wine shop and booth guests, church
# workers, kitchen staff and patrolling guards, plus two of Abiatti's guests (Carlo Vassallo and Luca Mannucci).
# Left out: Marco Abiatti (Landslide's own target), the rest of NPC/MainCharacters (Lawyer, Secretary,
# Assistant, priest, photographer, Abiatti's bodyguard and fans), the band, the stage crew and its guards, and the
# two NPCs the census found dead or disabled (Giacinto Lori, Ruggero Gallo).
POOL_ADDITIONS = [
    (MISSION_MAMBA[0], 0xDADF34513395BC28, "Carlo Vassallo"),
    ("streets", 0x7F019E75F5D33DF1, "Beato Rosati"),
    ("town", 0x67D9A085B6B02286, "Hansi Schumacher"),
    (MISSION_MAMBA[0], 0x2C1E201D94DB3225, "Nicolas Scanarotti"),
    (MISSION_MAMBA[0], 0x854E6FEA3BFE28A6, "Luca Mannucci"),
    (MISSION_MAMBA[0], 0x545EF8BFC011C4CC, "Marta Clerici"),
    ("mainsquare", 0x9DA3479E4DF18F81, "Clodoveo Rametta"),
    ("streets", 0x79D17DB68950A9EE, "Carisio Mazzanti"),
    (MISSION_MAMBA[0], 0x50BD84F5984A8741, "Matteo Martinelli"),
    (MISSION_MAMBA[0], 0x4E4E6BDEA896248D, "Bobby Douche"),
    ("town", 0xCF40017981A6F30A, "Mattia Fiorio"),
    ("mainsquare", 0xAF62E7D67A8E84B6, "Michelangelo Tartaruga"),
    (MISSION_MAMBA[0], 0x34ADEB3B9E6C8B56, "Ernesto Dellucci"),
    (MISSION_MAMBA[0], 0x2AD9D1ED8D1CF641, "Carlo Occupato"),
    ("streets", 0xF5738D13677FC60C, "Annamaria Perduca"),
    ("beachsquare", 0xD2F27935EB18FEB2, "Bartolomeo Civetta"),
    (MISSION_MAMBA[0], 0x63B059883B11411F, "Mikki Disco"),
    (MISSION_MAMBA[0], 0x7A72A7E9EC2F93C9, "Stefano Marini"),
    ("cafe", 0x7F4257B05D9EF9DE, "Celso Cremonesi"),
    ("graveyard", 0xF3A69DC763BA0032, "Efisio Pisani"),
    (MISSION_MAMBA[0], 0x0BE13C94B6CF5B50, "Francesco Rettani"),
    (MISSION_MAMBA[0], 0xC12790384808CF3C, "Ortensio Elefanti"),
    ("mainsquare", 0xD0CD9176E0F66204, "Silvio Gioe"),
    (MISSION_MAMBA[0], 0x3F495BA9213E0D2F, "Valerio Polloni"),
    ("beach", 0x035D5B9F2C52866E, "Luisa Doria"),
    (MISSION_MAMBA[0], 0x862F562C0E8FC7B7, "Basso Padovesi"),
    ("streets", 0x9B07667C7FBC76B0, "Eligio Trevisani"),
    (MISSION_MAMBA[0], 0xDA8286280D1647A1, "Tiziano Acciaio"),
    ("bakery", 0x477A588A262125E7, "Dario Bronzo"),
    (MISSION_MAMBA[0], 0x60892E05819C576C, "Oreste Ferri"),
    ("town", 0x5D74AEFFADECE444, "Ettore Ricci"),
    (MISSION_MAMBA[0], 0xFCD9BECD109870E8, "Crispino Russo"),
    ("pier", 0x1BEE56EB8291F215, "Demetrio Palermitano"),
    (MISSION_MAMBA[0], 0x210FC863EDFDF9FD, "Berta Toscani"),
    ("streets", 0x442ED5B0F7EDCF0E, "Gilda Veronesi"),
    (MISSION_MAMBA[0], 0xD062049C703D0FA1, "Sandro Paranoico"),
    ("mainsquare", 0xE71BE7ECEB4D3B1F, "Giacobbe Conti"),
    (MISSION_MAMBA[0], 0x1C4E65D1A81CC3E3, "Ausonio Lucchese"),
    ("beachsquare", 0x05225E14C62A1182, "Baldovino Orlandini"),
    (MISSION_MAMBA[0], 0xC71DB22A88B68C11, "Nunzio Trevisan"),
    ("pier", 0x77740053C62705EF, "Margot Conway"),
    (MISSION_MAMBA[0], 0x2F494E8286105DDF, "Daniela Napolitani"),
    ("mainsquare", 0xF7CF778CE1A815D0, "Valentino Verdi"),
    (MISSION_MAMBA[0], 0x24F5BDE87A45B343, "Amaranto Zetticci"),
    ("streets", 0x46E3A1DB18F1569F, "Carolina Fallaci"),
    (MISSION_MAMBA[0], 0xBF68CA63281880F6, "Mario Fratelli"),
    ("town", 0x95793449CE1C44CF, "Fabiana Maddalone"),
    (MISSION_MAMBA[0], 0x2491EF48B0A852F4, "Delfio Trentino"),
    ("streets", 0x148E5613CB733F2D, "Greta Livorni"),
    ("town", 0xD5F64607B0087940, "Cassiopea Tamburino"),
]

# Marco Abiatti and the other story characters never enter the pool.
NEVER_IN_POOL = [0x33160A4D0FD92760]

# --- fr_override_landslide (copy of override_bingo) --------------------------------------------------------------

TERRITORY = 0x8926D401C9249780  # Helper_Evergreen_CPD_TerritorySetupRandomizer

# The override brick's external scenes (override_bingo's others go, with their deletes and overrides);
# dressing_mission_mamba for DELETE_SCENE_PROPS.
OVERRIDE_SCENES = [LOCATION, EVERGREEN_SETUP, AGENCYMARKUP_LOCATION, MISSION_MAMBA[0], DRESSING_MISSION_MAMBA[0]]

# Spots inside the closed mansion block. The randomizer's slot (its CPD index) keeps meaning a usable place: the
# slot now names a reachable spot of the same kind. The unreachable spot entities stay (no slot spawns them).
#   slot property: (old entity, new entity, why)
SLOT_REPOINTS = {
    "availableSupplier01": (0xFADD3FE97A972ADA, 0x48ADF8D57AEB75FC,
                            "Helper_Evergreen_SupplierPackage_01 (-11, -53, 14): 40 m from Landslide's acts; its "
                            "supplier is mansion staff -> SupplierPackage_02 (16, 15, 17), the delivery man"),
    "availableSafe02": (0xA25C9DAB8056FB44, 0x403F20A003A43FB7,
                        "Safe_Unique_04 (-36, -24, 27), 37 m -> Safe_Unique (church, -60, 91, 5)"),
    "availableSafe04": (0x6AC0794557E04B59, 0xC71204AF550BE863,
                        "Safe_Unique_05 (-42, -52, 27), 52 m -> Safe_Unique_03 (-44, -17, 7)"),
    "availableSafe05": (0xE38D51621482F60C, 0x0F267931BF1E8322,
                        "Safe_Unique_01 (-127, -119, 18), 102 m -> Safe_Unique_07 (town hall, 73, -24, 26)"),
    "availableSafe06": (0x3CC72354DD5E4666, 0x403F20A003A43FB7,
                        "Safe_Unique_06 (-69, -19, 18), 23 m -> Safe_Unique (church)"),
    "availableSafe07": (0x172B7EDB9B82FF99, 0xC71204AF550BE863,
                        "Safe_Unique_02 (-56, -17, 23), 32 m -> Safe_Unique_03"),
    "availableStash04": (0xCECC62420EA3E886, 0x039D7058A2FD4843,
                         "Stash Attic (-42, -43, 33), 55 m -> Stash Office (70, -19, 26)"),
    "availableStash05": (0xE180060CA8D5AF5A, 0xF4BFBC679E57741E,
                         "Stash Fortress (-93, -114, 1), 90 m -> Stash Toilet (74, -37, 12)"),
    "availableStash06": (0x61801C45B2D2D9A8, 0xC28246124B27B84A,
                         "Stash Observatory (-73, -11, 18), 23 m -> Stash PlaneArea_02 (71, -7, 33)"),
    "availableStash07": (0xC8728B2DCBEB839D, 0xAC7078F3BA0A6BC2,
                         "Stash PlaneArea_01 (-56, -25, 23), 32 m -> Stash InsideRadioTowerFence (-57, 83, -2)"),
    "availableStash08": (0x458497EC4C5B41ED, 0x87879189C4CD8E40,
                         "Stash Cavern (-95, -51, 8), 31 m -> Stash PlaneArea_06 (-65, -8, 7)"),
    "availableStash09": (0x61AB6CD8DFE23AEA, 0x49461E39C9EB2D3A,
                         "Stash PlaneArea (-52, -79, -1), 62 m -> Stash GlassStore (45, 15, 13)"),
    "availableStash10": (0x083ED9CFC66A0B45, 0x9509F27C773BE8A1,
                         "Stash PlaneArea_03 (-57, -34, 8), 16 m, toward the caves -> Stash Streets (48, 56, 7)"),
    "availableStash16": (0x98C00F80127A67AC, 0xE6E61B72759E4101,
                         "Stash PlaneArea_05 (-70, -25, 0), 15 m, toward the caves -> Stash PlaneArea_04 "
                         "(-39, -13, 9)"),
    "availableMule07": (0x7163D40B618973C5, 0xF92BE591CE991B6C,
                        "MoneyMuleSetup_Unique_06: its acts (Acts 7) are 18-21 m away, inside the mansion -> "
                        "MoneyMuleSetup_Unique (church)"),
}

# Freelancer's four exits ("Any exit used" in the mission brick). Each is a push button whose Exit pin starts an exit
# sequence in location (pin connection overrides), and the sequence's end triggers the button, which ends the mission
# the Freelancer way. Landslide removes all four of the vanilla exits these sequences belonged to (blockedoff_mansion
# the mansion cave's two, mission_mamba the car's, dressing_mission_mamba the north-west one) and has three of its
# own: the pier speedboat, the car on the south road and the old-town gate, each with its own sequence in
# mission_mamba. Three buttons move onto those (same id, so the game names the exit as before) and are re-wired to
# Landslide's sequence there; their location enable conditions, which belonged to their old places, go.
#   button: dict(place=("entity", mission_mamba, Landslide exit whose place it takes),
#                old=(location, sequence, start pin, end pin), new=(mission_mamba, sequence, start pin, end pin),
#                wired_by="brick" (the exit is wired to that sequence in mission_mamba), drop=[properties], why=...)
EXIT_MOVES = {
    0x08DB2FE9103A73CF: dict(
        place=("entity", MISSION_MAMBA[0], 0x05841A6699B934BD), old=(LOCATION,) + (0x332B67E5FC48813A, "StartExitSequence", "ExitSequenceOver"),
        new=(MISSION_MAMBA[0],) + (0x809F73F3AB0481BD, "StartExitSequence", "ExitSequenceOver"), wired_by="brick", drop=[],
        why="Exit_Evergreen_PushButton (mansion cave, -144, -138, -1, 116 m from Landslide's acts) -> the pier "
            "speedboat (Exit_UseKey01, Speedboat_Exit)"),
    0x6721EBE4C0FDED96: dict(
        place=("entity", MISSION_MAMBA[0], 0x6D437E5C6DCE641A), old=(LOCATION,) + (0xC87853C91C0AB145, "StartExitSequence", "ExitSequenceOver"),
        new=(MISSION_MAMBA[0],) + (0x582FE3394273B2D9, "Play", "OnStop"), wired_by="brick", drop=["m_aEnableConditions"],
        why="Exit_Evergreen_PushButton_02 (mansion cave, -60, -77, -1, 60 m) -> the old-town gate "
            "(Exit_OldTown_Gate, OldTown_Exit)"),
    0xD5AA959458203ABF: dict(
        place=("entity", MISSION_MAMBA[0], 0x8025FF3E1596CEBD), old=(LOCATION,) + (0x44724F00F7782BB3, "StartExitSequence", "ExitSequenceOver"),
        new=(MISSION_MAMBA[0],) + (0x8D56F5B083410A02, "StartExitSequence", "ExitSequenceOver"), wired_by="brick", drop=["m_aEnableConditions"],
        why="Exit_Evergreen_PushButton_03 (the vanilla car, 29, -66, 13; Landslide deletes that exit) -> "
            "Landslide's car (Exit_UseKey, Car_Exit, 29, -95, 13)"),
}
# The fourth, Exit_Evergreen_PushButton_01 (north-west shore, -93, 67, -1), stays as it is: its sequence
# (8c6e2d48c6b60954) is still in location under Landslide, only the vanilla exit it ended in is gone. Untested.
UNMOVED_EXITS = [0xC0632396F86D316E]
DELETE_EXITS = {}  # none: Landslide has an exit of its own for each button it moves
# Landslide's own exits end the mission outside Freelancer's exit logic (no payout; the mission may count as
# failed), so the override brick deletes them from mission_mamba; their sequences stay for the buttons above.
DELETE_BASE_EXITS = [
    (0x8025FF3E1596CEBD, "Exit_UseKey"),
    (0x05841A6699B934BD, "Exit_UseKey01"),
    (0x6D437E5C6DCE641A, "Exit_OldTown_Gate"),
]

# Spawnable spots farther than build_bricks.REACH_M from Landslide's acts that are judged reachable anyway.
REACH_EXCEPTIONS = {
    0x31620C5D0D9F9E80: "MoneyMuleSetup_Unique_05 (41, 44, 7), 19 m: the mule's start point, at street level between "
                        "its three acts, which are 4-9 m from Landslide's",
}

# Scripted moments of override_bingo that hang off day NPCs, whose bricks Landslide does not load: a street
# performer's water break (PerformBreak 2, in npc_ambient_beachsquare_day, with its poisonable bottle and sick spot)
# and a screenplay in npc_ambient_streets_day. Deleted with everything under them.
DELETE_OVERRIDE = [
    (0x074F4A8AC6157131, "PerformBreak 2"),
    (0xF197401C57515F26, "Screenplay 2"),
]

# Clashes the audit (tools/landslide/landslide_audit.py) reports that were looked at and are left: (brick, entity id):
# why. An exit button stands by the door of the car it drives off in, as the variant's own exit there does.
CLASH_ACCEPTED = {
    (MISSION_MAMBA[0], 0x5C20553C5D14A6EF): "Avance_Alpha_1_A_00, Landslide's getaway car: Exit_Evergreen_PushButton_03 "
                                            "takes the place of Landslide's car exit (Exit_UseKey) by its door",
}

# Landslide's props that stand where a Freelancer spot spawns, deleted by our override brick (build_bricks
# delete_scene_props; tools/landslide/landslide_audit.py runs the clash audit).
# Deleted, not moved: the spot is Freelancer's design (two slots name it), and the prop is decoration.
#   brick: [(entity id, name, why)]
DELETE_SCENE_PROPS = {
    DRESSING_MISSION_MAMBA[0]: [
        (0x61996CFCD6565A8E, "Vending_Machine_Soda_B",
         "(73.64, -37.55, 12.00), in the town hall's toilets (location's 114_Toilets_A): 0.31 m from Stash Toilet "
         "(f4bfbc679e57741e, 73.80, -37.28, 12.00; availableStash02 and availableStash05), so the supply crate spawned "
         "inside it and could not be opened (seen in game); its only link is the fusebox's TurnOn/TurnOff pins"),
        (0xF93114CBB9EF4AB5, "Setpiece_Hips_Stand_LookAtWares",
         "the vending machine's blend-in spot, placed on it (m_eidParent) and highlighting it; 0.55 m from the stash"),
    ],
}

# --- Starting locations (hero spawns) --------------------------------------------------------------------------
#
# Freelancer picks 47's start itself (no planning screen; the mild contract names no entrance): the game spawns him at
# one of the scene's herospawn entities (found by the entrance's repository id, m_sId). An entrance id with no hero
# spawn in the scene does NOT fall back to the scene's default one (m_bDefaultHeroSpawn), as the data seemed to say:
# observed in game (snow_festival), 47 stays at location.brick's placeholder, in the sky. The mod's runtime start fix
# (src/mod/HeroStart.h) covers it: with no spawn for the requested entrance, it re-points one (the default, here
# Mamba_HeroSpawn) to that entrance once the scene's entities exist. In vanilla mild Sapienza the candidates are
# agencymarkup_location's six and agencymarkup_octopus's five (override_bingo loads both, and turns off MainSquare's
# default flag). Under the swap the scene is _scene_mamba plus our two bricks; build_bricks.py walks the reference
# tables of everything that loads and finds hero spawns in two bricks only: mission_mamba (Mamba_HeroSpawn, Landslide's
# own start and the default) and agencymarkup_location (loaded by mission_mamba and by our override brick).
# agencymarkup_octopus is no longer loaded, so its five starts (MansionGarden, MansionKitchen, LemonGarden,
# HeroSpawn_BioLab inside the closed mansion block and its caves, and MainSquare) do not exist here: an entrance id of
# theirs lands on Mamba_HeroSpawn through the start fix.
#
# Every hero spawn left in the scene must be within build_bricks.REACH_M of Landslide's acts and outside every
# collision box / pathfinder obstacle of blockedoff_mansion and dressing_mission_mamba (those bricks hold no area
# volume: what closes the mansion is blockers in its gate, doors, tower and cave mouths); build_bricks.py refuses to
# build otherwise.
DEFAULT_HERO_SPAWN = 0x64607F3477566399  # mission_mamba Mamba_HeroSpawn (-42, 43, 1), 3 m from Landslide's acts
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189  # [assembly:/_pro/design/levelflow.template?/herospawn.entitytemplate]
HERO_SPAWN_REQUIRED = [MISSION_MAMBA[0], AGENCYMARKUP_LOCATION, BLOCKEDOFF_MANSION[0], DRESSING_MISSION_MAMBA[0]]
HERO_SPAWN_FORBIDDEN = [AGENCYMARKUP_OCTOPUS]
# The bricks that close the mansion: their collision boxes and pathfinder boxes ("boxes").
BLOCKER_BRICKS = [(BLOCKEDOFF_MANSION, "boxes"), (DRESSING_MISSION_MAMBA, "boxes")]
SPOT_BLOCKER_CHECK = False  # spots were audited by distance only

# Hero spawns our override brick deletes (with what only serves them), by brick: (entity id, name, why). Deleted, not
# moved: a hero spawn is found by its repository id (m_sId), not by a slot number, so nothing keyed to it breaks, and
# its intro sequence animates 47 relative to an origin at the old place (Ruins_Intro_StartingLocation's hero track
# runs from the Spatial at the ruins), so a moved spawn would still play 47 walking out of the ruins. With the spawn
# gone its entrance id lands on Mamba_HeroSpawn through the mod's start fix (the game itself does not fall back to
# the default; observed in game), as agencymarkup_octopus's do.
DELETE_HERO_SPAWNS = {
    AGENCYMARKUP_LOCATION: [
        (0xF9084ED3B4CEFBF6, "Ruins_HeroSpawn",
         "(-143, -138, -1), 116 m from Landslide's nearest act: the castle ruins on the shore below the mansion, "
         "reached only through the mansion grounds and caves that blockedoff_mansion seals (its vanilla Freelancer "
         "exit, Exit_Evergreen_PushButton at -144, -138, -1, is gone under Landslide too)"),
        (0xC12DEFBE5378DC32, "HeroSpawnSeqDir_Ruins",
         "the Ruins intro's sequence director; its spawner is Ruins_HeroSpawn (override_bingo's Use_FlyIn_IntroCue "
         "override on it goes too)"),
    ],
}

# Hero spawns farther than build_bricks.REACH_M from Landslide's acts that are judged reachable anyway: none.
HERO_SPAWN_EXCEPTIONS = {}

# Entities of location.brick that Landslide's bricks use as a parent folder of world-placed props and blockers
# (blockedoff_mansion places its 92 entities, gate and door blockers included, under it): location's 000_Outside,
# which stands at (34.17, -35.76, 12.84); build_bricks.folder_matrices composes its transform (taking it as
# the identity would put those blockers 51 m from their places and keep the blocker checks from seeing them).
LOCATION_WORLD_FOLDERS = {0xA3ABFC17113FA981}

# Not copied: scene_costaltown_mild_bingo's 20 entities (ValueBool operations and sets over the stashes' VBB_Spawn,
# with no pin connection and nothing reading them) are vestigial, and _scene_mamba is used as it is.

# --- The package's checks ----------------------------------------------------------------------------------------

# The vanilla resources the Landslide bricks were built against (gen_package_inc.py adds each TEMP's blueprint), then
# the night ambient bricks the target pool takes NPCs from.
FINGERPRINTS = [
    (0x00FD4FC867F311FE, "_scene_mamba.entity"),
    (0x006026A87FB981F5, "mission_mamba.brick"),
    (0x006864DBF5FD7CD4, "evergreen_setup.brick"),
    (0x00E9F09C3B030590, "location.brick"),
    (0x00B6D38769189EA2, "agencymarkup_location.brick"),
    (0x0085BEC4E9BBCCF9, "blockedoff_mansion.brick"),
    (0x000C38D90184DD2C, "dressing_mission_mamba.brick"),
    # The Freelancer sources the bricks copy (and the vanilla scene they belong with).
    (0x00D15B6A8159D00D, "mission_mild_bingo.brick"),
    (0x00AC32D2CCC449A7, "override_bingo.brick"),
    (0x004B6755A8F753FA, "scene_costaltown_mild_bingo.entity"),
] + [(h, "npc_ambient_%s_night.brick" % n) for n, h in sorted(NIGHT.items())]
