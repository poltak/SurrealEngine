
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

void UActor::TickFalling(float elapsed)
{
	if (HasLeftWorld())
		return;

	UZoneInfo* zone = Region().Zone;
	UDecoration* decor = UObject::TryCast<UDecoration>(this);
	UPawn* pawn = UObject::TryCast<UPawn>(this);

	// Deus Ex: a player who falls onto a ladder holds on to it
	if (pawn && TickLadder(pawn, elapsed))
		return;

	// UnrealScript property references
	vec3& acceleration = Acceleration();
	vec3& velocity = Velocity();
	float groundSpeed = 0.0f;

	if (pawn)
	{
		groundSpeed = pawn->GroundSpeed();
		float maxAccel = engine->LaunchInfo.ue1Version > 219 ? pawn->AirControl() * pawn->AccelRate() : 0.0f;

		// As AActor::physFalling of Deus Ex: a pawn that moves slower than 10 units per second in the XY plane can
		// get to that speed in this tick. A player who jumps against a wall then moves onto the ledge at the top of
		// the jump.
		float speedXY = length(velocity.xy());
		if (engine->LaunchInfo.IsDeusEx() && speedXY < 10.0f)
			maxAccel += (10.0f - speedXY) / elapsed;

		float accel = length(acceleration);
		if (accel > maxAccel)
			acceleration = normalize(acceleration) * maxAccel;
	}

	float gravityScale = 2.0f;
	float fluidFriction = 0.0f;

	if (decor && decor->bBobbing())
	{
		gravityScale = 1.0f;
	}
	else if (pawn && pawn->FootRegion().Zone->bWaterZone() && velocity.z < 0.0f)
	{
		fluidFriction = pawn->FootRegion().Zone->ZoneFluidFriction();
	}

	OldLocation() = Location();
	bJustTeleported() = false;

	float fluidFactor = 1.0f - fluidFriction * elapsed;
	vec3 accelVector = acceleration * 1.5f;
	vec3 gravityVector = gravityScale * zone->ZoneGravity();

	// AActor::physFalling of Deus Ex (build 1112fm) moves the actor with the mean velocity of the tick: the old velocity
	// plus half of the change that the acceleration and the gravity give. After the move, the velocity gets the full
	// change (see the end of this function). So the actor follows the true curve of a throw at each frame rate. A jump
	// of the player (JumpZ 300, gravity 950) goes up 47.4 units, which is necessary for the 48 units high platforms
	// at the ladders on the roofs of 02_NYC_Warehouse. A move with the end velocity of the tick goes up only 44.8
	// units at 30 frames per second.
	bool meanVelocityMove = engine->LaunchInfo.IsDeusEx();
	if (meanVelocityMove)
	{
		accelVector = acceleration;
		gravityVector = 0.5f * gravityScale * zone->ZoneGravity();
	}

	vec3 oldVelocity = velocity;
	vec3 newVelocity = oldVelocity * fluidFactor + (accelVector + gravityVector) * 0.5f * elapsed;

	// Limit air control to controlling which direction we are moving in the XY plane, but not increase the speed beyond the ground speed
	vec2 velocity2d = velocity.xy();
	vec2 newVelocity2d = newVelocity.xy();
	float curSpeedSquared = dot(velocity2d, velocity2d);
	if (pawn && curSpeedSquared >= (groundSpeed * groundSpeed) && dot(newVelocity2d, newVelocity2d) > curSpeedSquared)
	{
		float xySpeed = length(velocity2d);
		newVelocity = vec3(normalize(newVelocity2d) * xySpeed, newVelocity.z);
	}
	velocity = newVelocity;

	bool bounced = false;
	bool slid = false;
	float timeLeft = elapsed;
	for (int iteration = 0; timeLeft > 0.0f && iteration < 5; iteration++)
	{
		if (ShouldAbortMovementTick(PHYS_Falling))
			break;

		float zoneTerminalVelocity = zone->ZoneTerminalVelocity();
		if (dot(velocity, velocity) > zoneTerminalVelocity * zoneTerminalVelocity)
		{
			velocity = normalize(velocity) * zoneTerminalVelocity;
			newVelocity = velocity;
		}

		vec3 moveDelta = (newVelocity + zone->ZoneVelocity() * elapsed * 25.0f) * timeLeft;
		vec3 dirNormal = normalize(newVelocity);

		CollisionHit hit = TryMove(moveDelta);
		timeLeft -= timeLeft * hit.Fraction;

		if (hit.Fraction < 1.0f)
		{
			// As AActor::physFalling of the original, Deus Ex gives no HitWall event for the floor that an actor
			// lands on. The actor gets the Landed event only.
			bool landsOnFloor = engine->LaunchInfo.IsDeusEx() && !bBounce() && hit.Normal.z >= 0.7071f;
			if (hit.Actor && hit.Actor->IsA("Pawn"))
			{
				// So projectiles don't think they hit a wall.
			}
			else if (!landsOnFloor)
			{
				FireHitWall(hit);
			}

			// Hit the level
			if (bBounce())
			{
				bounced = true;
				vec3 reflectedDelta = reflect(moveDelta, hit.Normal);
				hit = TryMove(reflectedDelta);
			}
			else
			{
				if (hit.Normal.z < 0.7071f)
				{
					// We hit a slope. Try to follow it.
					vec3 alignedDelta = (moveDelta - hit.Normal * dot(moveDelta, hit.Normal)) * (1.0f - hit.Fraction);
					if (dot(moveDelta, alignedDelta) >= 0.0f) // Don't end up going backwards
					{
						hit = TryMove(alignedDelta);
						if (hit.Fraction < 1.0f && hit.Normal.z > 0.7071f)
						{
							PhysLanded(hit.Actor, hit.Normal);
							return;
						}
						// A second wall
						if (hit.Fraction < 1.0f && engine->LaunchInfo.IsDeusEx())
							FireHitWall(hit);
					}

					// adjust velocity along the slope
					if (!bBounce())
						RecomputeVelocityFromDisplacement(elapsed);

					slid = true;
					timeLeft = 0.0f;
				}
				else
				{
					PhysLanded(hit.Actor, hit.Normal);
					timeLeft = 0.0f;
				}
			}
		}
	}

	// The velocity at the end of the tick, as AActor::physFalling of Deus Ex: two times the mean velocity of the move,
	// less the old velocity. After a slide along a wall, the horizontal velocity stays the one of the slide. The
	// original keeps the mean velocity when the actor goes down more slowly at the end (water friction).
	if (meanVelocityMove && Physics() == PHYS_Falling && !bJustTeleported() && !bounced)
	{
		if (velocity.z < oldVelocity.z || oldVelocity.z >= 0.0f)
		{
			if (slid)
				velocity.z = 2.0f * velocity.z - oldVelocity.z;
			else
				velocity = 2.0f * velocity - oldVelocity;
		}
	}
}
