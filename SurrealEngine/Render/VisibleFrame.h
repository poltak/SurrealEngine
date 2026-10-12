#pragma once

#include "Engine.h"
#include "VisibleNode.h"
#include "VisibleTranslucent.h"
#include "VisibleCorona.h"
#include "VisibleActor.h"
#include "VisiblePortal.h"
#include "BspClipper.h"
#include "RenderDevice/RenderDevice.h"

class VisibleFrame
{
public:
	void Process(const vec3& location, const mat4& worldToView, const Coords& viewRotation, bool mirrorFlag = false, int portalDepth = 0, const Array<PortalSpan>& portalSpans = {}, const vec4& portalPlane = vec4(0.0f, 0.0f, 0.0f, 1.0f));
	void Draw();
	void DrawCoronas();

	// Deus Ex: find out if the player can see the actor in this frame, and give the actor its render time then
	void AddRenderTimeTest(UActor* actor);

	RenderDevice* Device = nullptr;

	SceneNode Frame;
	BspClipper Clipper;
	vec4 ViewLocation = vec4(0.0f);
	Coords ViewRotation = {};
	int ViewZone = 0;
	//uint64_t ViewZoneMask = 0;
	int FrameCounter = 0;
	bool MirrorFlag = false;
	int PortalDepth = 0;

	Array<VisibleNode> OpaqueNodes;
	Array<VisibleActor> Actors;
	Array<VisibleTranslucent> Translucents;
	Array<VisibleCorona> Coronas;
	Array<VisiblePortal> Portals;

private:
	// A part of the screen rectangle of an actor that is in one leaf of the BSP tree
	struct ActorPiece
	{
		enum { MaxPoints = 12 };
		UActor* Actor = nullptr;
		int Next = -1;
		int Count = 0;
		vec3 Points[MaxPoints];
	};

	void FilterActorPiece(ActorPiece piece, BspNode* node, bool outside);
	void AddActorPiece(const ActorPiece& piece, BspNode* node, int side);
	void TestActorPieces(BspNode* node, int side);

	enum { MaxActorPieces = 64 };
	Array<ActorPiece> ActorPieces;
	int ActorPieceBudget = 0;
	mat4 WorldToProjection;

	void SetupSceneFrame(const mat4& worldToView);
	void ProcessNode(BspNode* node);
	void ProcessNodeSurface(BspNode* node, bool front);
	void SortTranslucent();

	void DrawOpaqueNodes();
	void DrawOpaqueActors();
	void DrawTranslucent();
	void DrawPortals();

	int FindZoneAt(const vec3& location);
	int FindZoneAt(const vec4& location, BspNode* node, BspNode* nodes);

	vec3 WarpLocationToOtherSide(UWarpZoneInfo* warpZone, vec3 p);
	vec3 WarpNormalToOtherSide(UWarpZoneInfo* warpZone, vec3 n);
	Coords WarpRotationToOtherSide(UWarpZoneInfo* warpZone, Coords rotation);
};
