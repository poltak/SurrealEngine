
#include "Precomp.h"
#include "Engine.h"
#include "Harness.h"
#include "Utils/Logger.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Collision/TopLevel/CollisionHit.h"
#include <algorithm>
#include <cmath>

// Deus Ex AI perception. These functions follow Engine.dll (build 1112fm).

// True if nothing blocks the line. Level geometry blocks. An actor blocks if bBlockSight is set and it is
// not hidden. The source, the owners of the source and the target do not block.
static bool AILineOfSight(ULevel* level, UActor* source, UActor* target, const vec3& start, const vec3& end)
{
	for (const CollisionHit& hit : level->Collision.Trace(start, end, 0.0f, 0.0f, true, true, true))
	{
		if (!hit.Actor)
			return false;

		bool ownHit = false;
		for (UActor* a = source; a; a = a->Owner())
		{
			if (a == hit.Actor)
			{
				ownHit = true;
				break;
			}
		}
		if (ownHit || hit.Actor == target)
			continue;

		if (hit.Actor->bBlockSight() && !hit.Actor->bHidden())
			return false;
	}
	return true;
}

// 1 if the angle is fully in the field of view, 0 if it is fully out, and a linear ramp over the
// angular size of the target at the edge. All values are in degrees.
static float AIFovFactor(float angle, float fov, float angularSize)
{
	while (angle <= -180.0f) angle += 360.0f;
	while (angle > 180.0f) angle -= 360.0f;
	angle = std::abs(angle);

	float half = (fov - angularSize) * 0.5f;
	if (half + angularSize < angle)
		return 0.0f;
	if (half < angle)
		return 1.0f - (angle - half) / angularSize;
	return 1.0f;
}

bool UActor::IsDetectable()
{
	return !HasProperty("bDetectable") || GetBool("bDetectable");
}

float UPawn::AICanSee(UActor* other, float visibility, bool bCheckVisibility, bool bCheckDir, bool bCheckCylinder, bool bCheckLOS)
{
	float result = AICanSeeInternal(other, visibility, bCheckVisibility, bCheckDir, bCheckCylinder, bCheckLOS);
	static int traceCount = 0;
	if (Harness::Get().TraceAI && other && (UObject::TryCast<UPlayerPawn>(other) || traceCount++ < 60))
	{
		char buf[256];
		snprintf(buf, sizeof(buf), "AI trace: %s AICanSee(%s, vis=%.3f, checks=%d%d%d%d) dist=%.0f fov=%.0f aspect=%.2f minAng=%.6f thresh=%.3f -> %.4f",
			Name.ToString().c_str(), other->Name.ToString().c_str(), visibility, (int)bCheckVisibility, (int)bCheckDir, (int)bCheckCylinder, (int)bCheckLOS,
			length(other->Location() - Location()), AIHorizontalFov(), AspectRatio(), MinAngularSize(), VisibilityThreshold(), result);
		LogMessage(buf);
	}
	return result;
}

float UPawn::AICanSeeInternal(UActor* other, float visibility, bool bCheckVisibility, bool bCheckDir, bool bCheckCylinder, bool bCheckLOS)
{
	if (!other || visibility <= 0.0f || !other->IsDetectable())
		return 0.0f;

	vec3 eye = Location() + vec3(0.0f, 0.0f, BaseEyeHeight());
	vec3 delta = other->Location() - eye;
	float distSq = std::max(dot(delta, delta), 1.0f);

	float radiusSq = other->CollisionRadius() * other->CollisionRadius();
	float heightSq = other->CollisionHeight() * other->CollisionHeight();
	float angularSize = (heightSq + radiusSq) / distSq;
	if (angularSize <= MinAngularSize())
		return 0.0f;

	// Smaller than 1 degree: one line to the center is sufficient
	if (angularSize < 0.0003046793f)
		bCheckCylinder = false;

	visibility = visibility * angularSize * 64.0f;

	if (bCheckDir && visibility > 0.0f)
	{
		const float radToFullDeg = 114.59155902616465f; // 2 * 180 / pi
		float verticalFov = AspectRatio() > 0.0f ? AIHorizontalFov() / AspectRatio() : 0.0f;
		float widthDeg = std::atan(std::sqrt(radiusSq / distSq)) * radToFullDeg;
		float heightDeg = std::atan(std::sqrt(heightSq / distSq)) * radToFullDeg;

		Rotator view = UObject::TryCast<UPlayerPawn>(this) ? ViewRotation() : Rotation();
		Rotator add = AIAddViewRotation();
		view.Pitch += add.Pitch;
		view.Yaw += add.Yaw;
		view.Roll += add.Roll;

		vec3 X, Y, Z;
		Coords::Rotation(view).GetAxes(X, Y, Z);
		Rotator dir = Rotator::FromVector(vec3(dot(delta, X), dot(delta, Y), dot(delta, Z)));

		float yawFactor = AIFovFactor(dir.Yaw * 360.0f / 65536.0f, AIHorizontalFov(), widthDeg);
		float pitchFactor = AIFovFactor(dir.Pitch * 360.0f / 65536.0f, verticalFov, heightDeg);
		if (distSq < 22500.0f && pitchFactor < 0.75f)
			pitchFactor = 0.75f;
		visibility = pitchFactor * (yawFactor * visibility);
	}

	if (bCheckVisibility && visibility > 0.0f)
		visibility *= other->AIVisibility(true);

	visibility = clamp(visibility - VisibilityThreshold(), 0.0f, 1.0f);

	if (!bCheckLOS || visibility <= 0.0f)
		return visibility;

	ULevel* level = XLevel();
	bool visible = false;
	if (!bCheckCylinder)
	{
		visible = AILineOfSight(level, this, other, eye, other->Location());
	}
	else
	{
		// Player: the eyes first. Then the top and the bottom of the collision cylinder.
		UPlayerPawn* otherPlayer = UObject::TryCast<UPlayerPawn>(other);
		if (otherPlayer)
			visible = AILineOfSight(level, this, other, eye, other->Location() + vec3(0.0f, 0.0f, otherPlayer->BaseEyeHeight()));
		if (!visible)
			visible = AILineOfSight(level, this, other, eye, other->Location() + vec3(0.0f, 0.0f, other->CollisionHeight()));
		if (!visible)
			visible = AILineOfSight(level, this, other, eye, other->Location() - vec3(0.0f, 0.0f, other->CollisionHeight()));

		// Not a player: the left side and the right side of the cylinder also
		if (!visible && !otherPlayer)
		{
			vec3 flat(delta.x, delta.y, 0.0f);
			float len = length(flat);
			flat = len > 0.0001f ? flat / len : vec3(0.0f);
			vec3 side(flat.y * other->CollisionRadius(), -flat.x * other->CollisionRadius(), 0.0f);
			visible = AILineOfSight(level, this, other, eye, other->Location() + side);
			if (!visible)
				visible = AILineOfSight(level, this, other, eye, other->Location() - side);
		}
	}

	return visible ? visibility : 0.0f;
}

float UPawn::AICanHear(UActor* other, float volume, float radius)
{
	if (!other || volume <= 0.0f || !other->IsDetectable())
		return 0.0f;
	if (radius <= 0.0f)
		radius = 800.0f;

	// Height counts double
	vec3 delta = other->Location() - Location();
	delta.z *= 2.0f;
	float distSq = dot(delta, delta);
	if (distSq >= radius * radius)
		return 0.0f;

	float result = (1.0f - std::sqrt(distSq) / radius) * volume - HearingThreshold();
	return clamp(result, 0.0f, 1.0f);
}

float UPawn::AICanSmell(UActor* other, float smell)
{
	// The original returns 0 always
	return 0.0f;
}

static float AILightBrightness(uint8_t brightness, uint8_t saturation)
{
	return brightness * (1.0f / 255.0f) * (saturation + 127.5f) * (1.0f / 382.5f);
}

float UActor::AIGetLightLevel(const vec3& location)
{
	ULevel* level = XLevel();
	float light = 0.0f;

	if (bUnlit())
	{
		light = 1.0f;
	}
	else
	{
		for (UActor* actor : level->Actors)
		{
			if (!actor || actor == this || actor->LightType() == 0 || actor->LightBrightness() == 0)
				continue;

			float radius = actor->WorldLightRadius();
			vec3 delta = location - actor->Location();
			float distSq = dot(delta, delta);
			if (distSq >= radius * radius)
				continue;

			// A static light must have a free line to the location
			if (actor->bStatic() && level->Collision.TraceAnyHit(location, actor->Location(), this, false, true, true))
				continue;

			float falloff = 1.0f - std::sqrt(distSq / (radius * radius));
			light += AILightBrightness(actor->LightBrightness(), actor->LightSaturation()) * falloff;
		}
		light *= 0.5f;
	}

	UZoneInfo* zone = Region().Zone;
	if (zone)
		light += 2.0f * AILightBrightness(zone->AmbientBrightness(), zone->AmbientSaturation());

	return clamp(light, 0.0f, 1.0f);
}

float UActor::AIVisibility(std::optional<bool> bIncludeVelocity)
{
	// The light level is sampled again after 0.25 seconds. Between two samples the value moves from the
	// last sample to the current one.
	float now = Level()->TimeSeconds();
	float elapsed = now - VisUpdateTime();
	float visibility;
	if (elapsed > 0.5f)
	{
		VisUpdateTime() = now;
		visibility = AIGetLightLevel(Location());
		CurrentVisibility() = visibility;
		LastVisibility() = visibility;
	}
	else if (elapsed > 0.25f)
	{
		LastVisibility() = CurrentVisibility();
		VisUpdateTime() = now;
		CurrentVisibility() = AIGetLightLevel(Location());
		visibility = LastVisibility();
	}
	else
	{
		visibility = (elapsed / 0.25f) * (CurrentVisibility() - LastVisibility()) + LastVisibility();
	}

	// A moving actor is up to 50% more visible
	if (bIncludeVelocity && *bIncludeVelocity)
	{
		float speedFactor = clamp((length(Velocity()) - 30.0f) / (200.0f - 30.0f), 0.0f, 1.0f);
		visibility = speedFactor * visibility * 0.5f + visibility;
	}

	return clamp(visibility, 0.0f, 1.0f);
}

bool UActor::InStasis()
{
	ULevel* level = XLevel();
	float now = Level()->TimeSeconds();

	if (now - LastRenderTime() <= 5.0f)
		return false;
	if (!bStasis())
		return false;
	if (!bForceStasis() && Physics() != PHYS_None && Physics() != PHYS_Rotating)
		return false;

	uint8_t zoneNumber = Region().ZoneNumber;
	float zoneLastRender = zoneNumber < 64 ? level->ZoneLastRenderTime[zoneNumber] : 0.0f;
	if (now - zoneLastRender <= 5.0f && DistanceFromPlayer() <= 1200.0f)
		return false;

	return Level()->NetMode() == 0;
}
