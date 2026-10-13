
#include "Precomp.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Math/coords.h"
#include "Utils/Random.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <algorithm>
#include <cmath>

// The Deus Ex AI movement functions of APawn in Engine.dll (build 1112fm, UnPawnExt.cpp):
//
// - AIDirectionReachable moves the pawn in one direction, one test step at a time, as far as it can go.
//   It gives the last position that is inside a distance band around a focus point.
// - AIPickRandomDestination tries random directions around a central direction with AIDirectionReachable.
// - GetPathnodeList gives the navigation points that the pawn can go to first, with their distances.
// - ComputePathnodeDistances writes the path distance from the pawn into visitedWeight of each navigation point.
//
// The test steps are walkMove, flyMove and swimMove of the original engine with bAdjust = 0. They go through other
// pawns and through decorations. The reach tests in ReachTest.cpp use the same steps.

namespace
{
	// -1: nearer than the band. 0: in the band. 1: farther than the band.
	int RangeCheck(float minDistSquared, float distSquared, float maxDistSquared)
	{
		if (distSquared < minDistSquared)
			return -1;
		if (maxDistSquared < distSquared)
			return 1;
		return 0;
	}
}

// ULevel::MoveActor with bTest and bIgnorePawns: move as far as the world and the blocking actors permit.
// The move goes through each pawn, and through each decoration that is not static.
// No touch or bump events. The collision hash keeps the old position, the caller puts the pawn back.
CollisionHit UPawn::AITestMove(const vec3& delta)
{
	CollisionHit blockingHit;
	if (dot(delta, delta) < 0.00000001f)
		return blockingHit;

	CollisionHitList hits = XLevel()->Collision.Trace(Location(), Location() + delta, CollisionHeight(), CollisionRadius(), bCollideActors(), bCollideWorld(), false);
	for (auto& hit : hits)
	{
		if (hit.Actor)
		{
			if (hit.Actor == this || UObject::TryCast<UPawn>(hit.Actor))
				continue;
			if (UObject::TryCast<UDecoration>(hit.Actor) && !hit.Actor->bStatic())
				continue;
			if (!hit.Actor->bBlockActors() || !bBlockActors())
				continue;
			if (hit.Actor->IsBasedOn(this) || IsBasedOn(hit.Actor))
				continue;
		}
		blockingHit = hit;
		break;
	}

	Location() += delta * blockingHit.Fraction;
	return blockingHit;
}

// ULevel::FarMoveActor with bTest: put the pawn at a location, or at a free place near it. No events.
// The caller puts the pawn back.
bool UPawn::AITestFarMove(const vec3& location)
{
	auto spot = CheckLocation(location, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!spot.first)
		return false;
	Location() = spot.second;
	return true;
}

// The original sets Region and FootRegion of the pawn in each test move. This engine does not change them in a test.
// These functions give the two regions for the place of the pawn in a test.
PointRegion UPawn::AITestRegion()
{
	return XLevel()->Model->FindRegion(Location(), Level());
}

PointRegion UPawn::AITestFootRegion()
{
	vec3 footLocation = Location();
	footLocation.z -= CollisionHeight();
	return XLevel()->Model->FindRegion(footLocation, Level());
}

// APawn::walkMove(Delta, Hit, GoalActor, threshold, bAdjust = 0): one test step on the ground.
// MoveOk: the pawn went the step. MoveBlocked: a wall or an actor stopped the pawn. MoveFalling: the end of the step
// has no floor. MoveBumpedGoal: the pawn hit the goal actor.
// A step that finds no floor in a drop of the step height leaves the pawn at the end of the step, above the ground.
// The jump test of the walk reach test starts at that place.
int UPawn::AIWalkMove(vec3 delta, float threshold, UActor* goalActor, CollisionHit* lastHit)
{
	CollisionHit localHit;
	CollisionHit& hit = lastHit ? *lastHit : localHit;
	vec3 startLocation = Location();
	delta.z = 0.0f;

	UZoneInfo* zone = AITestRegion().Zone;
	float gravityDirection = zone && zone->ZoneGravity().z > 0.0f ? 1.0f : -1.0f;
	vec3 down = vec3(0.0f, 0.0f, gravityDirection * MaxStepHeight());
	vec3 up = down * -1.0f;

	hit = AITestMove(delta);
	if (goalActor && hit.Actor == goalActor)
		return MoveBumpedGoal;
	if (hit.Fraction < 1.0f)
	{
		// Try to step up
		delta = delta * (1.0f - hit.Fraction);
		hit = AITestMove(up);
		hit = AITestMove(delta);
		if (goalActor && hit.Actor == goalActor)
			return MoveBumpedGoal;
		hit = AITestMove(down);
		if (goalActor && hit.Actor == goalActor)
			return MoveBumpedGoal;

		// Only a good floor is acceptable
		if (hit.Fraction < 1.0f && hit.Normal.z < 0.7f)
		{
			Location() = startLocation;
			return MoveBlocked;
		}
	}

	// Drop to the floor
	vec3 stepEnd = Location();
	hit = AITestMove(vec3(0.0f, 0.0f, gravityDirection * (MaxStepHeight() + 2.0f)));
	if (hit.Fraction == 1.0f)
	{
		Location() = stepEnd;
		return MoveFalling;
	}
	if (hit.Normal.z < 0.7f)
	{
		Location() = startLocation;
		return MoveFalling;
	}

	vec3 moved = Location() - startLocation;
	if (dot(moved, moved) < threshold * threshold)
		return MoveBlocked;
	return MoveOk;
}

// APawn::flyMove(Delta, GoalActor, threshold, bAdjust = 0)
int UPawn::AIFlyMove(vec3 delta, float threshold, UActor* goalActor)
{
	vec3 startLocation = Location();
	vec3 up = vec3(0.0f, 0.0f, MaxStepHeight());

	CollisionHit hit = AITestMove(delta);
	if (goalActor && hit.Actor == goalActor)
		return MoveBumpedGoal;
	if (hit.Fraction < 1.0f)
	{
		delta = delta * (1.0f - hit.Fraction);
		AITestMove(up);
		hit = AITestMove(delta);
		if (goalActor && hit.Actor == goalActor)
			return MoveBumpedGoal;
	}

	vec3 moved = Location() - startLocation;
	if (dot(moved, moved) < threshold * threshold)
		return MoveBlocked;
	return MoveOk;
}

// APawn::swimMove(Delta, GoalActor, threshold, bAdjust = 0)
int UPawn::AISwimMove(vec3 delta, float threshold, UActor* goalActor)
{
	vec3 startLocation = Location();
	vec3 up = vec3(0.0f, 0.0f, MaxStepHeight());

	CollisionHit hit = AITestMove(delta);
	if (goalActor && hit.Actor == goalActor)
		return MoveBumpedGoal;

	UZoneInfo* zone = AITestRegion().Zone;
	if (!zone || !zone->bWaterZone())
	{
		// The pawn came out of the water. Put it back to the water line (APawn::findWaterLine): the first place on
		// the line of the step that is out of the water. The search halves the line until it is shorter than 1 unit.
		vec3 start = startLocation;
		vec3 end = Location();
		while (dot(end - start, end - start) >= 1.0f)
		{
			vec3 middle = (start + end) * 0.5f;
			UZoneInfo* middleZone = XLevel()->Model->FindRegion(middle, Level()).Zone;
			if (middleZone && middleZone->bWaterZone())
				start = middle;
			else
				end = middle;
		}
		if (end != Location())
			AITestMove(end - Location());
		return MoveBlocked;
	}

	if (hit.Fraction < 1.0f)
	{
		delta = delta * (1.0f - hit.Fraction);
		AITestMove(up);
		hit = AITestMove(delta);
		if (goalActor && hit.Actor == goalActor)
			return MoveBumpedGoal;
	}

	vec3 moved = Location() - startLocation;
	if (dot(moved, moved) < threshold * threshold)
		return MoveBlocked;
	return MoveOk;
}

// The focus is a copy. A caller can give the location of this pawn as the focus, and the test moves the pawn.
bool UPawn::AIDirectionReachable(vec3 focus, int yaw, int pitch, float minDist, float maxDist, vec3& bestDest)
{
	enum class Mode { Walk, Fly, Swim };

	float minDistSquared = minDist * minDist;
	float maxDistSquared = maxDist * maxDist;
	vec3 savedLocation = Location();
	vec3 savedVelocity = Velocity();
	float stepSize = clamp(CollisionRadius(), 5.0f, 25.0f);
	int stepsLeft = 100;
	bool found = false;
	vec3 bestLocation = Location();

	vec3 toFocus = Location() - focus;
	int range = RangeCheck(minDistSquared, dot(toFocus, toFocus), maxDistSquared);

	Mode mode;
	Rotator direction;
	bool inWater = Region().Zone && Region().Zone->bWaterZone();
	if (inWater)
	{
		mode = Mode::Swim;
		direction = Rotator(pitch, yaw, 0);
	}
	else if (Physics() == PHYS_Walking || Physics() == PHYS_Swimming)
	{
		mode = Mode::Walk;
		direction = Rotator(0, yaw, 0);
	}
	else if (Physics() == PHYS_Flying)
	{
		mode = Mode::Fly;
		direction = Rotator(pitch, yaw, 0);
	}
	else
	{
		return false;
	}

	vec3 X, Y, Z;
	Coords::Rotation(direction).GetAxes(X, Y, Z);

	int result = MoveOk;
	while (result == MoveOk)
	{
		if (mode == Mode::Walk)
			result = AIWalkMove(X * stepSize, 4.1f);
		else if (mode == Mode::Fly)
			result = AIFlyMove(X * stepSize, 4.1f);
		else
			result = AISwimMove(X * stepSize, 4.1f);

		PointRegion region = XLevel()->Model->FindRegion(Location(), Level());
		vec3 footLocation = Location();
		footLocation.z -= CollisionHeight();
		PointRegion footRegion = XLevel()->Model->FindRegion(footLocation, Level());

		if (result != MoveOk)
		{
			if (region.ZoneNumber == 0)
			{
				result = MoveBlocked;
			}
			else if (mode == Mode::Walk && result == MoveFalling && MaxStepHeight() < stepSize)
			{
				// A ledge. The pawn is at the end of the step, above the ground. Go on from there with smaller steps.
				stepSize = MaxStepHeight();
				result = MoveOk;
			}
		}

		// The pawn does not go into a zone that causes damage, and it does not go into or out of the water
		if (footRegion.Zone && footRegion.Zone->bPainZone() && footRegion.Zone->DamageType() != ReducedDamageType())
			result = MoveBlocked;
		bool nowInWater = region.Zone && region.Zone->bWaterZone();
		if ((mode == Mode::Swim) != nowInWater)
			result = MoveBlocked;

		stepsLeft--;
		if (stepsLeft < 0)
		{
			result = MoveBlocked;
			break;
		}
		if (result != MoveOk)
			break;

		toFocus = Location() - focus;
		int newRange = RangeCheck(minDistSquared, dot(toFocus, toFocus), maxDistSquared);
		if (newRange == 0)
		{
			bestLocation = Location();
			found = true;
			// The pawn came into the band from the far side. The first position in the band is the result.
			if (range > 0)
				result = MoveBlocked;
		}
		else if (range != newRange)
		{
			// The pawn went through the band in one step
			if (range != 0)
			{
				bestLocation = Location();
				found = true;
			}
			result = MoveBlocked;
		}
		range = newRange;
	}

	if (AITracePath())
	{
		char buf[200];
		snprintf(buf, sizeof(buf), " yaw=%d min=%.0f max=%.0f", yaw & 65535, minDist, maxDist);
		std::string text = "Harness: direction " + Name.ToString() + buf;
		if (found)
		{
			snprintf(buf, sizeof(buf), " dest=(%.0f %.0f %.0f)", bestLocation.x, bestLocation.y, bestLocation.z);
			text += buf;
		}
		else
		{
			text += " no";
		}
		LogMessage(text);
	}

	Location() = savedLocation;
	Velocity() = savedVelocity;
	bestDest = bestLocation;
	return found;
}

// AActor::RandomBiasedRotation. A distribution of 0 gives an even spread. A distribution of 1 gives the central direction.
Rotator UActor::RandomBiasedRotation(int centralYaw, float yawDistribution, int centralPitch, float pitchDistribution)
{
	auto biased = [](float distribution, float negativeScale, float positiveScale)
		{
			float d = (clamp(distribution, 0.0f, 1.0f) + 1.0f) * 0.5f;
			float x = FRand() * 2.0f - 1.0f;
			float ax = std::abs(x);
			float f;
			if (ax <= d)
				f = ax * (1.0f - d) / d;
			else
				f = (ax - d) * d / (1.0f - d) + (1.0f - d);
			return (int)(f * (x < 0.0f ? negativeScale : positiveScale));
		};

	int yaw = biased(yawDistribution, -32768.0f, 32767.0f);
	int pitch = biased(pitchDistribution, -16384.0f, 16383.0f);
	return Rotator(centralPitch + pitch, centralYaw + yaw, 0);
}

bool UPawn::AIPickRandomDestination(float minDist, float maxDist, int centralYaw, float yawDistribution, int centralPitch, float pitchDistribution, int tries, float multiplier, vec3& dest)
{
	tries = std::max(tries, 1);
	multiplier = clamp(multiplier, 0.0001f, 1.0f);

	vec3 bestDest = Location();
	bool success = false;

	for (; tries > 0; tries--)
	{
		Rotator rot = RandomBiasedRotation(centralYaw, yawDistribution, centralPitch, pitchDistribution);
		success = AIDirectionReachable(Location(), rot.Yaw, rot.Pitch, minDist, maxDist, bestDest);
		if (success)
		{
			// A multiplier below 1 gives a position at that part of the distance that the pawn can go
			if (multiplier < 1.0f)
			{
				float dist = length(bestDest - Location());
				success = AIDirectionReachable(Location(), rot.Yaw, rot.Pitch, minDist, dist * multiplier, bestDest);
			}
			if (success)
				break;
		}
	}

	dest = bestDest;
	return success;
}

int UPawn::AICalcMoveFlags()
{
	return (bCanWalk() ? R_WALK : 0) + (bCanFly() ? R_FLY : 0) + (bCanSwim() ? R_SWIM : 0) + (bCanJump() ? R_JUMP : 0) +
		(bCanOpenDoors() ? R_DOOR : 0) + (bCanDoSpecial() ? R_SPECIAL : 0) + (bIsPlayer() ? R_PLAYERONLY : 0);
}

// APawn::GetPathnodeList: the navigation points that the pawn (or the start actor) can go to first, with their distances
Array<UPawn::AINavInfo> UPawn::AIGetPathnodeList(int maxCount, UActor* startActor, bool usePrunedPaths, bool sort)
{
	Array<AINavInfo> list;

	int moveFlags = AICalcMoveFlags();
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();

	UNavigationPoint* startNav = nullptr;
	vec3 startLocation = Location();
	if (startActor)
	{
		startNav = UObject::TryCast<UNavigationPoint>(startActor);
		if (!startNav)
			startLocation = startActor->Location();
	}

	if (!startNav)
	{
		// The pawn stands on a navigation point
		UNavigationPoint* moveTargetNav = UObject::TryCast<UNavigationPoint>(MoveTarget());
		if (moveTargetNav && IsOverlapping(moveTargetNav))
			startNav = moveTargetNav;
		for (UNavigationPoint* nav = Level()->NavigationPointList(); nav && !startNav; nav = nav->nextNavigationPoint())
		{
			if (IsOverlapping(nav))
				startNav = nav;
		}
	}

	if (startNav)
	{
		const auto& specs = XLevel()->ReachSpecs;
		auto addPaths = [&](FixedArrayView<int, 16> paths)
			{
				for (int i = 0; i < 16 && paths[i] != -1 && (int)list.size() < maxCount; i++)
				{
					if (paths[i] < 0 || (size_t)paths[i] >= specs.size())
						break;
					const LevelReachSpec& spec = specs[paths[i]];
					UNavigationPoint* end = UObject::TryCast<UNavigationPoint>(spec.endActor);
					if (end && radius <= spec.collisionRadius && height <= spec.collisionHeight && (spec.reachFlags & moveFlags) == spec.reachFlags)
						list.push_back({ end, (float)spec.distance });
				}
			};
		addPaths(startNav->Paths());
		if (usePrunedPaths)
			addPaths(startNav->PrunedPaths());
	}
	else
	{
		// No navigation point at the start. Take the navigation points inside 1000 units that the pawn can go to directly.
		for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		{
			vec3 delta = nav->Location() - startLocation;
			float distSquared = dot(delta, delta);
			if (distSquared >= 1000000.0f)
				continue;

			float dist = std::sqrt(distSquared);
			if ((int)list.size() < maxCount)
			{
				list.push_back({ nav, dist });
			}
			else
			{
				int slot = -1;
				for (int i = 0; i < (int)list.size(); i++)
				{
					if (dist < list[i].Distance && (slot < 0 || list[i].Distance < list[slot].Distance))
						slot = i;
				}
				if (slot >= 0)
					list[slot] = { nav, dist };
			}
		}

		Array<AINavInfo> reachable;
		for (const AINavInfo& info : list)
		{
			if (ActorReachableDeusEx(info.NavPoint, false))
				reachable.push_back(info);
		}
		list = std::move(reachable);
	}

	if (sort && list.size() > 1)
		std::stable_sort(list.begin(), list.end(), [](const AINavInfo& a, const AINavInfo& b) { return a.Distance < b.Distance; });

	return list;
}

void UPawn::AIComputePathnodeDistances(UActor* startActor)
{
	int moveFlags = AICalcMoveFlags();
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();

	ClearPaths();
	for (UNavigationPoint* nav = Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
		nav->visitedWeight() = 10000000;

	// The original goes along the paths with a recursive function. A work list gives the same distances.
	Array<UNavigationPoint*> work;
	for (const AINavInfo& info : AIGetPathnodeList(32, startActor, false, false))
	{
		if (info.Distance < (float)info.NavPoint->visitedWeight())
		{
			info.NavPoint->visitedWeight() = (int)info.Distance;
			work.push_back(info.NavPoint);
		}
	}

	const auto& specs = XLevel()->ReachSpecs;
	while (!work.empty())
	{
		UNavigationPoint* nav = work.back();
		work.pop_back();

		auto paths = nav->Paths();
		for (int i = 0; i < 16 && paths[i] != -1; i++)
		{
			if (paths[i] < 0 || (size_t)paths[i] >= specs.size())
				break;
			const LevelReachSpec& spec = specs[paths[i]];
			UNavigationPoint* end = UObject::TryCast<UNavigationPoint>(spec.endActor);
			if (!end || radius > spec.collisionRadius || height > spec.collisionHeight || (moveFlags & spec.reachFlags) != spec.reachFlags)
				continue;

			int weight = nav->visitedWeight() + spec.distance;
			if (weight < end->visitedWeight())
			{
				end->visitedWeight() = weight;
				work.push_back(end);
			}
		}
	}
}
