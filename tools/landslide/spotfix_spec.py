"""The spot fixes of appended map variants: for each option the clash audit (variant_audit.py) found
spoiling some of Freelancer's territory spots, a fix brick of ours
(fr_<option>) that the variant appends after the option's bricks, as the full Gluttony appends fr_gluttony
(gluttony_spec.py). What a fix brick does and how it is checked: spot_fix.py. Each class below is one spec: one
full version (src/core/LandslideFull.cpp, its key NAME, "appends": the contract's scene and bricks stay) in its
map's own package (Runtime/chunk<partition>patch290.rpkg).

How the places were chosen (from the game files of the Deck copy, build-landslide/audit/<map>):

- A supplier the option crowds is moved (MOVES) to the nearest floor of the scene's navmesh (the option's own when it
  has one, else Freelancer's) with 1 m of clearance on its main island, no act of any brick within 5 m in plan (as
  The Icon's moved suppliers, placement.SUPPLIER_GAP_M; the place chosen keeps a margin past it, nearest act given
  below), no act of the option inside its private volume (which moves with it) and no more of Freelancer's own than
  at its vanilla place, no solid prop within 0.8 m, other spots at least 2 m away. Its rotation is kept unless its
  NPC (who turns with it and faces his local -Y) would face a wall or a prop: then its yaw turns him to open floor
  (spot_fix.check fails one with under 3 m of navmesh in front of him: placement.supplier_problem).
- A stash the option puts a prop on is moved the least distance that clears it: floor within 0.6 m of its height, no
  prop within 0.6 m (none within 1 m chosen), no act within 2 m. A stash the option seals off is moved out to the
  nearest such floor, at least 3 m from every other spot and act. A moved stash keeps its rotation unless its open
  side (local -Y) would face a wall or a prop: then its yaw (placement.crate_yaw, a turn about up, level) turns it to
  the side with the most navmesh in front, and spot_fix.check fails a crate with under 0.8 m there, or one facing the
  tighter side (placement.crate_problem).
- Heights: the walking floor (placement.Placement.floor_z: the navmesh floor less the local offset of the navmesh
  above the acts, which stand on the floor; it gives the vanilla supplier of Santa Fortuna back within 0.01 m), never
  the navmesh's own height, which lies 0.05-0.2 m above the floor.
- A spot the option seals off (no floor near it on the option's navmesh, a closed area) or buries is not moved: its
  territory slot is re-pointed (REPOINTS) to a spot of the same kind that the audit found clean, as the full
  Landslide's SLOT_REPOINTS do. A safe has five clues and a guard, a mule its acts: re-pointing keeps each of those
  groups whole where IOI placed it.
- Unless no clean spot of the kind is left: Garden Show seals off all eight safes, so they move out (MOVES), each
  with its whole group (its five clues; where it has one, its guard and his three acts), as The Icon's
  (icon_spec.SPOT_MOVES). A moved safe: open, flat floor outside the mansion (not on steps), 1.4 m from the navmesh's
  edge, no act within 3 m (the privacy check's 2 m and a margin), no prop within 1.2 m, other spots at least 4 m and
  other safes 15 m away. Its clues lie on the floor (no table is known to be there) 4-14 m from it, spread round it as
  IOI's are, no act within 1.6 m, no prop within 1 m, other clues at least 3 m away. A guard stands at his first act
  2-4 m from the safe, his other acts 3-8 m from it, 2 m from any other act. Rotations are kept, so an act made for a
  wall or a mirror (Act_MR_Stand_Mirror_FreshenUp, Act_MR_Stand_Wall_Lean_Phone_Text_Shoulder_Right) plays in the
  open: a property override cannot retype it as icon_spec's RETYPE does.

BROKEN lists every spot of the audit's findings: the build fails if a slot still names one the fix does not move.
CLASH_ACCEPTED (landslide_audit.audit's accepted clashes) lists the audit's findings that are left on purpose.
"""
MISSIONS = "assembly:/_pro/scenes/missions/"


class SpotFix:
    """The defaults of a spot fix spec (spot_fix.py)."""
    SPOT_FIX = True
    MOVES = {}
    REPOINTS = {}
    DELETES = []
    BROKEN = {}
    CLASH_ACCEPTED = {}
    # An option brick in no mounted package (Sloth's outfits_skunk, chunk1): outfits only, left out of the checks.
    NOT_MOUNTED_OK = []


# --- Santa Fortuna (partition 18, "colombia") -------------------------------------------------------------------------

class SantaFortuna(SpotFix):
    PARTITION = (18, "colombia")
    SCENE_PATH = MISSIONS + "colombia/scene_hippo_mild_trompo.entity"
    SCENE = 0x000D46440A431296
    # Freelancer's scenes the option is offered on: (label, the scene and the contract's bricks, TEMP hashes).
    FREELANCER_SCENES = [
        ("mild", [0x000D46440A431296, 0x009A7356A1593833]),  # scene_hippo_mild_trompo, mission_mild_trompo
        ("showdown", [0x00819EC80A774632, 0x00B05AB27AE01D3B]),  # scene_hippo_hot_trompo, mission_hot_trompo
    ]
    OVERRIDE = (0x00A0CD2B099538B1, 0x00E8B8AA18F809D3)  # override_trompo
    # The fix brick's source: intel_6core_hippo, one of IOI's empty bricks (its root only), chunk18.
    FIX_SOURCE = (0x00E3D19796C52D70, 0x008A8786B2E168D5)
    FIX_SOURCE_NAME = "intel_6core_hippo"


class Revolutionary(SantaFortuna):
    """The Revolutionary (elusive_highball): a meeting round the planning table of the "murder house", about 20 acts
    inside the private volume of Helper_Evergreen_SupplierPackage_02 (availableSupplier01), the nearest 0.5 m, and two
    post-its on the table above him. He moves 10 m north, out of the house into the yard by the fruit truck (the
    nearest act 7.2 m away, under both scenes)."""
    NAME = "revolutionary"
    DISPLAY = "The Revolutionary"
    OPTION = "revolutionary"
    FIX_PATH = MISSIONS + "colombia/fr_revolutionary.brick"
    FIX_IDS = (0x00448854B4FB99A7, 0x00AC0F74ED273E49)
    APPEND_BRICKS = [(MISSIONS + "colombia/elusive_highball.brick", 0x008BF14772B57FFE, 0x002BD6EAE154F593)]
    MOVES = {
        0x13585097AA6EAE12: dict(kind="supplier", to=(-60.37, 310.23, 26.06),
                                 why="availableSupplier01 (Helper_Evergreen_SupplierPackage_02): the Highballers' "
                                     "meeting at his table (about 20 acts in his private volume, 0.5 m nearest)"),
    }
    BROKEN = {0x13585097AA6EAE12: "about 20 elusive_highball acts in its private volume, 0.5 m nearest"}
    FINGERPRINTS = [
        (0x008BF14772B57FFE, "elusive_highball.brick"),
        (0x00A0CD2B099538B1, "override_trompo.brick"),
        (0x00E3D19796C52D70, "intel_6core_hippo.brick"),
    ]


class EmbraceOfTheSerpent(SantaFortuna):
    """Embrace of the Serpent (scenario_anaconda): 14 acts of its soldiers inside the private volume (17.5 x 17.7 m)
    of Helper_Evergreen_SupplierPackage_01 (availableSupplier02), the nearest 0.6 m. The volume is large and the
    soldiers are all over that part of the jungle: the nearest floor with none of them in it, and no act within 5 m,
    is 40 m west, in the tunnel to the village (the nearest act 10.4 m away)."""
    NAME = "serpent"
    DISPLAY = "Embrace of the Serpent"
    OPTION = "embrace_of_the_serpent"
    FIX_PATH = MISSIONS + "colombia/fr_embrace_of_the_serpent.brick"
    FIX_IDS = (0x00228DA6AB636C57, 0x00C48A11270837B5)
    APPEND_BRICKS = [(MISSIONS + "colombia/scenario_anaconda.brick", 0x008AB510D55AF5A3, 0x00DB1E9F60E0C44E)]
    MOVES = {
        0xD746966F6D8C778A: dict(kind="supplier", to=(-106.01, 143.30, 27.46),
                                 why="availableSupplier02 (Helper_Evergreen_SupplierPackage_01): 14 scenario_anaconda "
                                     "acts in his private volume, 0.6 m nearest"),
    }
    BROKEN = {0xD746966F6D8C778A: "14 scenario_anaconda acts in its private volume, 0.6 m nearest"}
    FINGERPRINTS = [
        (0x008AB510D55AF5A3, "scenario_anaconda.brick"),
        (0x00A0CD2B099538B1, "override_trompo.brick"),
        (0x00E3D19796C52D70, "intel_6core_hippo.brick"),
    ]


# --- Hokkaido (partition 22, "hokkaido") ------------------------------------------------------------------------------

class Reflection(SpotFix):
    """The Reflection (scenario_bellini, the Mirror World): two stashes against its new props, two spots and a mule
    in areas its navmesh leaves out, and the lobby supplier with three of its acts 3.3-4.3 m away.

    - Stash10 (Spawnable_07) stands 0.45 m in front of a 2 m divider: 0.9 m out from it.
    - Stash09 (Spawnable_14) stands 0.5 m in front of a mirror window: 1.4 m out from its plane.
    - Supplier01 (Helper_Evergreen_SupplierPackage_01): three bellini acts within 5 m (3.3 m nearest): 10 m west along
      the same floor (the nearest act 6.1 m away).
    - Stash01 (the garage, by exit 02), Stash08 and Mule04: no floor near them on Reflection's navmesh. Their slots
      name Stash04 (the basement corridor by the garage), Stash07 (5 m from Stash08) and Mule08.
    Exit 02 (the garage) is switched off at runtime (src/core/VariantExits.cpp), not here. Every start moves to the
    Mirror World's (src/mod/HeroStart.h)."""
    NAME = "reflection"
    DISPLAY = "The Reflection"
    OPTION = "reflection"
    PARTITION = (22, "hokkaido")
    SCENE_PATH = MISSIONS + "hokkaido/scene_snowcrane_mild_shogi.entity"
    SCENE = 0x00DAC05E7AF4B6E0
    FREELANCER_SCENES = [
        ("mild", [0x00DAC05E7AF4B6E0, 0x00D6C83B2487913C]),  # scene_snowcrane_mild_shogi, mission_mild_shogi
        ("showdown", [0x00E369E26027DEA4, 0x0003B182E78A42B8]),  # scene_snowcrane_hot_shogi, mission_hot_shogi
    ]
    OVERRIDE = (0x001B8721449A41C9, 0x00F4631DF8921D40)  # override_shogi
    FIX_PATH = MISSIONS + "hokkaido/fr_reflection.brick"
    FIX_IDS = (0x0038F581EB60055C, 0x0080449CFFAFEA03)
    FIX_SOURCE = (0x009126337D7A011F, 0x003DA9B47359FFDC)  # vr_overrides_flu_ps4_vr1 (root and one scene), chunk22
    FIX_SOURCE_NAME = "vr_overrides_flu_ps4_vr1"
    APPEND_BRICKS = [(MISSIONS + "hokkaido/scenario_bellini.brick", 0x002DAF48CF0FD7E6, 0x00C1F36A786202FF)]
    MOVES = {
        # Turned half round: its open side faced the divider 0.35 m off (placement.crate_yaw; a quarter turn does not
        # fit the corridor).
        0x229146F0DA45872A: dict(kind="stash", to=(-115.26, 37.81, -0.47), yaw=135.2,
                                 why="availableStash10 (Spawnable_07): 0.45 m from Divider_Auditorium_Consulate_2m_09"),
        0x0EC71FC9DB65DFD4: dict(kind="stash", to=(-32.67, 42.09, -0.47),
                                 why="availableStash09 (Spawnable_14): 0.5 m from window_hokkaido_200x360_mirror01"),
        0x1DF6C7786C368CE3: dict(kind="supplier", to=(-41.76, 45.37, -0.47),
                                 why="availableSupplier01 (SupplierPackage_01): three bellini acts 3.3-4.3 m away"),
    }
    REPOINTS = {
        "availableStash01": (0xD0A3101C8530B112, 0x2FEF65F84D0D9AE7,
                             "Spawnable (the garage, -68, -63, -9): sealed off -> Spawnable_03 (-14, -19, -5)"),
        "availableStash08": (0x3ECC61E8E7B60C1D, 0x28F104031598DD82,
                             "Spawnable_06 (-39, 12, -0.5): sealed off -> Spawnable_05 (-34, 9, -0.5)"),
        "availableMule04": (0x2ACBCA4B68A8D30C, 0x0EC14DE8AE808F96,
                            "MoneyMuleSetup_Unique_03 (-86, 62, 8): sealed off -> MoneyMuleSetup_Unique_06 "
                            "(-78, 17, -0.7)"),
    }
    BROKEN = {
        0x229146F0DA45872A: "Divider_Auditorium_Consulate_2m_09 0.45 m away",
        0x0EC71FC9DB65DFD4: "window_hokkaido_200x360_mirror01 0.52 m away",
        0x1DF6C7786C368CE3: "three bellini acts 3.3-4.3 m away",
        0xD0A3101C8530B112: "sealed off (the garage)",
        0x3ECC61E8E7B60C1D: "sealed off",
        0x2ACBCA4B68A8D30C: "sealed off",
    }
    FINGERPRINTS = [
        (0x002DAF48CF0FD7E6, "scenario_bellini.brick"),
        (0x001B8721449A41C9, "override_shogi.brick"),
        (0x009126337D7A011F, "vr_overrides_flu_ps4_vr1.brick"),
    ]


# --- Dartmoor (partition 3, "ancestral") ------------------------------------------------------------------------------

class Dartmoor(SpotFix):
    PARTITION = (3, "ancestral")
    SCENE_PATH = MISSIONS + "ancestral/scene_ancestral_mild_bridge.entity"
    SCENE = 0x007A3439FFD302E7
    # Garden Show and Sloth are offered on the mild scene only (Variants.cpp kDartmoorHot).
    FREELANCER_SCENES = [
        ("mild", [0x007A3439FFD302E7, 0x0050299D79861FA5]),  # scene_ancestral_mild_bridge, mission_mild_bridge
    ]
    OVERRIDE = (0x00E188E8DD90384D, 0x00C41F19933FF745)  # override_bridge
    FIX_SOURCE = (0x0018590AE0DD463E, 0x00670FD2E30A993D)  # vr_overrides_ancestral_bulldog (its root only), chunk3
    FIX_SOURCE_NAME = "vr_overrides_ancestral_bulldog"


# Dartmoor's spots, by entity id (override_bridge).
D_SAFE_01, D_SAFE_02, D_SAFE_03 = 0x888F49A793301A74, 0x884792F5BEC3023A, 0xBD6ABCD07B5F2E7B
D_SAFE_04, D_SAFE_05, D_SAFE_06 = 0xA82F1E5F6AF5FA37, 0x54B38367445FBC70, 0x879FE5B0EC978106
D_SAFE_07, D_SAFE_08 = 0xFD4F231521CEC54E, 0x4F41CF6A034C79AE  # Safe_Unique_08 (availableSafe07), _07 (Safe08)
D_STASH = {  # slot number: entity
    1: 0x9BF4159276890D8A, 2: 0xA15CA563A2CAE29B, 3: 0x235E2271403DC301, 4: 0x010E60E569EA3A41,
    5: 0x913E319E05A366D1, 6: 0xB0475C815D271FDA, 7: 0xB8A0FAB33F650CCD, 8: 0x2A0974481EFC2B40,
    9: 0xC04B5404FA159AC8, 10: 0x7210C1B4C917DF00, 11: 0x5F30820D1A40248D, 12: 0x40E6670AA7BD25DD,
    13: 0xF8A60AA270230E2B, 14: 0x93A9DD5681B9DA67, 15: 0x4964FE8017C336ED, 16: 0x5E3CD65A1A3DDBB7,
}
D_MULE = {1: 0xC0234736AF86C7D3, 2: 0xE15F14C8CB2834C1, 3: 0xAC6A7F799BC39BCC, 4: 0xE5E534F01E4B26A4,
          5: 0xD7CCC78BE2CC81C1, 6: 0x6486B48AB7BF12EE, 7: 0x9C83B7356FF965B2, 8: 0xFE147CE475557DF9}

# Garden Show's safes, out of the closed mansion with their clues, guards and guards' acts (GardenShow): (entity, kind,
# world position on the walking floor, why). Chosen on Garden Show's navmesh (007E5EF2677D6424) with the
# show's acts and props, the stash and supplier moves of the same fix brick in place; the place names are the folders
# of scenario_bulldog's entities there.
GARDEN_SAFES = [
    # availableSafe01: Safe_Unique_01 (30, 46, 11) -> the back garden's east end, below the balconies
    (D_SAFE_01, "safe", (29.50, 61.00, 1.07),
     "availableSafe01 (Safe_Unique_01): the back garden's east end, below the balconies, 18 m away"),
    (0x72CAC2958BAEC7EF, "clue", (36.50, 60.50, 1.08),
     "availableSafe01 Clue01 (Documents_Unique): floor, 7.0 m east of the safe"),
    (0xBAE9075C8CA3F256, "clue", (24.50, 66.00, 1.09),
     "availableSafe01 Clue02 (Tablet_Unique): floor, 7.1 m north-west of the safe"),
    (0xCA035245AA2A8BC9, "clue", (34.00, 67.00, 1.11),
     "availableSafe01 Clue03 (Laptop_Unique): floor, 7.5 m north-east of the safe"),
    (0x5FEECFFF5D0D08F0, "clue", (29.00, 72.00, -0.13),
     "availableSafe01 Clue04 (Tablet_Unique_08): floor, 11.0 m north of the safe"),
    (0xA8DF61532D9F9305, "clue", (17.50, 64.00, 1.07),
     "availableSafe01 Clue05 (Documents_Unique_10): floor, 12.4 m west of the safe"),
    # availableSafe02: Safe_Unique_02 (3, 57, 18) -> the back garden, at the foot of the terrace steps
    (D_SAFE_02, "safe", (9.00, 65.00, 3.02),
     "availableSafe02 (Safe_Unique_02): the back garden, at the foot of the terrace steps, 18 m away"),
    (0x375275FF078C9940, "clue", (14.00, 60.00, 4.01),
     "availableSafe02 Clue01 (Documents_Unique_05): floor, 7.1 m south-east of the safe"),
    (0x93A64ADE4D0A4E5D, "clue", (2.50, 73.00, 2.00),
     "availableSafe02 Clue02 (Tablet_Unique_05): floor, 10.3 m north-west of the safe"),
    (0x3272E2764B79AB8C, "clue", (21.00, 61.00, 4.01),
     "availableSafe02 Clue03 (Laptop_Unique_02): floor, 12.6 m east of the safe"),
    (0x64C339B95485D89C, "clue", (18.50, 57.00, 4.00),
     "availableSafe02 Clue04 (Documents_Unique_15): floor, 12.4 m south-east of the safe"),
    (0xBE65A5CD22668BCB, "clue", (11.00, 58.00, 3.99),
     "availableSafe02 Clue05 (Tablet_Unique_13): floor, 7.3 m south of the safe"),
    # availableSafe03: Safe_Unique_03 (22, 40, 11) -> the east service yard, by the parcel carts
    (D_SAFE_03, "safe", (51.50, 32.50, 2.91),
     "availableSafe03 (Safe_Unique_03): the east service yard, by the parcel carts, 31 m away"),
    (0x8D9F832AFD4A5186, "clue", (51.50, 39.50, 2.99),
     "availableSafe03 Clue01 (Documents_Unique_01): floor, 7.0 m north of the safe"),
    (0x2B9F414E7E0E2298, "clue", (44.50, 33.00, 2.95),
     "availableSafe03 Clue02 (Tablet_Unique_01): floor, 7.0 m west of the safe"),
    (0x4EA3067817F7416D, "clue", (52.00, 25.50, 2.98),
     "availableSafe03 Clue03 (Documents_Unique_02): floor, 7.0 m south of the safe"),
    (0x41BE852A80BCD70E, "clue", (44.00, 26.00, 2.94),
     "availableSafe03 Clue04 (Tablet_Unique_10): floor, 9.9 m south-west of the safe"),
    (0x676E53892833F2D6, "clue", (48.00, 19.50, 2.83),
     "availableSafe03 Clue05 (Documents_Unique_12): floor, 13.5 m south of the safe"),
    # availableSafe04: Safe_Unique_04 (-29, 54, 4) -> the back terrace, west end
    (D_SAFE_04, "safe", (-26.50, 60.00, 4.01),
     "availableSafe04 (Safe_Unique_04): the back terrace, west end, 6 m away"),
    (0xD6B940ED5A28B26D, "clue", (-33.50, 60.00, 4.01),
     "availableSafe04 Clue01 (Documents_Unique_07): floor, 7.0 m west of the safe"),
    (0x2928542731D3A4C8, "clue", (-19.50, 59.00, 3.99),
     "availableSafe04 Clue02 (Tablet_Unique_06): floor, 7.1 m east of the safe"),
    (0x6AF4C7258456FFB3, "clue", (-39.50, 56.50, 4.00),
     "availableSafe04 Clue03 (Documents_Unique_06): floor, 13.5 m west of the safe"),
    (0xABE283FBE2F5D52F, "clue", (-13.00, 60.00, 4.00),
     "availableSafe04 Clue04 (Documents_Unique_16): floor, 13.5 m east of the safe"),
    (0x5DECF30575387828, "clue", (-23.50, 56.50, 4.00),
     "availableSafe04 Clue05 (Tablet_Unique_14): floor, 4.6 m south-east of the safe"),
    # availableSafe05: Safe_Unique_05 (33, 7, 4) -> the front drive, by the east wing
    (D_SAFE_05, "safe", (33.00, -1.00, 3.01),
     "availableSafe05 (Safe_Unique_05): the front drive, by the east wing, 8 m away"),
    (0x7CF1B10856DDEF42, "clue", (26.00, -1.00, 3.08),
     "availableSafe05 Clue01 (Documents_Unique_04): floor, 7.0 m west of the safe"),
    (0x874F0B83E47127C6, "clue", (38.00, -6.00, 2.93),
     "availableSafe05 Clue02 (Tablet_Unique_02): floor, 7.1 m south-east of the safe"),
    (0x69F71B6206DA5128, "clue", (38.00, 4.00, 3.01),
     "availableSafe05 Clue03 (Laptop_Unique_01): floor, 7.1 m north-east of the safe"),
    (0x6B4DDE6B272E5481, "clue", (27.50, -8.00, 2.96),
     "availableSafe05 Clue04 (Tablet_Unique_11): floor, 8.9 m south-west of the safe"),
    (0x8B4C8E471506C2DE, "clue", (43.00, -1.00, 3.02),
     "availableSafe05 Clue05 (Documents_Unique_13): floor, 10.0 m east of the safe"),
    (0xD53E65BB07050966, "guard", (30.50, -2.50, 3.03),
     "availableSafe05's guard (Setpiece_Evergreen_SafeActor_Unique), at his first act"),
    (0x4668BDF6A0CC11BB, "act", (30.50, -2.50, 3.03),
     "availableSafe05's guard act act_mr_stand_smoke_armscrossed, 2.9 m south-west of the safe"),
    (0x1D6EA1E56453D4ED, "act", (40.50, -3.50, 2.97),
     "availableSafe05's guard act act_mr_guard_stand_armsback, 7.9 m east of the safe"),
    (0x0C9D3F005B430A52, "act", (35.00, -8.00, 2.95),
     "availableSafe05's guard act act_mr_stand_mirror_freshenup, 7.3 m south of the safe"),
    # availableSafe06: Safe_Unique_06 (22, 11, 11) -> the front terrace
    (D_SAFE_06, "safe", (15.00, -5.00, 3.99),
     "availableSafe06 (Safe_Unique_06): the front terrace, 19 m away"),
    (0xCCA859E0F261B2A1, "clue", (15.00, -12.00, 4.01),
     "availableSafe06 Clue01 (Documents_Unique_08): floor, 7.0 m south of the safe"),
    (0xA8A87D03DD514E28, "clue", (22.00, -5.00, 3.03),
     "availableSafe06 Clue02 (Tablet_Unique_07): floor, 7.0 m east of the safe"),
    (0x20AA7FE24CEF85A7, "clue", (8.50, -8.00, 4.03),
     "availableSafe06 Clue03 (Documents_Unique_09): floor, 7.2 m south-west of the safe"),
    (0xA2C6CC9D56AA5234, "clue", (21.50, -14.50, 2.98),
     "availableSafe06 Clue04 (Tablet_Unique_15): floor, 11.5 m south-east of the safe"),
    (0x99FC2FC5B1CDD4E7, "clue", (8.50, -15.00, 3.97),
     "availableSafe06 Clue05 (Tablet_Unique_16): floor, 11.9 m south-west of the safe"),
    (0x466E651506191C30, "guard", (15.00, -8.00, 4.00),
     "availableSafe06's guard (Setpiece_Evergreen_SafeActor_Unique_01), at his first act"),
    (0x2894162956815C40, "act", (15.00, -8.00, 4.00),
     "availableSafe06's guard act act_mr_guard_stand_armsback, 3.0 m south of the safe"),
    (0x8D5B9830378B4183, "act", (20.00, -2.50, 3.04),
     "availableSafe06's guard act act_mr_stand_warmhands_100cm, 5.6 m north-east of the safe"),
    (0x1533976A4D51B7D9, "act", (9.00, -5.00, 4.04),
     "availableSafe06's guard act act_mr_stand_wall_lean_phone_text_shoulder_right, 6.0 m west of the safe"),
    # availableSafe07: Safe_Unique_08 (-18, 17, 18) -> the front grounds, west end, by the bushes
    (D_SAFE_07, "safe", (-24.50, -1.00, 2.90),
     "availableSafe07 (Safe_Unique_08): the front grounds, west end, by the bushes, 25 m away"),
    (0x52F8D35BCE12B4B5, "clue", (-31.50, -1.00, 2.98),
     "availableSafe07 Clue01 (Tablet_Unique_04): floor, 7.0 m west of the safe"),
    (0x5099FD25AAB33DCE, "clue", (-24.50, -8.00, 2.91),
     "availableSafe07 Clue02 (Documents_Unique_03): floor, 7.0 m south of the safe"),
    (0x500B9AAD1088F828, "clue", (-18.50, -4.50, 2.96),
     "availableSafe07 Clue03 (Tablet_Unique_03): floor, 6.9 m south-east of the safe"),
    (0x737CEEBE6B0072D9, "clue", (-32.50, -9.50, 2.91),
     "availableSafe07 Clue04 (Tablet_Unique_12): floor, 11.7 m south-west of the safe"),
    (0xB3AF6649B9C51756, "clue", (-36.50, 4.00, 2.90),
     "availableSafe07 Clue05 (Documents_Unique_14): floor, 13.0 m north-west of the safe"),
    # availableSafe08: Safe_Unique_07 (-13, 29, 18) -> the west terrace, by the show's security barriers
    (D_SAFE_08, "safe", (-24.00, 32.50, 4.02),
     "availableSafe08 (Safe_Unique_07): the west terrace, by the show's security barriers, 18 m away"),
    (0x11D7EF970E737E19, "clue", (-31.00, 30.50, 4.00),
     "availableSafe08 Clue01 (Documents_Unique_11): floor, 7.3 m west of the safe"),
    (0xBF8104E685FB290A, "clue", (-30.00, 37.50, 3.99),
     "availableSafe08 Clue02 (Laptop_Unique_03): floor, 7.8 m north-west of the safe"),
    (0x5DFE644A65709099, "clue", (-36.50, 35.00, 3.99),
     "availableSafe08 Clue03 (Tablet_Unique_09): floor, 12.7 m west of the safe"),
    (0xD0B89A32668731B6, "clue", (-34.00, 24.00, 4.03),
     "availableSafe08 Clue04 (Documents_Unique_17): floor, 13.1 m south-west of the safe"),
    (0xA97B42CC480EF9CE, "clue", (-37.50, 29.00, 4.01),
     "availableSafe08 Clue05 (Tablet_Unique_17): floor, 13.9 m west of the safe"),
]


# Garden Show's moved stashes turned so their open side (local -Y) faces open floor (placement.crate_yaw; in game
# Stash09, moved 3 m west out of the mansion with its rotation kept, opened toward the mansion's wall 0.9 m
# of navmesh off: "squeeze between the house and the crate"). Stash11 had 1.0 m in front, Stash14 0.6 m; each now has
# 3 m. Stash05 (out of the plant exhibition, in the greenhouse) stood 0.9 m inside the greenhouse's north double door
# (Door_Double_Greenhouse_Bulldog_B), across 1.45 m of its 2.05 m opening: "you have to walk around it to get inside"
# (crate open #2 at 1.2, 140.7). It now stands against the greenhouse's south wall east of the
# passage from the south door, turned half round to face the exhibition table across 1.85 m of floor
# (placement.crate_in_the_way finds nothing). The others keep their vanilla rotation (1.9 m or more in front).
GARDEN_STASH_YAW = {5: -179.7, 9: -92.4, 11: -177.2, 14: 47.8}

# Garden Show's moved supplier turned half round. His NPC (Bhavan Supplier, CHAR_Ancestral_Worker_MansionStaff_F, a
# child of the spot turned 176 degrees in it) faces his local -Y; with the spot's vanilla rotation (yaw 0) kept he faced
# north (heading 86), at the mansion's south wall: 2.1 m of navmesh in front (2.4 m from his feet, a 1 m-wide
# obstruction there: 4 m either side of it), and the player found him staring at it (seen in game). Half round
# he faces south (heading -94) across the terrace toward the front grounds: over 8 m of
# open navmesh from heading -140 to -50, he faces its middle (placement.supplier_problem: 3.0 m, the full measure).
# His private volume (LaundryRoom, a child of the spot 3.1 m in front of him) turns with him onto that open floor.
GARDEN_SUPPLIER_YAW = -180.0


class GardenShow(Dartmoor):
    """The Garden Show (scenario_smoothsnake): the mansion is closed. Its navmesh has no floor in the rooms of all 8
    safes, 9 of the 16 stashes, 3 of the 8 mules and Supplier02 (the audit's 3 m test missed Safe04, Safe05 and
    Stash09, whose nearest floor, 2-2.8 m away, is behind a wall); Stash02 has one of the show's acts 0.34 m from it,
    Stash05 (the plant exhibition) a sign 0.95 m away and no floor there, Stash01 a young maple tree 0.5 m away.

    - Supplier02 (Helper_Evergreen_SupplierPackage, the laundry room upstairs) moves outside, onto the south terrace
      (24 m; the nearest act 7.3 m away), turned half round to face across the terrace (GARDEN_SUPPLIER_YAW).
    - The nine sealed stashes move out of their closed rooms to the nearest open floor (4-20 m: the terraces, the
      open hall of the ground floor, the grounds), Stash02 3.3 m along the terrace off the act, Stash05 out of the
      exhibition to the greenhouse's south wall (4.8 m; not in front of its north door, where it stood first),
      Stash01 0.9 m off the maple: every stash slot keeps a stash of its own.
    - The sealed mule slots name Mule05, Mule08 and Mule01.
    - All eight safes move out of the mansion (none is reachable under the show, so no slot could be re-pointed to
      one), each with its five clues and, for Safe05 and Safe06, its guard and his three acts (GARDEN_SAFES): round
      the mansion's outside, 7-31 m from where they were (the back garden and terrace, the east service yard, the
      front drive and terrace, the front grounds' west end, the west terrace).
    Exit 02's boat sequence with its guard deleted, and the start Evergreen_AlexaBalcony inside, are runtime matters
    (src/core/VariantExits.cpp, src/core/HeroStart.*)."""
    NAME = "gardenshow"
    DISPLAY = "Garden Show"
    OPTION = "garden_show"
    FIX_PATH = MISSIONS + "ancestral/fr_garden_show.brick"
    FIX_IDS = (0x00B0619C7BEBFEDC, 0x00A3595AEF450311)
    APPEND_BRICKS = [(MISSIONS + "ancestral/scenario_smoothsnake.brick", 0x00E56CA467419D97, 0x0007338CF9FA38AA)]
    MOVES = dict(
        [(0xFF9E9AB27A8DAB46, dict(kind="supplier", to=(-10.00, -4.90, 4.07), yaw=GARDEN_SUPPLIER_YAW,
                                   why="availableSupplier02 (Helper_Evergreen_SupplierPackage, the laundry room "
                                       "upstairs, -7, 16, 18): sealed off"))] +
        [(D_STASH[n], dict(kind="stash", to=to, yaw=GARDEN_STASH_YAW.get(n),
                           why="availableStash%02d (%s): %s" % (n, name, why)))
         for n, name, to, why in (
            (1, "Spawnable", (-122.59, 2.10, -0.05), "the young maple maple_fal_tiny_a_09 0.52 m away"),
            # 1.2 m east of where it first went (-29.79, 57.69), off the cast-iron table 0.9 m in front of it, its back
            # 0.6 m of navmesh off the mansion's wall (placement.crate_passage).
            (2, "Spawnable_01", (-28.60, 57.69, 4.00), "Act_FR_Stand_RArmSide of the show 0.34 m away"),
            (5, "Spawnable_04", (3.80, 133.35, 0.39), "in the plant exhibition, a sign 0.95 m away, no floor"),
            (7, "Spawnable_05 at 25, 21, 4", (41.16, 23.48, 2.92), "sealed off"),
            (9, "Spawnable_14 at -31, 19, 4", (-33.60, 18.30, 3.04), "sealed off"),
            (10, "Spawnable_07 at 31, 27, 11", (40.73, 28.10, 2.94), "sealed off"),
            (11, "Spawnable_08 at 28, 53, 11", (23.31, 56.50, 4.00), "sealed off"),
            (12, "Spawnable_16 at -30, 50, 11", (-26.08, 55.68, 3.99), "sealed off"),
            (13, "Spawnable_09 at 17, 33, 18", (17.04, 38.52, 3.99), "sealed off"),
            (14, "Spawnable_10 at 9, 1, 18", (11.09, -2.84, 4.02), "sealed off"),
            (15, "Spawnable_11 at -18, 45, 18", (-17.51, 45.12, 4.00), "sealed off"),
            (16, "Spawnable_15 at 12, 53, 18", (11.09, 50.94, 4.02), "sealed off"))] +
        [(eid, dict(kind=kind, to=to, why=why)) for eid, kind, to, why in GARDEN_SAFES])
    REPOINTS = dict(
        [("availableMule%02d" % n, (D_MULE[n], D_MULE[to], why)) for n, to, why in (
            (4, 5, "MoneyMuleSetup_Unique_03 (0.3, 27, 18): sealed off"),
            (6, 8, "MoneyMuleSetup_Unique_04 (-39, 48, 11): sealed off"),
            (7, 1, "MoneyMuleSetup_Unique_07 (-24, 6, 4): sealed off"))])
    BROKEN = dict(
        [(0xFF9E9AB27A8DAB46, "sealed off"), (D_STASH[1], "maple_fal_tiny_a_09 0.52 m away"),
         (D_STASH[2], "an act 0.34 m away"), (D_STASH[5], "a sign 0.95 m away, no floor")] +
        [(D_STASH[n], "sealed off") for n in (7, 9, 10, 11, 12, 13, 14, 15, 16)] +
        [(D_MULE[n], "sealed off") for n in (4, 6, 7)] +
        [(eid, "sealed off") for eid, kind, _, _ in GARDEN_SAFES if kind == "safe"])
    FINGERPRINTS = [
        (0x00E56CA467419D97, "scenario_smoothsnake.brick"),
        (0x00E188E8DD90384D, "override_bridge.brick"),
        (0x0018590AE0DD463E, "vr_overrides_ancestral_bulldog.brick"),
    ]


class Sloth(Dartmoor):
    """Sloth (ancestral_harebell: the hoarder's mansion): Stash14 (Spawnable_10) under a suitcase box, pots and fabric
    rolls (0.3-0.6 m), and Safe02 (Safe_Unique_02) with a painting and a chair against it and no floor in front.
    Their slots name Stash03 and Safe04. The papers lying on Stash09 and the creams, documents and globe by safe clues
    are left (CLASH_ACCEPTED): flat or small, the crate and the scan still work."""
    NAME = "sloth"
    DISPLAY = "Sloth"
    OPTION = "sloth"
    FIX_PATH = MISSIONS + "ancestral/fr_sloth.brick"
    FIX_IDS = (0x0064412BCC303F9C, 0x00061D1ACE672DB5)
    APPEND_BRICKS = [(MISSIONS + "ancestral/ancestral_harebell.brick", 0x002F7D8B6D31F6E0, 0x0062F83DFAFC749F)]
    NOT_MOUNTED_OK = [0x00D98058A63761D7]  # outfits_skunk (chunk1): outfits only
    REPOINTS = {
        "availableStash14": (D_STASH[14], D_STASH[3],
                             "Spawnable_10 (9, 1, 18): Suitcase_Box_A_00, two pots and fabric rolls on it"),
        "availableSafe02": (D_SAFE_02, D_SAFE_04,
                            "Safe_Unique_02 (2.5, 57, 18): Painting_Classic_Frame_D_00_06 and Chair_Antique_Worn_A_0002 "
                            "against it, no floor in front"),
    }
    BROKEN = {D_STASH[14]: "a suitcase box, pots and fabric on it", D_SAFE_02: "a painting and a chair against it"}
    FINGERPRINTS = [
        (0x002F7D8B6D31F6E0, "ancestral_harebell.brick"),
        (0x00E188E8DD90384D, "override_bridge.brick"),
        (0x0018590AE0DD463E, "vr_overrides_ancestral_bulldog.brick"),
    ]


# --- Mumbai (partition 17, "mongoose") --------------------------------------------------------------------------------

class IllusionsOfGrandeur(SpotFix):
    """Illusions of Grandeur (scenario_kingcobra): its navmesh leaves out the block of the luxury apartments it closes
    (override_blockluxury) and the construction site: Safe01, Safe06, Stash07, Stash13, Stash16 and Mule03 have no
    floor near them; Mule01 is 0.63 m from a parked minibus, with no floor there. Their slots name Safe07, Safe03,
    Stash06 (the water channel, as Stash07 by the water), Stash11, Stash10, Mule05 and Mule02. Freelancer's starts
    HeroSpawn_Barge and HeroSpawn_Bollywood_Dancer, inside too, are the runtime start fix's (src/core/HeroStart.*).
    The crate check (landslide_audit.crate_faces) finds Stash07, Stash13 and Stash16 with no navmesh in
    front (1.75-3 m under Freelancer's): the same closed block, already re-pointed. The crates their slots name now
    have 3 m in front (spot_fix.check)."""
    NAME = "illusions"
    DISPLAY = "Illusions of Grandeur"
    OPTION = "illusions_of_grandeur"
    PARTITION = (17, "mongoose")
    SCENE_PATH = MISSIONS + "mumbai/scene_mongoose_mild_ludo.entity"
    SCENE = 0x0090306D8E645E2E
    FREELANCER_SCENES = [
        ("mild", [0x0090306D8E645E2E, 0x0085023077F99752]),  # scene_mongoose_mild_ludo, mission_mild_ludo
        ("showdown", [0x005867F42AD9288F, 0x00E3A5B5E3AD1F77]),  # scene_mongoose_hot_ludo, mission_ludo
    ]
    OVERRIDE = (0x00B03EB467FEA625, 0x00184B3FC2B4B0AB)  # override_ludo
    FIX_PATH = MISSIONS + "mumbai/fr_illusions_of_grandeur.brick"
    FIX_IDS = (0x001B235A4E50D08E, 0x00C5EDF2262160B6)
    FIX_SOURCE = (0x00E78C0E8CA566AE, 0x009AEDEC062CD5D7)  # vr_overrides_mongoose (its root only), chunk17
    FIX_SOURCE_NAME = "vr_overrides_mongoose"
    APPEND_BRICKS = [(MISSIONS + "mumbai/scenario_kingcobra.brick", 0x00EFFE23BA968693, 0x008ADD3CCFA5B56E),
                     (MISSIONS + "mumbai/outfits_kingcobra.brick", 0x00EC26C1DF84A092, 0x009F04A42841B328)]
    REPOINTS = {
        "availableSafe01": (0xEE9EB1730DC570C0, 0x68AE4BD2E089B6A5,
                            "Safe_Unique_01 (-48, 40, 15): sealed off -> Safe_Unique_07 (-66, -45, 2)"),
        "availableSafe06": (0x1EDBA0918CDA8EA5, 0x178F6CB0ADA6CFD4,
                            "Safe_Unique_06 (-44, 16, 10): sealed off -> Safe_Unique_03 (-128, -142, 0)"),
        "availableStash07": (0xA3B78AF96B7E6A21, 0xEF9B4F8E5BE7EEFA,
                             "Spawnable_ConstructionSite (11, 21, -1): sealed off -> Spawnable_WaterChannel "
                             "(0, -38, -1)"),
        "availableStash13": (0xD9EE92EE2005ABD2, 0xA50C7C57E4F015F4,
                             "Spawnable_Apartments_05 (-34, 26, 25): sealed off -> Spawnable_Apartments_04 "
                             "(-42, -50, -1)"),
        "availableStash16": (0x4F0BF123639F03FD, 0x5F8167B0F80580D3,
                             "Spawnable_Apartments_08 (-49, 38, 10): sealed off -> Spawnable_Apartments_03 "
                             "(-87, -18, 5)"),
        "availableMule03": (0x0716BC30EB82B7D2, 0x342AC67509FA40EA,
                            "MoneyMuleSetup_Unique_02 (-35, 10, 15): sealed off -> MoneyMuleSetup_Unique_04 "
                            "(-4, -67, 8)"),
        "availableMule01": (0x8B6CFBDECE61D7D1, 0x5C6AD31A69CEDA7A,
                            "MoneyMuleSetup_Unique (-59, 74, -1): Mumbai_Taxi_MiniBus_A_02 0.63 m away, no floor -> "
                            "MoneyMuleSetup_Unique_01 (-117, 6, -1)"),
    }
    BROKEN = {0xEE9EB1730DC570C0: "sealed off", 0x1EDBA0918CDA8EA5: "sealed off", 0xA3B78AF96B7E6A21: "sealed off",
              0xD9EE92EE2005ABD2: "sealed off", 0x4F0BF123639F03FD: "sealed off", 0x0716BC30EB82B7D2: "sealed off",
              0x8B6CFBDECE61D7D1: "Mumbai_Taxi_MiniBus_A_02 0.63 m away"}
    FINGERPRINTS = [
        (0x00EFFE23BA968693, "scenario_kingcobra.brick"),
        (0x00EC26C1DF84A092, "outfits_kingcobra.brick"),
        (0x00B03EB467FEA625, "override_ludo.brick"),
        (0x00E78C0E8CA566AE, "vr_overrides_mongoose.brick"),
    ]


# --- Miami (partition 19, "miami") ------------------------------------------------------------------------------------

class Miami(SpotFix):
    PARTITION = (19, "miami")
    SCENE_PATH = MISSIONS + "miami/scene_flamingo_mild_pinochle.entity"
    SCENE = 0x0017CB8C7D1C65D8
    FREELANCER_SCENES = [
        ("mild", [0x0017CB8C7D1C65D8, 0x004D4229C4B64540]),  # scene_flamingo_mild_pinochle, mission_mild_pinochle
        ("showdown", [0x000B15D29325A23F, 0x004515D081FAE064]),  # scene_flamingo_hot_pinochle, mission_hot_pinochle
    ]
    OVERRIDE = (0x006084EBBAE6AF4F, 0x00A8DCCBAECC6674)  # override_pinochle
    FIX_SOURCE = (0x009D5E83492C6928, 0x007297E04AC19324)  # difficulty_easy_miami_cottonmouth (its root only), chunk19
    FIX_SOURCE_NAME = "difficulty_easy_miami_cottonmouth"
    # mission_sambuca's workshop: a roll container, canisters and a trolley round Safe08 (Safe_Unique_07), 0.4-0.8 m,
    # no floor in front. Its slot names Safe04 (Safe_Unique_04, -66, 12, 0).
    REPOINTS = {
        "availableSafe08": (0x03DBA4DE4030A0D2, 0x9B28B05BA5B914F4,
                            "Safe_Unique_07 (-142, -7, 11): boxed in by mission_sambuca's Roll_Container_A_Open_B_00_01, "
                            "canisters and Trolly_Wheel_A -> Safe_Unique_04 (-66, 12, 0)"),
    }
    BROKEN = {0x03DBA4DE4030A0D2: "a roll container, canisters and a trolley round it"}


class TheUndying(Miami):
    """The Undying (mission_sambuca): Safe08 boxed in (Miami)."""
    NAME = "undying"
    DISPLAY = "The Undying"
    OPTION = "the_undying"
    FIX_PATH = MISSIONS + "miami/fr_the_undying.brick"
    FIX_IDS = (0x00066AFAE19E328B, 0x002E0052B8353CCE)
    APPEND_BRICKS = [(MISSIONS + "miami/mission_sambuca.brick", 0x00D7DD7E8B8E3723, 0x0052AC190550231C)]
    FINGERPRINTS = [
        (0x00D7DD7E8B8E3723, "mission_sambuca.brick"),
        (0x006084EBBAE6AF4F, "override_pinochle.brick"),
        (0x009D5E83492C6928, "difficulty_easy_miami_cottonmouth.brick"),
    ]


class TheUndyingReturns(Miami):
    """The Undying Returns (scenario_sambuca2, which loads mission_sambuca): Safe08 boxed in, as The Undying's."""
    NAME = "undyingreturns"
    DISPLAY = "The Undying Returns"
    OPTION = "the_undying_returns"
    FIX_PATH = MISSIONS + "miami/fr_the_undying_returns.brick"
    FIX_IDS = (0x000A4C6A5974F532, 0x008921F9CCF4DC43)
    APPEND_BRICKS = [(MISSIONS + "miami/scenario_sambuca2.brick", 0x00BE05F31F56D254, 0x00CC729D30C5F4EB)]
    FINGERPRINTS = [
        (0x00BE05F31F56D254, "scenario_sambuca2.brick"),
        (0x00D7DD7E8B8E3723, "mission_sambuca.brick"),
        (0x006084EBBAE6AF4F, "override_pinochle.brick"),
        (0x009D5E83492C6928, "difficulty_easy_miami_cottonmouth.brick"),
    ]


# --- Paris (partition 27, "paris") ------------------------------------------------------------------------------------

class TheBanker(SpotFix):
    """The Banker (scenario_frenchmartini): Stash15 (Spawnable_15) behind an antique leather sofa (0.72 m, no floor
    there) with a table lamp 0.4 m above it. Its slot names Stash10 (Spawnable_10, -148, -9, -0.5). The crate check
    (landslide_audit.crate_faces) finds Stash15's open side against the sofa (no navmesh in front, 3 m
    under Freelancer's): the same sofa, already re-pointed; Stash10 has 2.3 m in front (spot_fix.check)."""
    NAME = "banker"
    DISPLAY = "The Banker"
    OPTION = "the_banker"
    PARTITION = (27, "paris")
    SCENE_PATH = MISSIONS + "paris/scene_peacock_mild_craps.entity"
    SCENE = 0x0074C54DEDDE9A7A
    FREELANCER_SCENES = [
        ("mild", [0x0074C54DEDDE9A7A, 0x00C257440C6A6F3A]),  # scene_peacock_mild_craps, mission_mild_craps
        ("showdown", [0x00A253096D93422B, 0x00C7C33D9B7F165F]),  # scene_peacock_hot_craps, mission_hot_craps
    ]
    OVERRIDE = (0x00598B6364BC39F5, 0x004ACDA3BB626CA1)  # override_craps
    FIX_PATH = MISSIONS + "paris/fr_the_banker.brick"
    FIX_IDS = (0x006CB049FA39E076, 0x00E8480082F57F32)
    FIX_SOURCE = (0x0002B258EDBAA5CF, 0x007FF49D2B80E28D)  # dressing_mission_peacock (root and one scene), chunk27
    FIX_SOURCE_NAME = "dressing_mission_peacock"
    APPEND_BRICKS = [(MISSIONS + "paris/scenario_frenchmartini.brick", 0x00250CEFFB630F13, 0x004FEBFEEA1D4177)]
    REPOINTS = {
        "availableStash15": (0x0FF6C5C4EA064A8E, 0xB18C7E78D94B12D6,
                             "Spawnable_15 (-192, -12, -0.5): behind sofa_leather_antique_a, under "
                             "Lamp_Table_Iron_Tuscan_A -> Spawnable_10 (-148, -9, -0.5)"),
    }
    BROKEN = {0x0FF6C5C4EA064A8E: "sofa_leather_antique_a 0.72 m away, no floor"}
    FINGERPRINTS = [
        (0x00250CEFFB630F13, "scenario_frenchmartini.brick"),
        (0x00598B6364BC39F5, "override_craps.brick"),
        (0x0002B258EDBAA5CF, "dressing_mission_peacock.brick"),
    ]


# --- Whittleton Creek (partition 16, "skunk") -------------------------------------------------------------------------

class ABitterPill(SpotFix):
    """A Bitter Pill (scenario_gartersnake: the For Sale house as a drug lab, with its own navmesh and two trespassing
    rules of its own, "First Floor" and "Basement" (b8173f8fde526644), whose disguise zones cover the whole house):

    - Safe07 (Safe_Unique_08, the For Sale house's garage, -20.4, -31.5, 5.0) stands inside the display fridge
      Kitchen_Fridge_C_00 (4996bf5145a4c730) the option puts there, 0.91 m in plan from its origin (in game
      the safe was in the fridge), with the option's toys and boxes on two of its clues. Its slot names
      Safe04 (Safe_Unique_04, the BBQ house's basement, -29.8, -0.4, 2.0): the nearest of the other seven, none of
      which the option touches (no prop, no act within 2 m, its clues clear, under none of the option's trespassing
      zones).

    Only spots inside something the option adds move: its trespassing zones alone are no reason (a safe,
    stash or supplier in a hostile area is fine), so Supplier01 (in the hostile basement, a scared
    NPC's act in his private volume) and Stash08 (the attic) stay."""
    NAME = "bitterpill"
    DISPLAY = "A Bitter Pill"
    OPTION = "a_bitter_pill"
    PARTITION = (16, "skunk")
    SCENE_PATH = MISSIONS + "skunk/scene_skunk_mild_solitaire.entity"
    SCENE = 0x00ACF2242E22C7CA
    FREELANCER_SCENES = [
        ("mild", [0x00ACF2242E22C7CA, 0x00B4DA544A107E74]),  # scene_skunk_mild_solitaire, mission_mild_solitaire
        ("showdown", [0x00C48C8FE142BE7F, 0x00106DEEE9EFBB76]),  # scene_skunk_hot_solitaire, mission_hot_solitaire
    ]
    OVERRIDE = (0x002118F4F5EFB3ED, 0x005BC0FB02E9C182)  # override_solitaire
    FIX_PATH = MISSIONS + "skunk/fr_a_bitter_pill.brick"
    FIX_IDS = (0x00FC4CC073DC94D0, 0x0077179A0A86DABC)
    FIX_SOURCE = (0x0096D03899E253FC, 0x00170DE3EAB8B5B4)  # intel_6core_skunk (its root only), chunk16
    FIX_SOURCE_NAME = "intel_6core_skunk"
    APPEND_BRICKS = [(MISSIONS + "skunk/scenario_gartersnake.brick", 0x00794561315ED1ED, 0x003247D5B90E66B3)]
    MOVES = {}
    REPOINTS = {
        "availableSafe07": (0x67BD17D846E3FAEA, 0xBE47BBA78FC7FA4A,
                            "Safe_Unique_08 (-20, -32, 5): inside scenario_gartersnake's Kitchen_Fridge_C_00 -> "
                            "Safe_Unique_04 (-30, -0.4, 2)"),
    }
    BROKEN = {0x67BD17D846E3FAEA: "inside Kitchen_Fridge_C_00 (0.91 m from its origin)"}
    FINGERPRINTS = [
        (0x00794561315ED1ED, "scenario_gartersnake.brick"),
        (0x002118F4F5EFB3ED, "override_solitaire.brick"),
        (0x0096D03899E253FC, "intel_6core_skunk.brick"),
    ]


# In package order (after the full variants of build_bricks.VARIANTS).
SPECS = [Revolutionary, EmbraceOfTheSerpent, Reflection, GardenShow, Sloth, IllusionsOfGrandeur, TheUndying,
         TheUndyingReturns, TheBanker, ABitterPill]
