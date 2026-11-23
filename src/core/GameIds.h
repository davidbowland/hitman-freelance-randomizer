#pragma once

// Game ids that more than one feature uses: runtime resource ids of templates
// (TEMP) and blueprints (TBLU), and sub-entity ids (m_nEntityID) inside them.

#include <cstdint>

namespace fr {

// The evergreen_setup brick ([assembly:/_pro/scenes/bricks/evergreen_setup.brick].pc_entitytype),
// Freelancer's own systems, loaded in every Freelancer scene.
inline constexpr uint64_t kEvergreenSetupBrick = 0x006864DBF5FD7CD4ull;

// The campaign seed's entity (ZValueInt, m_nValue) in evergreen_setup: the
// supplier's five gun selectors take their m_Seed from it.
inline constexpr uint64_t kCampaignSeedEntityId = 0xb73d9349324f8074ull;

// "Purchased item actions" in evergreen_setup.
inline constexpr uint64_t kPurchasedActionsId = 0x5e8a8e9d261a2a22ull;

// setpiece_evergreen_stash_items (the supply crates' shared items).
inline constexpr uint64_t kStashItemsTemp = 0x00100757521CC642ull;
inline constexpr uint64_t kStashItemsTblu = 0x00512EC270635802ull;

// Its crate seed (ZValueInt_Basic, m_nValue): the opened stash's seed is
// copied into it when a crate opens.
inline constexpr uint64_t kStashItemsSeedId = 0xc1d8d4ccc0415c1aull;

// The option helper of the supplier and the supply crates: one instance per
// offered item (its TEMP id is unknown).
inline constexpr uint64_t kCrateOptionTblu = 0x00BCA591C00A579Dull;

// prop_questitem_evergreen_moneyitem_{small,medium,large}_amount_runtime.
inline constexpr uint64_t kMoneyItemSmallTemp = 0x004CFD04E4EAC420ull;
inline constexpr uint64_t kMoneyItemSmallTblu = 0x001A503482CA411Full;
inline constexpr uint64_t kMoneyItemMediumTemp = 0x005B48FA527599F8ull;
inline constexpr uint64_t kMoneyItemMediumTblu = 0x001153C8FE0BC805ull;
inline constexpr uint64_t kMoneyItemLargeTemp = 0x00999B1FC9296C28ull;
inline constexpr uint64_t kMoneyItemLargeTblu = 0x00C6CD7FA1A8DC8Aull;

} // namespace fr
