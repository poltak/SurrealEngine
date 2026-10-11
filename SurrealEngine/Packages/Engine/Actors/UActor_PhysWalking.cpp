
#include "Precomp.h"
#include "UActor.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Engine.h"

bool UActor::ShouldAbortJumping(UPawn* pawn, vec3 oldPosition, vec3 stepDownDelta)
{
	//check if we can still walk, if so, nothing changes
	if (TryStepToGround(stepDownDelta))
	{
		return false;
	}

	if (pawn->bCanJump())
	{
		CallEvent(this, EventName::MayFall);

		//the pawn decides to not jump over the ledge,
		//stop imediately and backtrack
		if (!pawn->bCanJump())
		{
			Velocity() = vec3(0.0f);
			Acceleration() = vec3(0.0f);
			TryMove(oldPosition - Location());
			return true;
		}
	}

	SetPhysics(PHYS_Falling);
	SetBase(nullptr, true);
	return false;
}

// Ladders of Deus Ex. In the Engine.dll of the game (build 1112fm), the physics of a player start with this check
// when the player walks, falls or swims. A wall near the player that has a texture of the group "Ladder" makes the
// player climb, and the usual physics do not run.
bool UActor::TickLadder(UPawn* pawn, float elapsed)
{
	if (!engine->LaunchInfo.IsDeusEx())
		return false;

	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(pawn);
	if (!player || !IsAtLadder())
		return false;

	// With a movement key down, the player goes up or down in the direction of the view. The view pitch gets 33.75
	// degrees more for this, so a level view goes up. The player goes down only with a view that points far down.
	if (Acceleration() != vec3(0.0f))
	{
		Rotator rot = player->ViewRotation();
		rot.Pitch += (rot.Pitch >= 0) ? 0x1800 : -0x1800;
		pawn->bIsWalking() = true;
		Velocity().z = pawn->GroundSpeed() * Coords::Rotation(rot).XAxis.z * 0.5f;
	}

	TickFlying(elapsed);

	// With no movement key down, the player stays at the same place of the ladder
	if (Acceleration() == vec3(0.0f))
		Velocity() = vec3(0.0f);
	return true;
}

// The search for a ladder goes from the feet of the player to points at a distance of two collision radii, at nine
// heights from the feet up to 32 units higher. It looks forward, to the left, to the right and then back. A line
// that hits level geometry with a texture of the group "Ladder" ends the search.
bool UActor::IsAtLadder()
{
	ULevel* level = XLevel();
	if (!level || !level->Model)
		return false;

	vec3 start = Location() - vec3(0.0f, 0.0f, CollisionHeight() * 0.95f);
	for (int yawOffset : { 0, -0x4000, 0x4000, 0x8000 })
	{
		Rotator rot(0, Rotation().Yaw + yawOffset, Rotation().Roll);
		vec3 end = start + Coords::Rotation(rot).XAxis * (CollisionRadius() * 2.0f);
		for (int rise = 0; rise <= 32; rise += 4)
		{
			// The line stops at level geometry and at movers. Only level geometry gives a texture.
			for (const CollisionHit& hit : level->Collision.Trace(start, end + vec3(0.0f, 0.0f, (float)rise), 0.0f, 0.0f, true, true, false))
			{
				if (hit.Actor && !UObject::TryCast<UMover>(hit.Actor))
					continue;
				if (!hit.Actor && hit.Node && hit.Node->Surf >= 0 && (size_t)hit.Node->Surf < level->Model->Surfaces.size())
				{
					UTexture* texture = level->Model->Surfaces[hit.Node->Surf].Material;
					if (texture && texture->Outer() && texture->Outer()->Name == "Ladder")
						return true;
				}
				break;
			}
		}
	}
	return false;
}

void UActor::TickWalking(float elapsed)
{
	// Only pawns can walk!
	UPawn* pawn = PreparePawnMovementTick();
	if (!pawn)
		return;

	if (TickLadder(pawn, elapsed))
		return;

	// Update the actor velocity based on the acceleration and zone

	UZoneInfo* zone = Region().Zone;
	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(this);
	bool isCrouching = player && player->bIsWalking();

	Velocity().z = 0.0f;

	float accelRate = pawn->AccelRate() * (isCrouching ? 0.3f : 1.0f);
	float maxSpeed = (player ? player->GroundSpeed() : pawn->GroundSpeed() * pawn->DesiredSpeed()) * (isCrouching ? 0.3f : 1.0f);
	ApplyMovementAcceleration(elapsed, accelRate, zone->ZoneGroundFriction(), maxSpeed);

	Velocity().z = 0.0f;

	// The classic step up, move and step down algorithm:

	float gravityDirection = zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	vec3 stepUpDelta(0.0f, 0.0f, -gravityDirection * pawn->MaxStepHeight());
	vec3 stepDownDelta(0.0f, 0.0f, gravityDirection * pawn->MaxStepHeight() * stepDownDeltaFactor);

	// "Step up and move" as long as we have time left and only hitting surfaces with low enough slope that it could be walked
	float timeLeft = elapsed;
	vec3 vel = Velocity() + zone->ZoneVelocity() * elapsed * 25.0f;

	//old position for tracking
	vec3 oldPosition = Location();

	//included Z in check - if its not 0 due to Zone properties, no action would have been taken previously
	//could lead to latend bugs
	if (length(vel) > 0)
	{
		for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
		{
			if (ShouldAbortMovementTick(PHYS_Walking))
				break;

			vec3 moveDelta = vel * timeLeft;

			//movement logic was inverted, causing overhaed buttons to be to easy to push
			// -> kevlar suit button in VortexRikers activates by moving under it
			//alternative approach: first move without stepUp -> only step up on collision
			//also yields a simpler code path since it avoids the need for "headbump" checks

			// try move forward
			CollisionHit hit = TryMove(moveDelta);
			timeLeft -= timeLeft * hit.Fraction;
			moveDelta = vel * timeLeft;

			//check for fall and backtrack
			if (ShouldAbortJumping(pawn, oldPosition, stepDownDelta))
				return;

			// if hit, step up and try again - maybe there was a ledge to get over
			if (hit.Fraction < 1.0f)
			{
				TryMove(stepUpDelta);
				oldPosition = Location();
				hit = TryMove(moveDelta);
				timeLeft -= timeLeft * hit.Fraction;

				//check for fall and backtrack
				if (ShouldAbortJumping(pawn, oldPosition, stepDownDelta * 2.0f))
				{
					TryMove(-stepUpDelta);
					return;
				}

				// move back down to original vertical position
				TryMove(-stepUpDelta);
			}

			oldPosition = Location();

			if (hit.Fraction < 1.0f)
			{
				if (player && hit.Actor)
				{
					if (UObject::IsType<UDecoration>(hit.Actor) && UObject::Cast<UDecoration>(hit.Actor)->bPushable() && dot(hit.Normal, moveDelta) < -0.9f)
					{
						// We hit a pushable decoration that is facing our movement direction

						//why does hitting a pushable decoration set the teleport flag?
						bJustTeleported() = true;
						vel = Velocity() = Velocity() * Mass() / (Mass() + hit.Actor->Mass());
						FireHitWall(hit);
						timeLeft = 0.0f;
					}
					else if (hit.Actor->bCollideActors() && hit.Actor->CollisionHeight() > 0.0f && hit.Actor->CollisionRadius() > 0.0f)
					{
						// TODO: We hit a non-movable actor

					}
				}
				else if (hit.Normal.z < 0.2f && hit.Normal.z > -0.2f)
				{
					// We hit a wall
					FireHitWall(hit);

					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						timeLeft -= timeLeft * hit.Fraction;
						if (hit.Fraction < 1.0f)
						{
							FireHitWall(hit);
						}
					}
					else
					{
						timeLeft = 0.0f;
					}
				}
			}

			//check for fall and backtrack
			if (ShouldAbortJumping(pawn, oldPosition, stepDownDelta))
				return;
		}
	}
	else
	{
		// Can we reach the ground from here?
		if (!TryStepToGround(stepDownDelta))
		{
			SetPhysics(PHYS_Falling);
			SetBase(nullptr, true);
		}
	}

	RecomputeVelocityFromDisplacement(elapsed);
	Velocity().z = 0.0f;
}
