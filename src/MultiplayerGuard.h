#pragma once

// Keeps target prediction and target leading away from other human players.
// Rule: an actor that is not the local player but uses the Player NPC base (0x7) is treated
// as a remote human player (multiplayer clients such as SkyMP). This rule is NOT verified
// in-game yet. In single player no other actor uses that base, so it changes nothing there.
namespace MultiplayerGuard
{
	inline constexpr RE::FormID kPlayerBaseFormID = 0x7;

	inline bool IsOtherHumanPlayer( RE::TESObjectREFR* a_ref )
	{
		if( !a_ref )
			return false;

		if( a_ref == RE::PlayerCharacter::GetSingleton() )
			return false;

		auto actor = a_ref->As<RE::Actor>();
		if( !actor )
			return false;

		auto base = actor->GetActorBase();
		return base && base->GetFormID() == kPlayerBaseFormID;
	}
}
