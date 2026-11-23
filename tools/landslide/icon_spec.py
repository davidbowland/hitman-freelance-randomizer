"""The full Icon's Freelancer layer: what build_bricks.py changes in IOI's two Freelancer bricks of mild Sapienza so
they play on The Icon's own scene, a set-only map.

The swap loads The Icon's scene (_scene_copperhead, whose content is mission_copperhead: Dino Bosco's film shoot, at
night) with one brick instead of the contract's mission_mild_bingo: our copy of it, fr_mission_mild_icon, which in turn
loads our copy of override_bingo, fr_override_icon. As for the full Landslide and the full Author (landslide_spec.py,
author_spec.py), both copies keep every entity id of the original, so the game's CPD keys (MildBingo_*) and the mod's
tables still apply; what changes is listed here, each item with its reason. Ids are the bricks' sub-entity ids;
positions are world coordinates (every moved entity hangs off the brick root, checked).

How The Icon differs from Landslide and The Author: those two keep the whole town, so a bad spot could be re-pointed
to another vanilla spot. The Icon's walkable area is the movie set, about 80 x 165 m (x 10..92, y -105..62), and
only 8 of Freelancer's 34 territory spots are inside it and on its navmesh. The other 26 are moved (SPOT_MOVES): the
entity keeps its id and its slot and gets a new place beside one of The Icon's own acts (where The Icon's NPCs
stand, so the navmesh reaches it), together with its clues, its safe guard and the guard's acts, a mule's acts, and
the supplier's NPC. Sit and lean acts are retyped to standing ones (RETYPE: a seat or rail is not guaranteed at the
new place), and the mules' approach paths through the old town go (DELETE_APPROACH_POINTS).

How places are judged (tools/landslide/placement.py): with The Icon's navmesh (NAVMESH, the NAVP read raw from the
extracted files), each nominal place is snapped onto the navmesh floor with a clearance from its edge, must be on the
default start's island (an island: areas connected by the mesh's adjacency), is never put on one of The Icon's acts,
and every kept spot, exit and start must stand on that island; a rejected place stops the build. Checked on the real
navmesh (_scene_mission_copperhead.navp: 2426 areas, one island of 2412 and a 14-area ledge at z 21 by
(28..34, -5..3) that no Freelancer place uses): every moved place lands on the start's island; 15 places the design
set beside an act and the clearance push moved onto it now step past it (the audit's "snapped" lines give each final
place); the delivery-man supplier (an upstairs room the navmesh does not reach) moves, and the Apartment start (the
same upstairs) goes. Without the NAVP, the nominal places of the design are used as they are, each checked to be
within a few meters of an act of The Icon, and the build says loudly that the navmesh check did not run. Either way
no spot or start may be inside a collision box or pathfinder obstacle of mission_copperhead or blockedoff_mansion,
and every spawnable entity must be within build_bricks.REACH_M of The Icon's acts. build_bricks.py prints that audit
(--audit).

Design: from vanilla IOI data only.
"""

COASTALTOWN = "assembly:/_pro/scenes/missions/coastaltown/"
NAME = "icon"
DISPLAY = "The Icon"
MISSION_PATH = COASTALTOWN + "fr_mission_mild_icon.brick"
OVERRIDE_PATH = COASTALTOWN + "fr_override_icon.brick"

# Vanilla resources (TEMP, TBLU).
MISSION_MILD_BINGO = (0x00D15B6A8159D00D, 0x00F23DCB39B5FD0D)
OVERRIDE_BINGO = (0x00AC32D2CCC449A7, 0x00C63AC62C8E65C9)
MISSION_COPPERHEAD = (0x00381237C575847C, 0x00B72511ECAD65CA)
STREETS_NIGHT = (0x00376CE096E14ECE, 0x004A33959350A3F7)  # npc_ambient_streets_night (mission_copperhead loads it)
MISSION_MAMBA = (0x006026A87FB981F5, 0x000A254D361DC059)  # only to confirm the old-town gate's place (EXIT_MOVES)

SCENE = 0x0066F0E706869F2E  # _scene_copperhead.entity, the scene the swap loads (its one entity: mission_copperhead)
SCENE_PATH = COASTALTOWN + "_scene_copperhead.entity"
BASE = MISSION_COPPERHEAD

LOCATION = 0x00E9F09C3B030590
EVERGREEN_SETUP = 0x006864DBF5FD7CD4
AGENCYMARKUP_LOCATION = 0x00B6D38769189EA2
AGENCYMARKUP_LOCATION_TBLU = 0x0025F7D01875A6CA
AGENCYMARKUP_OCTOPUS = 0x00199C55C9BC50D6  # not loaded under The Icon (override_bingo loaded it)
AGENCYMARKUP_COPPERHEAD = 0x00C1F5A907A29575  # hides five of agencymarkup_location's starts (m_bHide)
BLOCKEDOFF_MANSION = (0x0085BEC4E9BBCCF9, 0x007129A21F0B0DB1)

# The Icon's navmesh (_scene_mission_copperhead.navp, 526132 bytes in chunk26): build_bricks.py finds
# *<HASH>.NAVP in its --navp folders (scripts/build-landslide-pkg.sh passes every --raw). Fingerprinted below: the
# positions depend on it.
NAVMESH = (0x00E920F30DEF45C1, "_scene_mission_copperhead.navp")
# Freelancer's (mission01.navp), for the crate check of the slots' crates that stay (build_bricks.check_crates;
# required, in a --navp or --bin folder: build-landslide/audit/sapienza holds it).
FREELANCER_NAVMESH = (0x008980F97BE736B2, "mission01.navp")

# Where a spot counts as reachable: near an act of mission_copperhead (build_bricks.REACH_M); its 347 acts are where
# The Icon's own NPCs stand.
REACH_SOURCES = [("mission_copperhead", MISSION_COPPERHEAD)]
REACH_MIN_ACTS = 300
# Read-only bricks for the exit places' confirmation (build_bricks EXIT_MOVES "confirm"), not reach sources.
CONFIRM_SOURCES = [("mission_mamba", MISSION_MAMBA)]

# --- fr_mission_mild_icon (copy of mission_mild_bingo) -----------------------------------------------------------

# The mission brick's external scenes (before the bricks pool actors come from): location, the Freelancer setup, our
# override brick ("OVERRIDE"), the base; npc_ambient_streets_night follows (pool actors live there).
MISSION_SCENES = [LOCATION, EVERGREEN_SETUP, "OVERRIDE", MISSION_COPPERHEAD[0]]
POOL_BRICKS = {"streets_night": STREETS_NIGHT[0]}

SPAWN_TARGETS = 0x98CA7BAED06A1211  # Helper_Evergreen_Spawn_Targets: m_aActors keeps its 59 slots

# The brick's own 21 dedicated targets: these 3 have every act within 5 m of an act of The Icon (Rosita Masi 3-5 m,
# Massimo Dugo 0-3 m, Gregorio Fricano in the canteen kitchen 1-2 m) and keep their slot (39, 45, 50); the other 18
# have acts 17-161 m from every Icon act (the mansion, the lab, the harbor, the church, the old town) and leave the
# pool, staying in the brick, disabled.
KEEP_LOCAL_TARGETS = [
    0xA0A21A86D45CA1D1,  # Rosita Masi
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
    0x913F4F2C2DF2FEB7,  # Martina Spagna
    0x579753E9E5091542,  # Miriam Tasca
    0x484C149BBBF34747,  # Egeo Carini
    0xA7337D652D8E1478,  # Beltramo Ruisi
    0x8A73C205C4795263,  # Zeno Pasquarelli
    0x6AFA1C50E26198B4,  # Dom Bonano
]

# Pool actors of other bricks that keep their slot: none (mission_mild_bingo's are mission01's and the day crowd's,
# which The Icon does not load).
KEEP_SCENE_TARGETS = []

# The 56 NPCs that take the other slots, in slot order (the comment is the slot and The Icon's group): 49 of
# mission_copperhead, spread over every group of The Icon (movie crew, SFX crew, canteen, every sentry post, the start
# area, the pyrotechnics entrance, backstage, the balcony and the Q&A), and one of the night street crowd after every
# seven of them (only street NPCs within 10 m of The Icon's acts). No in-game census yet.
# Left out of the pool (the target pool swap may still take them; only NEVER_IN_POOL never): Berto Russo (his outfit
# is the figurine reveal of the action-doll storyboard scene), Giacinto Lori and Ruggero Gallo (found dead or disabled
# by the Landslide census), and the street NPCs 15-45 m from the set (Maurizio Romani, Carolina Fallaci, Cornelio
# Bellucci, Annamaria Perduca, Carisio Mazzanti).
POOL_ADDITIONS = [
    ("base", 0x935071EB0578F506, "Baldassarre Sagese"),  # 0 NPC_SET_MOVIE
    ("base", 0x7D010A7961AF5D14, "Cosima Rotolo"),  # 1 NPC_SET_MOVIE
    ("base", 0x9FF9CA592155C2E7, "Francesco Palerma"),  # 2 NPC_SET_MOVIE
    ("base", 0x2E69E8D0FD1297F2, "Gabriele Romano"),  # 3 NPC_SET_MOVIE
    ("base", 0x835090806C68F321, "Giovani Sagese"),  # 4 NPC_SET_MOVIE
    ("base", 0xDE27273029A95E20, "Medoro Arcuri"),  # 5 NPC_SET_MOVIE
    ("base", 0xACCE5E12234A4582, "Quarto Buccho"),  # 6 NPC_SET_MOVIE
    ("streets_night", 0xCCD10073F52D4393, "Romolo Pirozzi"),  # 7 npc_ambient_streets_night
    ("base", 0x1E35129658026E04, "Remo Cattaneo"),  # 8 NPC_SET_MOVIE
    ("base", 0x677515B52A9F2550, "Vincenza Lombardi"),  # 9 NPC_SET_MOVIE
    ("base", 0xE5FF7A23941D5A17, "Adalgiso Lucchese"),  # 10 NPC_SET_SFX
    ("base", 0x103E7B37047AC78D, "Amaranto 'Tony' Mazzi"),  # 11 NPC_SET_SFX
    ("base", 0x8758AE8BC0903C35, "Arnaldo Monaldo"),  # 12 NPC_SET_SFX
    ("base", 0x66E6820CD1019056, "Ciriaco Trentino"),  # 13 NPC_SET_SFX
    ("base", 0x1F9CFFE63B223576, "Derek Archambault"),  # 14 NPC_SET_SFX
    ("streets_night", 0xF2011A8EA083F769, "Ramiro Padovesi"),  # 15 npc_ambient_streets_night
    ("base", 0xAAA5785231C8F69B, "Gilberto Castiglione"),  # 16 NPC_SET_SFX
    ("base", 0x7A13FB085A42B33B, "Roberto Greco"),  # 17 NPC_SET_SFX
    ("base", 0x6C5747E093E1C20D, "Abramo Endrizzi"),  # 18 NPC_Canteen
    ("base", 0x643D521CB80A66F6, "Alfredo Napolitano"),  # 19 NPC_Canteen
    ("base", 0xC8395D5FB09AE98B, "Arturo Tossi"),  # 20 NPC_Canteen
    ("base", 0xF6FBFB23E3E31767, "Camillo Lettiere"),  # 21 NPC_Canteen
    ("base", 0x21C0D8667C488E31, "Daniele Angelo"),  # 22 NPC_Canteen
    ("streets_night", 0x9B07667C7FBC76B0, "Eligio Trevisani"),  # 23 npc_ambient_streets_night
    ("base", 0xABED8C44D85F8A26, "Fabiano Milani"),  # 24 NPC_Canteen
    ("base", 0x4D5B4CEB405FCF5A, "Giuseppe Monaldo"),  # 25 NPC_Canteen
    ("base", 0x8CC88BFE61A1E417, "Christian Pisani"),  # 26 NPC_Entrance
    ("base", 0xA3479DE5B13B3174, "Aladino Calabresi"),  # 27 Sentry_Guards/StreetPatrols
    ("base", 0x89E7BE391E6BC3B0, "Tarquinio Colombo"),  # 28 Sentry_Guards/StreetPatrols
    ("base", 0x73A25ABCEAE2EBE5, "Manfredo Esposito"),  # 29 Sentry_Guards/Sentry_TargetTruck
    ("base", 0x3568957A059D6C44, "Francesco Lucchese"),  # 30 Sentry_Guards/Toilet_Guards
    ("streets_night", 0xEF6A4278A3CA170E, "Roberta Cammelli"),  # 31 npc_ambient_streets_night
    ("base", 0x2EA6D3034CF30031, "Ippolito Davide"),  # 32 Sentry_Guards/Sentry_Tunnel_Toilette
    ("base", 0xD61DCB20B1862C7C, "Rinaldo Aiello"),  # 33 Sentry_Guards/EnterSneakWay
    ("base", 0x474CBB7CEDBB5380, "Quintiliano Trentino"),  # 34 Sentry_Guards/Sentry_FX_Backstreet
    ("base", 0xC3A4182E1A2076C6, "Arnaldo Lucchese"),  # 35 Sentry_Guards/Cafe_Guards
    ("base", 0x9ED2E9DB97E0C83A, "Carmine Toscani"),  # 36 Sentry_Guards/Sentry_Cafe
    ("base", 0x40B28E8A73F5A16D, "Ermenegildo Lori"),  # 37 Sentry_Guards/Sentry_Backstreet_1
    ("base", 0x5588436BB6CE88C3, "Ludovico Pisano"),  # 38 Sentry_Guards/Sentry_SFX_Plateu
    ("streets_night", 0x0228BE6A6845B663, "Chiaffredo Fiorentino"),  # 40 npc_ambient_streets_night
    ("base", 0x6DC76AC6506FAA75, "Bruno Bruno"),  # 41 Sentry_Guards/Sentry_BackTrailer
    ("base", 0x40BB0649A28FE2DE, "Azeglio Mancini"),  # 42 NPC_PublicArea/NPCsAtStartArea
    ("base", 0x3F5F80436EBAEEA7, "Giuliano Pisano"),  # 43 NPC_PublicArea/NPCsAtStartArea
    ("base", 0x026F9236B437AF08, "Isidoro Lorenzo"),  # 44 NPC_PublicArea/NPCsAtStartArea
    ("base", 0x6CB58C64CEF78BD4, "Tabita Costa"),  # 46 NPC_PublicArea/NPCsAtStartArea
    ("base", 0xC3139D7540510B4F, "Achille Ferri"),  # 47 NPC_PublicArea/NPCsAtPyroTechnicsEntrance
    ("base", 0x0D3BE224CF625661, "Arnaldo Castiglione"),  # 48 NPC_PublicArea/NPCsAtPyroTechnicsEntrance
    ("streets_night", 0xCC074455ABE1B701, "Vera Bertucci"),  # 49 npc_ambient_streets_night
    ("base", 0x374B3E0F757B84A8, "Germana Castiglione"),  # 51 NPC_PublicArea/NPCsAtPyroTechnicsEntrance
    ("base", 0xF19606A4015ABF3F, "Brunilde Genovesi"),  # 52 NPC_PublicArea/NPCsAtBackstage
    ("base", 0x8AE5F5C9FD9C5152, "Ileana Fiorentini"),  # 53 NPC_PublicArea/NPCsAtBackstage
    ("base", 0x71C61C5691A90E4F, "Adam Nilsson"),  # 54 NPC_PublicArea/NPCs_Balcony
    ("base", 0x3A6FC0211DBD27A0, "Basilio Genovesi"),  # 55 NPC_PublicArea/Q and A people
    ("base", 0xA034563C63FF6FED, "Luisa Doria"),  # 56 NPC_PublicArea/NPC_Ambient_Streets
    ("base", 0xC872CE8A283AB898, "Benilde Giordano"),  # 57 NPC_PublicArea
    ("streets_night", 0x7F019E75F5D33DF1, "Beato Rosati"),  # 58 npc_ambient_streets_night
]

# Never in the pool (fr::IconFull().neverTargets too): Dino Bosco, The Icon's own target, whose script runs the shoot,
# the Q&A and the VIP evacuation (and 399 accident-kill entities), and Sophia Wilde, his agent, scripted into his Q&A.
NEVER_IN_POOL = [
    0x0C285FADF7D81D2D,  # Dino Bosco (CHAR_Unique_LeadActor_Red, repository id 9c81b545-a234-46c5-9296-54f9f2a0bb6d)
    0x9C3AA306C142E51B,  # Sophia Wilde (CHAR_Worker_MovieCrew_Female_Agent, 005e3ab8-508c-471b-8426-53aca5c6ef4b)
]

# --- fr_override_icon (copy of override_bingo) -------------------------------------------------------------------

TERRITORY = 0x8926D401C9249780  # Helper_Evergreen_CPD_TerritorySetupRandomizer

# The override brick's external scenes (override_bingo's others go, with their deletes and overrides: octopus_pro1,
# mission01, npc_mission_octopus, agencymarkup_octopus, mission_guidance, dressing_caves_biolab, the 15 day crowds).
OVERRIDE_SCENES = [LOCATION, EVERGREEN_SETUP, AGENCYMARKUP_LOCATION, MISSION_COPPERHEAD[0]]

# Scripted moments of override_bingo that hang off day NPCs, whose bricks The Icon does not load (as for Landslide).
DELETE_OVERRIDE = [
    (0x074F4A8AC6157131, "PerformBreak 2"),
    (0xF197401C57515F26, "Screenplay 2"),
]

# No slot is re-pointed: the set holds 34 distinct places, so every slot keeps its own entity (moved where needed).
SLOT_REPOINTS = {}

# Spawnable entities farther than build_bricks.REACH_M from The Icon's acts that are judged reachable anyway: Safe08
# and two of its clues on the town hall's top floor (z 26), which no act of The Icon reaches (its nearest are on the
# Mayor's office floor below, z 21.4, where Safe08's own guard and his three acts stand: 15-16 m with the height
# counted twice). The Icon keeps the town hall; the navmesh check (once the NAVP is extracted) verifies their floor,
# and the design's fallback (Safe08 to the Mayor's office) is below.
REACH_EXCEPTIONS = {
    0x0F267931BF1E8322: "Safe_Unique_07 (73, -24, 26), 16 m: the town hall's top floor, above its guard's floor",
    0x5DAC2ECDF8B35058: "Safe08's clue Laptop_Unique_07 (76, -26, 31), 15 m: the town hall's top floor",
    0x23CE37EFABA04FC4: "Safe08's clue Documents_Unique_17 (82, -10, 26), 16 m: the town hall's top floor",
    # The sports car exit (EXIT_MOVES), where The Icon's own exit stands on the south road by its default start.
    0x08DB2FE9103A73CF: "Exit_Evergreen_PushButton on Dino's sports car (34, -104, 13): The Icon's own exit there, "
                        "on the south road where its walk-in starts (no act of The Icon is that far south)",
    # The old-town gate exit (EXIT_MOVES), where mission_copperhead itself puts location's gate exit.
    0x6721EBE4C0FDED96: "Exit_Evergreen_PushButton_02 at the old-town gate (62, 67, 7), 22 m: The Icon's own exit "
                        "there (mission_copperhead moves location's gate exit to this place), past the north street",
}

# Kept as they are (already inside the set): Safe08 0f267931bf1e8322 (town hall top floor, 73, -24, 26) with its
# clues, guard and guard acts, Stash01 039d7058a2fd4843 (town hall office, 70, -19, 26), Stash03 c28246124b27b84a
# (canteen roof), Stash13 49461e39c9eb2d3a (glass store), Stash14 9509f27c773be8a1 (north street), Mule02
# a2934d7ce8135e3c and Mule06 31620c5d0d9f9e80. (Stash02 f4bfbc679e57741e, the toilet, was kept too until the clash
# audit found it inside a blocker: SPOT_MOVES has it now.) The navmesh check passes all of them, the town
# hall's top floor (z 26) included (unused fallbacks of the design: Safe08 to (78.0, -29.0, 21.4) and Stash01 to
# (73.5, -33.0, 21.4) in the Mayor's office). Supplier02 48adf8d57aeb75fc (the delivery man, upstairs at 16, 15, 17)
# was to stay, but The Icon's navmesh has no floor within 9 m of it (the upstairs rooms above the SFX street are not
# on it): it moves (SPOT_MOVES). The design's fallback, (24.5, 16.5, 9.9) in the backstage street, is off the mesh
# too (the street is 2 m wide there, between acts); it went to the street below the Apartment (28.0, 8.0, 11.7), 2.6 m
# from an Icon act, and then to the street north of the backstage (26.0, 27.0, 7.3), where no act is in its
# private volume (the suppliers' note above SPOT_MOVES' supplier entries).

# The moved ones: entity -> dict(kind, to=nominal world position, anchor=the Icon entity whose place it borrows
# (its act, agitated-guard point or item spawner table), table=on a table (clues), yaw=degrees (optional; the vanilla
# rotation is kept otherwise), why). A stash whose kept rotation opens it toward a wall or a prop has a yaw
# (placement.crate_yaw at its placed spot: the open side, local -Y, to the side with the most navmesh in front; level):
# Stash05 Fortress (in game, the full Bad Boy: it opened 0.95 m of navmesh off the crew entrance's wall,
# tilted 11.5 degrees from its vanilla slope), Stash08 Cavern (1.0 m), Stash09 PlaneArea (1.05 m) and Stash16
# PlaneArea_05 (0.95 m) each face 2.4-3 m now. build_bricks fails a moved stash placement.crate_problem finds. kind is placement.CLEARANCE's: safe, stash, supplier, guard (a safe's guard),
# mule (a mule's start), act, clue.
SPOT_MOVES = {
    # availableSafe01: Setpiece_Evergreen_Container_Items_Safe_Unique 403f20a003a43fb7 (-60.4, 90.7, 4.9) -> town hall
    0x403F20A003A43FB7: dict(kind="safe", to=(81.46, -22.61, 12.01), anchor=0x791014CBEBEDF818,
            why="town hall basement (Icon Saferoom_Basement), by zagitatedguardpointentity"),
    0x42856F6E1C25F4A8: dict(kind="clue", to=(82.42, -19.86, 12.00), anchor=0x8594E771A0E81444, table=False,
            why="availableSafe01 Clue01: floor by act_mr_stand_piss"),
    0xD621A6C17675EBE7: dict(kind="clue", to=(72.10, -28.13, 13.03), anchor=0xDE49DBADCE568E0D, table=True,
            why="availableSafe01 Clue02: table (item spawner ItemSpawner14)"),
    0x24DE34A57EB01F3D: dict(kind="clue", to=(76.34, -22.93, 12.00), anchor=0xCDEDE79E9A7F2910, table=False,
            why="availableSafe01 Clue03: floor by act_mr_stand_phone_text_pace"),
    # Clue04 lay on the middle shelf of location's Shelving_Unit_Wooden_Garage_A, not a table: the design's place
    # (80.02, -19.77, 12.79) is at the shelf's back, under its top shelf (the ice-cream cup stacks, 13.27), out of
    # sight. The shelf's front there is ItemSpawner13's (its rat poison, 0.18 m from the free part of the shelf, too
    # close for a document stack), so it lies on the floor in front of the shelf.
    0xE151A1CB38AD8A7E: dict(kind="clue", to=(79.00, -19.80, 12.00), anchor=0x4AF6FAD0BDBE28AC, table=False,
            why="availableSafe01 Clue04: floor in front of the garage shelf (Shelving_Unit_Wooden_Garage_A) whose "
                "middle shelf holds ItemSpawner13"),
    0x96B4583A7041A0A3: dict(kind="clue", to=(75.40, -39.16, 12.00), anchor=0x508EB09F1CBCEBD7, table=False,
            why="availableSafe01 Clue05: floor by act_mr_stand_inspect_100cm"),
    # availableSafe02: Setpiece_Evergreen_Container_Items_Safe_Unique_04 a25c9dab8056fb44 (-36.4, -24.1, 27.3) -> cant
    0xA25C9DAB8056FB44: dict(kind="safe", to=(82.59, -31.57, 16.60), anchor=0x1C0479AC0B4CD44D,
            why="canteen (cafe) interior, terrace floor, by act_fr_stand_phone; 0.25 m off the design's place, clear "
                "of Icon acts"),
    0x8BC172EC20668304: dict(kind="clue", to=(79.57, -32.55, 16.60), anchor=0xB0A8C6C22310839F, table=False,
            why="availableSafe02 Clue01: floor by act_mr_sit_phone_text_lean_forward"),
    0xCBD76287AF15B636: dict(kind="clue", to=(87.11, -43.73, 16.60), anchor=0xF5C6C6CFB27FF040, table=False,
            why="availableSafe02 Clue02: floor by act_mr_stand_anxious_looking_around"),
    0x8C2ABCE5F3C036A8: dict(kind="clue", to=(75.32, -22.08, 17.60), anchor=0xE0D93AF5DC4CAA10, table=True,
            why="availableSafe02 Clue03: table (item spawner ItemSpawner17)"),
    0x30F9E3578047101F: dict(kind="clue", to=(71.65, -37.55, 16.60), anchor=0xEC5FD6D9197955E5, table=False,
            why="availableSafe02 Clue04: floor by act_mr_stand_waitimpatient"),
    0xEC7B1D74FEC69D09: dict(kind="clue", to=(82.69, -37.86, 16.60), anchor=0xD2B63BD0A56E3BA5, table=False,
            why="availableSafe02 Clue05: floor by act_mr_stand_look_up"),
    0x7061B8C616D0B2FE: dict(kind="guard", to=(80.10, -34.64, 16.60), anchor=0xBD852C1C535791FF,
            why="availableSafe02's guard, beside its first act; 0.65 m off the design's place, clear of Icon acts"),
    0xBD852C1C535791FF: dict(kind="act", to=(81.86, -35.51, 16.60), anchor=0xD4D43774B0C4A1B3,
            why="availableSafe02's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act"),
    0x50850F8514E312FD: dict(kind="act", to=(76.21, -28.37, 16.60), anchor=0xDAA87A3ACEFEC69B,
            why="availableSafe02's guard act act_mr_stand_lean_smoke_rail, 1.2 m from an Icon act"),
    0x18651C38CAA42C14: dict(kind="act", to=(82.90, -32.10, 16.60), anchor=0x42D28A35DCCAD1B2,
            why="availableSafe02's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act"),
    # availableSafe03: Setpiece_Evergreen_Container_Items_Safe_Unique_03 c71204af550be863 (-43.5, -16.9, 6.9) -> SFX w
    0xC71204AF550BE863: dict(kind="safe", to=(22.85, -3.58, 12.80), anchor=0xE1DFC90938737DFC,
            why="SFX workshop back street, by act_mr_stand_wall_lean_phone_text_shoulder_left"),
    0x5EC1FF9391E8B295: dict(kind="clue", to=(23.83, -5.85, 12.81), anchor=0xDB972B8038354626, table=False,
            why="availableSafe03 Clue01: floor by act_mr_stand_armscrossed"),
    0xCF074799648CDE24: dict(kind="clue", to=(32.92, 4.48, 12.41), anchor=0x157D9D03455FED60, table=False,
            why="availableSafe03 Clue02: floor by act_mr_stand_clipboard"),
    0xBE91769446741140: dict(kind="clue", to=(32.51, -4.61, 12.83), anchor=0x029BA91B15840EAE, table=False,
            why="availableSafe03 Clue03: floor by act_mr_stand_neutral"),
    0x8ABDCDE569C33452: dict(kind="clue", to=(24.05, -14.23, 12.82), anchor=0x05B9B374EC44264C, table=False,
            why="availableSafe03 Clue04: floor by act_mr_stand_armsback"),
    0xA085E109375F0CD6: dict(kind="clue", to=(28.88, 0.63, 12.60), anchor=0x8B804C3736B7E897, table=False,
            why="availableSafe03 Clue05: floor by act_mr_stand_clipboard"),
    0x1CF17095DFA1C1B9: dict(kind="guard", to=(20.60, -9.96, 12.82), anchor=0x2F4A4B67B3174B55,
            why="availableSafe03's guard, beside its first act"),
    0x2F4A4B67B3174B55: dict(kind="act", to=(21.80, -11.16, 12.82), anchor=0x5EF888219A16399C,
            why="availableSafe03's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act"),
    0x8938929F2F8B8B58: dict(kind="act", to=(29.20, -4.42, 12.70), anchor=0x741E971C81F1FE55,
            why="availableSafe03's guard act act_mr_crouch_look_for_keys, 1.2 m from an Icon act"),
    0xE6F0E85FC0FDAC06: dict(kind="act", to=(21.31, 2.62, 13.24), anchor=0xE90D3C1FA926424F,
            why="availableSafe03's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act"),
    # availableSafe04: Setpiece_Evergreen_Container_Items_Safe_Unique_05 6ac0794557e04b59 (-42.1, -51.5, 27.3) -> crew
    0x6AC0794557E04B59: dict(kind="safe", to=(26.41, -84.42, 13.33), anchor=0xC164228A4347ADF1,
            why="crew entrance (south gate), by act_mr_stand_armscrossed"),
    0x92787C3E72D26B5F: dict(kind="clue", to=(24.64, -80.49, 13.46), anchor=0x5D92CB701065555D, table=False,
            why="availableSafe04 Clue01: floor by act_mr_stand_smoke"),
    0x8DC81B47671CBB5D: dict(kind="clue", to=(31.64, -73.94, 12.82), anchor=0xE9E708438CE96E15, table=False,
            why="availableSafe04 Clue02: floor by act_mr_stand_angry"),
    0x1D95AE390D996113: dict(kind="clue", to=(22.28, -85.56, 13.00), anchor=0x2CBB09F8B06A7974, table=False,
            why="availableSafe04 Clue03: floor by act_mr_stand_angry"),
    0x9B4F745BF9DEE2CF: dict(kind="clue", to=(30.88, -73.55, 12.82), anchor=0x9497D1584C6445A0, table=False,
            why="availableSafe04 Clue04: floor by act_mr_cross_stand_angry"),
    0x9876DF95BFF0401C: dict(kind="clue", to=(41.42, -83.25, 12.94), anchor=0x772FCFA8FDCC3864, table=True,
            why="availableSafe04 Clue05: table (item spawner ItemSpawner43)"),
    # availableSafe05: Setpiece_Evergreen_Container_Items_Safe_Unique_01 e38d51621482f60c (-127.0, -119.0, 17.7) -> py
    0xE38D51621482F60C: dict(kind="safe", to=(78.66, 17.16, 12.54), anchor=0xA6C27D5E879A9A01,
            why="pyrotechnics entrance yard, by act_mr_stand_armscrossed"),
    0x191AFFCD6E815DFD: dict(kind="clue", to=(81.98, 20.04, 12.52), anchor=0x70043F11D048A4A5, table=False,
            why="availableSafe05 Clue01: floor by act_fr_stand_rarmside"),
    0x8C308887EF83CE5C: dict(kind="clue", to=(73.97, 8.98, 12.48), anchor=0x3558EB231A786387, table=False,
            why="availableSafe05 Clue02: floor by act_mr_stand_armscrossed"),
    0xF66658721F118011: dict(kind="clue", to=(71.78, 5.73, 12.61), anchor=0x18467D26E97700BE, table=False,
            why="availableSafe05 Clue03: floor by act_mr_stand_smoke_armscrossed"),
    0xF66B59C40DECBF0F: dict(kind="clue", to=(86.18, -2.33, 12.70), anchor=0x4A6DBDE82E988D54, table=False,
            why="availableSafe05 Clue04: floor by act_fr_sit_couch_relaxed"),
    0x9DBE3F5106788240: dict(kind="clue", to=(77.00, -1.78, 12.86), anchor=0xE906B100F643A720, table=False,
            why="availableSafe05 Clue05: floor by act_mr_stand_smoke"),
    # availableSafe06: Setpiece_Evergreen_Container_Items_Safe_Unique_06 3cc72354dd5e4666 (-68.8, -19.0, 18.1) -> toil
    0x3CC72354DD5E4666: dict(kind="safe", to=(66.34, -45.79, 12.01), anchor=0x639BB7E6E422833F,
            why="toilets, lower street, by act_mr_stand_mirror_macho"),
    0x2B60D3A820ABF7CC: dict(kind="clue", to=(68.73, -43.33, 12.00), anchor=0xF9716ADAB34A543A, table=False,
            why="availableSafe06 Clue01: floor by act_fh_stand_inspect_front"),
    0x6CCC36B52AFE3117: dict(kind="clue", to=(69.46, -36.58, 12.00), anchor=0xD740E4F36A6B12DD, table=False,
            why="availableSafe06 Clue02: floor by act_mr_stand_puzzled"),
    0x16021EF149457DD2: dict(kind="clue", to=(74.19, -43.92, 12.00), anchor=0xFC8EC0934DAB166B, table=False,
            why="availableSafe06 Clue03: floor by act_mr_stand_wall_lean_left_armscrossed"),
    0x8E07E74AE68427BC: dict(kind="clue", to=(67.57, -39.57, 12.01), anchor=0x131F3B0264B73053, table=False,
            why="availableSafe06 Clue04: floor by act_fh_stand_inspect_ground"),
    0x5E611B2ACD3C00D4: dict(kind="clue", to=(71.93, -41.87, 12.00), anchor=0xFB169394185C24FC, table=False,
            why="availableSafe06 Clue05: floor by act_mr_stand_anxious_looking_around"),
    0x04EC2CA6E85ECBB8: dict(kind="guard", to=(63.87, -39.98, 12.00), anchor=0x1FD9C72BC5BF0AAE,
            why="availableSafe06's guard, beside its first act"),
    0x1FD9C72BC5BF0AAE: dict(kind="act", to=(65.07, -41.18, 12.00), anchor=0xDBBDB860ADA2A7D7,
            why="availableSafe06's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act"),
    0x301D87785C6F9C83: dict(kind="act", to=(71.39, -38.01, 12.00), anchor=0x424DB824679022F8,
            why="availableSafe06's guard act act_mr_stand_lookatscreen_pace, 1.2 m from an Icon act"),
    0x68118635E6BC5C59: dict(kind="act", to=(76.66, -45.34, 12.00), anchor=0x44FC554E32885A06,
            why="availableSafe06's guard act act_mr_sit_phone_text_lean_forward, 1.2 m from an Icon act"),
    # availableSafe07: Setpiece_Evergreen_Container_Items_Safe_Unique_02 172b7edb9b82ff99 (-55.7, -17.0, 22.7) -> back
    0x172B7EDB9B82FF99: dict(kind="safe", to=(29.60, 17.65, 9.92), anchor=0x76D8AF9FB1328832,
            why="backstage street, by act_mr_stand_armscrossed"),
    0x120EAF984A9944C7: dict(kind="clue", to=(29.98, 14.97, 10.13), anchor=0x8E975BDAF60BF8ED, table=False,
            why="availableSafe07 Clue01: floor by act_mr_stand_phone_filming; 1.45 m off the design's place, clear of "
                "Icon acts"),
    0xA8C068FC084ACF8A: dict(kind="clue", to=(27.18, 15.76, 9.93), anchor=0xB34B2B7FB4A90E16, table=False,
            why="availableSafe07 Clue02: floor by act_fr_merchant_stand_marketsale_look_02; 0.35 m off the design's "
                "place, clear of Icon acts"),
    0x3E94850C5A17B57A: dict(kind="clue", to=(28.24, 14.69, 10.11), anchor=0xD5CB0C4ECCFBF31D, table=False,
            why="availableSafe07 Clue03: floor by act_fr_stand_neutral"),
    0x74E6F6163ADDDE9D: dict(kind="clue", to=(32.56, 14.97, 10.16), anchor=0xCCE2CAB027503637, table=False,
            why="availableSafe07 Clue04: floor by act_fh_stand_inspect_front"),
    0x10521450856C13D8: dict(kind="clue", to=(28.02, 15.38, 9.94), anchor=0x1CE1D2B019F9FD46, table=False,
            why="availableSafe07 Clue05: floor by act_fr_stand_rarmside"),
    0x0A19F9D9E5F138FC: dict(kind="guard", to=(26.54, 15.86, 10.09), anchor=0xC48AC1B393DE0067,
            why="availableSafe07's guard, beside its first act; 0.45 m off the design's place, clear of Icon acts"),
    0xC48AC1B393DE0067: dict(kind="act", to=(28.87, 14.41, 10.09), anchor=0x9ADA5CF2E276A3A3,
            why="availableSafe07's guard act act_mr_stand_think_100cm, 1.2 m from an Icon act; 0.95 m off the "
                "design's place, clear of Icon acts"),
    0xA9B094F850C787C2: dict(kind="act", to=(32.88, 15.70, 9.95), anchor=0x1BD19FF119B5DF24,
            why="availableSafe07's guard act act_mr_guard_stand_armsback, 1.2 m from an Icon act"),
    0x096329EEFD9BCBAF: dict(kind="act", to=(29.33, 13.72, 10.18), anchor=0xD10CF7D073C04839,
            why="availableSafe07's guard act act_mr_stand_lean_look_down_rail, 1.2 m from an Icon act"),
    # availableStash04: Setpiece_Evergreen_Stash_Unique_Spawnable_Attic (-42.1, -43.4, 32.6)
    0xCECC62420EA3E886: dict(kind="stash", to=(78.08, -17.46, 12.00), anchor=0x2DC71CCE2E3886CF,
            why="town hall basement, east room, by zagitatedguardpointentity"),
    # availableStash05: Setpiece_Evergreen_Stash_Unique_Spawnable_Fortress (-93.0, -114.1, 0.5)
    0xE180060CA8D5AF5A: dict(kind="stash", yaw=82.8, to=(26.04, -79.59, 13.46), anchor=0x5D92CB701065555D,
            why="crew entrance (south gate), by act_mr_stand_smoke"),
    # availableStash06: Setpiece_Evergreen_Stash_Unique_Spawnable_Observatory (-73.3, -10.6, 18.1)
    0x61801C45B2D2D9A8: dict(kind="stash", to=(32.83, 12.87, 10.13), anchor=0x8E975BDAF60BF8ED,
            why="backstage street, by act_mr_stand_phone_filming"),
    # availableStash07: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea_01 (-55.8, -25.4, 22.7)
    0xC8728B2DCBEB839D: dict(kind="stash", to=(20.95, -16.33, 12.82), anchor=0x05B9B374EC44264C,
            why="SFX back street, by act_mr_stand_armsback"),
    # availableStash08: Setpiece_Evergreen_Stash_Unique_Spawnable_Cavern (-94.5, -50.5, 7.7)
    0x458497EC4C5B41ED: dict(kind="stash", yaw=-73.7, to=(82.88, 17.44, 12.52), anchor=0x70043F11D048A4A5,
            why="pyrotechnics entrance yard, by act_fr_stand_rarmside"),
    # availableStash09: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea (-51.9, -78.9, -1.2)
    0x61AB6CD8DFE23AEA: dict(kind="stash", yaw=62.5, to=(78.37, -24.18, 16.60), anchor=0x975EFBC730426087,
            why="canteen kitchen, by act_mr_stand_phone_text"),
    # availableStash10: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea_03 (-56.8, -34.1, 7.8)
    0x083ED9CFC66A0B45: dict(kind="stash", to=(68.44, -64.09, 16.60), anchor=0x5A7848CB8C592611,
            why="Q&A square, south end, by act_mr_stand_inspect_front"),
    # availableStash11: Setpiece_Evergreen_Stash_Unique_Spawnable_InsideRadioTowerFence (-57.0, 83.1, -1.5)
    0xAC7078F3BA0A6BC2: dict(kind="stash", to=(60.97, 41.82, 9.56), anchor=0x9EF63603C95F9B32,
            why="north street (toilet lady), by act_mr_sit_couch_relaxed"),
    # availableStash12: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea_06 (-64.9, -7.9, 6.9)
    0x87879189C4CD8E40: dict(kind="stash", to=(60.15, -56.00, 15.20), anchor=0x345822638B8F00D0,
            why="start area, east stairs, by act_mr_stand_armsback"),
    # availableStash15: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea_04 (-38.5, -13.1, 9.3)
    0xE6E61B72759E4101: dict(kind="stash", to=(52.45, -45.72, 12.82), anchor=0x427F0C937051F2BF,
            why="movie set generator yard, by act_mr_stand_wall_lean_smoke"),
    # availableStash16: Setpiece_Evergreen_Stash_Unique_Spawnable_PlaneArea_05 (-69.8, -25.2, 0.4)
    0x98C00F80127A67AC: dict(kind="stash", yaw=-0.1, to=(75.37, 10.18, 12.48), anchor=0x3558EB231A786387,
            why="pyrotechnics entrance, north, by act_mr_stand_armscrossed"),
    # availableMule01: Setpiece_Evergreen_MoneyMuleSetup_Unique f92be591ce991b6c (Gianmario Mule) -> start area loop
    0xF92BE591CE991B6C: dict(kind="mule", to=(38.00, -63.24, 12.82), anchor=0x1847F47AACDA77EC,
            why="availableMule01's start, at its first act"),
    0x1847F47AACDA77EC: dict(kind="act", to=(38.00, -63.24, 12.82), anchor=0x05E79E195D8C1D29,
            why="availableMule01 act act_mr_sit_ground_edge, by an Icon act_mr_stand_armscrossed"),
    0xFC8DC8C2229870ED: dict(kind="act", to=(54.67, -51.81, 12.82), anchor=0xDF9D24C756DB6249,
            why="availableMule01 act act_mr_stand_think_100cm, by an Icon act_mr_stand_inspect_front"),
    0x65032BEDC5237E8D: dict(kind="act", to=(59.78, -59.57, 15.20), anchor=0x9659D398E61C9FA1,
            why="availableMule01 act act_mr_sit_phone_text_lean_forward, by an Icon act_mr_stand_armscrossed"),
    # availableMule03: Setpiece_Evergreen_MoneyMuleSetup_Unique_02 f498e3b273437a5f (Gianmario Mule) -> pyrotechnics e
    0xF498E3B273437A5F: dict(kind="mule", to=(71.80, 4.26, 12.67), anchor=0xFA9EC36BA2A9FBD7,
            why="availableMule03's start, at its first act; 1.10 m off the design's place, clear of Icon acts"),
    0xFA9EC36BA2A9FBD7: dict(kind="act", to=(71.80, 4.26, 12.67), anchor=0xC5F1059C63947ECD,
            why="availableMule03 act act_fr_stand_look_far, by an Icon act_mr_stand_wavearms_big; 1.10 m off the "
                "design's place, clear of Icon acts"),
    0x5099851A2A4D0FDD: dict(kind="act", to=(82.68, 19.44, 12.52), anchor=0x70043F11D048A4A5,
            why="availableMule03 act act_fr_stand_clapping_01, by an Icon act_fr_stand_rarmside"),
    0x4DFB949A9AEB793F: dict(kind="act", to=(77.70, -2.38, 12.86), anchor=0xE906B100F643A720,
            why="availableMule03 act act_fr_stand_smoke, by an Icon act_mr_stand_smoke"),
    # availableMule04: Setpiece_Evergreen_MoneyMuleSetup_Unique_03 7b8413891f12d291 (Gianmario Mule) -> SFX back stree
    0x7B8413891F12D291: dict(kind="mule", to=(24.75, -14.83, 12.82), anchor=0x48EE458FD2DD19C0,
            why="availableMule04's start, at its first act"),
    0x48EE458FD2DD19C0: dict(kind="act", to=(24.75, -14.83, 12.82), anchor=0x05B9B374EC44264C,
            why="availableMule04 act act_mr_sit_chairbasic_leanforward, by an Icon act_mr_stand_armsback"),
    0xAB8FA9B06D030D8B: dict(kind="act", to=(29.58, 0.03, 12.60), anchor=0x5404F7746E71C45B,
            why="availableMule04 act act_mr_crouch_inspect_0cm, by an Icon act_mr_stand_clipboard"),
    0xB49FA56336150245: dict(kind="act", to=(17.11, -7.94, 13.51), anchor=0x67EFE433D69F9E88,
            why="availableMule04 act act_mr_sit_phone_text_lean_forward, by an Icon act_mr_stand_inspect_100cm"),
    # availableMule05: Setpiece_Evergreen_MoneyMuleSetup_Unique_07 56d0c7a495270ef9 (Gianmario Mule) -> north street a
    0x56D0C7A495270EF9: dict(kind="mule", to=(60.23, 38.58, 9.45), anchor=0x2A129BDAB689DF47,
            why="availableMule05's start, at its first act"),
    0x2A129BDAB689DF47: dict(kind="act", to=(60.23, 38.58, 9.45), anchor=0xC7D0716632C9A7E8,
            why="availableMule05 act act_fr_stand_lean_back_wall_smoke, by an Icon act_fr_stand_tablet_read"),
    0xA9A87CC859A2EDD0: dict(kind="act", to=(49.82, 20.04, 9.22), anchor=0xB2B7BC1C200B395F,
            why="availableMule05 act act_mr_stand_phone_text_pace, by an Icon act_mr_sit_phone_relaxed"),
    0xAA913AC0188DD0FD: dict(kind="act", to=(33.03, 38.53, 7.30), anchor=0x2CE8488247AA47B4,
            why="availableMule05 act act_mr_stand_lean_left_shoulder_armscrossed_wall, by an Icon "
                "act_fr_sit_ground_01"),
    # availableMule07: Setpiece_Evergreen_MoneyMuleSetup_Unique_06 7163d40b618973c5 (Gianmario Mule) -> Q&A square and
    0x7163D40B618973C5: dict(kind="mule", to=(65.98, -67.13, 16.60), anchor=0x94AAC20CBA0009EB,
            why="availableMule07's start, at its first act"),
    0x94AAC20CBA0009EB: dict(kind="act", to=(65.98, -67.13, 16.60), anchor=0x7E3995F4ED60AE0B,
            why="availableMule07 act act_fr_stand_smoke, by an Icon act_mr_stand_rail_smoke"),
    0x12EA7EDFFE2CD2B5: dict(kind="act", to=(72.26, -62.87, 16.60), anchor=0x95D4ABF74999E6ED,
            why="availableMule07 act act_fr_sit_bench_phone_text, by an Icon act_mr_stand_confused; 0.95 m off the "
                "design's place, clear of Icon acts"),
    0xC5CEE049D9319D0A: dict(kind="act", to=(58.22, -45.76, 16.60), anchor=0xCA53F2CD3F0B0227,
            why="availableMule07 act act_fr_stand_look_far, by an Icon act_fr_stand_impatient"),
    # availableMule08: Setpiece_Evergreen_MoneyMuleSetup_Unique_04 c484b67115efb6f3 (Gianmario Mule) -> canteen terrac
    0xC484B67115EFB6F3: dict(kind="mule", to=(74.19, -33.28, 16.60), anchor=0xA3D1770D2C5AC48A,
            why="availableMule08's start, at its first act"),
    0xA3D1770D2C5AC48A: dict(kind="act", to=(74.19, -33.28, 16.60), anchor=0x70DC4C36FB362A8D,
            why="availableMule08 act act_mr_stand_takepicture_phone_100cm, by an Icon "
                "act_mr_stand_drink_waterbottle_100cm"),
    0x6D95714DE4D43789: dict(kind="act", to=(80.67, -22.18, 16.60), anchor=0x975EFBC730426087,
            why="availableMule08 act act_mr_stand_lookatcar_wave, by an Icon act_mr_stand_phone_text"),
    0x78E877B67E8FCDCB: dict(kind="act", to=(70.57, -37.26, 16.60), anchor=0x43C3DA2319868331,
            why="availableMule08 act act_mr_stand_takepicture_phone_100cm, by an Icon act_mr_stand_armscrossed; 0.25 "
                "m off the design's place, clear of Icon acts"),
    # The suppliers: a supplier will not deal while someone is about (Freelancer's "busy"), and each helper carries
    # its private volume (Garage: 5.9 x 17.9 x 3.0 m and 7.4 x 16.8 x 3.0 m, the vanilla rooms' size) wherever it
    # moves. So a supplier goes where no act of The Icon, of our bricks or of The Bad Boy's layer (mission_caipirinha)
    # is inside that volume or within placement.SUPPLIER_GAP_M of the helper (the clash audit's privacy
    # check, tools/landslide/landslide_audit.py). The first places were beside Icon acts: Supplier01 on the
    # start-area stairs (66.71, -61.48, 15.20) had an Icon bystander's act_mr_stand_wall_lean_smoke 2.5 m away, a kept
    # target's act and the Q&A balcony's five standing NPCs inside its volume, and was busy all the time in the first
    # full Bad Boy session;
    # Supplier02 in the street below the Apartment (28.00, 8.00, 11.73) had two Icon acts in its volume, one 3 m away.
    # availableSupplier02: Helper_Evergreen_SupplierPackage_02 (16.4, 14.8, 16.7; upstairs, off The Icon's navmesh) ->
    # the street north of the backstage (Streets_D, by the optician's), 10.6 m from the nearest act
    0x48ADF8D57AEB75FC: dict(kind="supplier", to=(26.00, 27.00, 7.32), anchor=0xB462E94B3256EFBA,
            why="the street north of the backstage, 11 m from an Icon act_mr_stand_smoke_armscrossed (Sunset_Beach); "
                "its vanilla place upstairs is not on The Icon's navmesh, and its first place (28.00, 8.00, 11.73) "
                "had Icon acts in its private volume"),
    # availableSupplier01: Helper_Evergreen_SupplierPackage_01 (-11.3, -53.0, 13.5) -> the Q&A square's south end
    0xFADD3FE97A972ADA: dict(kind="supplier", to=(74.00, -72.00, 16.75), anchor=0x8A5E463C9B65EBE3,
            why="the Q&A square's south end, by the ivy wall, 9 m from an Icon act_mr_sit_sleeping; its first place, "
                "the start-area stairs (66.71, -61.48, 15.20), was never free of NPCs"),
    # availableStash02: Stash Toilet (73.80, -37.28, 12.00), kept by the design, is inside mission_copperhead's
    # CollisionBox82 (aa51c3cf259b6bd3, 1.39 x 1.22 x 1.94 m, m_bCollideHitman; and 0.3 m from CollisionBox83), the
    # blocker round the film crew's storage corner of the toilets (Dressing_Copperhead_Toilets: road cases, a set lamp,
    # crates of water): The Icon's navmesh has no floor within 1.5 m of it
    # (tools/landslide/landslide_audit.py). The blocker is not decoration, so the stash moves, as the design moves the
    # set's other spots: 2.6 m south-west, onto the open floor in front of that corner, still in the toilets.
    0xF4BFBC679E57741E: dict(kind="stash", to=(72.03, -39.05, 12.15), anchor=0x424DB824679022F8,
            why="toilets: in front of the film crew's storage corner, by an Icon act_mr_stand_inspect_front; its "
                "vanilla place is inside mission_copperhead's CollisionBox82"),
}

# Acts retyped where they move: act -> (its vanilla type, the new type, an entity of override_bingo of the new type
# whose type it takes). Sit and lean acts need a seat, a rail or a wall that the new place does not guarantee; a
# standing act works anywhere. The mules' and, by the same rule, three safe guards' acts.
# build_bricks.py checks both types by name. A retyped act takes the like entity's entity subsets too
# (brick_edit.Brick.retype): act_mr_stand_think_100cm and act_fr_stand_look_far have no Replicable subset, and five of
# these acts left in override_bingo's Replicable subset crashed every full Icon load (HITMAN3.exe+0xF2DE19).
RETYPE = {
    0x50850F8514E312FD: ("act_mr_stand_lean_smoke_rail", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x68118635E6BC5C59: ("act_mr_sit_phone_text_lean_forward", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x096329EEFD9BCBAF: ("act_mr_stand_lean_look_down_rail", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x1847F47AACDA77EC: ("act_mr_sit_ground_edge", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x65032BEDC5237E8D: ("act_mr_sit_phone_text_lean_forward", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x48EE458FD2DD19C0: ("act_mr_sit_chairbasic_leanforward", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0xB49FA56336150245: ("act_mr_sit_phone_text_lean_forward", "act_mr_stand_think_100cm", 0xFC8DC8C2229870ED),
    0x2A129BDAB689DF47: ("act_fr_stand_lean_back_wall_smoke", "act_fr_stand_smoke", 0x4DFB949A9AEB793F),
    0xAA913AC0188DD0FD: ("act_mr_stand_lean_left_shoulder_armscrossed_wall", "act_mr_stand_phone_text_pace",
                         0xA9A87CC859A2EDD0),
    0x12EA7EDFFE2CD2B5: ("act_fr_sit_bench_phone_text", "act_fr_stand_look_far", 0xC5CEE049D9319D0A),
}

# The approach paths of the moved mules' acts (Mule01's and Mule04's) route them through the old town: each moved act
# loses its m_aApproachPath, and these ApproachPathPoint entities go.
DELETE_APPROACH_POINTS = [
    0xE404E9CDE2090B88, 0xA62301F99A268B55, 0xD66C15157543C1C5, 0x4703EC96E769A76C,  # Mule01's
    0x9386F060EEAC9A60, 0x2C9A3A5EB4FE8101, 0x39DEE85D2F291796, 0x9FB2E5A488817B2E,  # Mule04's
]

# Moved with an entity by the same offset: the supplier helper's drama act (root-parented), with the design's nominal
# place as a cross-check. The helper's private volume (Garage) and its NPC hang off the helper and follow it (checked).
RIGID = {
    0xFADD3FE97A972ADA: [(0x3CD21E85D25C056C, (72.16, -70.43, 16.75))],  # Act_MR_Crouch_Clean_Surface_000cm
    # Act_MR_Stand_Lean_Back_Smoke_Wall, the delivery man's act 0.5 m from his helper (untested: no wall behind it
    # is guaranteed at the new place).
    0x48ADF8D57AEB75FC: [(0x04B4363D1BB53FB4, (26.17, 27.48, 7.32))],
}

# Freelancer's four exits ("Any exit used" in the mission brick): push buttons whose Exit pin starts an exit sequence,
# whose end triggers the button. Under The Icon:
# - Exit_Evergreen_PushButton (the ruins boat, -144, -138, -1; far outside the set) moves onto Dino's sports car:
#   The Icon's Exit_UseKey_Sportscar, wired there to Sportscar_Exit_HM (Exit -> StartExitSequence, ExitSequenceOver
#   -> Trigger, checked) and to an event transmitter (Exit -> TransmitEvent, checked, re-made for the button). The
#   Icon's own exit is deleted (it ends the mission outside Freelancer's exit logic), and with it its need for the
#   Bosco car key: like every Freelancer exit, the button needs no item.
# - Exit_Evergreen_PushButton_02 (the cave seaplane, -60, -77, -1; in the closed caves) moves to the old-town gate,
#   where mission_copperhead puts location's exit fb8dfcd7a113e19b (a property override, taken as the button's
#   transform; confirmed by mission_mamba's own world-placed copy of that exit), wired as location wires it: its
#   sequence c821c3e0ff6c3888 is started by Play and ends in fb8d's Trigger (the pin connection override_bingo deletes,
#   checked). Its location enable conditions belonged to the cave and go.
# - Exit_Evergreen_PushButton_03 (the road car, 29, -66, 13; inside the set by the crew entrance) is deleted with its
#   pin overrides and leaves "Any exit used": The Icon deletes the car
#   it belongs to and piles rubble, a collision box and a collision capsule on its place, so the button stands inside a
#   blocker with no floor about it (the clash audit, landslide_audit.py), and its sequence
#   44724f00f7782bb3 drives off in a car that is not there. The Icon then has two Freelancer exits.
# - Exit_Evergreen_PushButton_01 (the harbor speedboat, -93, 67, -1) is deleted with its pin overrides and leaves "Any
#   exit used": The Icon deletes its sequence 8c6e2d48c6b60954 and has no fourth exit.
EXIT_MOVES = {
    0x08DB2FE9103A73CF: dict(
        place=("entity", MISSION_COPPERHEAD[0], 0xEB4CA2663E5F712E),
        old=(LOCATION, 0x332B67E5FC48813A, "StartExitSequence", "ExitSequenceOver"),
        new=(MISSION_COPPERHEAD[0], 0xF0937CC2750F853C, "StartExitSequence", "ExitSequenceOver"),
        extra=[("Exit", (MISSION_COPPERHEAD[0], 0x3C17A7D72F7117E8), "TransmitEvent")],
        wired_by="brick", drop=[],
        why="Exit_Evergreen_PushButton (the ruins boat, -144, -138, -1) -> Dino's sports car (The Icon's "
            "Exit_UseKey_Sportscar, Sportscar_Exit_HM)"),
    0x6721EBE4C0FDED96: dict(
        place=("override", LOCATION, 0xFB8DFCD7A113E19B),
        old=(LOCATION, 0xC87853C91C0AB145, "StartExitSequence", "ExitSequenceOver"),
        new=(LOCATION, 0xC821C3E0FF6C3888, "Play", "OnStop"),
        wired_by="vanilla", drop=["m_aEnableConditions"],
        confirm=(MISSION_MAMBA[0], 0x6D437E5C6DCE641A),
        why="Exit_Evergreen_PushButton_02 (the cave seaplane, -60, -77, -1) -> the old-town gate (location's exit "
            "fb8dfcd7a113e19b where The Icon puts it)"),
}
UNMOVED_EXITS = []
DELETE_EXITS = {
    0xD5AA959458203ABF: "Exit_Evergreen_PushButton_03 (the road car, 29, -66, 13): The Icon deletes that car "
                        "(location's Avance_Alpha_1_A_00 3a579881fa46f497) and its Exit_Road and piles rubble on its "
                        "place: the button stands inside mission_copperhead's CollisionBox 266ab55a89495e5d (2.09 x "
                        "2.07 x 3.37 m) and CollisionCapsule06, 0.58 m from Ruins_RubblePile_A_00_22, with no navmesh "
                        "floor about it; its sequence would drive off in a "
                        "car that is not there",
    0xC0632396F86D316E: "Exit_Evergreen_PushButton_01 (the harbor speedboat, -93, 67, -1, far outside the set; The "
                        "Icon deletes its sequence 8c6e2d48c6b60954 and has no fourth exit)",
}
MISSION_ANY_EXIT = 0xE320A6EE11DFC348  # mission_mild_bingo's "Any exit used" (m_aValues: the buttons' exitActivated)
DELETE_BASE_EXITS = [(0xEB4CA2663E5F712E, "Exit_UseKey_Sportscar")]

# --- Starting locations (hero spawns) --------------------------------------------------------------------------
#
# Under the swap the scene is _scene_copperhead plus our two bricks; build_bricks.py walks the reference tables of
# everything that loads and finds hero spawns in mission_copperhead (HeroSpawn_Copperhead_Main, The Icon's own start on
# the south road and the default) and agencymarkup_location (its six). agencymarkup_octopus is not loaded, so its five
# starts land on the default through the mod's runtime start fix (src/mod/HeroStart.h; the game does NOT fall back to
# the default itself, observed in game). For a Freelancer contract The Icon's long story intro is gated off by its
# ContractIdComparer (contract ...-000000000006), so the short walk-in (Copperhead_Intro_05) plays.
# agencymarkup_location's Cafe_Tower (canteen roof) stays (agencymarkup_copperhead hides it, m_bHide: untested
# whether a hidden start can still be picked; if not, the default plays); its four starts outside the set go, and so
# does the Apartment (above the SFX street, also hidden): its room (z 17) is not on The Icon's navmesh (the floor
# below it is the street, z 10.7), so it fails the navmesh check.
DEFAULT_HERO_SPAWN = 0xC66B2A6B05EA15FD  # mission_copperhead HeroSpawn_Copperhead_Main (43, -115, 12)
HERO_SPAWN_TYPE = 0x00F3EA8B2DB49189
HERO_SPAWN_REQUIRED = [MISSION_COPPERHEAD[0], AGENCYMARKUP_LOCATION, BLOCKEDOFF_MANSION[0]]
HERO_SPAWN_FORBIDDEN = [AGENCYMARKUP_OCTOPUS]
# Bricks whose blockers no start or spot may be inside: blockedoff_mansion's boxes, and mission_copperhead's collision
# boxes and pathfinder obstacles ("solid": its pathfinder boxes are navmesh generation hints).
BLOCKER_BRICKS = [(BLOCKEDOFF_MANSION, "boxes"), (MISSION_COPPERHEAD, "solid")]
SPOT_BLOCKER_CHECK = True

DELETE_HERO_SPAWNS = {
    AGENCYMARKUP_LOCATION: [
        (0x39560BA8C11B2FCD, "Harbour1_HeroSpawn", "(-76, 64, 0): the harbor, far outside the set"),
        (0x2064A8828CDF7084, "HeroSpawnSeqDir_Harbour", "the Harbor intro's sequence director"),
        (0xF9084ED3B4CEFBF6, "Ruins_HeroSpawn", "(-143, -138, -1): the castle ruins below the closed mansion"),
        (0xC12DEFBE5378DC32, "HeroSpawnSeqDir_Ruins", "the Ruins intro's sequence director"),
        (0xCBFC6AD8CE70D9B0, "Church_Coffin_HeroSpawn", "(-39, 98, 4): the church morgue, far outside the set"),
        (0x860F635D30031237, "HeroSpawnSeqDir_MorgueCoffin_Disguised", "the morgue intro's sequence director"),
        (0x341CA5E97CA5A69D, "AbandonedShop_HeroSpawn", "(21, 69, 5): the old town north of the set's gate"),
        (0x5D36DFA8F1182C03, "HeroSpawnSeqDir_AbandonedShop", "the AbandonedShop intro's sequence director"),
        (0xA1045ABC4C0991B2, "Apartment_HeroSpawn", "(27, 12, 17): an upstairs room The Icon's navmesh does not reach"),
        (0xFF60B2007A5E7B44, "HeroSpawnSequenceDirector", "the Apartment intro's sequence director (its m_rSpawner)"),
    ],
}
# Starts farther than build_bricks.REACH_M from The Icon's acts that are judged reachable anyway (with the navmesh,
# each still needs floor on the start's island).
HERO_SPAWN_EXCEPTIONS = {
    0xC66B2A6B05EA15FD: "HeroSpawn_Copperhead_Main (43, -115, 12), 36 m: The Icon's own start, where its walk-in "
                        "begins on the south road (its pathfinder seed points at y -94 and -98 mark the road)",
}
# location's 000_Outside (34.17, -35.76, 12.84), composed by build_bricks.folder_matrices (landslide_spec.py): the
# Icon's set dressings (Dressing_Copperhead_Outside) hang off it too.
LOCATION_WORLD_FOLDERS = {0xA3ABFC17113FA981}

# Clashes the audit (tools/landslide/landslide_audit.py) reports that were looked at and are left: (brick, entity id):
# why.
CLASH_ACCEPTED = {
    (MISSION_COPPERHEAD[0], 0x2676A9349A0617E9): "the CollisionBox of Dino's sports car (Italian_Sportscar_A_00, a "
                                                 "cut-sequence car with no collision of its own): "
                                                 "Exit_Evergreen_PushButton takes the place of The Icon's own exit "
                                                 "(Exit_UseKey_Sportscar) by its door",
    (MISSION_COPPERHEAD[0], 0x5C9C3D4E289E7E3D): "Italian_Sportscar_A_00, Dino's sports car, the exit's car (above)",
    (MISSION_COPPERHEAD[0], 0x9B94B4B043F46E31): "Movieset_Flightcases_A_Lid01, 0.54 m from the moved clue "
                                                 "Tablet_Unique_12 (9876df95bff0401c): the lid is part of a stack of "
                                                 "flight cases whose navmesh hole begins 0.3 m east of the clue; the "
                                                 "clue lies on the navmesh floor beside it, at The Icon's own item "
                                                 "spawner there (ItemSpawner43, 0.35 m)",
}

# --- The package's checks ----------------------------------------------------------------------------------------

# The vanilla resources the Icon's bricks were built against (gen_package_inc.py adds each TEMP's blueprint): the
# scene, its brick and the navmesh the positions were checked on, the bricks our copies reference by entity id, and
# the Freelancer sources they copy.
FINGERPRINTS = [
    (SCENE, "_scene_copperhead.entity"),
    (MISSION_COPPERHEAD[0], "mission_copperhead.brick"),
    (NAVMESH[0], NAVMESH[1]),
    (STREETS_NIGHT[0], "npc_ambient_streets_night.brick"),
    (AGENCYMARKUP_COPPERHEAD, "agencymarkup_copperhead.brick"),
    (EVERGREEN_SETUP, "evergreen_setup.brick"),
    (LOCATION, "location.brick"),
    (AGENCYMARKUP_LOCATION, "agencymarkup_location.brick"),
    (BLOCKEDOFF_MANSION[0], "blockedoff_mansion.brick"),
    (MISSION_MILD_BINGO[0], "mission_mild_bingo.brick"),
    (OVERRIDE_BINGO[0], "override_bingo.brick"),
    (0x004B6755A8F753FA, "scene_costaltown_mild_bingo.entity"),
]

# Not copied: scene_costaltown_mild_bingo's 20 vestigial entities; _scene_copperhead is used as it is. Not moved (the
# design's optional 3.6): four of Freelancer's extra item spawners outside the set; out of reach they do no harm.
