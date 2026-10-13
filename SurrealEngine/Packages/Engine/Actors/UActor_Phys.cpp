
#include "Precomp.h"
#include "UActor.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Actors/UProjectile.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"

void UActor::TickPhysics(float elapsed)
{
	for (float timeLeft = elapsed; timeLeft > 0.0f && !bDeleteMe(); timeLeft -= 0.02f)
	{
		float physTimeElapsed = std::min(timeLeft, 0.02f);
		int mode = Physics();
		if (mode != PHYS_None)
		{
			switch (mode)
			{
			case PHYS_Walking: TickWalking(physTimeElapsed); break;
			case PHYS_Falling: TickFalling(physTimeElapsed); break;
			case PHYS_Swimming: TickSwimming(physTimeElapsed); break;
			case PHYS_Flying: TickFlying(physTimeElapsed); break;
			case PHYS_Rotating: break;
			case PHYS_Projectile: TickProjectile(physTimeElapsed); break;
			case PHYS_Rolling: TickRolling(physTimeElapsed); break;
			case PHYS_Interpolating: TickInterpolating(physTimeElapsed); break;
			case PHYS_MovingBrush: TickMovingBrush(physTimeElapsed); break;
			case PHYS_Spider: TickSpider(physTimeElapsed); break;
			case PHYS_Trailer: TickTrailer(physTimeElapsed); break;
			}
			TickRotating(physTimeElapsed); // Rotation logic applies to multiple physics modes and not just PHYS_Rotating
		}

		CheckPendingTouch();
	}
}

void UActor::SetPhysics(uint8_t newPhysics, UActor* newFloor)
{
	if (!engine->LaunchInfo.IsDeusEx())
	{
		Physics() = newPhysics;
		return;
	}

	// As AActor::setPhysics in the Engine.dll of Deus Ex (build 1112fm): a change of the physics mode also sets the
	// base. An actor that walks or rests gets the floor as its base, and each other mode has no base.
	if (Physics() == newPhysics)
		return;

	Physics() = newPhysics;
	if (newPhysics == PHYS_Walking || newPhysics == PHYS_None || newPhysics == PHYS_Rolling || newPhysics == PHYS_Rotating || newPhysics == PHYS_Spider)
	{
		if (!newFloor)
			FindBase();
		else if (ActorBase() != newFloor)
			SetSupportBase(newFloor);
	}
	else if (ActorBase())
	{
		SetBase(nullptr, true);
	}

	// A script event of the lines above can change the mode again
	if (Physics() == PHYS_None || Physics() == PHYS_Rotating)
	{
		Velocity() = vec3(0.0f);
		Acceleration() = vec3(0.0f);
	}
}

// As AActor::SetSupportBase in the Engine.dll of Deus Ex: the actor below decides what happens with an actor that
// comes to stand on it. The SupportActor event of the Actor script makes it the base. The scripts of pawns,
// decorations and movers also give damage for a hard landing, and some of them push the actor off.
void UActor::SetSupportBase(UActor* newBase)
{
	if (newBase)
		CallEvent(newBase, "SupportActor", { ExpressionValue::ObjectValue(this) });
	else
		SetBase(nullptr, true);
}

// As AActor::FindBase in the Engine.dll of Deus Ex: the actor or the level geometry that is not more than 8 units
// below this actor becomes its base
void UActor::FindBase()
{
	UActor* newBase = nullptr;
	if (ULevel* level = XLevel())
	{
		bool useBlockPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
		for (const CollisionHit& hit : level->Collision.Trace(Location(), Location() - vec3(0.0f, 0.0f, 8.0f), CollisionHeight(), CollisionRadius(), true, true, false))
		{
			if (!hit.Actor)
			{
				newBase = Level();
				break;
			}
			if (hit.Actor == this)
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
				isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = hit.Actor->bBlockActors() && bBlockActors();
			if (isBlocking)
			{
				newBase = hit.Actor;
				break;
			}
		}
	}

	// An actor that stands on this actor cannot be its base. IsBasedOn(other) of this engine is true when the other
	// actor stands on this one, which is the reverse of AActor::IsBasedOn in the original.
	if (ActorBase() != newBase && (!newBase || !IsBasedOn(newBase)))
		SetSupportBase(newBase);
}

void UActor::SetCollision(bool newColActors, bool newBlockActors, bool newBlockPlayers)
{
	XLevel()->Collision.RemoveFromCollision(this);
	bCollideActors() = newColActors;
	bBlockActors() = newBlockActors;
	bBlockPlayers() = newBlockPlayers;
	XLevel()->Collision.AddToCollision(this);
}

bool UActor::SetLocation(const vec3& newLocation)
{
	auto result = CheckLocation(newLocation, CollisionRadius(), CollisionHeight(), bCollideWorld() || bCollideWhenPlacing());
	if (!result.first)
		return false;

	XLevel()->Collision.RemoveFromCollision(this);
	Location() = result.second;
	XLevel()->Collision.AddToCollision(this);

	if (Level()->bBegunPlay())
	{
		// Send touch notifications for anything at the new location
		for (UActor* actor : XLevel()->Collision.CollidingActors(Location(), CollisionHeight(), CollisionRadius()))
		{
			if (actor != this && !actor->IsBasedOn(this) && !IsBasedOn(actor) && bCollideActors() && actor->bCollideActors())
			{
				Touch(actor);
			}
		}

		// Untouch everything we aren't overlapping anymore
		if (engine->LaunchInfo.IsUnrealTournament_469())
		{
			for (const auto actor : Touching_UT469())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
		else
		{
			for (const auto actor : Touching())
			{
				if (actor && !IsOverlapping(actor))
					UnTouch(actor);
			}
		}
	}

	return true;
}

bool UActor::SetRotation(const Rotator& newRotation)
{
	// To do: return false if there isn't room

	Rotator delta = newRotation - Rotation();
	Rotation() = newRotation;
	TurnBasedActors(delta);
	return true;
}

// carried items and actors on movers should rotate with the actor their based on
void UActor::TurnBasedActors(const Rotator& deltaRotation)
{
	if ((deltaRotation.Yaw & 0xffff) == 0)
		return;
	Coords yawRot = Coords::YawRotation(deltaRotation.YawRadians());
	vec3 baseLoc = Location();
	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		if (!basedActor) { i++; continue; }
		vec3 basedLoc = basedActor->Location();
		vec3 rotatedOffset = yawRot * (basedLoc - baseLoc);
		basedActor->TryMove((baseLoc + rotatedOffset) - basedLoc, false, false);
		basedActor->SetRotation(basedActor->Rotation() + deltaRotation);
		if (UPawn* pawn = UObject::TryCast<UPawn>(basedActor))
			pawn->ViewRotation().Yaw += deltaRotation.Yaw;
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}
}

bool UActor::SetCollisionSize(float newRadius, float newHeight)
{
	// To do: return false if there isn't room

	XLevel()->Collision.RemoveFromCollision(this);
	CollisionRadius() = newRadius;
	CollisionHeight() = newHeight;
	XLevel()->Collision.AddToCollision(this);
	return true;
}

// The base of an actor that stands on level geometry. In Deus Ex this is the level info, as in Unreal: a line check
// of the original gives the level info as the actor of a hit on level geometry, and the scripts of the game use
// "Base != Level" with no test for None (DeusExPlayer.DoJump).
UActor* UActor::FloorBase(UActor* hitActor)
{
	if (!hitActor && engine->LaunchInfo.IsDeusEx())
		return Level();
	return hitActor;
}

void UActor::PhysLanded(UActor* hitActor, const vec3& hitNormal)
{
	// landed on the floor
	CallEvent(this, EventName::Landed, { ExpressionValue::VectorValue(hitNormal) });

	if (Physics() == PHYS_Falling) // Landed event might have changed the physics mode
	{
		if (engine->LaunchInfo.IsDeusEx())
		{
			// As AActor::processLanded: the new physics mode comes with the floor, and the floor gets the SupportActor event
			SetPhysics(UObject::TryCast<UPawn>(this) ? PHYS_Walking : PHYS_None, FloorBase(hitActor));
		}
		else if (UObject::TryCast<UPawn>(this))
		{
			SetPhysics(PHYS_Walking);
			SetBase(FloorBase(hitActor), true);
		}
		else
		{
			SetPhysics(PHYS_None);
			SetBase(FloorBase(hitActor), true);
			Velocity() = vec3(0.0f);
		}
	}
}

bool UActor::HasLeftWorld()
{
	if (Region().ZoneNumber != 0)
		return false;

	CallEvent(this, EventName::FellOutOfWorld);
	return true;
}

void UActor::ApplyMovementAcceleration(float elapsed, float accelRate, float friction, float maxSpeed)
{
	if (dot(Acceleration(), Acceleration()) > 0.0001f)
	{
		// Acceleration must never exceed the acceleration rate
		float accelSpeed = length(Acceleration());
		vec3 accelDir = Acceleration() * (1.0f / accelSpeed);
		if (accelSpeed > accelRate)
			Acceleration() = accelDir * accelRate;

		float speed = length(Velocity());
		Velocity() = Velocity() - (Velocity() - accelDir * speed) * (friction * elapsed);
	}
	else
	{
		float speed = length(Velocity());
		if (speed > 0.0f)
		{
			float newSpeed = std::max(speed - speed * friction * 2.0f * elapsed, 0.0f);
			Velocity() = Velocity() * (newSpeed / speed);
		}
	}

	Velocity() = Velocity() + Acceleration() * elapsed;

	float speed = length(Velocity());
	if (speed > 0.0f && speed > maxSpeed)
		Velocity() = Velocity() * (maxSpeed / speed);
}

bool UActor::ShouldAbortMovementTick(uint8_t expectedPhysics)
{
	return bDeleteMe() || Physics() != expectedPhysics;
}

UPawn* UActor::PreparePawnMovementTick()
{
	UPawn* pawn = UObject::TryCast<UPawn>(this);
	if (!pawn)
		return nullptr;

	if (HasLeftWorld())
		return nullptr;

	OldLocation() = Location();
	bJustTeleported() = false;

	return pawn;
}

// For Deus Ex this function is AActor::processHitWall of Engine.dll (build 1112fm), which has a filter before the
// script event HitWall:
// - A contact with a pawn gives no event.
// - A pawn gets no event when it has no acceleration.
// - A pawn gets no event when the wall does not face against its way. The dot product of the direction to its
//   destination and the wall normal must be MinHitWall at most. The persons of Deus Ex have a very large MinHitWall,
//   so each wall counts for them. The player has -0.6, and its destination is the origin of the map.
// - For a pawn that walks, the direction and the normal have no vertical part. The event gets that normal.
// - A pawn that has no HitWall function in its state, and that does not fall, ends its move: MoveTimer becomes -1
//   and bFromWall becomes true.
// An actor that is not a pawn gets the event when it has a HitWall function in its state.
void UActor::FireHitWall(const CollisionHit& hit)
{
	UActor* hitActor = hit.Actor ? hit.Actor : static_cast<UActor*>(Level());
	vec3 hitNormal = hit.Normal;

	if (engine->LaunchInfo.IsDeusEx())
	{
		UPawn* pawn = UObject::TryCast<UPawn>(this);
		// --tracepath=<name> of the test harness: a log line for a wall contact of the pawn with that name. A pawn that
		// pushes against a wall has some contacts in each frame, so the same line comes one time in a second at most.
		auto trace = [&](const std::string& result)
			{
				if (!pawn || !pawn->AITracePath())
					return;
				char buf[100];
				snprintf(buf, sizeof(buf), " normal=(%.2f %.2f %.2f) ", hit.Normal.x, hit.Normal.y, hit.Normal.z);
				std::string text = "Harness: hitwall " + Name.ToString() + " actor=" + hitActor->Name.ToString() + buf + result;
				static std::string lastText;
				static float lastTime = 0.0f;
				float time = Level()->TimeSeconds();
				if (text == lastText && time >= lastTime && time - lastTime < 1.0f)
					return;
				lastText = text;
				lastTime = time;
				LogMessage(text);
			};

		if (UObject::TryCast<UPawn>(hitActor))
		{
			trace("no event, the actor is a pawn");
			return;
		}

		if (pawn)
		{
			if (Acceleration() == vec3(0.0f))
			{
				trace("no event, no acceleration");
				return;
			}

			// FVector::SafeNormal
			vec3 dir = pawn->Destination() - Location();
			float sizeSquared = dot(dir, dir);
			dir = sizeSquared < 0.00000001f ? vec3(0.0f) : dir * (1.0f / std::sqrt(sizeSquared));
			if (Physics() == PHYS_Walking)
			{
				hitNormal.z = 0.0f;
				dir.z = 0.0f;
			}
			if (pawn->MinHitWall() < dot(dir, hitNormal))
			{
				trace("no event, the wall does not face against the way");
				return;
			}

			if (!IsEventEnabled(EventName::HitWall) && Physics() != PHYS_Falling)
			{
				pawn->MoveTimer() = -1.0f;
				pawn->bFromWall() = true;
				trace("no event, the move ends");
				return;
			}
		}

		trace("event");
	}

	CallEvent(this, EventName::HitWall, { ExpressionValue::VectorValue(hitNormal), ExpressionValue::ObjectValue(hitActor) });
}

bool UActor::TryStepToGround(vec3 stepDownDelta)
{
	CollisionHit floorHit = TryMove(stepDownDelta, true);

	//check if floo was reached, or if we would be falling now
	if (floorHit.Fraction == 1.0f || floorHit.Normal.z < 0.7071f)
	{
		//if we would be falling, move back up
		return false;
	}

	// We could reach the ground. Step down there.
	floorHit = TryMove(stepDownDelta);
	if (floorHit.Fraction != 1.0f)
	{
		UActor* floor = FloorBase(floorHit.Actor);
		if (!engine->LaunchInfo.IsDeusEx())
			SetBase(floor, true);
		else if (ActorBase() != floor)
			SetSupportBase(floor); // As APawn::physWalking of Deus Ex: a new floor gets the SupportActor event
	}
	return true;
}

void UActor::RecomputeVelocityFromDisplacement(float elapsed)
{
	if (!bJustTeleported())
		Velocity() = (Location() - OldLocation()) / elapsed;
}

// Finds a place for the collision box of an actor at the location or near it.
// This is ULevel::FindSpot and ULevel::AdjustSpot of Engine.dll (build 1112fm) with no check for other actors.
// The search moves the box out of the walls that it cuts: first along the three axes, then along the eight diagonals.
// It gives up when the box moved more than about 1.2 times its diagonal.
//
// assumeFit: use the location with no search when the box fits there. ULevel::SpawnActor does this.
// ULevel::FarMoveActor (SetLocation and the reach tests of a pawn) does the search along the axes first.
std::pair<bool, vec3> UActor::CheckLocation(vec3 location, float radius, float height, bool check, bool assumeFit)
{
	if (!check)
		return { true, location };

	// ULevel::SinglePointCheck with the level geometry only
	auto fits = [&](const vec3& point)
	{
		return XLevel()->Collision.OverlapTest(point, height, radius, false, true, false).empty();
	};

	vec3 extent(radius, radius, height);
	if (extent == vec3(0.0f))
		return { fits(location), location };

	if (assumeFit && fits(location))
		return { true, location };

	// ULevel::AdjustSpot: a line from the center of the box to a point on its side. If the line hits the level or a mover,
	// move the box away from the surface by the part of the line that is behind it.
	vec3 adjusted = location;
	TraceFlags flags;
	flags.world = true;
	flags.movers = true;
	auto adjustSpot = [&](const vec3& traceDest, float traceLen)
	{
		CollisionHit hit = XLevel()->Collision.TraceFirstHit(adjusted, traceDest, nullptr, vec3(0.0f), flags);
		if (hit.Fraction < 1.0f)
			adjusted += hit.Normal * ((1.05f - hit.Fraction) * traceLen);
	};

	for (int i = -1; i < 2; i += 2)
	{
		adjustSpot(adjusted + vec3(i * extent.x, 0.0f, 0.0f), extent.x);
		adjustSpot(adjusted + vec3(0.0f, i * extent.y, 0.0f), extent.y);
		adjustSpot(adjusted + vec3(0.0f, 0.0f, i * extent.z), extent.z);
	}
	if (fits(adjusted))
		return { true, adjusted };

	float diagonal = length(extent) + 2.0f;
	for (int i = -1; i < 2; i += 2)
	{
		for (int j = -1; j < 2; j += 2)
		{
			for (int k = -1; k < 2; k += 2)
			{
				adjustSpot(adjusted + vec3(i * extent.x, j * extent.y, k * extent.z), diagonal);
			}
		}
	}

	vec3 moved = adjusted - location;
	if (dot(moved, moved) > 1.5f * dot(extent, extent))
		return { false, location };

	if (fits(adjusted))
		return { true, adjusted };

	return { false, location };
}

bool UActor::IsOverlapping(UActor* other)
{
	return XLevel()->Collision.IsOverlapping(this, other);
}

BoundingBox UActor::GetWorldCollisionBox(bool bVisual)
{
	LogUnimplemented("Actor.GetWorldCollisionBox");
	return {};
}

// The move after a second wall, as AActor::TwoWallAdjust of the original engine
void UActor::TwoWallAdjust(const vec3& desiredDir, vec3& delta, const vec3& hitNormal, const vec3& oldHitNormal, float hitTime)
{
	if (dot(hitNormal, oldHitNormal) > 0.0f)
	{
		// Slide along the new wall
		delta = (delta - hitNormal * dot(delta, hitNormal)) * (1.0f - hitTime);
		if (dot(desiredDir, delta) <= 0.0f)
			delta = vec3(0.0f);
	}
	else
	{
		// A corner of 90 degrees or less. Move along the line where the two walls meet.
		vec3 newDir = cross(hitNormal, oldHitNormal);
		float len = length(newDir);
		newDir = (len > 0.00001f) ? newDir / len : vec3(0.0f);
		delta = newDir * ((1.0f - hitTime) * dot(newDir, delta));
		if (dot(desiredDir, delta) < 0.0f)
			delta = -delta;
	}
}

// Deus Ex: where does an object land that flies from a place with a start velocity, falls with the gravity of the
// zone and bounces from walls? The AI uses this for grenades. This does the same as AActor::ParabolicTrace of the
// original Engine.dll: it moves a cylinder in time steps of "granularity" seconds. The result is the flight time.
// The result is 0, with the start place as final place, if the object does not come to rest in maxTime, leaves the
// level or gets into water.
float UActor::ParabolicTrace(vec3& finalLocation, const vec3& startLocation, const vec3& startVelocity, float maxTime, float elasticity, bool bounce, float landingSpeed, const vec3& cylinder, bool checkActors, float granularity)
{
	granularity = clamp(granularity, 0.005f, 1.0f);

	ULevel* level = XLevel();
	UZoneInfo* levelZone = Level();
	vec3 location = startLocation;
	vec3 velocity = startVelocity;
	PointRegion region = level->Model->FindRegion(location, levelZone);

	float hitTime = 1.0f;
	vec3 hitNormal(0.0f);

	// Move the place by delta until the level or a blocking actor stops it. The trace goes 2 units further than
	// the move, and the place stops 2 units before the hit, as the actor move of the original does it.
	auto move = [&](const vec3& delta)
	{
		if (std::abs(delta.x) < 0.0001f && std::abs(delta.y) < 0.0001f && std::abs(delta.z) < 0.0001f)
			return;

		hitTime = 1.0f;
		hitNormal = vec3(0.0f);

		const float extra = 2.0f;
		float len = length(delta);
		vec3 dir = delta / len;
		for (const CollisionHit& hit : level->Collision.Trace(location, location + delta + dir * extra, cylinder.z, cylinder.x, checkActors, true, false))
		{
			// The level blocks, and each actor that blocks actors and does not stand on this actor
			if (hit.Actor && (IsBasedOn(hit.Actor) || !hit.Actor->bBlockActors()))
				continue;
			hitTime = hit.Fraction;
			hitNormal = hit.Normal;
			break;
		}

		vec3 moved = delta;
		if (hitTime < 1.0f)
		{
			float hitDistance = (extra + len) * hitTime;
			if (hitDistance > extra)
			{
				moved = (delta + dir * extra) * hitTime - dir * extra;
				hitTime = (hitDistance - extra) / len;
			}
			else
			{
				moved = vec3(0.0f);
				hitTime = 0.0f;
			}
		}

		location += moved;
		region = level->Model->FindRegion(location, levelZone);
	};
	auto zone = [&]() { return region.Zone ? region.Zone : levelZone; };
	auto landed = [&](const vec3& speed) { return hitNormal.z > 0.7f && (!bounce || dot(speed, speed) < landingSpeed * landingSpeed); };
	auto reflect = [&]() { velocity = (velocity - hitNormal * (2.0f * dot(velocity, hitNormal))) * elasticity; };

	bool done = false;
	int steps = 0;
	while (!done)
	{
		bool hitInStep = false;

		velocity += zone()->ZoneGravity() * granularity;
		vec3 delta = (velocity + zone()->ZoneVelocity()) * granularity;
		move(delta);

		if (zone()->bWaterZone())
		{
			steps = 0;
			location = startLocation;
			done = true;
		}
		else if (hitTime < 1.0f)
		{
			hitInStep = true;
			if (landed(velocity))
			{
				done = true;
			}
			else
			{
				// Bounce, and use the rest of the time step to slide along the wall
				reflect();
				vec3 oldHitNormal = hitNormal;
				vec3 slide = (delta - hitNormal * dot(delta, hitNormal)) * (1.0f - hitTime);
				if (dot(slide, delta) >= 0.0f)
				{
					move(slide);
					if (hitTime < 1.0f)
					{
						reflect();
						if (landed(velocity))
							done = true;

						float len = length(delta);
						vec3 desiredDir = (len > 0.00001f) ? delta / len : vec3(0.0f);
						TwoWallAdjust(desiredDir, slide, hitNormal, oldHitNormal, hitTime);
						move(slide);
						if (!done && hitTime < 1.0f && landed(slide))
							done = true;
					}
				}
			}
		}

		if (!bounce || !hitInStep)
		{
			float terminalVelocity = zone()->ZoneTerminalVelocity();
			if (dot(velocity, velocity) > terminalVelocity * terminalVelocity)
				velocity = normalize(velocity) * terminalVelocity;
		}

		steps++;
		if (region.ZoneNumber == 0 || steps * granularity > maxTime || dot(delta, delta) > 2500000.0f)
		{
			// The object left the level, is in the air for too long, or is too fast
			steps = 0;
			location = startLocation;
			done = true;
		}
	}

	finalLocation = location;
	return steps * granularity;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

UObject* UActor::Trace(vec3& hitLocation, vec3& hitNormal, const vec3& traceEnd, const vec3& traceStart, bool bTraceActors, const vec3& extent, bool bTraceBSP, uint8_t BSPTraceFlags)
{
	LogUnimplemented("Actor.Trace() [U227 - BSPTraceFlags parameter isn't implemented");
	TraceFlags flags;
	flags.movers = true;
	flags.world = bTraceBSP;
	if (bTraceActors)
	{
		flags.pawns = true;
		flags.others = true;
		flags.onlyProjectiles = true;
	}

	// hack?
	if (IsA("ChallengeHUD"))
	{
		flags.zoneChanges = true;
	}

	CollisionHit hit = XLevel()->Collision.TraceFirstHit(traceStart, traceEnd, this, extent, flags);
	hitNormal = hit.Normal;
	hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
	return hit.Actor;
}

bool UActor::FastTrace(const vec3& traceEnd, const vec3& traceStart)
{
	return !XLevel()->Collision.TraceAnyHit(traceStart, traceEnd, this, false, true, false);
}

bool UActor::TraceSurfHitInfo(vec3& Start, vec3& End, vec3* HitLocation, vec3* HitNormal, UTexture* HitTex, int* HitFlags)
{
	const TraceFlags flags = {
		.movers = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(Start, End, this, vec3(), flags);

	if (!hit.Node)
		return false;

	if (HitLocation)
		*HitLocation = Start + (End - Start) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	if (HitTex)
		HitTex = XLevel()->Model->Surfaces[hit.Node->Surf].Material;

	if (HitFlags)
		*HitFlags = hit.Node->NodeFlags;

	return true;
}

bool UActor::TraceThisActor(vec3& TraceEnd, vec3 TraceStart, vec3* HitLocation, vec3* HitNormal, std::optional<vec3> Extent)
{
	TraceFlags flags{
		.pawns = true,
		.movers = true,
		.others = true,
		.world = true
	};

	const auto hit = XLevel()->Collision.TraceFirstHit(TraceStart, TraceEnd, this, Extent ? *Extent : vec3(), flags);

	if (!hit.Node && !hit.Actor)
		return false;

	if (HitLocation)
		*HitLocation = TraceStart + (TraceEnd - TraceStart) * hit.Fraction;

	if (HitNormal)
		*HitNormal = hit.Normal;

	return true;
}

CollisionHit UActor::TryMove(const vec3& delta, bool dryRun, bool isOwnBaseBlocking)
{
	// Static and non-movable objects can't move
	if (bStatic() || !bMovable())
	{
		CollisionHit hit;
		hit.Fraction = 0.0f;
		return hit;
	}

	// Avoid moving if movement is too small as the physics code doesn't like very small numbers
	if (dot(delta, delta) < 0.00000001f)
		return {};

	// A move that is not a number must not go into the collision code: a trace with NaN goes through the full level
	if (!std::isfinite(delta.x + delta.y + delta.z))
		return {};

	// Analyze what we will hit if we move as requested and stop if it is the level or a blocking actor
	bool useBlockPlayers = UObject::TryCast<UPlayerPawn>(this) || UObject::TryCast<UProjectile>(this);
	CollisionHit blockingHit;
	CollisionHitList hits;
	if (!Brush())
	{
		hits = XLevel()->Collision.Trace(Location(), Location() + delta, CollisionHeight(), CollisionRadius(), bCollideActors(), bCollideWorld(), false);
		if (bCollideWorld() || bBlockActors() || bBlockPlayers())
		{
			for (auto& hit : hits)
			{
				if (hit.Actor)
				{
					bool isBlocking;
					if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
						isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
					else
						isBlocking = hit.Actor->bBlockActors() && bBlockActors();

					// We never hit ourselves or anything moving along with us
					if (isBlocking && (isOwnBaseBlocking || !hit.Actor->IsBasedOn(this)) && !IsBasedOn(hit.Actor))
					{
						blockingHit = hit;
						break;
					}
				}
				else
				{
					blockingHit = hit;
					break;
				}
			}
		}
	}

	if (dryRun)
		return blockingHit;

	vec3 actuallyMoved = delta * blockingHit.Fraction;
	vec3 OldLocation = Location();

	XLevel()->Collision.RemoveFromCollision(this);
	Location() += actuallyMoved;
	XLevel()->Collision.AddToCollision(this);

	for (size_t i = 0; i < BasedActors.size(); )
	{
		UActor* basedActor = BasedActors[i];
		basedActor->TryMove(actuallyMoved, false, false);
		// UnrealScript events triggered in TryMove can call methods such as SetBase or Destroy, so need to guard while iterating.
		if (i < BasedActors.size() && BasedActors[i] == basedActor)
			i++;
	}

	// Notify actor of encroachment
	if (Brush() && (bBlockPlayers() || bBlockActors() || bCollideActors()))
	{
		Array<UActor*> encroachingActors = XLevel()->Collision.EncroachingActors(this);
		for (UActor* actor : encroachingActors)
		{
			if (actor == this || actor->Brush())
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
			{
				bool stopMovement = CallEvent(this, EventName::EncroachingOn, { ExpressionValue::ObjectValue(actor) }).ToBool();
				if (stopMovement)
				{
					XLevel()->Collision.RemoveFromCollision(this);
					Location() = OldLocation;
					XLevel()->Collision.AddToCollision(this);

					CollisionHit hit;
					hit.Fraction = 0.0f;
					return hit;
				}
			}
		}

		for (UActor* actor : encroachingActors)
		{
			if (actor == this)
				continue;

			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(actor) || UObject::TryCast<UProjectile>(actor))
				isBlocking = actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = actor->bBlockActors() && bBlockActors();

			if (isBlocking)
				CallEvent(actor, EventName::EncroachedBy, { ExpressionValue::ObjectValue(this) }).ToBool();
		}
	}

	// Send bump notification if we hit an actor
	if (blockingHit.Actor)
	{
		if (!blockingHit.Actor->IsBasedOn(this))
		{
			CallEvent(blockingHit.Actor, EventName::Bump, { ExpressionValue::ObjectValue(this) });
			CallEvent(this, EventName::Bump, { ExpressionValue::ObjectValue(blockingHit.Actor) });
		}
	}

	// Send touch notifications for anything we crossed while moving
	for (auto& hit : hits)
	{
		if (hit.Fraction >= blockingHit.Fraction)
			break;

		if (hit.Actor && !hit.Actor->IsBasedOn(this) && !IsBasedOn(hit.Actor) && bCollideActors() && hit.Actor->bCollideActors())
		{
			// We can't touch stuff we are blocked by
			bool isBlocking;
			if (useBlockPlayers || UObject::TryCast<UPlayerPawn>(hit.Actor) || UObject::TryCast<UProjectile>(hit.Actor))
				isBlocking = hit.Actor->bBlockPlayers() && bBlockPlayers();
			else
				isBlocking = hit.Actor->bBlockActors() && bBlockActors();
			if (!isBlocking)
				Touch(hit.Actor);
		}
	}

	// Untouch everything we aren't overlapping anymore
	if (engine->LaunchInfo.IsUnrealTournament_469())
	{
		for (const auto actor : Touching_UT469())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}
	else
	{
		for (const auto actor : Touching())
			if (actor && !IsOverlapping(actor))
				UnTouch(actor);
	}

	UpdateActorZone();

	return blockingHit;
}

CollisionHit UActor::TryMoveSmooth(const vec3& delta)
{
	CollisionHit hit = TryMove(delta);
	if (hit.Fraction != 1.0f)
	{
		// We hit a slope. Try to follow it.
		vec3 alignedDelta = (delta - hit.Normal * dot(delta, hit.Normal)) * (1.0f - hit.Fraction);
		if (dot(delta, alignedDelta) >= 0.0f) // Don't end up going backwards
		{
			CollisionHit hit2 = TryMove(alignedDelta);
			return hit2; // XXX: does this break anything?
		}
	}

	return hit;
}
