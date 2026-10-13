
#include "Precomp.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/UWarpZoneInfo.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/NavigationPoint/UInventorySpot.h"
#include "Packages/Engine/Actors/NavigationPoint/UWarpZoneMarker.h"
#include "Packages/Engine/Actors/Triggers/UTrigger.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <algorithm>
#include <cmath>

// The reach tests of APawn in Engine.dll of Deus Ex (build 1112fm): actorReachable, pointReachable, Reachable,
// walkReachable, flyReachable, swimReachable, FindBestJump, SuggestJumpVelocity and jumpLanding, and the part of
// execactorReachable that uses the reach specs.
//
// A reach test answers this question: can the pawn go from its place to a destination in a straight line? The test
// moves the pawn in test steps (walkMove, flyMove and swimMove in AIMovement.cpp), 100 steps at most, and puts it
// back. The test steps go through other pawns and through decorations, so only the level and the movers stop a
// test. The test is good when the pawn comes nearer to the destination than a threshold:
// - 15 units for a point, and for an actor that is not in the list below.
// - The two collision radii minus 2 units for an inventory item or a trigger.
// - The two collision radii plus the melee range of the pawn for a pawn as the goal. The melee range counts up to
//   1.5 times the collision radius of the pawn.
// A pawn that can jump goes in steps of 128 units and tries a jump where a step finds no floor. A pawn that cannot
// jump goes in steps of its collision radius.

namespace
{
	const float MaxReachDistSquared = 640000.0f; // 800 units

	// FVector::SafeNormal
	vec3 SafeNormal(const vec3& v)
	{
		float squared = dot(v, v);
		if (squared < 0.00000001f)
			return vec3(0.0f);
		return v * (1.0f / std::sqrt(squared));
	}

	// A zone that causes damage of a type that the pawn has no protection against
	bool IsPainZone(UPawn* pawn, UZoneInfo* zone)
	{
		return zone && zone->bPainZone() && zone->DamageType() != pawn->ReducedDamageType();
	}

	bool IsWaterZone(UZoneInfo* zone)
	{
		return zone && zone->bWaterZone();
	}

	// The end of each reach test: a pawn in the zone of a warp zone marker is at that marker. Put the pawn back.
	int FinishReach(UPawn* pawn, int success, int reachFlags, UActor* goalActor, const vec3& originalPos, const vec3& realVelocity)
	{
		if (!success && goalActor)
		{
			UWarpZoneMarker* marker = UObject::TryCast<UWarpZoneMarker>(goalActor);
			if (marker)
				success = pawn->AITestRegion().Zone == static_cast<UZoneInfo*>(marker->markedWarpZone());
		}
		pawn->Location() = originalPos;
		pawn->Velocity() = realVelocity;
		return success ? reachFlags : 0;
	}

	int FlyReachable(UPawn* pawn, const vec3& dest, float threshold, int reachFlags, UActor* goalActor);

	// APawn::swimReachable
	int SwimReachable(UPawn* pawn, const vec3& dest, float threshold, int reachFlags, UActor* goalActor)
	{
		reachFlags |= R_SWIM;
		int success = 0;
		vec3 originalPos = pawn->Location();
		vec3 realVelocity = pawn->Velocity();
		float thresholdSquared = threshold * threshold;
		float moveSize = std::max(200.0f, pawn->CollisionRadius());
		float moveSizeSquared = moveSize * moveSize;
		int ticks = 100;
		int stillMoving = UPawn::MoveOk;

		while (stillMoving != UPawn::MoveBlocked)
		{
			vec3 direction = dest - pawn->Location();
			float distanceSquared = dot(direction, direction);
			if (distanceSquared > thresholdSquared || std::abs(direction.z) > pawn->CollisionHeight())
			{
				if (distanceSquared < moveSizeSquared)
					stillMoving = pawn->AISwimMove(direction, 8.0f, goalActor);
				else
					stillMoving = pawn->AISwimMove(SafeNormal(direction) * moveSize, 4.1f, goalActor);

				if (stillMoving == UPawn::MoveBumpedGoal)
				{
					stillMoving = UPawn::MoveBlocked;
					success = 1;
				}

				UZoneInfo* zone = pawn->AITestRegion().Zone;
				if (!IsWaterZone(zone))
				{
					// The pawn came out of the water
					stillMoving = UPawn::MoveBlocked;
					if (pawn->bCanFly())
					{
						reachFlags = FlyReachable(pawn, dest, threshold, reachFlags, goalActor);
						success = reachFlags;
					}
					else if (pawn->bCanWalk() && dest.z < pawn->Location().z + pawn->MaxStepHeight() + 50.0f)
					{
						// A pawn that walks goes on when it can climb out of the water at this place
						float climb = std::max(pawn->CollisionHeight() + pawn->MaxStepHeight(), dest.z - pawn->Location().z);
						CollisionHit hit = pawn->AITestMove(vec3(0.0f, 0.0f, climb));
						if (hit.Fraction == 1.0f)
						{
							success = FlyReachable(pawn, dest, threshold, R_WALK, goalActor);
							reachFlags = R_WALK;
						}
					}
				}
				else if (IsPainZone(pawn, zone))
				{
					stillMoving = UPawn::MoveBlocked;
					success = 0;
				}
			}
			else
			{
				stillMoving = UPawn::MoveBlocked;
				success = 1;
			}

			ticks--;
			if (ticks < 0)
				stillMoving = UPawn::MoveBlocked;
		}

		return FinishReach(pawn, success, reachFlags, goalActor, originalPos, realVelocity);
	}

	// APawn::flyReachable
	int FlyReachable(UPawn* pawn, const vec3& dest, float threshold, int reachFlags, UActor* goalActor)
	{
		reachFlags |= R_FLY;
		int success = 0;
		vec3 originalPos = pawn->Location();
		vec3 realVelocity = pawn->Velocity();
		float thresholdSquared = threshold * threshold;
		float moveSize = std::max(200.0f, pawn->CollisionRadius());
		float moveSizeSquared = moveSize * moveSize;
		int ticks = 100;
		int stillMoving = UPawn::MoveOk;

		while (stillMoving != UPawn::MoveBlocked)
		{
			vec3 direction = dest - pawn->Location();
			float distanceSquared = dot(direction, direction);
			if (distanceSquared > thresholdSquared || std::abs(direction.z) > pawn->CollisionHeight())
			{
				if (distanceSquared < moveSizeSquared)
					stillMoving = pawn->AIFlyMove(direction, 8.0f, goalActor);
				else
					stillMoving = pawn->AIFlyMove(SafeNormal(direction) * moveSize, 4.1f, goalActor);

				if (stillMoving == UPawn::MoveBumpedGoal)
				{
					stillMoving = UPawn::MoveBlocked;
					success = 1;
				}
				else if (stillMoving != UPawn::MoveBlocked)
				{
					UZoneInfo* zone = pawn->AITestRegion().Zone;
					if (IsWaterZone(zone))
					{
						stillMoving = UPawn::MoveBlocked;
						if (pawn->bCanSwim() && !IsPainZone(pawn, zone))
						{
							reachFlags = SwimReachable(pawn, dest, threshold, reachFlags, goalActor);
							success = reachFlags;
						}
					}
				}
			}
			else
			{
				stillMoving = UPawn::MoveBlocked;
				success = 1;
			}

			ticks--;
			if (ticks < 0)
				stillMoving = UPawn::MoveBlocked;
		}

		return FinishReach(pawn, success, reachFlags, goalActor, originalPos, realVelocity);
	}

	// APawn::SuggestJumpVelocity: the horizontal velocity for a jump to a destination. The function finds the time
	// in the air for the start velocity upward, in steps of 0.05 seconds. The horizontal velocity is the distance
	// divided by that time, and the ground speed of the pawn at most.
	void SuggestJumpVelocity(UPawn* pawn, const vec3& dest, vec3& velocity)
	{
		UZoneInfo* zone = pawn->AITestRegion().Zone;
		float gravityZ = zone ? zone->ZoneGravity().z : -100.0f;
		if (gravityZ >= 0.0f)
			gravityZ = -100.0f;

		float floor = dest.z - pawn->Location().z;
		float startZ = velocity.z;
		float time = 0.0f;
		float currentZ = 0.0f;
		while (floor < currentZ || velocity.z > 0.0f)
		{
			velocity.z += gravityZ * 0.05f;
			time += 0.05f;
			currentZ += velocity.z * 0.05f;
		}
		if (std::abs(velocity.z) > 1.0f)
			time -= (currentZ - floor) / velocity.z;

		velocity = dest - pawn->Location();
		velocity.z = 0.0f;
		float speed;
		if (time <= 0.0f)
		{
			velocity = SafeNormal(velocity);
			speed = pawn->GroundSpeed();
		}
		else
		{
			float dist = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
			if (dist > 0.0f)
				velocity = velocity * (1.0f / dist);
			speed = std::min(pawn->GroundSpeed(), dist / time);
		}
		velocity = velocity * speed;
		velocity.z = startZ;
	}

	// APawn::FindBestJump with the move of the pawn: a jump from the place of the pawn in the direction of the
	// destination. The jump is good when the pawn comes down more than 8 units nearer to the destination, less
	// than 350 units below its start, and not in a zone that it cannot be in. The pawn stays where it came down.
	bool FindBestJump(UPawn* pawn, const vec3& dest, vec3 velocity)
	{
		vec3 realLocation = pawn->Location();
		velocity.z = pawn->JumpZ();
		SuggestJumpVelocity(pawn, dest, velocity);
		pawn->AIJumpLanding(velocity, true);

		if (IsPainZone(pawn, pawn->AITestFootRegion().Zone))
			return false;
		if (!pawn->bCanSwim() && IsWaterZone(pawn->AITestRegion().Zone))
			return false;
		if (realLocation.z - pawn->Location().z >= 350.0f)
			return false;
		return length(dest - realLocation) - length(dest - pawn->Location()) > 8.0f;
	}

	// APawn::walkReachable
	int WalkReachable(UPawn* pawn, const vec3& dest, float threshold, int reachFlags, UActor* goalActor)
	{
		reachFlags |= R_WALK;
		int success = 0;
		vec3 originalPos = pawn->Location();
		vec3 realVelocity = pawn->Velocity();
		float moveSize = pawn->bCanJump() ? std::max(128.0f, pawn->CollisionRadius()) : pawn->CollisionRadius();
		// As in the original, the limit for a full step stays the same when the step becomes smaller
		float moveSizeSquared = moveSize * moveSize;
		float maxZDiff = pawn->CollisionHeight();
		if (goalActor)
			maxZDiff = std::max(maxZDiff, goalActor->CollisionHeight());
		CollisionHit hit;
		int ticks = 100;
		int stillMoving = UPawn::MoveOk;

		while (stillMoving == UPawn::MoveOk)
		{
			vec3 direction = dest - pawn->Location();
			float zDiff = direction.z;
			direction.z = 0.0f;
			float distanceSquared = dot(direction, direction);

			// The destination is too far above the pawn for the distance that is left
			if (zDiff > maxZDiff && (zDiff - maxZDiff) * (zDiff - maxZDiff) * 0.8f > distanceSquared)
				break;

			if (distanceSquared <= threshold * threshold)
			{
				// The pawn is near the destination. The height must agree also.
				stillMoving = UPawn::MoveBlocked;
				if (std::abs(zDiff) < maxZDiff)
				{
					success = 1;
				}
				else if (hit.Normal.z < 0.95f && hit.Normal.z > 0.7f)
				{
					// The pawn is on a slope, which lifts it or lowers it in relation to the destination
					float slope = std::sqrt(1.0f / (hit.Normal.z * hit.Normal.z) - 1.0f);
					if (zDiff < 0.0f && -zDiff < pawn->CollisionHeight() + pawn->CollisionRadius() * slope)
					{
						success = 1;
					}
					else
					{
						float goalRadius = goalActor ? goalActor->CollisionRadius() : 46.0f;
						if (pawn->CollisionRadius() < goalRadius && zDiff < maxZDiff + (goalRadius + 15.0f - pawn->CollisionRadius()) * slope)
							success = 1;
					}
				}
			}
			else
			{
				if (distanceSquared < moveSizeSquared)
				{
					stillMoving = pawn->AIWalkMove(direction, 8.0f, goalActor, &hit);
				}
				else
				{
					direction = SafeNormal(direction);
					stillMoving = pawn->AIWalkMove(direction * moveSize, 4.1f, goalActor, &hit);
				}

				if (stillMoving != UPawn::MoveOk)
				{
					if (stillMoving == UPawn::MoveBumpedGoal)
					{
						stillMoving = UPawn::MoveBlocked;
						success = 1;
					}
					else if (pawn->AITestRegion().ZoneNumber == 0)
					{
						// The pawn is out of the level
						stillMoving = UPawn::MoveBlocked;
						success = 0;
					}
					else if (pawn->bCanFly())
					{
						stillMoving = UPawn::MoveBlocked;
						reachFlags = FlyReachable(pawn, dest, threshold, reachFlags, goalActor);
						success = reachFlags;
					}
					else if (pawn->bCanJump())
					{
						reachFlags |= R_JUMP;
						if (stillMoving == UPawn::MoveFalling)
							stillMoving = FindBestJump(pawn, dest, SafeNormal(direction) * pawn->GroundSpeed()) ? UPawn::MoveOk : UPawn::MoveBlocked;
					}
					else if (stillMoving == UPawn::MoveFalling && moveSize > pawn->MaxStepHeight())
					{
						// Try again with smaller steps
						moveSize = pawn->MaxStepHeight();
						stillMoving = UPawn::MoveOk;
					}
				}

				if (IsPainZone(pawn, pawn->AITestFootRegion().Zone))
				{
					stillMoving = UPawn::MoveBlocked;
					success = 0;
				}

				UZoneInfo* zone = pawn->AITestRegion().Zone;
				if (IsWaterZone(zone))
				{
					stillMoving = UPawn::MoveBlocked;
					if (pawn->bCanSwim() && !IsPainZone(pawn, zone))
					{
						reachFlags = SwimReachable(pawn, dest, threshold, reachFlags, goalActor);
						success = reachFlags;
					}
				}
			}

			ticks--;
			if (ticks < 0)
				stillMoving = UPawn::MoveBlocked;
		}

		return FinishReach(pawn, success, reachFlags, goalActor, originalPos, realVelocity);
	}

	// The destination of a reach test for an actor: the place where the pawn stands when it is at the actor
	vec3 AdjustedDestination(UPawn* pawn, const vec3& location)
	{
		vec3 realLocation = pawn->Location();
		vec3 point = location;
		if (pawn->AITestFarMove(location))
		{
			point = pawn->Location();
			pawn->Location() = realLocation;
		}
		return point;
	}
}

// APawn::jumpLanding: the place where a pawn that falls with a velocity comes down. The steps are 0.1 seconds, 36 at
// most. With movePawn, the pawn stays at that place. The result is the start place when the test finds no landing.
vec3 UPawn::AIJumpLanding(vec3 velocity, bool movePawn)
{
	vec3 origPos = Location();
	const float timeStep = 0.1f;
	bool landed = false;
	int ticks = 0;

	while (!landed)
	{
		UZoneInfo* zone = AITestRegion().Zone;
		if (!zone)
		{
			Location() = origPos;
			break;
		}

		velocity = velocity * (1.0f - timeStep * zone->ZoneFluidFriction()) + zone->ZoneGravity() * timeStep;
		vec3 adjusted = (velocity + zone->ZoneVelocity()) * timeStep;

		CollisionHit hit = AITestMove(adjusted);
		if (IsWaterZone(AITestRegion().Zone))
		{
			landed = true;
		}
		else if (hit.Fraction < 1.0f)
		{
			if (hit.Normal.z > 0.7f)
			{
				landed = true;
			}
			else
			{
				// Go along the wall with the rest of the step
				vec3 oldHitNormal = hit.Normal;
				vec3 delta = (adjusted - hit.Normal * dot(adjusted, hit.Normal)) * (1.0f - hit.Fraction);
				if (dot(delta, adjusted) >= 0.0f)
				{
					hit = AITestMove(delta);
					if (hit.Fraction < 1.0f)
					{
						if (hit.Normal.z > 0.7f)
							landed = true;
						TwoWallAdjust(SafeNormal(adjusted), delta, hit.Normal, oldHitNormal, hit.Fraction);
						hit = AITestMove(delta);
						if (hit.Normal.z > 0.7f)
							landed = true;
					}
				}
			}
		}

		ticks++;
		if (AITestRegion().ZoneNumber == 0 || ticks > 35 || dot(velocity, velocity) > 2500000.0f)
		{
			Location() = origPos;
			landed = true;
		}
	}

	vec3 landing = Location();
	if (!movePawn)
		Location() = origPos;
	return landing;
}

// APawn::Reachable: the reach test for the way that the pawn moves at this time. The result is the reach flags of
// the way, or 0 when the pawn cannot go to the point.
int UPawn::ReachableDeusEx(const vec3& point, float threshold, UActor* goalActor)
{
	if (IsWaterZone(AITestRegion().Zone))
		return SwimReachable(this, point, threshold, 0, goalActor);
	if (Physics() == PHYS_Walking || Physics() == PHYS_Swimming)
		return WalkReachable(this, point, threshold, 0, goalActor);
	if (Physics() == PHYS_Flying)
		return FlyReachable(this, point, threshold, 0, goalActor);
	return 0;
}

// APawn::actorReachable
bool UPawn::ActorReachableDeusEx(UActor* other, bool knowVisible)
{
	if (!other)
		return false;

	vec3 dir = other->Location() - Location();
	float distSquared = dot(dir, dir);
	UPawn* otherPawn = UObject::TryCast<UPawn>(other);

	if (otherPawn)
	{
		if (IsPainZone(this, otherPawn->FootRegion().Zone))
			return false;
	}
	else
	{
		// Only a pawn can be farther away than 800 units
		if (distSquared > MaxReachDistSquared)
			return false;
		if (IsPainZone(this, other->Region().Zone))
			return false;
	}

	if (IsWaterZone(other->Region().Zone) && !bCanSwim())
		return false;

	if (!knowVisible)
	{
		// The level and the movers hide an actor
		vec3 viewPoint = Location();
		viewPoint.z += BaseEyeHeight();
		TraceFlags flags;
		flags.world = true;
		flags.movers = true;
		CollisionHit hit = XLevel()->Collision.TraceFirstHit(viewPoint, other->Location(), this, vec3(0.0f), flags);
		if (hit.Fraction != 1.0f && hit.Actor != other)
			return false;
	}

	if (otherPawn)
	{
		float threshold = CollisionRadius() + std::min(1.5f * CollisionRadius(), MeleeRange()) + other->CollisionRadius();
		if (distSquared <= threshold * threshold)
			return true;
		if (distSquared > MaxReachDistSquared)
			threshold = std::max(threshold, std::sqrt(distSquared) - 800.0f);
		return ReachableDeusEx(AdjustedDestination(this, other->Location()), threshold, other) != 0;
	}

	float threshold = 15.0f;
	if (UObject::TryCast<UInventory>(other) || UObject::TryCast<UTrigger>(other))
		threshold = other->CollisionRadius() + CollisionRadius() - 2.0f;
	UActor* goalActor = (other->bBlockActors() || UObject::TryCast<UWarpZoneMarker>(other)) ? other : nullptr;
	return ReachableDeusEx(AdjustedDestination(this, other->Location()), threshold, goalActor) != 0;
}

// APawn::pointReachable
bool UPawn::PointReachableDeusEx(vec3 point, bool knowVisible)
{
	vec3 delta = point - Location();
	if (delta.x * delta.x + delta.y * delta.y > 1000000.0f)
		return false;

	UZoneInfo* pointZone = XLevel()->Model->FindRegion(point, Level()).Zone;
	if (IsWaterZone(pointZone) && !bCanSwim() && !IsWaterZone(AITestRegion().Zone))
		return false;
	if (IsPainZone(this, pointZone))
	{
		UZoneInfo* footZone = AITestFootRegion().Zone;
		if (!(footZone && footZone->bPainZone()))
			return false;
	}

	if (!knowVisible)
	{
		vec3 viewPoint = Location();
		viewPoint.z += BaseEyeHeight();
		if (!FastTrace(point, viewPoint))
			return false;
	}

	return ReachableDeusEx(AdjustedDestination(this, point), 15.0f, nullptr) != 0;
}

// APawn::execactorReachable, the function that a script calls.
// A pawn that stands on a navigation point and asks for a second navigation point gets the answer from the reach
// specs: the answer is yes only when a reach spec that the pawn can use goes from the first point to the second.
// An inventory item with a marker counts as that marker. All other cases use the reach test.
bool UPawn::ActorReachableScriptDeusEx(UActor* actor)
{
	if (!actor)
		return false;

	UActor* realActor = nullptr;
	UInventory* inventory = UObject::TryCast<UInventory>(actor);
	if (inventory && inventory->myMarker())
	{
		realActor = actor;
		actor = inventory->myMarker();
	}

	UNavigationPoint* goal = UObject::TryCast<UNavigationPoint>(actor);
	if (goal && !XLevel()->ReachSpecs.empty() && CollisionRadius() <= 115.0f)
	{
		float maxDist = std::max(48.0f, CollisionRadius());
		auto standsOn = [&](UNavigationPoint* nav)
			{
				if (!(std::abs(nav->Location().z - Location().z) < CollisionHeight()))
					return false;
				float dx = nav->Location().x - Location().x;
				float dy = nav->Location().y - Location().y;
				return dx * dx + dy * dy < maxDist * maxDist;
			};

		bool onPath = false;
		UNavigationPoint* target = UObject::TryCast<UNavigationPoint>(MoveTarget());
		if (target && standsOn(target))
		{
			onPath = true;
			if (target == goal || AICanMoveTo(target, goal))
				return true;
		}
		for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		{
			if (standsOn(nav))
			{
				onPath = true;
				if (nav == goal || AICanMoveTo(nav, goal))
					return true;
			}
		}
		if (onPath && Physics() != PHYS_Flying)
			return false;
	}

	if (realActor)
		actor = realActor;
	bool reachable = ActorReachableDeusEx(actor, false);
	if (AITracePath())
		LogMessage("Harness: reach " + Name.ToString() + " goal=" + actor->Name.ToString() + (reachable ? " yes" : " no"));
	return reachable;
}
