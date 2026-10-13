
#include "Precomp.h"
#include "Engine.h"
#include "Harness.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/NavigationPoint/UNavigationPoint.h"
#include "Packages/Engine/Actors/NavigationPoint/UInventorySpot.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include <algorithm>
#include <cmath>

// The path search of APawn in Engine.dll of Deus Ex (build 1112fm): execFindPathToward, execFindPathTo,
// findPathToward, findPathTo, breadthPathFrom, HandleSpecial and the sorted list of anchor candidates
// (FSortedPathList).
//
// A search has a start anchor, which is a navigation point near the pawn, and an end anchor near the goal:
// - A pawn that stands on the navigation point that it went to (MoveTarget) has that point as its start anchor.
//   If not, the start anchor is the nearest navigation point that the pawn sees and can go to in a straight line.
// - A goal that is a navigation point is the end anchor. If not, the end anchor is the nearest navigation point
//   from which a pawn can go to the goal in a straight line.
// - The search (breadthPathFrom) goes from the end anchor back along the reach specs. It stops at the first point
//   that is marked as an end point: a neighbor of the start anchor when the pawn stands on that anchor, and the
//   start anchor itself when the pawn is some distance away from it.
// - The result is the navigation point that the pawn must go to next.
//
// A pawn that walks from point to point stands on its start anchor in each search. Then the result comes from the
// reach specs only. No reach test from the place of the pawn is part of it, so a small difference of that place
// does not change the way. A guard on patrol then takes the same way at each frame rate.
//
// The reach tests that the search uses (PointReachableDeusEx, ActorReachableDeusEx) are in ReachTest.cpp.

namespace
{
	const int MaxSorted = 32;
	const int MaxPathDistSquared = 640000; // 800 units
	const int NotVisited = 10000000;

	// The start of a search, for the --tracepath option of the test harness
	struct SearchTrace
	{
		UNavigationPoint* Start = nullptr;
		bool Anchored = false;
	};

	// A door on the line between two navigation points stops a pawn that cannot open doors. A door that opens only
	// when it takes damage stops each pawn that is not a player.
	bool DoorBlocksPath(UPawn* pawn, const vec3& start, const vec3& end)
	{
		TraceFlags flags;
		flags.movers = true;
		flags.world = true;
		CollisionHit hit = pawn->XLevel()->Collision.TraceFirstHit(start, end, pawn, vec3(0.0f), flags);
		UMover* mover = UObject::TryCast<UMover>(hit.Actor);
		if (!mover)
			return false;
		return !pawn->bCanOpenDoors() || (!pawn->bIsPlayer() && mover->bDamageTriggered());
	}

	// FReachSpec::supports
	bool Supports(const LevelReachSpec& spec, int radius, int height, int moveFlags)
	{
		return radius <= spec.collisionRadius && height <= spec.collisionHeight && (spec.reachFlags & moveFlags) == spec.reachFlags;
	}

	const LevelReachSpec* GetSpec(UPawn* pawn, int index)
	{
		const auto& specs = pawn->XLevel()->ReachSpecs;
		if (index < 0 || (size_t)index >= specs.size())
			return nullptr;
		const LevelReachSpec& spec = specs[index];
		return spec.startActor && spec.endActor ? &spec : nullptr;
	}

	// FSortedPathList: navigation points in the order of a distance, 32 at most
	struct SortedPathList
	{
		UNavigationPoint* Path[MaxSorted] = {};
		int Dist[MaxSorted] = {};
		int NumPoints = 0;

		void AddPath(UNavigationPoint* node, int dist)
		{
			int n = 0;
			while (n < NumPoints && dist > Dist[n])
				n++;
			if (n >= MaxSorted)
				return;

			if (NumPoints < MaxSorted)
				NumPoints++;
			for (int i = NumPoints - 1; i > n; i--)
			{
				Path[i] = Path[i - 1];
				Dist[i] = Dist[i - 1];
			}
			Path[n] = node;
			Dist[n] = dist;
		}

		void RemovePath(int p)
		{
			for (int i = p; i < NumPoints - 1; i++)
			{
				Path[i] = Path[i + 1];
				Dist[i] = Dist[i + 1];
			}
			NumPoints--;
		}

		// FindVisiblePaths: make the two candidate lists, this one for the start and destPoints for the end.
		// The distances in the lists are squared. A list that has its anchor gets no candidates.
		void FindVisiblePaths(UPawn* searcher, const vec3& dest, SortedPathList* destPoints, bool clearPaths, bool& startAnchor, bool& endAnchor)
		{
			// A pawn that stands on the navigation point that it went to starts the search at that point
			UNavigationPoint* target = UObject::TryCast<UNavigationPoint>(searcher->MoveTarget());
			if (target && std::abs(target->Location().z - searcher->Location().z) < target->CollisionHeight() + searcher->CollisionHeight())
			{
				float dx = target->Location().x - searcher->Location().x;
				float dy = target->Location().y - searcher->Location().y;
				float radiusSquared = searcher->CollisionRadius() * searcher->CollisionRadius();
				if (searcher->bIsPlayer() && UObject::TryCast<UInventorySpot>(target))
					radiusSquared += radiusSquared;
				if (dx * dx + dy * dy < radiusSquared)
				{
					startAnchor = true;
					Path[0] = target;
					Dist[0] = 0;
					NumPoints = 1;
				}
			}

			for (UNavigationPoint* nav = searcher->Level()->NavigationPointList(); nav; nav = nav->nextNavigationPoint())
			{
				if (clearPaths)
				{
					nav->visitedWeight() = NotVisited;
					nav->nextOrdered() = nullptr;
					nav->prevOrdered() = nullptr;
					nav->bEndPoint() = false;
					if (nav->bSpecialCost())
						nav->cost() = CallEvent(nav, "SpecialCost", { ExpressionValue::ObjectValue(searcher) }).ToInt();
					else
						nav->cost() = nav->ExtraCost();
				}

				if (!startAnchor)
				{
					vec3 d = searcher->Location() - nav->Location();
					int dist = (int)dot(d, d);
					if (dist < MaxPathDistSquared)
						AddPath(nav, dist);
				}

				if (!endAnchor)
				{
					vec3 d = dest - nav->Location();
					int dist = (int)dot(d, d);
					if (dist < MaxPathDistSquared)
						destPoints->AddPath(nav, dist);
				}
			}
		}

		// findStartAnchor: the nearest candidate that the pawn sees and can go to in a straight line. It becomes the
		// first entry. A pawn that stands on that point is anchored. If not, the point is where the search must end.
		bool FindStartAnchor(UPawn* searcher, bool& anchored)
		{
			vec3 viewPoint = searcher->Location();
			viewPoint.z += searcher->BaseEyeHeight();
			while (NumPoints > 0)
			{
				if (searcher->FastTrace(Path[0]->Location(), viewPoint) && searcher->PointReachableDeusEx(Path[0]->Location(), true))
				{
					Dist[0] = (int)std::sqrt((double)Dist[0]);
					if (Dist[0] < std::max((int)searcher->CollisionRadius(), 48) &&
						std::abs(Path[0]->Location().z - searcher->Location().z) < searcher->CollisionHeight())
					{
						anchored = true;
						return true;
					}
					Path[0]->bEndPoint() = true;
					Path[0]->bestPathWeight() = Dist[0];
					return true;
				}
				RemovePath(0);
			}
			return false;
		}

		// checkAnchorPath: can a pawn on the anchor go to the destination in a straight line?
		// When it can, the pawn stays at the anchor, and the caller puts it back.
		bool CheckAnchorPath(UPawn* searcher, const vec3& dest)
		{
			vec3 realLocation = searcher->Location();
			vec3 d = dest - Path[0]->Location();
			if (dot(d, d) < (float)MaxPathDistSquared && searcher->FastTrace(Path[0]->Location(), dest) && searcher->AITestFarMove(Path[0]->Location()))
			{
				if (searcher->PointReachableDeusEx(dest, false))
					return true;
				searcher->Location() = realLocation;
			}
			NumPoints = 1;
			return false;
		}

		// expandAnchor: the pawn stands on the anchor, so each neighbor of the anchor is a point where the search
		// can end. The search adds the distance from the anchor to that neighbor (bestPathWeight).
		// As in the original, the pruned paths are left out when all 16 path slots of the anchor are in use.
		void ExpandAnchor(UPawn* searcher)
		{
			UNavigationPoint* anchor = Path[0];
			anchor->cost() = 1000000; // a way must not go through the anchor
			int moveFlags = searcher->AICalcMoveFlags();
			int radius = (int)searcher->CollisionRadius();
			int height = (int)searcher->CollisionHeight();

			bool pruned = false;
			int i = 0;
			while (i < 16)
			{
				int index = pruned ? anchor->PrunedPaths()[i] : anchor->Paths()[i];
				if (index == -1)
				{
					if (pruned)
						return;
					pruned = true;
					i = 0;
					continue;
				}

				const LevelReachSpec* spec = GetSpec(searcher, index);
				if (spec && Supports(*spec, radius, height, moveFlags) && !DoorBlocksPath(searcher, spec->startActor->Location(), spec->endActor->Location()))
				{
					spec->endActor->bEndPoint() = true;
					spec->endActor->bestPathWeight() = spec->distance;
				}
				i++;
			}
		}

		// The last step of a search for a pawn that does not stand on a navigation point. The search gave the start
		// anchor, which is the nearest point. A different candidate can give a shorter way. Such a candidate must
		// be at about the height of the pawn. A candidate in the direction of the result must also be shorter by
		// 15 percent, or by 150 units when that is less. The pawn takes the best of these candidates that it sees
		// and can go to in a straight line.
		void FindAltStart(UPawn* searcher, UActor*& bestPath)
		{
			SortedPathList altPoints;
			int bestWeight = Path[0]->visitedWeight() + Dist[0];
			for (int i = 1; i < NumPoints; i++)
			{
				UNavigationPoint* node = Path[i];
				int weight = node->visitedWeight() + (int)std::sqrt((double)Dist[i]);
				if (weight >= bestWeight)
					continue;
				if (!(std::abs(node->Location().z - searcher->Location().z) < 120.0f))
					continue;
				if (dot(node->Location() - searcher->Location(), bestPath->Location() - searcher->Location()) >= 0.0f)
				{
					int limit = std::max((int)(bestWeight * 0.85), bestWeight - 150);
					if (weight >= limit)
						continue;
				}
				altPoints.AddPath(node, weight);
			}

			vec3 viewPoint = searcher->Location();
			viewPoint.z += searcher->BaseEyeHeight();
			for (int i = 0; i < altPoints.NumPoints; i++)
			{
				UNavigationPoint* node = altPoints.Path[i];
				if (searcher->FastTrace(node->Location(), viewPoint) && searcher->PointReachableDeusEx(node->Location(), true))
				{
					bestPath = node;
					return;
				}
			}
		}
	};

	// APawn::breadthPathFrom: go from the start along the reach specs that end at each point, in the order of the
	// cost, until the first point that is marked as an end point. visitedWeight is the cost from the start to a
	// point. previousPath of a point is the point that the search came from, so the chain of previousPath goes from
	// the result back to the start. The points that wait for their turn are a list in the order of their cost
	// (nextOrdered, prevOrdered). binTree is a point near the middle of that list, where the search for the place
	// of a new point starts.
	bool BreadthPathFrom(UPawn* pawn, UNavigationPoint* start, UActor*& bestPath, bool singlePath, int moveFlags)
	{
		UNavigationPoint* currentNode = start;
		UNavigationPoint* binTree = start;
		int radius = (int)pawn->CollisionRadius();
		int height = (int)pawn->CollisionHeight();
		int n = 0;
		int p = 0;
		int realSplit = 1;

		while (currentNode)
		{
			if (currentNode->bEndPoint())
			{
				start->previousPath() = nullptr;
				bestPath = currentNode;
				return true;
			}

			if (!currentNode->bPlayerOnly() || pawn->bIsPlayer() || currentNode == start)
			{
				for (int i = 0; i < 16; i++)
				{
					int index = currentNode->upstreamPaths()[i];
					if (index == -1)
						break;
					const LevelReachSpec* spec = GetSpec(pawn, index);
					if (!spec || !Supports(*spec, radius, height, moveFlags))
						continue;

					UNavigationPoint* nextNode = spec->startActor;
					int newVisit = nextNode->cost() + spec->distance + currentNode->visitedWeight();
					if (nextNode->bEndPoint())
						newVisit += nextNode->bestPathWeight();
					if (nextNode->visitedWeight() <= newVisit)
						continue;

					// A shorter way to nextNode. Take the point out of the list when it is in it.
					if (nextNode->prevOrdered())
					{
						nextNode->prevOrdered()->nextOrdered() = nextNode->nextOrdered();
						if (nextNode->nextOrdered())
							nextNode->nextOrdered()->prevOrdered() = nextNode->prevOrdered();
						if (binTree == nextNode)
						{
							if (nextNode->prevOrdered()->visitedWeight() > newVisit)
								binTree = nextNode->prevOrdered();
						}
						else if (nextNode->visitedWeight() > binTree->visitedWeight() && newVisit < binTree->visitedWeight())
						{
							realSplit--;
						}
					}
					else if (newVisit > binTree->visitedWeight())
					{
						realSplit++;
					}
					else
					{
						realSplit--;
					}

					nextNode->previousPath() = currentNode;
					nextNode->visitedWeight() = newVisit;

					// Put the point into the list at the place of its cost
					UNavigationPoint* nextCheck = binTree->visitedWeight() < newVisit ? binTree : currentNode;
					int numChecks = 0;
					while (nextCheck->nextOrdered() && nextCheck->nextOrdered()->visitedWeight() < nextNode->visitedWeight())
					{
						numChecks++;
						if (numChecks > 500)
						{
							LogMessage("Path search: the ordered list of " + start->Name.ToString() + " has no end");
							return false;
						}
						nextCheck = nextCheck->nextOrdered();
					}
					if (nextCheck->nextOrdered() != nextNode)
					{
						if (nextCheck->nextOrdered())
							nextCheck->nextOrdered()->prevOrdered() = nextNode;
						nextNode->nextOrdered() = nextCheck->nextOrdered();
						nextCheck->nextOrdered() = nextNode;
						nextNode->prevOrdered() = nextCheck;
					}
				}

				realSplit++;
				int move = (int)(realSplit * 0.5);
				while (p < move)
				{
					p++;
					if (binTree->nextOrdered())
						binTree = binTree->nextOrdered();
				}
			}

			n++;
			if (singlePath && n > 4)
				return false;
			if (n > 1000)
			{
				LogMessage("Path search: 1000 navigation points searched from " + start->Name.ToString());
				return false;
			}
			currentNode = currentNode->nextOrdered();
		}
		return false;
	}

	// Turn the chain of previousPath around, so that it goes from its old end to the given point
	void ReversePath(UNavigationPoint* node)
	{
		UNavigationPoint* prev = nullptr;
		while (node->previousPath())
		{
			UNavigationPoint* next = node->previousPath();
			node->previousPath() = prev;
			prev = node;
			node = next;
		}
		node->previousPath() = prev;
	}

	// APawn::findPathTo: the navigation point that the pawn must go to next on its way to a place
	bool SearchPathTo(UPawn* pawn, const vec3& dest, bool singlePath, UActor*& bestPath, bool clearPaths, SearchTrace* trace);

	// APawn::findPathToward: the navigation point that the pawn must go to next on its way to an actor
	bool SearchPathToward(UPawn* pawn, UActor* goal, bool singlePath, UActor*& bestPath, bool clearPaths, SearchTrace* trace)
	{
		bestPath = nullptr;
		if (!goal || !pawn->Level()->NavigationPointList() || pawn->XLevel()->ReachSpecs.empty())
			return false;

		// The way to a pawn that falls goes to the place where that pawn comes down
		if (pawn->Physics() != PHYS_Flying)
		{
			UPawn* goalPawn = UObject::TryCast<UPawn>(goal);
			if (goalPawn && goalPawn->Physics() == PHYS_Falling)
				return SearchPathTo(pawn, goalPawn->AIJumpLanding(goalPawn->Velocity(), false), singlePath, bestPath, clearPaths, trace);
		}

		SortedPathList startPoints, endPoints;
		bool startAnchor = false;
		bool endAnchor = false;
		vec3 realLocation = pawn->Location();
		vec3 dest = goal->Location();

		UNavigationPoint* goalNav = UObject::TryCast<UNavigationPoint>(goal);
		if (goalNav)
		{
			endPoints.Path[0] = goalNav;
			endPoints.Dist[0] = 0;
			endPoints.NumPoints = 1;
			endAnchor = true;
		}

		startPoints.FindVisiblePaths(pawn, dest, &endPoints, clearPaths, startAnchor, endAnchor);
		if (startPoints.NumPoints == 0 || endPoints.NumPoints == 0)
			return false;

		if (!startAnchor && !startPoints.FindStartAnchor(pawn, startAnchor))
		{
			pawn->Location() = realLocation;
			return false;
		}
		if (trace)
		{
			trace->Start = startPoints.Path[0];
			trace->Anchored = startAnchor;
		}

		if (startAnchor)
		{
			// The pawn stands on a navigation point. Look for a direct way from that point to the goal first.
			if ((goalNav && pawn->AICanMoveTo(startPoints.Path[0], goalNav)) || startPoints.CheckAnchorPath(pawn, dest))
			{
				bestPath = goalNav ? static_cast<UActor*>(goalNav) : startPoints.Path[0];
				pawn->Location() = realLocation;
				return true;
			}
			startPoints.ExpandAnchor(pawn);
		}

		if (!endAnchor)
		{
			// The nearest navigation point from which a pawn can go to the goal in a straight line
			for (int i = 0; i < endPoints.NumPoints; i++)
			{
				UNavigationPoint* node = endPoints.Path[i];
				if (pawn->FastTrace(dest, node->Location()) && pawn->AITestFarMove(node->Location()) && pawn->ActorReachableDeusEx(goal, true))
				{
					endAnchor = true;
					endPoints.Path[0] = node;
					endPoints.Dist[0] = (int)std::sqrt((double)endPoints.Dist[i]);
					break;
				}
			}

			// A pawn that hunts takes the navigation point that is nearest to the goal
			if (!endAnchor && pawn->bHunting())
				endAnchor = true;
		}

		if (endAnchor)
		{
			UActor* newPath = nullptr;
			endPoints.Path[0]->visitedWeight() = endPoints.Dist[0];
			bool found = BreadthPathFrom(pawn, endPoints.Path[0], newPath, singlePath, pawn->AICalcMoveFlags());
			pawn->Location() = realLocation;
			if (!found)
				return false;
			bestPath = newPath;
			if (!startAnchor && !singlePath)
				startPoints.FindAltStart(pawn, bestPath);
			return true;
		}

		// No navigation point has a direct way to the goal. Search from the start to a point near the goal that the
		// pawn sees and can go to, or to a neighbor of the point that is nearest to the goal.
		startPoints.FindVisiblePaths(pawn, dest, &endPoints, clearPaths, startAnchor, endAnchor);
		endPoints.ExpandAnchor(pawn);
		if (!endAnchor && !endPoints.FindStartAnchor(pawn, endAnchor))
		{
			pawn->Location() = realLocation;
			return false;
		}

		UActor* newPath = nullptr;
		startPoints.Path[0]->visitedWeight() = startPoints.Dist[0];
		bool found = BreadthPathFrom(pawn, startPoints.Path[0], newPath, singlePath, pawn->AICalcMoveFlags());
		pawn->Location() = realLocation;
		if (!found)
			return false;

		// The point that the search found must not be farther away from the goal than the pawn
		UNavigationPoint* endNode = static_cast<UNavigationPoint*>(newPath);
		vec3 fromNode = dest - endNode->Location();
		vec3 fromPawn = dest - pawn->Location();
		if (dot(fromNode, fromNode) > dot(fromPawn, fromPawn))
			return false;

		// The chain of previousPath goes from the found point back to the start. Turn it around and take its start.
		UNavigationPoint* first = endNode;
		while (first->previousPath())
			first = first->previousPath();
		ReversePath(endNode);
		bestPath = first;

		bool onAnchor = startAnchor && startPoints.Path[0] == first;
		UNavigationPoint* next = first->previousPath();
		if (!next)
			return !onAnchor;

		// Go to the second point of the way directly when the pawn sees it and can go to it
		if (onAnchor || std::abs(next->Location().z - pawn->Location().z) < 120.0f)
		{
			vec3 viewPoint = pawn->Location();
			viewPoint.z += pawn->BaseEyeHeight();
			if (pawn->FastTrace(next->Location(), viewPoint) && pawn->PointReachableDeusEx(next->Location(), true))
				bestPath = next;
			pawn->Location() = realLocation;
		}
		return true;
	}

	bool SearchPathTo(UPawn* pawn, const vec3& dest, bool singlePath, UActor*& bestPath, bool clearPaths, SearchTrace* trace)
	{
		bestPath = nullptr;
		if (!pawn->Level()->NavigationPointList() || pawn->XLevel()->ReachSpecs.empty())
			return false;

		SortedPathList startPoints, endPoints;
		bool startAnchor = false;
		bool endAnchor = false;
		vec3 realLocation = pawn->Location();

		startPoints.FindVisiblePaths(pawn, dest, &endPoints, clearPaths, startAnchor, endAnchor);
		if (startPoints.NumPoints == 0 || endPoints.NumPoints == 0)
			return false;

		if (!startAnchor && !startPoints.FindStartAnchor(pawn, startAnchor))
		{
			pawn->Location() = realLocation;
			return false;
		}
		if (trace)
		{
			trace->Start = startPoints.Path[0];
			trace->Anchored = startAnchor;
		}

		if (startAnchor)
		{
			// The pawn stands on a navigation point and can go from it to the place in a straight line. The result
			// is that navigation point.
			if (startPoints.CheckAnchorPath(pawn, dest))
			{
				bestPath = startPoints.Path[0];
				pawn->Location() = realLocation;
				return true;
			}
			startPoints.ExpandAnchor(pawn);
		}

		// The nearest navigation point from which a pawn can go to the place in a straight line
		for (int i = 0; i < endPoints.NumPoints; i++)
		{
			UNavigationPoint* node = endPoints.Path[i];
			if (pawn->FastTrace(dest, node->Location()) && pawn->AITestFarMove(node->Location()) && pawn->PointReachableDeusEx(dest, true))
			{
				endAnchor = true;
				endPoints.Path[0] = node;
				endPoints.Dist[0] = (int)std::sqrt((double)endPoints.Dist[i]);
				break;
			}
		}

		bool found = false;
		if (endAnchor)
		{
			UActor* newPath = nullptr;
			endPoints.Path[0]->visitedWeight() = endPoints.Dist[0];
			found = BreadthPathFrom(pawn, endPoints.Path[0], newPath, singlePath, pawn->AICalcMoveFlags());
			if (found)
				bestPath = newPath;
		}
		pawn->Location() = realLocation;
		if (found && !startAnchor && !singlePath)
			startPoints.FindAltStart(pawn, bestPath);
		return found;
	}

	// APawn::HandleSpecial: a navigation point with a SpecialHandling function (a lift, a teleporter, a warp zone)
	// can send the pawn to a different actor first
	void HandleSpecial(UPawn* pawn, UActor*& bestPath)
	{
		UActor* realPath = bestPath;
		UActor* newPath = UObject::TryCast<UActor>(CallEvent(bestPath, EventName::SpecialHandling, { ExpressionValue::ObjectValue(pawn) }).ToObject());
		bestPath = newPath;
		if (!newPath || newPath == realPath)
			return;

		if (pawn->bCanDoSpecial())
		{
			pawn->SpecialGoal() = newPath;
			if (!pawn->ActorReachableDeusEx(bestPath, false))
			{
				if (SearchPathToward(pawn, bestPath, false, newPath, true, nullptr) && newPath != realPath)
				{
					pawn->SpecialGoal() = bestPath;
					bestPath = newPath;
					return;
				}
			}
			else
			{
				if (!bestPath->IsEventEnabled(EventName::SpecialHandling))
					return;
				UActor* another = UObject::TryCast<UActor>(CallEvent(bestPath, EventName::SpecialHandling, { ExpressionValue::ObjectValue(pawn) }).ToObject());
				if (another)
				{
					if (another == bestPath)
						return;
					if (pawn->ActorReachableDeusEx(another, false))
					{
						bestPath = another;
						return;
					}
				}
			}
		}
		bestPath = nullptr;
	}

	std::string PlaceText(const vec3& place)
	{
		char buf[80];
		snprintf(buf, sizeof(buf), "(%.0f %.0f %.0f)", place.x, place.y, place.z);
		return buf;
	}

	// --tracepath=<name> of the test harness: one log line for each search of the pawn with that name.
	// "on" is the navigation point that the pawn stands on. "from" is the nearest point that it can go to, when it
	// stands on no point. "next" is the result before the special handling.
	void LogSearch(UPawn* pawn, const std::string& goal, const SearchTrace& trace, UActor* bestPath)
	{
		if (!pawn->AITracePath())
			return;
		std::string name = pawn->Name.ToString();
		std::string text = "Harness: path " + name + " at=" + PlaceText(pawn->Location()) + " goal=" + goal;
		if (trace.Start)
			text += (trace.Anchored ? " on=" : " from=") + trace.Start->Name.ToString();
		text += " next=" + (bestPath ? bestPath->Name.ToString() : std::string("None"));
		LogMessage(text);
	}

	// The part of execFindPathToward and execFindPathTo after the search
	UObject* FinishSearch(UPawn* pawn, UActor* bestPath)
	{
		pawn->SpecialPause() = 0.0f;
		pawn->bShootSpecial() = false;
		if (bestPath && bestPath->IsEventEnabled(EventName::SpecialHandling))
			HandleSpecial(pawn, bestPath);
		if (bestPath == pawn->SpecialGoal())
			pawn->SpecialGoal() = nullptr;
		return bestPath;
	}
}

// --tracepath=<name> of the test harness: is this the pawn that the option names?
bool UPawn::AITracePath()
{
	const std::string& name = Harness::Get().TracePath;
	return !name.empty() && Name == NameString(name);
}

// APawn::CanMoveTo: has the anchor a reach spec to the navigation point that this pawn can use?
bool UPawn::AICanMoveTo(UNavigationPoint* anchor, UNavigationPoint* dest)
{
	int moveFlags = AICalcMoveFlags();
	int radius = (int)CollisionRadius();
	int height = (int)CollisionHeight();
	for (int pass = 0; pass < 2; pass++)
	{
		for (int i = 0; i < 16; i++)
		{
			int index = pass == 0 ? anchor->Paths()[i] : anchor->PrunedPaths()[i];
			if (index == -1)
				break;
			const LevelReachSpec* spec = GetSpec(this, index);
			if (spec && spec->endActor == dest && Supports(*spec, radius, height, moveFlags) && !DoorBlocksPath(this, anchor->Location(), dest->Location()))
				return true;
		}
	}
	return false;
}

UObject* UPawn::FindPathTowardDeusEx(UActor* goal, bool singlePath, bool clearPaths)
{
	if (!goal)
		return nullptr;
	SearchTrace trace;
	UActor* bestPath = nullptr;
	if (!SearchPathToward(this, goal, singlePath, bestPath, clearPaths, &trace))
		bestPath = nullptr;
	LogSearch(this, goal->Name.ToString(), trace, bestPath);
	return FinishSearch(this, bestPath);
}

UObject* UPawn::FindPathToDeusEx(const vec3& dest, bool singlePath, bool clearPaths)
{
	SearchTrace trace;
	UActor* bestPath = nullptr;
	if (!SearchPathTo(this, dest, singlePath, bestPath, clearPaths, &trace))
		bestPath = nullptr;
	LogSearch(this, PlaceText(dest), trace, bestPath);
	return FinishSearch(this, bestPath);
}
