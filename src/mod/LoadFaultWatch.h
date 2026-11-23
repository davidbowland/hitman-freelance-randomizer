#pragma once

// The load fault watch: while a full map variant's load is in progress (from
// the swap in LoadScene until the load reaches play), every fatal-kind
// exception (core/LoadFault.h) any thread raises is written, with its module
// and offset and the likely return addresses on that thread's stack, to
// FreelancerRandomizer-loadfault.txt next to the log. The file is written
// from the exception handler itself (no heap, no lock), so it survives a
// crash that leaves no dump and no log line; at the next game start its lines
// are copied into the log ("load fault (last session): ...") and the file is
// kept as FreelancerRandomizer-loadfault.prev.txt. A load that reaches play
// logs how many faults it saw (the game handles some of its own).
//
// Also breadcrumbs in the log: after the mod's handlers of each loading stage
// of a watched load ("the mod's eLoading_X handlers are done"), so a load that
// dies between two stages shows whether the mod's code or the game's was
// running; and how many blueprint factories were built since the load began.

#include <string_view>

namespace load_fault {

// Game thread, once at mod start: reports a fault file the last session left
// and, with p_Watch (the DLL has the full map variants' recipes and Full
// map variants is on), adds the exception handler. Without it nothing is ever watched.
void Install(bool p_Watch) noexcept;

// Mod unload: stops a watched load (as Disarm) and removes the handler.
void Uninstall() noexcept;

// Game thread, when a full variant's swap is applied (its sentinel written):
// watches the load; p_Load names it ("badboy (full version)").
void Arm(std::string_view p_Load) noexcept;

// Game thread: the load reached play (p_Why), or another load began: logs what
// the watch saw and stops.
void Disarm(std::string_view p_Why) noexcept;

// Game thread, first thing in the SetLoadingStage detour (a static name).
void SetStage(const char* p_Stage) noexcept;

// Game thread, last thing in the SetLoadingStage detour: the breadcrumb.
void StageHandled(const char* p_Stage) noexcept;

// Blueprint factory constructor hook, any thread: one relaxed load when off.
void NoteFactoryBuilt() noexcept;

} // namespace load_fault
