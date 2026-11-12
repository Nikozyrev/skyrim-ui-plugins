#pragma once

#include <RE/P/PlayerCharacter.h>

/// Helpers that describe the state of the game.
namespace GameState
{
	/// True while a world is loaded.
	///
	/// The player reference only belongs to a cell while a world is loaded. In
	/// the main menu the world is torn down: its camera and its actors are not
	/// valid anymore, and a fullscreen overlay over the menu takes the input
	/// away from it.
	[[nodiscard]] inline bool IsWorldLoaded()
	{
		const auto* player = RE::PlayerCharacter::GetSingleton();
		return player != nullptr && player->GetParentCell() != nullptr;
	}
}
