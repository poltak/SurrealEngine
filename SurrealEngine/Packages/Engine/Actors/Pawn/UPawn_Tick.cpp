
#include "Precomp.h"
#include "UPawn.h"
#include "Packages/Core/UClass.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include "Utils/Logger.h"
#include "Engine.h"
#include "Utils/Random.h"

void UPawn::Tick(float elapsed)
{
	// Deus Ex changes the move timer after the physics. See TickPhysics.
	bool deusEx = engine->LaunchInfo.IsDeusEx();
	if (!deusEx)
		MoveTimer() -= elapsed;

	if (StateFrame)
	{
		if (deusEx && PollMoveDeusEx())
		{
		}
		else if (StateFrame->LatentState == LatentRunState::MoveTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination()))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::MoveToward)
		{
			if (MoveTarget())
			{
				Focus() = MoveTarget()->Location();
				TickRotateTo(Focus());
				if (TickMoveTo(MoveTarget()->Location()))
					StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeTo)
		{
			TickRotateTo(Focus());
			if (TickMoveTo(Destination()))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::StrafeFacing)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				Focus() = FaceTarget()->Location();
				TickRotateTo(Focus());
				vec3 oldDest = Destination();
				if (TickMoveTo(Destination()))
					StateFrame->LatentState = LatentRunState::Continue;
				Destination() = oldDest;
			}
			else if (engine->LaunchInfo.ue1Version <= 219)
			{
				// How did StrafeFacing work in this version? We don't have FaceTarget.
				LogUnimplemented("StrafeFacing not implemented for old versions");
				StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::TurnTo)
		{
			if (TickRotateTo(Focus()))
				StateFrame->LatentState = LatentRunState::Continue;
		}
		else if (StateFrame->LatentState == LatentRunState::TurnToward)
		{
			if (engine->LaunchInfo.ue1Version > 219 && FaceTarget())
			{
				Focus() = FaceTarget()->Location();
				if (TickRotateTo(FaceTarget()->Location()))
					StateFrame->LatentState = LatentRunState::Continue;
			}
			else if (engine->LaunchInfo.ue1Version <= 219)
			{
				// How did StrafeFacing work in this version? We don't have FaceTarget.
				LogUnimplemented("TurnToward not implemented for old versions");
				StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
		}
		else if (StateFrame->LatentState == LatentRunState::WaitForLanding)
		{
			if (Physics() != PHYS_Falling)
			{
				StateFrame->LatentState = LatentRunState::Continue;
			}
			else
			{
				// To do: need to send a LongFall event if the fall state lasts long enough
			}
		}
	}

	UActor::Tick(elapsed);

	if (bIsPlayer() && Role() >= ROLE_AutonomousProxy)
	{
		if (engine->LaunchInfo.ue1Version < 400 || bViewTarget())
			CallEvent(this, EventName::UpdateEyeHeight, { ExpressionValue::FloatValue(elapsed) });
		else
			ViewRotation() = Rotation();
	}

	if (Weapon())
	{
		Weapon()->Location() = Location();
		Weapon()->UpdateActorZone();
	}

	if (Role() == ROLE_Authority)
	{
		if (PainTime() > 0.0f)
		{
			PainTime() = std::max(PainTime() - elapsed, 0.0f);
			if (PainTime() == 0.0f)
				CallEvent(this, EventName::PainTimer);
		}
		if (SpeechTime() > 0.0f)
		{
			SpeechTime() = std::max(SpeechTime() - elapsed, 0.0f);
			if (SpeechTime() == 0.0f)
				CallEvent(this, EventName::SpeechTimer);
		}
		if (engine->LaunchInfo.ue1Version >= 436 && bAdvancedTactics())
			CallEvent(this, EventName::UpdateTactics, { ExpressionValue::FloatValue(elapsed) });


		SightCounter() -= elapsed;
		if (SightCounter() <= 0.0f)
		{
			//apply random offest to pawns sight counter so not all
			//pawns get evaluated at the same frame 
			//numbers are arbitrarily picked -> if it causes perf. problems, 
			//maybe switch to using some limit of how many pawns can be evaluated 
			//every frame
			SightCounter() += 0.4f - 0.2f * FRand();

			//search for target, if the pawn has an event to see them
			if (IsEventEnabled(EventName::SeePlayer))
			{
				//search every pawn in the map, if they are visible fire the related event
				for (UPawn* other = Level()->PawnList(); other; other = other->nextPawn())
				{
					//skip self, dead pawns and the currenty chosen enemy (handeld by the if(Enemy()) branch)
					if (other == this || other == Enemy() || other->Health() <= 0)
						continue;
					//if the enemy pawn is visible, notify the Unreal scrip about it
					if (CanSee(other))
						CallEvent(this, EventName::SeePlayer, { ExpressionValue::ObjectValue(other) });
				}
			}

			//if the pawn already has an enemy, update its tracking data
			if (Enemy())
			{
				if (CanSee(Enemy()))
				{
					LastSeenPos() = Enemy()->Location();
					LastSeeingPos() = Location();
					if (engine->LaunchInfo.ue1Version > 219)
						LastSeenTime() = Level()->TimeSeconds();
				}
				else if (IsEventEnabled(EventName::EnemyNotVisible))
				{
					CallEvent(this, EventName::EnemyNotVisible);
				}
			}
		}
	}
}

// As APawn::performPhysics in the Engine.dll of Deus Ex (build 1112fm): after the move of a tick, the move timer
// goes down and the pawn updates the mean time of a physics step. APawn::moveToward uses the two values.
void UPawn::TickPhysics(float elapsed)
{
	bool hasPhysics = Physics() != PHYS_None;
	UActor::TickPhysics(elapsed);
	if (hasPhysics && engine->LaunchInfo.IsDeusEx())
	{
		MoveTimer() -= elapsed;
		AvgPhysicsTime() = 0.8f * AvgPhysicsTime() + 0.2f * elapsed;
	}
}

void UPawn::TickRotating(float elapsed)
{
	if (Physics() == PHYS_Spider)
		return;

	bRotateToDesired() = true;
	bFixedRotationDir() = false;

	if (Rotation() == DesiredRotation())
		return;

	Rotator rot = Rotation();

	if ((DesiredRotation().Yaw & 0xffff) != (rot.Yaw & 0xffff))
	{
		rot.Yaw = Rotator::TurnToShortest(rot.Yaw, DesiredRotation().Yaw, (int)std::abs(RotationRate().Yaw * elapsed));
	}

	if ((DesiredRotation().Pitch & 0xffff) != (rot.Pitch & 0xffff))
	{
		rot.Pitch = DesiredRotation().Pitch & 0xffff;
		if (rot.Pitch < 0x8000)
		{
			rot.Pitch = std::max(rot.Pitch, RotationRate().Pitch);
		}
		else if (rot.Pitch < 0x10000 - RotationRate().Pitch)
		{
			rot.Pitch = 0x10000 - RotationRate().Pitch;
		}
	}

	// To do: apply RotationRate().Roll

	Rotation() = rot;

	if (Rotation() == DesiredRotation())
	{
		CallEvent(this, EventName::EndedRotation);
	}
}

bool UPawn::TickRotateTo(const vec3& target)
{
	if (Physics() == PHYS_Spider)
		return true;

	DesiredRotation() = Rotator::FromVector(target - Location());

	if (Physics() == PHYS_Walking && (!MoveTarget() || !MoveTarget()->IsA("Pawn")))
	{
		DesiredRotation().Pitch = 0;
	}

	int doneAngle = 2000;
	return (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) < doneAngle) || (std::abs(DesiredRotation().Yaw - (Rotation().Yaw & 0xffff)) > 0xffff - doneAngle);
}

bool UPawn::TickMoveTo(const vec3& target)
{
	if (MoveTimer() < 0.0f)
		return true;

	if (Physics() == PHYS_Walking)
	{
		vec2 delta = target.xy() - Location().xy();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity(), Velocity());
		if (distSqr < 1.0f || distSqr < velocitySqr * 0.05f)
			return true;

		Acceleration() = vec3(normalize(delta) * AccelRate(), 0.0f);
	}
	else
	{
		vec3 delta = target - Location();
		float distSqr = dot(delta, delta);
		float velocitySqr = dot(Velocity(), Velocity());
		if (distSqr < 1.0f || distSqr < velocitySqr * 0.05f)
			return true;

		Acceleration() = normalize(delta) * AccelRate();
	}

	return false;
}

void UPawn::MoveTo(const vec3& newDestination, float speed)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	Destination() = newDestination;
	Focus() = newDestination;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveTo;
	StartMoveDeusEx();
}

void UPawn::MoveToward(UActor* newTarget, float speed)
{
	if (!newTarget)
		return;

	MoveTarget() = newTarget;
	Destination() = newTarget->Location();
	Focus() = newTarget->Location();
	bReducedSpeed() = false;
	DesiredSpeed() = clamp(speed, 0.0f, MaxDesiredSpeed());
	// The pawn looks at a target that is a pawn again after this time. Deus Ex has 1.2 seconds here.
	if (UObject::TryCast<UPawn>(newTarget))
		MoveTimer() = engine->LaunchInfo.IsDeusEx() ? 1.2f : 1.0f;
	else
		SetMoveDuration(newTarget->Location() - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::MoveToward;
	StartMoveDeusEx();
}

// The speed of the two strafe moves. Deus Ex gave these script functions a speed argument with the default 1.0
// (APawn::execStrafeTo and APawn::execStrafeFacing in its Engine.dll): a pawn that is not a player gets that speed,
// with MaxDesiredSpeed as the limit. The other games give such a pawn 80 percent of MaxDesiredSpeed.
float UPawn::StrafeSpeed(float speed)
{
	if (bIsPlayer())
		return MaxDesiredSpeed();
	if (engine->LaunchInfo.IsDeusEx())
		return clamp(speed, 0.0f, MaxDesiredSpeed());
	return 0.8f * MaxDesiredSpeed();
}

void UPawn::StrafeFacing(const vec3& newDestination, UActor* newTarget, float speed)
{
	if (!newTarget)
		return;

	// Without a new speed here, a pawn keeps the speed of its last move. A guard of Deus Ex that went from its patrol
	// to "run and fire" then ran with its walking speed.
	bReducedSpeed() = false;
	DesiredSpeed() = StrafeSpeed(speed);
	Destination() = newDestination;
	if (engine->LaunchInfo.ue1Version > 219)
		FaceTarget() = newTarget;
	Focus() = newTarget->Location();
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeFacing;
	StartMoveDeusEx();
}

void UPawn::StrafeTo(const vec3& newDestination, const vec3& newFocus, float speed)
{
	MoveTarget() = nullptr;
	bReducedSpeed() = false;
	DesiredSpeed() = StrafeSpeed(speed);
	Destination() = newDestination;
	Focus() = newFocus;
	SetMoveDuration(newDestination - Location());
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::StrafeTo;
	StartMoveDeusEx();
}

// The script functions that start a move also turn the pawn and set its acceleration at once in the original, so
// the pawn moves in the tick of the call.
void UPawn::StartMoveDeusEx()
{
	if (!engine->LaunchInfo.IsDeusEx())
		return;
	TickRotateTo(Focus());
	MoveTowardDeusEx(Destination());
}

// The work of each tick for the four latent moves, as execPollMoveTo, execPollMoveToward, execPollStrafeTo and
// execPollStrafeFacing of APawn in the Engine.dll of Deus Ex (build 1112fm). Returns false when the pawn is not in
// one of these moves.
bool UPawn::PollMoveDeusEx()
{
	LatentRunState state = StateFrame->LatentState;
	if (state != LatentRunState::MoveTo && state != LatentRunState::MoveToward && state != LatentRunState::StrafeTo && state != LatentRunState::StrafeFacing)
		return false;

	if (state == LatentRunState::MoveToward)
	{
		UActor* target = MoveTarget();
		if (!target)
		{
			StateFrame->LatentState = LatentRunState::Continue;
			return true;
		}
		vec3 dest = target->Location();
		if (Physics() == PHYS_Flying && UObject::TryCast<UPawn>(target))
			dest.z += 0.7f * target->CollisionHeight();
		else if (Physics() == PHYS_Spider)
			dest -= Floor() * target->CollisionRadius();
		Destination() = dest;
		Focus() = dest;
	}
	else if (state == LatentRunState::StrafeFacing)
	{
		UActor* faceTarget = FaceTarget();
		if (!faceTarget)
		{
			StateFrame->LatentState = LatentRunState::Continue;
			return true;
		}
		Focus() = faceTarget->Location();
	}

	// The script of a pawn with bAdvancedTactics can change the destination for this tick only
	vec3 oldDestination = Destination();
	float oldDesiredSpeed = DesiredSpeed();
	bool alterDestination = bAdvancedTactics() && Physics() == PHYS_Walking;

	if (state == LatentRunState::StrafeFacing)
	{
		TickRotateTo(Focus());
		if (alterDestination)
			CallEvent(this, "AlterDestination");
	}
	else
	{
		if (alterDestination)
			CallEvent(this, "AlterDestination");
		TickRotateTo(state == LatentRunState::StrafeTo ? Focus() : Destination());
	}

	if (MoveTowardDeusEx(Destination()))
		StateFrame->LatentState = LatentRunState::Continue;

	if (state == LatentRunState::MoveToward)
	{
		if (UActor* target = MoveTarget())
		{
			Destination() = target->Location();
			if (UObject::TryCast<UPawn>(target))
			{
				// A pawn that follows a pawn does not slow down near it
				DesiredSpeed() = oldDesiredSpeed;
				if (!bCanSwim() && target->Region().Zone && target->Region().Zone->bWaterZone())
					MoveTimer() = -1.0f;
			}
		}
	}
	else
	{
		Destination() = oldDestination;
	}
	return true;
}

// As APawn::moveToward in the Engine.dll of Deus Ex (build 1112fm): give the pawn the acceleration to a point.
// Returns true when the pawn is at the point or the move must end.
bool UPawn::MoveTowardDeusEx(const vec3& dest)
{
	vec3 direction = dest - Location();
	if (Physics() == PHYS_Walking)
	{
		direction.z = 0.0f;
	}
	else if (Physics() == PHYS_Falling)
	{
		// A pawn that falls can steer only when the gravity of the zone is below 90 percent of the usual gravity
		UZoneInfo* zone = Region().Zone;
		if (!zone || zone->ZoneGravity().z <= 0.9f * zone->Class->GetDefaultObject<UZoneInfo>()->ZoneGravity().z)
			return false;
		direction.z = 0.0f;
		float size = length(direction);
		Acceleration() = size > 0.0001f ? direction * (AccelRate() / size) : vec3(0.0f);
		if (Velocity().z >= 0.0f)
			return false;
		return Location().z + 100.0f < dest.z;
	}

	// A pawn that stands in the thing that it goes to takes it
	UActor* target = MoveTarget();
	if (target && UObject::TryCast<UInventory>(target))
	{
		vec3 offset = Location() - target->Location();
		if (std::abs(offset.z) < CollisionHeight() && offset.x * offset.x + offset.y * offset.y < CollisionRadius() * CollisionRadius())
			CallEvent(target, EventName::Touch, { ExpressionValue::ObjectValue(this) });
	}

	float distance = length(direction);
	bool glider = bCanGlide() && !bCanStrafe() && (Physics() == PHYS_Flying || Physics() == PHYS_Swimming);

	if (direction.x * direction.x + direction.y * direction.y < 256.0f && std::abs(direction.z) < std::max(48.0f, CollisionHeight()))
	{
		if (!glider)
			Acceleration() = vec3(0.0f);
		return true;
	}

	if (glider)
		direction = Coords::Rotation(Rotation()).XAxis;
	else if (distance > 0.0f)
		direction *= 1.0f / distance;

	Acceleration() = direction * AccelRate();

	if (MoveTimer() < 0.0f)
		return true;

	// A pawn that goes to a pawn is there when it is in reach for a hit
	target = MoveTarget();
	if (target && UObject::TryCast<UPawn>(target))
		return distance < MeleeRange() * 0.8f + target->CollisionRadius() + CollisionRadius();

	// Steer against a velocity that does not point at the destination
	float speed = length(Velocity());
	if (!glider && speed > 100.0f)
	{
		vec3 velocityDir = Velocity() * (1.0f / speed);
		Acceleration() -= (velocityDir - direction) * ((1.0f - dot(velocityDir, direction)) * speed * 0.2f);
	}

	// Slow down when the pawn would go past the destination in the next steps
	if (AvgPhysicsTime() * speed * 1.4f <= distance)
		return false;

	if (!bReducedSpeed())
	{
		bReducedSpeed() = true;
		DesiredSpeed() *= 0.5f;
	}
	if (speed > 0.0f)
		DesiredSpeed() = std::min(DesiredSpeed(), 200.0f / speed);
	return glider;
}

void UPawn::TurnTo(const vec3& newFocus)
{
	MoveTarget() = nullptr;
	Focus() = newFocus;
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnTo;
}

void UPawn::TurnToward(UActor* newTarget)
{
	if (!newTarget)
		return;

	if (engine->LaunchInfo.ue1Version > 219)
		FaceTarget() = newTarget;
	Focus() = newTarget->Location();
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::TurnToward;
}

void UPawn::WaitForLanding()
{
	if (StateFrame)
		StateFrame->LatentState = LatentRunState::WaitForLanding;
}

void UPawn::SetMoveDuration(const vec3& deltaMove)
{
	float scale = DesiredSpeed() * GetSpeed();
	MoveTimer() = scale > 0.0f ? 1.0f + 1.3f * length(deltaMove) / scale : 0.5f;
}

float UPawn::GetSpeed()
{
	switch (Physics())
	{
	case PHYS_Walking:
	case PHYS_Falling:
	case PHYS_Spider:
		return GroundSpeed();
	case PHYS_Flying:
		return AirSpeed();
	case PHYS_Swimming:
		return WaterSpeed();
	default:
		return 0.0f;
	}
}
