#pragma once

#include <string>

class UObject;
class UPlayerPawn;

// Carries the player and the objects that travel with the player from one map to the next.
// It follows the travel code of the Deus Ex Engine.dll (ExportTravel and ULevel::SpawnPlayActor).
class DeusExTravel
{
public:
	// Takes a snapshot of the travel properties of the pawn and of each object that those properties reach.
	// Returns the key of the snapshot. The key is the travel info text of the player.
	static std::string Create(UPlayerPawn* pawn);

	// Puts the snapshot into the pawn of the new map. Makes the other objects of the snapshot again.
	// Returns those other objects in the order of the snapshot. The pawn is not in the result.
	static Array<UObject*> Accept(UPlayerPawn* pawn, const std::string& key);
};
