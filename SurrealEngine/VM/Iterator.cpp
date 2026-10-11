
#include "Precomp.h"
#include "Iterator.h"
#include "Utils/File.h"
#include "Utils/StrTools.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"

#include <algorithm>

AllObjectsIterator::AllObjectsIterator(UObject* BaseClass, UObject** ReturnValue, UObject* InOuter)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), InOuter(InOuter), m_Objects(GC::GetObjects()), m_Iterator(m_Objects.begin())
{
}

bool AllObjectsIterator::Next()
{
	while (m_Iterator != m_Objects.end())
	{
		auto currentObject = dynamic_cast<UObject*>(*m_Iterator);

		++m_Iterator;

		if (currentObject && currentObject->IsA(BaseClass->Name) && (!InOuter || InOuter == currentObject->Outer()))
		{
			*ReturnValue = currentObject;
			return true;
		}
	}
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag)
{
}

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag, NameString MatchEvent)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag), MatchEvent(MatchEvent)
{
}

AllActorsIterator::AllActorsIterator(UObject* BaseClass, UObject** ReturnValue, NameString MatchTag, NameString MatchEvent, bool bAllLevels)
	: BaseClass(BaseClass), ReturnValue(ReturnValue), MatchTag(MatchTag), MatchEvent(MatchEvent), bAllLevels(bAllLevels)
{
}

bool AllActorsIterator::Next()
{
	bool matchTag = !MatchTag.IsNone();
	bool matchEvent = !MatchEvent.IsNone();

	size_t size = engine->Level->Actors.size();
	while (index < size)
	{
		UActor* actor = engine->Level->Actors[index++];
		if (actor && actor->IsA(BaseClass->Name) && (!matchTag || actor->Tag() == MatchTag) && (!matchEvent || actor->Event() == MatchEvent))
		{
			*ReturnValue = actor;
			return true;
		}
	}
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

AllFilesIterator::AllFilesIterator(const std::string& FileExtension, const std::string& FilePrefix, std::string& FileName) : FileExtension(FileExtension), FilePrefix(FilePrefix), FileName(FileName)
{
	auto packageNames = engine->packages->GetPackageNames();

	for (auto& packageName : packageNames)
	{
		auto package = engine->packages->GetPackage(packageName);

		if ((FileExtension.empty() || package->GetPackageFileExtension() == "." + FileExtension) &&
			(FilePrefix.empty() || StrTools::startswith(package->GetPackageFileName(), FilePrefix, true)))
		{
			FoundFiles.push_back(package->GetPackageFileName());
		}
	}

	iterator = FoundFiles.begin();
}

bool AllFilesIterator::Next()
{
	if (iterator == FoundFiles.end())
	{
		FileName = {};
		return false;
	}

	FileName = *iterator;
	iterator++;

	return true;
}

/////////////////////////////////////////////////////////////////////////////,

AllParticlesIterator::AllParticlesIterator(UObject** ReturnValue)
	: ReturnValue(ReturnValue)
{
}

bool AllParticlesIterator::Next()
{
	ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

BasedActorsIterator::BasedActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor) : BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* based : Caller->BasedActors)
	{
		if (!based || !based->Class) continue;
		if (based->IsA(BaseClass->Name))
			BasedActors.push_back(based);
	}
	iterator = BasedActors.begin();
}

bool BasedActorsIterator::Next()
{
	if (iterator == BasedActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

ChildActorsIterator::ChildActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor) : BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* levelActor : Caller->ChildActors)
	{
		if (levelActor->IsA(BaseClass->Name))
			ChildActors.push_back(levelActor);
	}

	iterator = ChildActors.begin();
}

bool ChildActorsIterator::Next()
{
	if (iterator == ChildActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

// Deus Ex. Follows AActor::execCycleActors in Engine.dll (build 1112fm):
//
// The index is a cursor into the actor list of the level that the script keeps between calls. The iterator
// starts at the cursor, goes around the end of the list, and visits each list entry one time at most. After
// each actor that it gives to the script, the cursor is the entry after that actor. So a script that leaves
// the loop early continues with the next actors in its next call, and it sees a smaller index when the
// cursor went around the end of the list.
CycleActorsIterator::CycleActorsIterator(UObject* BaseClass, UObject** Actor, int* outIndex)  : BaseClass(BaseClass), Actor(Actor), outIndex(outIndex)
{
	total = engine->Level->Actors.size();
	cursor = outIndex ? *outIndex : 0;
	if (cursor < 0 || (size_t)cursor >= total)
		cursor = 0;
}

bool CycleActorsIterator::Next()
{
	auto& actors = engine->Level->Actors;
	total = std::min(total, actors.size());

	UActor* found = nullptr;
	while (visited < total)
	{
		if ((size_t)cursor >= actors.size())
			cursor = 0;
		UActor* levelActor = actors[cursor++];
		visited++;
		if (levelActor && (!BaseClass || levelActor->IsA(BaseClass->Name)))
		{
			found = levelActor;
			break;
		}
	}

	*Actor = found;
	if (outIndex)
		*outIndex = cursor;
	return found != nullptr;
}

/////////////////////////////////////////////////////////////////////////////

IntDescIterator::IntDescIterator(std::string& className, std::string* entryName, std::string* desc, std::optional<bool> bSingleNames)
	: ClassName(className), EntryName(entryName), Desc(desc), bSingleNames(bSingleNames)
{
	IntObjects = engine->packages->GetIntObjects(ClassName);

	if (bSingleNames && *bSingleNames)
	{
		// TODO: Remove duplicates
	}

	it = IntObjects.begin();
}

bool IntDescIterator::Next()
{
	if (it == IntObjects.end())
	{
		EntryName = nullptr;
		Desc = nullptr;
		return false;
	}

	if (EntryName)
		*EntryName = it->Name.ToString();

	if (Desc)
		*Desc = it->Description;

	it++;

	return true;
}

/////////////////////////////////////////////////////////////////////////////

RadiusActorsIterator::RadiusActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor, float Radius, vec3 Location) : BaseClass(BaseClass), Actor(Actor), Radius(Radius), Location(Location)
{
	for (UActor* levelActor : engine->Level->Actors)
	{
		if (levelActor && levelActor->IsA(BaseClass->Name) && length(levelActor->Location() - Location) <= Radius)
			RadiusActors.push_back(levelActor);
	}

	iterator = RadiusActors.begin();
}

bool RadiusActorsIterator::Next()
{
	if (iterator == RadiusActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

TouchingActorsIterator::TouchingActorsIterator(UActor* Caller, UObject* BaseClass, UObject** outActor) : BaseClass(BaseClass), outActor(outActor)
{
	CollisionHitList hitList = Caller->XLevel()->Collision.OverlapTest(Caller->Location(), Caller->CollisionHeight(), Caller->CollisionRadius(), true, false, false);
	for (auto& hit : hitList)
	{
		// Only allow the Actors of type BaseClass
		if (hit.Actor && hit.Actor->IsA(BaseClass->Name))
			TouchingActors.push_back(hit.Actor);
	}

	iterator = TouchingActors.begin();
}

bool TouchingActorsIterator::Next()
{
	if (iterator == TouchingActors.end())
	{
		*outActor = nullptr;
		return false;
	}
	
	*outActor = *iterator;
	iterator++;

	return *outActor;
}

/////////////////////////////////////////////////////////////////////////////

TraceActorsIterator::TraceActorsIterator(UActor* SelfActor, UObject* BaseClass, UObject** Actor, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent) : SelfActor(SelfActor), BaseClass(BaseClass), Actor(Actor), HitLoc(HitLoc), HitNorm(HitNorm), End(End), Start(Start), Extent(Extent)
{
	TraceFlags flags;
	flags.movers = true;
	flags.world = true;
	flags.pawns = true;
	flags.others = true;
	flags.onlyProjectiles = false; // Should this be true or false?

	// Why is this tracing backwards? Is that correct?
	vec3 traceStart = End;
	vec3 traceEnd = Start;

	for (auto& hit : SelfActor->XLevel()->Collision.Trace(traceStart, traceEnd, Extent.z, Extent.x, flags.traceActors(), flags.traceWorld(), false))
	{
		if (hit.Actor && hit.Actor != SelfActor && hit.Actor->IsA(BaseClass->Name))
		{
			vec3 hitNormal = hit.Normal;
			vec3 hitLocation = traceStart + (traceEnd - traceStart) * hit.Fraction;
			tracedActors.push_back({ hit.Actor, *HitLoc, *HitNorm });
		}
	}

	iterator = tracedActors.begin();
}

bool TraceActorsIterator::Next()
{
	if (iterator == tracedActors.end() || tracedActors.empty())
	{
		*Actor = nullptr;
		*HitLoc = vec3(0.0f);
		*HitNorm = vec3(0.0f);
		return false;
	}

	*Actor = iterator->tracedActor;
	*HitLoc = iterator->HitLoc;
	*HitNorm = iterator->HitNorm;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

VisibleActorsIterator::VisibleActorsIterator(UActor* Caller, UObject* BaseClass, UObject** Actor, float Radius, const vec3& Location) : BaseClass(BaseClass), Actor(Actor), Radius(Radius), Location(Location)
{
	double sqrRadius = Radius * Radius;
	for (auto levelActor : engine->Level->Actors)
	{
		// Our checks:
		// * Whether the actor we're dealing with is not hidden and is the class of BaseClass
		// * Then whether the distance of the actor from our given Location is no more than Radius
		if (levelActor && !levelActor->bHidden() && levelActor->IsA(BaseClass->Name))
		{
			vec3 d = levelActor->Location() - Location;
			if (dot(d, d) <= sqrRadius && Caller->FastTrace(levelActor->Location(), Location))
				VisibleActors.push_back(levelActor);
		}
	}

	iterator = VisibleActors.begin();
}

bool VisibleActorsIterator::Next()
{
	if (iterator == VisibleActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

VisibleCollidingActorsIterator::VisibleCollidingActorsIterator(UObject* BaseClass, UObject** ReturnValue, float Radius, const vec3& Location, bool IgnoreHidden) : BaseClass(BaseClass), ReturnValue(ReturnValue), Radius(Radius), Location(Location), IgnoreHidden(IgnoreHidden)
{
	HitActors = engine->Level->Collision.CollidingActors(Location, Radius);
}

bool VisibleCollidingActorsIterator::Next()
{
	size_t size = HitActors.size();
	while (index < size)
	{
		UActor* actor = HitActors[index++];
		if (actor && (IgnoreHidden || !actor->bHidden()) && actor->IsA(BaseClass->Name))
		{
			*ReturnValue = actor;
			return true;
		}
	}
	*ReturnValue = nullptr;
	return false;
}

/////////////////////////////////////////////////////////////////////////////

ZoneActorsIterator::ZoneActorsIterator(UZoneInfo* zone, UObject* BaseClass, UObject** Actor) : Zone(zone), BaseClass(BaseClass), Actor(Actor)
{
	for (UActor* levelActor : engine->Level->Actors)
	{
		if (levelActor && levelActor->Region().Zone == zone && levelActor->IsA(BaseClass->Name))
		{
			ZoneActors.push_back(levelActor);
		}
	}

	iterator = ZoneActors.begin();
}

bool ZoneActorsIterator::Next()
{
	if (iterator == ZoneActors.end())
	{
		*Actor = nullptr;
		return false;
	}

	*Actor = *iterator;
	iterator++;

	return *Actor;
}

/////////////////////////////////////////////////////////////////////////////

Array<TraceHitInfo> CollectMultiLineCheckHits(UActor* SelfActor, const vec3& End, const vec3& Start, const vec3& Extent, bool visibilityOnly)
{
	// Mirrors ULevel::MultiLineCheck(Mem, End, Start, Extent, bCheckActors=1, LevelInfo, ExtraNodeFlags):
	// - hits come back sorted from Start to End
	// - a hit on level geometry has Actor = LevelInfo and ends the trace
	// - for a LevelInfo hit, the texture name, the texture group (the Outer of the texture) and the
	//   PolyFlags of the BSP surface (not of the texture) are reported. Actor hits report None/None/0.
	Array<TraceHitInfo> result;
	ULevel* level = SelfActor->XLevel();
	if (!level)
		return result;

	for (const CollisionHit& hit : level->Collision.Trace(Start, End, Extent.z, Extent.x, true, true, visibilityOnly))
	{
		TraceHitInfo info;
		info.HitLoc = mix(Start, End, hit.Fraction);
		info.HitNorm = hit.Normal;

		if (hit.Actor)
		{
			info.Actor = hit.Actor;
			result.push_back(info);
			continue;
		}

		info.Actor = SelfActor->Level();
		if (hit.Node && level->Model && hit.Node->Surf >= 0 && (size_t)hit.Node->Surf < level->Model->Surfaces.size())
		{
			const BspSurface& surf = level->Model->Surfaces[hit.Node->Surf];
			if (surf.Material)
			{
				info.TexName = surf.Material->Name;
				if (surf.Material->Outer())
					info.TexGroup = surf.Material->Outer()->Name;
			}
			info.Flags = (int)surf.PolyFlags;
		}
		result.push_back(info);
		break;
	}
	return result;
}

TraceTextureIterator::TraceTextureIterator(UActor* SelfActor, UObject** OutActor, NameString* TexName, NameString* TexGroup, int* Flags, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent)
	: OutActor(OutActor), TexName(TexName), TexGroup(TexGroup), Flags(Flags), HitLoc(HitLoc), HitNorm(HitNorm)
{
	Hits = CollectMultiLineCheckHits(SelfActor, End, Start, Extent, false);
}

bool TraceTextureIterator::Next()
{
	if (Index >= Hits.size())
	{
		*OutActor = nullptr;
		return false;
	}

	const TraceHitInfo& hit = Hits[Index++];
	*OutActor = hit.Actor;
	*HitLoc = hit.HitLoc;
	*HitNorm = hit.HitNorm;
	*TexName = hit.TexName;
	*TexGroup = hit.TexGroup;
	*Flags = hit.Flags;
	return true;
}

/////////////////////////////////////////////////////////////////////////////

TraceVisibleActorsIterator::TraceVisibleActorsIterator(UActor* SelfActor, UObject** OutActor, vec3* HitLoc, vec3* HitNorm, const vec3& End, const vec3& Start, const vec3& Extent)
	: OutActor(OutActor), HitLoc(HitLoc), HitNorm(HitNorm)
{
	// The original passes ExtraNodeFlags = NF_NotVisBlocking (4): the trace ignores BSP nodes that do not block sight.
	Hits = CollectMultiLineCheckHits(SelfActor, End, Start, Extent, true);
}

bool TraceVisibleActorsIterator::Next()
{
	if (Index >= Hits.size())
	{
		*OutActor = nullptr;
		return false;
	}

	const TraceHitInfo& hit = Hits[Index++];
	*OutActor = hit.Actor;
	*HitLoc = hit.HitLoc;
	*HitNorm = hit.HitNorm;
	return true;
}

/////////////////////////////////////////////////////////////////////////////

ReachablePathnodesIterator::ReachablePathnodesIterator(UPawn* pawn, UObject* baseClass, UObject** navPoint, UActor* fromPoint, float* distance, bool usePrunedPaths) : NavPoint(navPoint), Distance(distance)
{
	for (const UPawn::AINavInfo& info : pawn->AIGetPathnodeList(32, fromPoint, usePrunedPaths, true))
	{
		if (!baseClass || info.NavPoint->IsA(baseClass->Name))
			Items.push_back({ info.NavPoint, info.Distance });
	}
}

bool ReachablePathnodesIterator::Next()
{
	if (Index >= Items.size())
	{
		*NavPoint = nullptr;
		return false;
	}
	*NavPoint = Items[Index].first;
	*Distance = Items[Index].second;
	Index++;
	return true;
}
