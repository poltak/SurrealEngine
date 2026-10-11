
#include "Precomp.h"
#include "ULevel.h"
#include "Packages/DeusEx/AIEventManager.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/UViewport.h"
#include "Engine.h"
#include "VM/ScriptCall.h"

ULevel::ULevel(NameString name, UClass* base, ObjectFlags flags) : ULevelBase(name, base, flags)
{
	Collision.SetLevel(this);
	Light.SetLevel(this);
}

void ULevel::Load(ObjectStream* stream)
{
	ULevelBase::Load(stream);

	Model = stream->ReadObject<UModel>();

	if (Model)
		Model->LoadNow();

	int count = stream->ReadIndex();
	for (int i = 0; i < count; i++)
	{
		LevelReachSpec spec;
		spec.distance = stream->ReadInt32();
		spec.startActor = stream->ReadObject<UNavigationPoint>();
		spec.endActor = stream->ReadObject<UNavigationPoint>();
		spec.collisionRadius = stream->ReadInt32();
		spec.collisionHeight = stream->ReadInt32();
		spec.reachFlags = stream->ReadInt32();
		spec.bPruned = stream->ReadInt8();
		ReachSpecs.push_back(spec);
	}
}

void ULevel::Save(PackageStreamWriter* stream)
{
	ULevelBase::Save(stream);
	stream->WriteObject(Model);
	stream->WriteIndex((int)ReachSpecs.size());
	for (const LevelReachSpec& spec : ReachSpecs)
	{
		stream->WriteInt32(spec.distance);
		stream->WriteObject(spec.startActor);
		stream->WriteObject(spec.endActor);
		stream->WriteInt32(spec.collisionRadius);
		stream->WriteInt32(spec.collisionHeight);
		stream->WriteInt32(spec.reachFlags);
		stream->WriteInt8(spec.bPruned);
	}
}

void ULevel::TickActor(float elapsed, UActor* actor)
{
	if (!actor)
		return;

	// If we have an owner, tick it first
	if (!actor->bDeleteMe() && actor->Owner())
	{
		TickActor(elapsed, actor->Owner());
	}

	// Do we have an actor? is it deleted? did it already tick?
	if (actor->bDeleteMe() || actor->bTicked() == ticked)
		return;

	// Mark actor as ticked
	actor->bTicked() = ticked;

	// Tick the actor for this turn
	actor->Tick(elapsed);

	// Destroy the actor if its time
	if (actor->Role() >= ROLE_SimulatedProxy && actor->LifeSpan() != 0.0f)
	{
		actor->LifeSpan() = std::max(actor->LifeSpan() - elapsed, 0.0f);
		if (actor->LifeSpan() == 0.0f)
		{
			CallEvent(actor, EventName::Expired);
			actor->Destroy();
		}
	}
}

// The tick of one actor as ULevel::Tick and the start of AActor::Tick in the Engine.dll of Deus Ex do it.
// Returns false when the actor waits, because its owner did not tick yet in this frame.
bool ULevel::TickActorDeusEx(float elapsed, UActor* actor)
{
	// The original has the static actors at the start of the list and does not go to them
	if (actor->bDeleteMe() || actor->bStatic())
		return true;

	// An actor in stasis does not tick: the player did not see it for 5 seconds, and it is far away or its zone was
	// not drawn. An actor that is transient goes away then.
	if (actor->InStasis())
	{
		if (actor->bTransient() && actor->Role() >= ROLE_SimulatedProxy)
			actor->Destroy();
		return true;
	}

	if (actor->Owner() && actor->Owner()->bTicked() != ticked)
	{
		waitingActors.push_back(actor);
		return false;
	}

	actor->bTicked() = ticked;
	actor->Tick(elapsed);

	if (!actor->bDeleteMe() && actor->Role() >= ROLE_SimulatedProxy && actor->LifeSpan() != 0.0f)
	{
		actor->LifeSpan() = std::max(actor->LifeSpan() - elapsed, 0.0f);
		if (actor->LifeSpan() == 0.0f)
		{
			CallEvent(actor, EventName::Expired);
			actor->Destroy();
		}
	}
	return true;
}

void ULevel::TickActorsDeusEx(float elapsed)
{
	// The distance to the player is from the places at the start of the frame. The stasis check needs it for
	// actors that do not tick.
	UActor* player = engine->viewport ? engine->viewport->Actor() : nullptr;
	if (player && player->XLevel() == this && engine->LevelInfo->NetMode() == 0)
	{
		vec3 playerLocation = player->Location();
		for (UActor* actor : Actors)
		{
			if (actor && !actor->bStatic())
				actor->DistanceFromPlayer() = length(actor->Location() - playerLocation);
		}
	}

	// The list grows when an actor makes a new actor. The new actor ticks in this frame also.
	waitingActors.clear();
	int updated = 0;
	for (size_t i = 0; i < Actors.size(); i++)
	{
		if (Actors[i] && TickActorDeusEx(elapsed, Actors[i]))
			updated++;
	}

	// The actors that waited for their owner. An actor whose owner does not tick in this frame stays out.
	while (!waitingActors.empty() && updated > 0)
	{
		Array<UActor*> waiting;
		waiting.swap(waitingActors);
		updated = 0;
		for (UActor* actor : waiting)
		{
			if (actor->bTicked() != ticked && TickActorDeusEx(elapsed, actor))
				updated++;
		}
	}
	waitingActors.clear();
}

bool ULevel::HasActorName(const NameString& name) const
{
	for (UActor* actor : Actors)
	{
		if (actor && actor->Name == name)
			return true;
	}
	return false;
}

void ULevel::Tick(float elapsed, bool gamePaused)
{
	if (gamePaused)
	{
		for (size_t i = 0; i < Actors.size(); i++)
		{
			if (auto playerPawn = UObject::TryCast<UPlayerPawn>(Actors[i]))
				playerPawn->PausedInput(elapsed);
			else if (Actors[i] && Actors[i]->bAlwaysTick()) // Should this happen?
				TickActor(elapsed, Actors[i]);
		}
	}
	else if (engine->LevelInfo->bPlayersOnly())
	{
		for (size_t i = 0; i < Actors.size(); i++)
		{
			if (Actors[i] && (UObject::TryCast<UPlayerPawn>(Actors[i]) || Actors[i]->bAlwaysTick()))
				TickActor(elapsed, Actors[i]);
		}
	}
	else if (engine->LaunchInfo.IsDeusEx())
	{
		TickActorsDeusEx(elapsed);
	}
	else
	{
		for (size_t i = 0; i < Actors.size(); i++)
		{
			if (Actors[i])
				TickActor(elapsed, Actors[i]);
		}
	}

	// Deus Ex: the AI event manager runs after all actors, when the level is not paused (ULevel::Tick in Engine.dll)
	if (EventManager && !gamePaused && !engine->LevelInfo->bPlayersOnly())
		EventManager->Tick();

	Array<UActor*> newActorList;
	newActorList.reserve(Actors.size());
	for (UActor* actor : Actors)
	{
		if (actor)
		{
			actor->Index = (int)newActorList.size();
			newActorList.push_back(actor);
		}
	}
	Actors.swap(newActorList);

	ticked = !ticked;
}
