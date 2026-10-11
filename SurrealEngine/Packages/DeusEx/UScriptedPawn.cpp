
#include "Precomp.h"
#include "UScriptedPawn.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Inventory/UWeapon.h"
#include <algorithm>
#include <cmath>

// The native part of the NPC tick: AScriptedPawn::Tick, UpdateAgitation and UpdateFear in DeusEx.dll (build 1112fm).
//
// The native tick does the same work as the first part of the script function ScriptedPawn.Tick, and then the
// engine calls that script function also. So the timers that both parts count run two times in each tick in
// the original game. Three timers are counted by the native part only: FireTimer, SpecialTimer and
// ObstacleTimer. Without the native part, an NPC with a fire delay shoots one time and then never again.

namespace
{
	struct ScriptedPawnOffsets
	{
		bool Initialized = false;
		bool Valid = false;

#define DX_SCRIPTEDPAWN_PROPS(X) \
		X(bDisappear) X(PrePivotTime) X(DesiredPrePivot) X(bTickVisibleOnly) \
		X(AgitationCheckTimer) X(AgitationTimer) X(AgitationDecayRate) X(bAlliancesChanged) \
		X(FearTimer) X(FearDecayRate) X(FearLevel) X(FearSustainTime) \
		X(AlarmTimer) X(WeaponTimer) X(FireTimer) X(SpecialTimer) X(ReloadTimer) \
		X(AvoidWallTimer) X(AvoidBumpTimer) X(ObstacleTimer) X(CloakEMPTimer) X(TakeHitTimer) \
		X(CarcassCheckTimer) X(PotentialEnemyTimer) X(PotentialEnemyAlliance) X(BeamCheckTimer) \
		X(FutzTimer) X(PlayerAgitationTimer) X(DistressTimer) \
		X(bHasCloak) X(CloakThreshold) X(bAdvancedTactics) X(TurnDirection) X(NextDirection) \
		X(ActorAvoiding) X(bClearedObstacle) X(bOnFire) X(burnTimer) X(BurnPeriod) \
		X(bleedRate) X(bCanBleed) X(dropCounter) X(clotPeriod)

#define X(name) PropertyDataOffset name;
		DX_SCRIPTEDPAWN_PROPS(X)
#undef X

		void Init(UObject* obj)
		{
			Initialized = true;
			Valid = true;
#define X(name) name = obj->GetPropertyDataOffset(#name); if (name.DataOffset == ~(size_t)0) { Valid = false; LogMessage("ScriptedPawn native tick: property not found: " #name); }
			DX_SCRIPTEDPAWN_PROPS(X)
#undef X
		}
	};

	ScriptedPawnOffsets Offsets;

	// "if (timer > 0) { timer -= dt; if (timer < 0) timer = 0; }"
	void CountDown(float& timer, float elapsed)
	{
		if (timer > 0.0f)
		{
			timer -= elapsed;
			if (timer < 0.0f)
				timer = 0.0f;
		}
	}
}

void UScriptedPawn::Tick(float elapsed)
{
	if (NativeTick(elapsed))
		UPawn::Tick(elapsed);
}

// Returns false if the tick destroyed the actor
bool UScriptedPawn::NativeTick(float elapsed)
{
	if (!Offsets.Initialized)
		Offsets.Init(this);
	if (!Offsets.Valid || bDeleteMe())
		return true;

	float lastRendered = std::max(Level()->TimeSeconds() - LastRenderTime(), 0.0f);
	if (BoolValue(Offsets.bDisappear) && (InStasis() || lastRendered > 5.0f))
	{
		Destroy();
		return false;
	}

	float& prePivotTime = Value<float>(Offsets.PrePivotTime);
	if (prePivotTime > 0.0f)
	{
		const vec3& desiredPrePivot = Value<vec3>(Offsets.DesiredPrePivot);
		if (prePivotTime <= elapsed)
		{
			PrePivot() = desiredPrePivot;
			prePivotTime = 0.0f;
		}
		else
		{
			PrePivot() += (desiredPrePivot - PrePivot()) * (elapsed / prePivotTime);
			prePivotTime -= elapsed;
		}
	}

	bool doLowPriority = !(BoolValue(Offsets.bTickVisibleOnly) && DistanceFromPlayer() > 1200.0f);

	UpdateAgitation(elapsed);
	UpdateFear(elapsed);

	float& alarmTimer = Value<float>(Offsets.AlarmTimer);
	alarmTimer -= elapsed;
	if (alarmTimer < 0.0f)
		alarmTimer = 0.0f;

	float& weaponTimer = Value<float>(Offsets.WeaponTimer);
	if (Weapon())
		weaponTimer += elapsed;
	else
		weaponTimer = 0.0f;

	CountDown(Value<float>(Offsets.FireTimer), elapsed);
	CountDown(Value<float>(Offsets.SpecialTimer), elapsed);

	float& reloadTimer = Value<float>(Offsets.ReloadTimer);
	if (reloadTimer <= 0.0f || !Weapon())
		reloadTimer = 0.0f;
	else
		reloadTimer -= elapsed;

	CountDown(Value<float>(Offsets.AvoidWallTimer), elapsed);
	CountDown(Value<float>(Offsets.AvoidBumpTimer), elapsed);
	CountDown(Value<float>(Offsets.ObstacleTimer), elapsed);
	CountDown(Value<float>(Offsets.CloakEMPTimer), elapsed);
	CountDown(Value<float>(Offsets.TakeHitTimer), elapsed);
	CountDown(Value<float>(Offsets.CarcassCheckTimer), elapsed);

	float& potentialEnemyTimer = Value<float>(Offsets.PotentialEnemyTimer);
	if (potentialEnemyTimer > 0.0f)
	{
		potentialEnemyTimer -= elapsed;
		if (potentialEnemyTimer <= 0.0f)
		{
			potentialEnemyTimer = 0.0f;
			Value<NameString>(Offsets.PotentialEnemyAlliance) = NameString();
		}
	}

	CountDown(Value<float>(Offsets.BeamCheckTimer), elapsed);
	CountDown(Value<float>(Offsets.FutzTimer), elapsed);
	CountDown(Value<float>(Offsets.PlayerAgitationTimer), elapsed);

	float& distressTimer = Value<float>(Offsets.DistressTimer);
	if (distressTimer >= 0.0f)
	{
		distressTimer += elapsed;
		if (distressTimer > Value<float>(Offsets.FearSustainTime))
			distressTimer = -1.0f;
	}

	if (BoolValue(Offsets.bHasCloak))
	{
		bool enable = Health() <= Value<int>(Offsets.CloakThreshold);
		CallEvent(this, "EnableCloak", { ExpressionValue::BoolValue(enable) });
		if (bDeleteMe())
			return false;
	}

	BitfieldBool advancedTactics = BoolValue(Offsets.bAdvancedTactics);
	uint8_t& turnDirection = Value<uint8_t>(Offsets.TurnDirection);
	if (advancedTactics && (Acceleration() == vec3(0.0f) || Physics() != PHYS_Walking || turnDirection == 0))
	{
		advancedTactics = false;
		if (turnDirection != 0)
			MoveTimer() -= 4.0f;
		Value<UActor*>(Offsets.ActorAvoiding) = nullptr;
		Value<uint8_t>(Offsets.NextDirection) = 0;
		turnDirection = 0;
		BoolValue(Offsets.bClearedObstacle) = true;
		Value<float>(Offsets.ObstacleTimer) = 0.0f;
	}

	if (BoolValue(Offsets.bOnFire))
	{
		float& burnTimer = Value<float>(Offsets.burnTimer);
		burnTimer += elapsed;
		if (burnTimer >= Value<float>(Offsets.BurnPeriod))
		{
			CallEvent(this, "ExtinguishFire");
			if (bDeleteMe())
				return false;
		}
	}

	if (doLowPriority)
	{
		float& bleedRate = Value<float>(Offsets.bleedRate);
		float& dropCounter = Value<float>(Offsets.dropCounter);
		if (bleedRate > 0.0f && BoolValue(Offsets.bCanBleed))
		{
			float adjustedRate = (1.0f - clamp(bleedRate, 0.0f, 1.0f)) + 0.1f; // 10 drops in one second at most
			float dropPeriod = adjustedRate / clamp(length(Velocity()) * (1.0f / 512.0f), 0.05f, 1.0f);
			dropCounter += elapsed;
			while (dropCounter >= dropPeriod)
			{
				CallEvent(this, "SpurtBlood");
				dropCounter -= dropPeriod;
			}
			bleedRate -= elapsed / Value<float>(Offsets.clotPeriod);
		}
		if (bleedRate <= 0.0f)
		{
			dropCounter = 0.0f;
			bleedRate = 0.0f;
		}
	}

	return !bDeleteMe();
}

void UScriptedPawn::UpdateAgitation(float elapsed)
{
	CountDown(Value<float>(Offsets.AgitationCheckTimer), elapsed);

	float& agitationTimer = Value<float>(Offsets.AgitationTimer);
	float decayRate = Value<float>(Offsets.AgitationDecayRate);
	float decrement;
	if (agitationTimer <= 0.0f)
	{
		decrement = elapsed * decayRate;
	}
	else if (elapsed <= agitationTimer)
	{
		agitationTimer -= elapsed;
		decrement = 0.0f;
	}
	else
	{
		float mult = 1.0f - agitationTimer / elapsed;
		agitationTimer = 0.0f;
		decrement = elapsed * decayRate * mult;
	}

	BitfieldBool alliancesChanged = BoolValue(Offsets.bAlliancesChanged);
	if (alliancesChanged && decrement > 0.0f)
	{
		alliancesChanged = false;
		auto alliances = AlliancesEx();
		for (int i = 15; i >= 0; i--)
		{
			if (!alliances[i].AllianceName.IsNone() && !alliances[i].bPermanent && alliances[i].AllianceAgitation > 0.0f)
			{
				alliancesChanged = true;
				alliances[i].AllianceAgitation -= decrement;
				if (alliances[i].AllianceAgitation < 0.0f)
					alliances[i].AllianceAgitation = 0.0f;
			}
		}
	}
}

void UScriptedPawn::UpdateFear(float elapsed)
{
	float& fearTimer = Value<float>(Offsets.FearTimer);
	float decayRate = Value<float>(Offsets.FearDecayRate);
	float decrement;
	if (fearTimer <= 0.0f)
	{
		decrement = elapsed * decayRate;
	}
	else if (elapsed <= fearTimer)
	{
		fearTimer -= elapsed;
		return;
	}
	else
	{
		float mult = 1.0f - fearTimer / elapsed;
		fearTimer = 0.0f;
		decrement = elapsed * decayRate * mult;
	}

	float& fearLevel = Value<float>(Offsets.FearLevel);
	if (decrement > 0.0f && fearLevel > 0.0f)
	{
		fearLevel -= decrement;
		if (fearLevel < 0.0f)
			fearLevel = 0.0f;
	}
}

void UScriptedPawn::AddCarcass(const NameString& CarcassName)
{
	if (NumCarcasses() < 4)
	{
		bool carcassSeen = HaveSeenCarcass(CarcassName);
		if (carcassSeen == false)
		{
			Carcasses()[NumCarcasses()] = CarcassName;
			NumCarcasses() = NumCarcasses() + 1;
		}
	}
}

void UScriptedPawn::ConBindEvents()
{
	DeusExConBindEvents();
}

uint8_t UScriptedPawn::GetAllianceType(const NameString& AllianceName)
{
	auto alliex = AlliancesEx();
	EAllianceType result = EAllianceType::ALLIANCE_Neutral;
	for (int i = 0; i < 16; i++)
	{
		if (alliex[i].AllianceName == AllianceName)
		{
			if ((alliex[i].AllianceLevel < 0.0) || (alliex[i].AllianceAgitation >= 1.0))
			{
				result = EAllianceType::ALLIANCE_Hostile;
			}
			else if (alliex[i].AllianceLevel > 0.0)
			{
				result = EAllianceType::ALLIANCE_Friendly;
			}
			break;
		}
	}

	if (bLikesNeutral() && (result == EAllianceType::ALLIANCE_Neutral))
	{
		result = EAllianceType::ALLIANCE_Friendly;
	}
	if (bReverseAlliances())
	{
		if (result == EAllianceType::ALLIANCE_Friendly)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
		if (result == EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Friendly;
		}
	}
	return (uint8_t)result;
}

uint8_t UScriptedPawn::GetPawnAllianceType(UPawn* QueryPawn)
{
	// As AScriptedPawn::GetPawnAllianceType in DeusEx.dll: no pawn is neutral. The script calls this with None when
	// the instigator of an AI event is not a pawn.
	if (!QueryPawn)
		return (uint8_t)EAllianceType::ALLIANCE_Neutral;

	if (UScriptedPawn* qp = UObject::TryCast<UScriptedPawn>(QueryPawn))
	{
		uint8_t othersAlliance = qp->GetAllianceType(Alliance());
		if (othersAlliance == (uint8_t)EAllianceType::ALLIANCE_Hostile)
		{
			return (uint8_t)EAllianceType::ALLIANCE_Hostile;
		}
	}
	return GetAllianceType(QueryPawn->Alliance());
}

bool UScriptedPawn::HaveSeenCarcass(const NameString& CarcassName)
{
	for (int i = 0; i < NumCarcasses(); i++)
	{
		if (Carcasses()[i] == CarcassName)
		{
			return true;
		}
	}
	return false;
}

bool UScriptedPawn::IsValidEnemy(UPawn* TestEnemy, std::optional<bool> bCheckAlliance)
{
	// Follows AScriptedPawn::IsValidEnemy in DeusEx.dll (build 1112fm). bCheckAlliance is true by default.
	if (!TestEnemy || TestEnemy == this || TestEnemy->bDeleteMe() || !TestEnemy->IsDetectable() || TestEnemy->Health() < 1)
		return false;

	if (!bCheckAlliance || *bCheckAlliance)
		return GetPawnAllianceType(TestEnemy) == (uint8_t)EAllianceType::ALLIANCE_Hostile;

	return true;
}
