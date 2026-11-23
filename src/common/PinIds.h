#pragma once

// Pin and property ids: the CRC-32 of the name, as the engine hashes it.
// Each one is pinned to its value, so a misspelled name fails the build.
// Names that differ only in case are different pins (ItemSpawned and
// itemSpawned).

#include <Glacier/Hash.h>

#include <cstdint>

namespace fr {

// Random selectors and pickers (ZRandomSelector, ZVsTargetPicker, ...).
inline constexpr uint32_t kPinPick = Hash::Crc32("Pick");
static_assert(kPinPick == 0x39FFA0A5);
inline constexpr uint32_t kPinOnPickDone = Hash::Crc32("OnPickDone");
static_assert(kPinOnPickDone == 0xA24B3FBC);
inline constexpr uint32_t kPinOnPickFailed = Hash::Crc32("OnPickFailed");
static_assert(kPinOnPickFailed == 0x8D645AC2);
inline constexpr uint32_t kPinOnPicked = Hash::Crc32("OnPicked");
static_assert(kPinOnPicked == 0xE9FE63D4);
inline constexpr uint32_t kPinOnSelected = Hash::Crc32("OnSelected");
static_assert(kPinOnSelected == 0x1C676CD4);

// Generic flow.
inline constexpr uint32_t kPinIn = Hash::Crc32("In");
static_assert(kPinIn == 0xCA68AAEC);
inline constexpr uint32_t kPinOut = Hash::Crc32("Out");
static_assert(kPinOut == 0x81A72B79);
inline constexpr uint32_t kPinOut01 = Hash::Crc32("Out01");
static_assert(kPinOut01 == 0x9D009E6E);
inline constexpr uint32_t kPinOut02 = Hash::Crc32("Out02");
static_assert(kPinOut02 == 0x0409CFD4);
inline constexpr uint32_t kPinOut03 = Hash::Crc32("Out03");
static_assert(kPinOut03 == 0x730EFF42);
inline constexpr uint32_t kPinOut04 = Hash::Crc32("Out04");
static_assert(kPinOut04 == 0xED6A6AE1);
inline constexpr uint32_t kPinOut05 = Hash::Crc32("Out05");
static_assert(kPinOut05 == 0x9A6D5A77);
inline constexpr uint32_t kPinOut06 = Hash::Crc32("Out06");
static_assert(kPinOut06 == 0x03640BCD);
inline constexpr uint32_t kPinPoll = Hash::Crc32("Poll");
static_assert(kPinPoll == 0x248E557B);
inline constexpr uint32_t kPinTrigger = Hash::Crc32("Trigger");
static_assert(kPinTrigger == 0xD5D636C1);
inline constexpr uint32_t kPinEventOccurred = Hash::Crc32("EventOccurred");
static_assert(kPinEventOccurred == 0x16E4A6B6);
inline constexpr uint32_t kPinOnStart = Hash::Crc32("OnStart");
static_assert(kPinOnStart == 0x854B3952);
inline constexpr uint32_t kPinOnEnd = Hash::Crc32("OnEnd");
static_assert(kPinOnEnd == 0x008814F6);
inline constexpr uint32_t kPinOnDone = Hash::Crc32("OnDone");
static_assert(kPinOnDone == 0xF89540BF);
inline constexpr uint32_t kPinOnFailed = Hash::Crc32("OnFailed");
static_assert(kPinOnFailed == 0x66F33F72);
inline constexpr uint32_t kPinOnReached = Hash::Crc32("OnReached");
static_assert(kPinOnReached == 0x5760AAAD);

// Value entities.
inline constexpr uint32_t kPinSetValue = Hash::Crc32("SetValue");
static_assert(kPinSetValue == 0xED2287F4);
inline constexpr uint32_t kPinGetValue = Hash::Crc32("GetValue");
static_assert(kPinGetValue == 0x15D3B825);
inline constexpr uint32_t kPinOnValue = Hash::Crc32("OnValue");
static_assert(kPinOnValue == 0x074534E9);
inline constexpr uint32_t kPinNoValue = Hash::Crc32("NoValue");
static_assert(kPinNoValue == 0x6A6EECF8);
inline constexpr uint32_t kPinSetTrue = Hash::Crc32("SetTrue");
static_assert(kPinSetTrue == 0xA7B0C81D);
inline constexpr uint32_t kPinSetFalse = Hash::Crc32("SetFalse");
static_assert(kPinSetFalse == 0xDB98B7F0);
inline constexpr uint32_t kPinOnTrue = Hash::Crc32("OnTrue");
static_assert(kPinOnTrue == 0x1544EC99);
inline constexpr uint32_t kPinOnFalse = Hash::Crc32("OnFalse");
static_assert(kPinOnFalse == 0x31FF04ED);

// Objectives.
inline constexpr uint32_t kPinObjectiveId = Hash::Crc32("ObjectiveId");
static_assert(kPinObjectiveId == 0x7F808678);
inline constexpr uint32_t kPinStarted = Hash::Crc32("Started");
static_assert(kPinStarted == 0x7B2BAF46);
inline constexpr uint32_t kPinCompleted = Hash::Crc32("Completed");
static_assert(kPinCompleted == 0xB809DECD);

// Doors and lockpicks.
inline constexpr uint32_t kPinLockDoor = Hash::Crc32("LockDoor");
static_assert(kPinLockDoor == 0x0DFF2B60);
inline constexpr uint32_t kPinUnlockDoor = Hash::Crc32("UnlockDoor");
static_assert(kPinUnlockDoor == 0xB9BDE913);
inline constexpr uint32_t kPinStartedLockPick = Hash::Crc32("StartedLockPick");
static_assert(kPinStartedLockPick == 0xA3AF3C10);
inline constexpr uint32_t kPinLockpickCompleted = Hash::Crc32("LockpickCompleted");
static_assert(kPinLockpickCompleted == 0x9782623B);
inline constexpr uint32_t kPinItemUsed = Hash::Crc32("ItemUsed");
static_assert(kPinItemUsed == 0x8A7DCCB8);

// Items, repository keys and spawners.
inline constexpr uint32_t kPinItem = Hash::Crc32("Item");
static_assert(kPinItem == 0xBF298A20);
inline constexpr uint32_t kPinSetItem = Hash::Crc32("SetItem");
static_assert(kPinSetItem == 0x4557A18E);
inline constexpr uint32_t kPinPickup = Hash::Crc32("Pickup");
static_assert(kPinPickup == 0x46323CCB);
inline constexpr uint32_t kPinPickupIntoPocket = Hash::Crc32("PickupIntoPocket");
static_assert(kPinPickupIntoPocket == 0xDD16D11F);
inline constexpr uint32_t kPinDrop = Hash::Crc32("Drop");
static_assert(kPinDrop == 0xD027AA1C);
inline constexpr uint32_t kPinItemSpawned = Hash::Crc32("ItemSpawned");
static_assert(kPinItemSpawned == 0x3F83265F);
inline constexpr uint32_t kPinSpawnerItemSpawned = Hash::Crc32("itemSpawned");
static_assert(kPinSpawnerItemSpawned == 0xB91FA988);
inline constexpr uint32_t kPinDebugSpawnItem = Hash::Crc32("debug_SpawnItem");
static_assert(kPinDebugSpawnItem == 0x905F0FB7);
inline constexpr uint32_t kPinOnContainerItemSet = Hash::Crc32("OnContainerItemSet");
static_assert(kPinOnContainerItemSet == 0x624440BF);
inline constexpr uint32_t kPinOnRepositoryId = Hash::Crc32("OnRepositoryId");
static_assert(kPinOnRepositoryId == 0xF08183E1);
inline constexpr uint32_t kPinPushRepositoryId = Hash::Crc32("PushRepositoryId");
static_assert(kPinPushRepositoryId == 0x11C51774);
inline constexpr uint32_t kPinRemoveRepositoryId = Hash::Crc32("RemoveRepositoryId");
static_assert(kPinRemoveRepositoryId == 0x946484E5);

// Exits and Freelancer's hijackers.
inline constexpr uint32_t kPinExitActivated = Hash::Crc32("exitActivated");
static_assert(kPinExitActivated == 0xBE21300A);
inline constexpr uint32_t kPinSequenceDone = Hash::Crc32("sequenceDone");
static_assert(kPinSequenceDone == 0xA7564B0A);

// Actors and acts.
inline constexpr uint32_t kPinOnIActorChanged = Hash::Crc32("OnIActorChanged");
static_assert(kPinOnIActorChanged == 0x3521D729);
inline constexpr uint32_t kPinOnActorCast = Hash::Crc32("OnActorCast");
static_assert(kPinOnActorCast == 0x87CDEBD6);
inline constexpr uint32_t kPinOnCastRole = Hash::Crc32("OnCastRole");
static_assert(kPinOnCastRole == 0xC5B22F1A);
inline constexpr uint32_t kPinOnActTimeout = Hash::Crc32("OnActTimeout");
static_assert(kPinOnActTimeout == 0xD49C8E35);
inline constexpr uint32_t kPinOnEnterDrama = Hash::Crc32("OnEnterDrama");
static_assert(kPinOnEnterDrama == 0xAF7AE64D);
inline constexpr uint32_t kPinOnLeaveDrama = Hash::Crc32("OnLeaveDrama");
static_assert(kPinOnLeaveDrama == 0xAD84D865);
inline constexpr uint32_t kPinOnPauseDrama = Hash::Crc32("OnPauseDrama");
static_assert(kPinOnPauseDrama == 0x6733F879);
inline constexpr uint32_t kPinFXActivatePuke = Hash::Crc32("FXActivatePuke");
static_assert(kPinFXActivatePuke == 0xF2A2D229);

// Properties.
inline constexpr uint32_t kPropValue = Hash::Crc32("m_bValue");
static_assert(kPropValue == 0xA4F9DD21);
inline constexpr uint32_t kPropStartEnabled = Hash::Crc32("m_bStartEnabled");
static_assert(kPropStartEnabled == 0xB98FCBEA);
inline constexpr uint32_t kPropDamage = Hash::Crc32("m_fDamage");
static_assert(kPropDamage == 0x88E31A11);
inline constexpr uint32_t kPropInterval = Hash::Crc32("m_fInterval");
static_assert(kPropInterval == 0x12331DCC);
inline constexpr uint32_t kPropPriority = Hash::Crc32("m_nPriority");
static_assert(kPropPriority == 0xD28B8C38);
inline constexpr uint32_t kPropVisibilitySetting = Hash::Crc32("m_VisibilitySetting");
static_assert(kPropVisibilitySetting == 0xA7D017C9);

} // namespace fr
