#pragma once

#include "CollisionSystem.h"
#include "Packages/Engine/Resources/Level/ULevel.h"

class CollisionTester
{
public:
	CollisionTester(CollisionSystem* collision) : Collision(collision) {}

	static int NextCheckCounter() { return CheckCounter++; }

	static ivec3 GetStartExtents(const vec3& location, const vec3& extents) { return CollisionSystem::GetStartExtents(location, extents); }
	static ivec3 GetRayStartExtents(const vec3& from, const vec3& to) { return CollisionSystem::GetRayStartExtents(from, to); }
	static ivec3 GetSweepStartExtents(const vec3& from, const vec3& to, const vec3& extents) { return CollisionSystem::GetSweepStartExtents(from, to, extents); }

	static ivec3 GetEndExtents(const vec3& location, const vec3& extents) { return CollisionSystem::GetEndExtents(location, extents); }
	static ivec3 GetRayEndExtents(const vec3& from, const vec3& to) { return CollisionSystem::GetRayEndExtents(from, to); }
	static ivec3 GetSweepEndExtents(const vec3& from, const vec3& to, const vec3& extents) { return CollisionSystem::GetSweepEndExtents(from, to, extents); }

	const std::list<UActor*>& GetActors(int x, int y, int z) const
	{
		auto it = Collision->CollisionActors.find(CollisionSystem::GetBucketId(x, y, z));
		if (it != Collision->CollisionActors.end())
			return it->second;
		else
			return emptyList;
	}

	// Calls func one time for each actor in the grid cells that a box with these extents touches on its way from
	// 'from' to 'to'. The actor gets checkCounter, so it does not come again. func returns true to stop the search,
	// and the function then returns true.
	// A long way goes in parts of at most one cell (256 units). So the search looks only in the cells along the way,
	// and not in all cells of the bounding box of the way. The shot of the sniper rifle of Deus Ex goes 48000 units.
	template<typename Func>
	bool ForEachActorAlongSweep(const vec3& from, const vec3& to, const vec3& extents, int checkCounter, Func&& func) const
	{
		vec3 delta = to - from;
		float longest = std::max({ std::abs(delta.x), std::abs(delta.y), std::abs(delta.z) });
		if (!std::isfinite(longest))
			return false;

		// The bucket id of a cell repeats after 1024 cells on each axis. A way that is longer than that goes through
		// all buckets, so then look at all actors of the grid.
		int parts = std::max((int)std::ceil(longest * (1.0f / 256.0f)), 1);
		if (parts > 1024)
		{
			for (auto& bucket : Collision->CollisionActors)
			{
				for (UActor* actor : bucket.second)
				{
					if (VisitActor(actor, checkCounter, func))
						return true;
				}
			}
			return false;
		}

		for (int i = 0; i < parts; i++)
		{
			vec3 partFrom = (i == 0) ? from : from + delta * ((float)i / parts);
			vec3 partTo = (i + 1 == parts) ? to : from + delta * ((float)(i + 1) / parts);
			ivec3 start = GetSweepStartExtents(partFrom, partTo, extents);
			ivec3 end = GetSweepEndExtents(partFrom, partTo, extents);
			for (int z = start.z; z < end.z; z++)
			{
				for (int y = start.y; y < end.y; y++)
				{
					for (int x = start.x; x < end.x; x++)
					{
						for (UActor* actor : GetActors(x, y, z))
						{
							if (VisitActor(actor, checkCounter, func))
								return true;
						}
					}
				}
			}
		}
		return false;
	}

	ULevel* GetLevel() const { return Collision->Level; }

private:
	// A template on the actor type, so that this header does not need the full UActor class
	template<typename ActorT, typename Func>
	static bool VisitActor(ActorT* actor, int checkCounter, Func& func)
	{
		if (actor->Collision.CheckCounter == checkCounter)
			return false;
		actor->Collision.CheckCounter = checkCounter;
		return func(actor);
	}

	CollisionSystem* Collision = nullptr;
	inline static std::list<UActor*> emptyList;
	inline static int CheckCounter = 0;
};
