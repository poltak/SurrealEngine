
#include "Precomp.h"
#include "VisibleFrame.h"
#include "VisibleCorona.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/UWarpZoneInfo.h"
#include "Packages/Engine/Actors/Info/USkyZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Extension/Windows/TabGroup/URootWindow.h"

void VisibleFrame::Process(const vec3& location, const mat4& worldToView, const Coords& viewRotation, bool mirrorFlag, int portalDepth, const Array<PortalSpan>& portalSpans, const vec4& portalPlane)
{
	engine->render->Stats.Frames++;

	Device = engine->render->Device;
	FrameCounter = engine->render->FrameCounter++;
	MirrorFlag = mirrorFlag;
	PortalDepth = portalDepth;

	SetupSceneFrame(worldToView);

	Clipper.numDrawSpans = 0;
	Clipper.numSurfs = 0;
	Clipper.numTris = 0;
	WorldToProjection = Frame.Projection * Frame.WorldToView * Frame.ObjectToWorld;
	Clipper.Setup(WorldToProjection, portalSpans, portalPlane);

	ViewLocation = vec4(location, 1.0f);
	ViewZone = FindZoneAt(location);
	//ViewZoneMask = ViewZone ? 1ULL << ViewZone : -1;
	ViewRotation = viewRotation;

	OpaqueNodes.clear();
	Actors.clear();
	Translucents.clear();
	Coronas.clear();
	Portals.clear();
	ActorPieces.clear();

	ProcessNode(&engine->Level->Model->Nodes[0]);
}

// Deus Ex: the render time of an actor, as Render.dll (build 1112fm) gives it.
//
// The scripts and AActor::InStasis read LastRenderTime to find out if the player saw an actor a short time ago. A
// person that the player did not see does not look for enemies, and a thing in stasis does not tick. The original
// sets the time in URender::DrawActorSprite, so only for an actor that it draws, and it draws an actor only when a
// part of it is free of the level geometry in front of it:
//
// - URender::SetupDynamics and FDynamicSprite::Setup take an actor with a mesh or a sprite whose place is not behind
//   the camera plane. The sprite of the actor is the rectangle on the screen that has the box of the actor in it, at
//   the depth of the actor.
// - FDynamicChunk::Filter cuts this rectangle with the planes of the BSP nodes into one part for each leaf. A part
//   in solid space goes away.
// - When the traversal from front to back comes to a leaf, FDynamicFinalChunk::PreRender compares the parts of that
//   leaf with the spans that the nearer surfaces left free. The actor is drawn when a part has a free span.
//
// This function makes the parts of one actor. TestActorPieces compares them when ProcessNode comes to their leaf.
// The traversal is at the node of the actor at this time, and all leaves with a part are below that node.
void VisibleFrame::AddRenderTimeTest(UActor* actor)
{
	BspNode* node = actor->BspInfo.Node;
	if (!node)
		return;

	EDrawType dt = (EDrawType)actor->DrawType();
	bool hasMesh = dt == DT_Mesh && actor->Mesh();
	bool hasSprite = (dt == DT_Sprite || dt == DT_SpriteAnimOnce) && actor->Texture();
	if (!hasMesh && !hasSprite)
		return;

	// The rows of the matrix give the screen axes: clip x, clip y, and the depth
	const mat4& m = WorldToProjection;
	vec3 rowX(m[0 + 0 * 4], m[0 + 1 * 4], m[0 + 2 * 4]);
	vec3 rowY(m[1 + 0 * 4], m[1 + 1 * 4], m[1 + 2 * 4]);
	vec3 rowW(m[3 + 0 * 4], m[3 + 1 * 4], m[3 + 2 * 4]);

	vec4 center = m * vec4(actor->Location(), 1.0f);
	float depth = center.w;
	if (depth < 0.0f)
		return;

	// The rectangle on the screen, from -1 to 1 in each direction
	const BBox& bbox = actor->BspInfo.BoundingBox;
	vec2 rectMin(1.0f), rectMax(-1.0f);
	bool first = true;
	for (int i = 0; i < 8; i++)
	{
		vec4 corner = m * vec4((i & 1) ? bbox.max.x : bbox.min.x, (i & 2) ? bbox.max.y : bbox.min.y, (i & 4) ? bbox.max.z : bbox.min.z, 1.0f);
		if (corner.w < 1.0f)
		{
			// The box comes through the camera plane, so the actor is at the place of the player
			actor->LastRenderTime() = engine->LevelInfo->TimeSeconds();
			return;
		}
		vec2 pos(corner.x / corner.w, corner.y / corner.w);
		if (first)
		{
			rectMin = pos;
			rectMax = pos;
			first = false;
		}
		else
		{
			rectMin.x = std::min(rectMin.x, pos.x);
			rectMin.y = std::min(rectMin.y, pos.y);
			rectMax.x = std::max(rectMax.x, pos.x);
			rectMax.y = std::max(rectMax.y, pos.y);
		}
	}
	rectMin.x = std::max(rectMin.x, -1.0f);
	rectMin.y = std::max(rectMin.y, -1.0f);
	rectMax.x = std::min(rectMax.x, 1.0f);
	rectMax.y = std::min(rectMax.y, 1.0f);
	if (rectMin.x >= rectMax.x || rectMin.y >= rectMax.y)
		return;

	// The rectangle as a polygon in the level, at the depth of the actor
	float z = std::max(depth, 1.0f);
	vec3 base = actor->Location() + rowW * ((z - depth) / dot(rowW, rowW));
	vec3 stepX = rowX * (1.0f / dot(rowX, rowX));
	vec3 stepY = rowY * (1.0f / dot(rowY, rowY));
	auto point = [&](float x, float y) { return base + stepX * (x * z - center.x) + stepY * (y * z - center.y); };

	ActorPiece piece;
	piece.Actor = actor;
	piece.Count = 4;
	piece.Points[0] = point(rectMin.x, rectMin.y);
	piece.Points[1] = point(rectMax.x, rectMin.y);
	piece.Points[2] = point(rectMax.x, rectMax.y);
	piece.Points[3] = point(rectMin.x, rectMax.y);
	ActorPieceBudget = MaxActorPieces;
	FilterActorPiece(piece, node, actor->BspInfo.Outside);
}

// As FDynamicChunk::Filter: send a part down the BSP tree. A plane that goes through the part cuts it in two parts.
// A part that comes to a side of a node with no child node is in a leaf. It stays if the leaf is open space.
void VisibleFrame::FilterActorPiece(ActorPiece piece, BspNode* node, bool outside)
{
	BspNode* nodes = engine->Level->Model->Nodes.data();
	while (ActorPieceBudget >= 0)
	{
		float dist[ActorPiece::MaxPoints];
		int frontCount = 0, backCount = 0;
		for (int i = 0; i < piece.Count; i++)
		{
			const vec3& p = piece.Points[i];
			dist[i] = p.x * node->PlaneX + p.y * node->PlaneY + p.z * node->PlaneZ - node->PlaneW;
			if (dist[i] > 0.01f)
				frontCount++;
			else if (dist[i] < -0.01f)
				backCount++;
		}

		bool csg = node->IsCsg();

		if (frontCount != 0 && backCount != 0)
		{
			// The part of the polygon at the back of the plane goes its own way
			ActorPiece back;
			back.Actor = piece.Actor;
			ActorPiece front;
			front.Actor = piece.Actor;
			for (int i = 0; i < piece.Count; i++)
			{
				int j = (i + 1) % piece.Count;
				if (dist[i] >= 0.0f && front.Count < ActorPiece::MaxPoints)
					front.Points[front.Count++] = piece.Points[i];
				if (dist[i] <= 0.0f && back.Count < ActorPiece::MaxPoints)
					back.Points[back.Count++] = piece.Points[i];
				if ((dist[i] > 0.0f && dist[j] < 0.0f) || (dist[i] < 0.0f && dist[j] > 0.0f))
				{
					vec3 cut = piece.Points[i] + (piece.Points[j] - piece.Points[i]) * (dist[i] / (dist[i] - dist[j]));
					if (front.Count < ActorPiece::MaxPoints)
						front.Points[front.Count++] = cut;
					if (back.Count < ActorPiece::MaxPoints)
						back.Points[back.Count++] = cut;
				}
			}

			if (back.Count >= 3)
			{
				if (node->Back >= 0)
					FilterActorPiece(back, nodes + node->Back, outside && !csg);
				else if (outside && !csg)
					AddActorPiece(back, node, 0);
			}

			if (front.Count < 3)
				return;
			piece = front;
		}
		else if (frontCount == 0)
		{
			// All of the part is at the back of the plane. A part that lies in the plane has no leaf.
			if (backCount == 0)
				return;
			if (node->Back < 0)
			{
				if (outside && !csg)
					AddActorPiece(piece, node, 0);
				return;
			}
			outside = outside && !csg;
			node = nodes + node->Back;
			continue;
		}

		if (node->Front < 0)
		{
			if (outside || csg)
				AddActorPiece(piece, node, 1);
			return;
		}
		outside = outside || csg;
		node = nodes + node->Front;
	}
}

void VisibleFrame::AddActorPiece(const ActorPiece& piece, BspNode* node, int side)
{
	// A very large actor is in very many leaves. It counts as drawn with no more work, as before this test was here.
	if (--ActorPieceBudget < 0)
	{
		piece.Actor->LastRenderTime() = engine->LevelInfo->TimeSeconds();
		return;
	}

	if (node->PieceFrame != FrameCounter)
	{
		node->PieceFrame = FrameCounter;
		node->PieceHead[0] = -1;
		node->PieceHead[1] = -1;
	}

	int index = (int)ActorPieces.size();
	ActorPieces.push_back(piece);
	ActorPieces.back().Next = node->PieceHead[side];
	node->PieceHead[side] = index;
}

// As FDynamicFinalChunk::PreRender and URender::DrawActorSprite: the traversal is at a leaf. An actor with a part in
// this leaf that has a free span is drawn in this frame.
void VisibleFrame::TestActorPieces(BspNode* node, int side)
{
	if (node->PieceFrame != FrameCounter)
		return;

	float now = engine->LevelInfo->TimeSeconds();
	for (int index = node->PieceHead[side]; index >= 0; index = ActorPieces[index].Next)
	{
		const ActorPiece& piece = ActorPieces[index];
		if (piece.Actor->LastRenderTime() != now && Clipper.CheckSurface(piece.Points, piece.Count, false))
			piece.Actor->LastRenderTime() = now;
	}
}

void VisibleFrame::SetupSceneFrame(const mat4& worldToView)
{
	Frame.XB = engine->viewport->ViewportX();
	Frame.YB = engine->viewport->ViewportY();
	Frame.X = engine->viewport->ViewportWidth();
	Frame.Y = engine->viewport->ViewportHeight();
	Frame.FX = (float)engine->viewport->ViewportWidth();
	Frame.FY = (float)engine->viewport->ViewportHeight();

	if (engine->dxRootWindow && engine->dxRootWindow->RenderViewportSet)
	{
		float virtscale = engine->dxRootWindow->GetVirtualScale();
		float x = engine->dxRootWindow->renderX();
		float y = engine->dxRootWindow->renderY();
		float w = engine->dxRootWindow->renderWidth();
		float h = engine->dxRootWindow->renderHeight();

		// Center, scale and possibly extend viewport if it covers the entire root window
		if (x <= 0.0f && w >= engine->dxRootWindow->Width())
		{
			x = 0.0f;
			w = (float)engine->viewport->ViewportWidth();
		}
		else
		{
			x = (engine->dxRootWindow->UsedX + x) * virtscale;
			w *= virtscale;
		}
		if (y <= 0.0f && h >= engine->dxRootWindow->Height())
		{
			y = 0.0f;
			h = (float)engine->viewport->ViewportHeight();
		}
		else
		{
			y = (engine->dxRootWindow->UsedY + y) * virtscale;
			h *= virtscale;
		}

		Frame.X = (int)w;
		Frame.Y = (int)h;
		Frame.XB = (int)std::round(x);
		Frame.YB = (int)std::round(y);
		Frame.FX = w;
		Frame.FY = h;
	}

	Frame.FX2 = Frame.FX * 0.5f;
	Frame.FY2 = Frame.FY * 0.5f;
	Frame.ObjectToWorld = mat4::identity();
	Frame.WorldToView = worldToView;
	Frame.FovAngle = engine->CameraFovAngle;
	float Aspect = Frame.FY / Frame.FX;
	float RProjZ = (float)std::tan(radians(Frame.FovAngle) * 0.5f);
	float RFX2 = 2.0f * RProjZ / Frame.FX;
	float RFY2 = 2.0f * RProjZ * Aspect / Frame.FY;
	Frame.Projection = mat4::frustum(-RProjZ, RProjZ, -Aspect * RProjZ, Aspect * RProjZ, 1.0f, 32768.0f, handedness::left, clipzrange::zero_positive_w);
}

void VisibleFrame::ProcessNode(BspNode* node)
{
	// Skip node if it is not part of the portal zones we have seen so far
	//if ((node->ZoneMask & ViewZoneMask) == 0)
	//	return;

	// Skip node if its AABB is not visible
	if (node->RenderBound != -1 && !Clipper.IsAABBVisible(engine->Level->Model->Bounds[node->RenderBound]))
	{
		return;
	}

	// Add bsp node actors to the visible set
	for (UActor* actor = node->ActorList; actor != nullptr; actor = actor->BspInfo.Next)
	{
		if (actor->LastDrawFrame != FrameCounter)
		{
			actor->LastDrawFrame = FrameCounter;

			VisibleActor visactor;
			visactor.Process(this, actor);
		}
	}

	// Decide which side the plane the camera is
	vec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
	bool swapFrontAndBack = dot(ViewLocation, plane) < 0.0f;
	int back = node->Back;
	int front = node->Front;
	if (swapFrontAndBack)
		std::swap(front, back);

	// Recursively divide front space (toward the viewer)
	if (front >= 0)
	{
		ProcessNode(&engine->Level->Model->Nodes[front]);
	}
	else
	{
		// The leaf on the near side of the plane
		TestActorPieces(node, swapFrontAndBack ? 0 : 1);
	}

	// Draw surfaces on this plane
	BspNode* polynode = node;
	while (true)
	{
		ProcessNodeSurface(polynode, swapFrontAndBack);

		if (polynode->Plane < 0) break;
		polynode = &engine->Level->Model->Nodes[polynode->Plane];
	}

	// Possibly divide back space (away from the viewer)
	if (back >= 0)
	{
		ProcessNode(&engine->Level->Model->Nodes[back]);
	}
	else
	{
		// The leaf on the far side of the plane
		TestActorPieces(node, swapFrontAndBack ? 1 : 0);
	}
}

void VisibleFrame::ProcessNodeSurface(BspNode* node, bool front)
{
	if (node->NumVertices < 3 || node->Surf < 0)
		return;

	UModel* model = engine->Level->Model;
	const BspSurface& surface = model->Surfaces[node->Surf];

	int numverts = node->NumVertices;
	vec3* points = engine->render->GetTempVertexBuffer(numverts);
	BspVert* v = &model->Vertices[node->VertPool];
	if (MirrorFlag)
	{
		for (int j = 0; j < numverts; j++)
		{
			points[numverts - 1 - j] = model->Points[v[j].Vertex];
		}
	}
	else
	{
		for (int j = 0; j < numverts; j++)
		{
			points[j] = model->Points[v[j].Vertex];
		}
	}

	uint32_t PolyFlags = surface.PolyFlags;
	UTexture* texture = surface.Material;
	if (!texture)
		texture = engine->LevelInfo->DefaultTexture();

	if (surface.Material)
		PolyFlags |= surface.Material->PolyFlags();

	VisibleNode info;
	info.Node = node;
	info.Front = front;
	info.PolyFlags = PolyFlags;

	if (PortalDepth < 4)
	{
		if ((PolyFlags & (PF_FakeBackdrop | PF_Invisible)) == PF_FakeBackdrop)
		{
			int zone = front ? node->Zone1 : node->Zone0;
			UZoneInfo* zoneInfo = engine->GetZoneActor(zone);
			if (zoneInfo->SkyZone())
			{
				Array<PortalSpan> spans = Clipper.CheckPortal(points, numverts);
				if (!spans.empty())
				{
					USkyZoneInfo* skyZone = zoneInfo->SkyZone();
					for (auto& p : Portals)
					{
						if (p.SkyZone == skyZone)
						{
							p.Nodes.push_back(info);
							p.Spans.insert(p.Spans.end(), spans.begin(), spans.end());
							return;
						}
					}
					VisiblePortal portal;
					portal.Nodes.push_back(info);
					portal.SkyZone = skyZone;
					portal.Spans = std::move(spans);
					Portals.push_back(std::move(portal));
				}
				return;
			}
		}
		else if (PolyFlags & PF_Portal)
		{
			int portalZone = front ? node->Zone1 : node->Zone0;
			if (portalZone > 0)
			{
				UWarpZoneInfo* warpZone = UObject::TryCast<UWarpZoneInfo>(engine->GetZoneActor(portalZone));
				if (warpZone)
				{
					Array<PortalSpan> spans = Clipper.CheckPortal(points, numverts);
					if (!spans.empty())
					{
						for (auto& p : Portals)
						{
							if (p.WarpZone == warpZone)
							{
								p.Nodes.push_back(info);
								p.Spans.insert(p.Spans.end(), spans.begin(), spans.end());
								return;
							}
						}
						VisiblePortal portal;
						portal.Nodes.push_back(info);
						portal.WarpZone = warpZone;
						portal.Spans = std::move(spans);
						Portals.push_back(std::move(portal));
					}
					return;
				}
			}
		}
		else if (PolyFlags & PF_Mirrored)
		{
			if (PortalDepth > 0) // To do: cull backfacing surfaces so we don't need this hack
				return;

			Array<PortalSpan> spans = Clipper.CheckPortal(points, numverts);
			if (!spans.empty())
			{
				for (auto& p : Portals)
				{
					if (!p.WarpZone && !p.SkyZone) // To do: how do we best merge mirrors using the same plane?
					{
						p.Nodes.push_back(info);
						p.Spans.insert(p.Spans.end(), spans.begin(), spans.end());
						return;
					}
				}

				VisiblePortal portal;
				portal.Nodes.push_back(info);
				portal.Spans = std::move(spans);
				Portals.push_back(std::move(portal));
			}
			return;
		}
	}

	if (!Clipper.CheckSurface(points, numverts, (PolyFlags & PF_NoOcclude) == 0))
		return;

	if (engine->LaunchInfo.IsDeusEx())
	{
		// Deus Ex: AActor::InStasis reads the time at which the zone of the actor was last drawn
		int visibleZone = front ? node->Zone1 : node->Zone0;
		if (visibleZone >= 0 && visibleZone < 64)
			engine->Level->ZoneLastRenderTime[visibleZone] = engine->LevelInfo->TimeSeconds();
	}

	/*if (PolyFlags & PF_Portal)
	{
		ViewZoneMask |= 1ULL << node->Zone0;
		ViewZoneMask |= 1ULL << node->Zone1;
	}*/

	if (PolyFlags & PF_Invisible)
		return;

	if ((PolyFlags & (PF_Translucent | PF_Modulated)) == 0 && (PolyFlags & PF_Occlude) != PF_Occlude)
	{
		OpaqueNodes.push_back(info);
	}
	else
	{
		UModel* model = engine->Level->Model;
		const BspSurface& surface = model->Surfaces[node->Surf];
		vec3 v = model->Points[surface.pBase] - ViewLocation.xyz();
		Translucents.emplace_back(info, dot(v, v));
	}
}

void VisibleFrame::SortTranslucent()
{
	std::sort(Translucents.begin(), Translucents.end(), [](const VisibleTranslucent& a, const VisibleTranslucent& b) { return a.DistSqr < b.DistSqr; });
}

int VisibleFrame::FindZoneAt(const vec3& location)
{
	return FindZoneAt(vec4(location, 1.0f), &engine->Level->Model->Nodes.front(), engine->Level->Model->Nodes.data());
}

int VisibleFrame::FindZoneAt(const vec4& location, BspNode* node, BspNode* nodes)
{
	while (true)
	{
		vec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
		bool swapFrontAndBack = dot(location, plane) < 0.0f;
		int front = node->Front;
		int back = node->Back;
		if (swapFrontAndBack)
			std::swap(front, back);

		if (front >= 0)
		{
			node = nodes + front;
		}
		else
		{
			return swapFrontAndBack ? node->Zone0 : node->Zone1;
		}
	}
}

void VisibleFrame::Draw()
{
	DrawPortals();
	DrawOpaqueNodes();
	DrawOpaqueActors();
	SortTranslucent();
	DrawTranslucent();
}

void VisibleFrame::DrawCoronas()
{
	SceneNode frame2d = Frame;
	frame2d.ObjectToWorld = mat4::identity();
	frame2d.WorldToView = mat4::identity();
	Device->SetSceneNode(&frame2d);

	for (VisibleCorona &corona : Coronas)
		corona.Draw(this);
}

void VisibleFrame::DrawOpaqueNodes()
{
	Device->SetSceneNode(&Frame);
	for (VisibleNode& node : OpaqueNodes)
		node.Draw(this);
}

void VisibleFrame::DrawOpaqueActors()
{
	Device->SetSceneNode(&Frame);
	for (VisibleActor& actor : Actors)
		actor.DrawOpaque(this);
}

void VisibleFrame::DrawTranslucent()
{
	Device->SetSceneNode(&Frame);
	for (VisibleTranslucent& translucent : Translucents)
		translucent.Draw(this);
}

// Sorts the spans by row, then by their left edge.
// A portal can have many thousands of spans (the sky portal of an outdoor map), and std::sort took 5% of the frame time for them.
// The rows are small numbers, so a counting sort puts the spans in row order in linear time. A row has only a few spans.
static void SortPortalSpans(Array<PortalSpan>& spans)
{
	auto compare = [](const PortalSpan& a, const PortalSpan& b) { return a.y != b.y ? a.y < b.y : a.x0 < b.x0; };

	size_t count = spans.size();
	if (count < 2)
		return;

	int64_t minY = spans[0].y;
	int64_t maxY = spans[0].y;
	for (const PortalSpan& span : spans)
	{
		minY = std::min(minY, span.y);
		maxY = std::max(maxY, span.y);
	}

	size_t rows = (size_t)(maxY - minY) + 1;
	if (rows > 16384 || count < 64)
	{
		std::sort(spans.begin(), spans.end(), compare);
		return;
	}

	static std::vector<uint32_t> rowStart;
	static std::vector<uint32_t> rowNext;
	static std::vector<PortalSpan> sorted;

	rowStart.assign(rows + 1, 0);
	for (const PortalSpan& span : spans)
		rowStart[(size_t)(span.y - minY) + 1]++;
	for (size_t i = 0; i < rows; i++)
		rowStart[i + 1] += rowStart[i];

	rowNext.assign(rowStart.begin(), rowStart.end() - 1);
	sorted.resize(count);
	for (const PortalSpan& span : spans)
		sorted[rowNext[(size_t)(span.y - minY)]++] = span;

	for (size_t row = 0; row < rows; row++)
	{
		uint32_t begin = rowStart[row];
		uint32_t end = rowStart[row + 1];
		for (uint32_t i = begin + 1; i < end; i++)
		{
			PortalSpan span = sorted[i];
			uint32_t j = i;
			while (j > begin && sorted[j - 1].x0 > span.x0)
			{
				sorted[j] = sorted[j - 1];
				j--;
			}
			sorted[j] = span;
		}
	}

	for (size_t i = 0; i < count; i++)
		spans[i] = sorted[i];
}

void VisibleFrame::DrawPortals()
{
	for (VisiblePortal& portal : Portals)
	{
		// BspClipper requires the visible spans list to be sorted
		SortPortalSpans(portal.Spans);

		if (portal.SkyZone)
		{
			mat4 skyToView =
				Coords::ViewToRenderDev().ToMatrix() *
				ViewRotation.Inverse().ToMatrix() *
				Coords::Rotation(portal.SkyZone->Rotation()).ToMatrix() *
				Coords::Location(portal.SkyZone->Location()).ToMatrix();

			VisibleFrame skyframe;
			skyframe.Process(portal.SkyZone->Location(), skyToView, ViewRotation * Coords::Rotation(portal.SkyZone->Rotation()), MirrorFlag, PortalDepth + 1, portal.Spans);
			Device->SetSceneNode(&skyframe.Frame);
			skyframe.Draw();
			Device->ClearZ();
		}
		else if (portal.WarpZone)
		{
			// Warp camera
			vec3 newLocation = WarpLocationToOtherSide(portal.WarpZone, ViewLocation.xyz());
			Coords rotation = WarpRotationToOtherSide(portal.WarpZone, ViewRotation);
			mat4 worldToView = Coords::ViewToRenderDev().ToMatrix() * rotation.Inverse().ToMatrix() * mat4::translate(-newLocation);

			// Find clipping plane for portal and warp it
			auto node = portal.Nodes.front().Node;
			vec4 plane = { node->PlaneX, node->PlaneY, node->PlaneZ, -node->PlaneW };
			bool isFrontfacing = dot(ViewLocation, plane) < 0.0f;
			vec3 p = WarpLocationToOtherSide(portal.WarpZone, plane.xyz() * -plane.w);
			vec3 n = WarpNormalToOtherSide(portal.WarpZone, vec3(node->PlaneX, node->PlaneY, node->PlaneZ));
			vec4 portalPlane(n, -dot(p,n));
			if (!isFrontfacing)
				portalPlane = -portalPlane;

			VisibleFrame portalframe;
			portalframe.Process(newLocation, worldToView, rotation, MirrorFlag, PortalDepth + 1, portal.Spans, portalPlane);
			Device->SetSceneNode(&portalframe.Frame);
			portalframe.Draw();
			Device->ClearZ();
		}
		else // Mirror
		{
			UModel* model = engine->Level->Model;
			const BspSurface& surface = model->Surfaces[portal.Nodes.front().Node->Surf];
			vec3 v = model->Points[surface.pBase];
			vec3 n = model->Vectors[surface.vNormal];
			mat4 mirrorRotation = mat4::mirror(n);
			mat4 mirrorToView = Frame.WorldToView * mat4::translate(v) * mirrorRotation * mat4::translate(-v);

			// URender::OccludeBsp of Render.dll makes a child frame for a mirror. That frame shows the level from
			// the mirror image of the view location (FVector::MirrorByPlane), and the plane of the mirror is its
			// near clip plane. This frame does the same. The order of the nodes and the distances come from the
			// mirror image of the view location, and the things behind the mirror are not in the frame.
			float side = dot(ViewLocation.xyz() - v, n);
			vec3 mirrorLocation = ViewLocation.xyz() - n * (2.0f * side);
			vec4 mirrorPlane(n, -dot(v, n));
			if (side < 0.0f)
				mirrorPlane = -mirrorPlane;

			VisibleFrame mirrorframe;
			mirrorframe.Process(mirrorLocation, mirrorToView, ViewRotation * Coords::FromMatrix(mirrorRotation).Inverse(), !MirrorFlag, PortalDepth + 1, portal.Spans, mirrorPlane);
			Device->SetSceneNode(&mirrorframe.Frame);
			mirrorframe.Draw();
			Device->ClearZ();
		}
	}

	// Seal the portals
	Device->SetSceneNode(&Frame);
	for (VisiblePortal& portal : Portals)
	{
		for (const auto& info : portal.Nodes)
		{
			VisibleNode visnode(info);
			visnode.PolyFlags |= PF_Occlude | PF_Invisible;
			visnode.Front = true;
			visnode.Draw(this);

			if (info.PolyFlags & PF_NoOcclude)
			{
				if ((info.PolyFlags & (PF_Translucent | PF_Modulated)) == 0)
				{
					OpaqueNodes.push_back(info);
				}
				else
				{
					UModel* model = engine->Level->Model;
					const BspSurface& surface = model->Surfaces[info.Node->Surf];
					vec3 v = model->Points[surface.pBase] - ViewLocation.xyz();
					Translucents.emplace_back(info, dot(v, v));
				}
			}
		}
	}
}

vec3 VisibleFrame::WarpLocationToOtherSide(UWarpZoneInfo* warpZone, vec3 p)
{
	// Transform to warp space:
	{
		vec3 origin = warpZone->WarpCoords().Origin;
		mat3 rotate = warpZone->WarpCoords().ToMatrix();
		//mat3 invrotate = mat3::transpose(rotate);
		p = rotate * (p - origin);
	}

	// Transform from warp space:
	{
		vec3 origin = warpZone->OtherSideActor()->WarpCoords().Origin;
		mat3 rotate = warpZone->OtherSideActor()->WarpCoords().ToMatrix();
		mat3 invrotate = mat3::transpose(rotate);
		p = (invrotate * p) + origin;
	}

	return p;
}

vec3 VisibleFrame::WarpNormalToOtherSide(UWarpZoneInfo* warpZone, vec3 n)
{
	// Transform to warp space:
	{
		mat3 rotate = warpZone->WarpCoords().ToMatrix();
		mat3 invrotate = mat3::transpose(rotate);
		n = invrotate * n;
	}

	// Transform from warp space:
	{
		mat3 rotate = warpZone->OtherSideActor()->WarpCoords().ToMatrix();
		//mat3 invrotate = mat3::transpose(rotate);
		n = rotate * n;
	}

	return n;
}

Coords VisibleFrame::WarpRotationToOtherSide(UWarpZoneInfo* warpZone, Coords rotation)
{
	mat3 newRotation = rotation.ToMatrix();

	// Transform to warp space:
	{
		vec3 origin = warpZone->WarpCoords().Origin;
		mat3 rotate = warpZone->WarpCoords().ToMatrix();
		//mat3 invrotate = mat3::transpose(rotate);
		newRotation = rotate * newRotation;
	}

	// Transform from warp space:
	{
		vec3 origin = warpZone->OtherSideActor()->WarpCoords().Origin;
		mat3 rotate = warpZone->OtherSideActor()->WarpCoords().ToMatrix();
		mat3 invrotate = mat3::transpose(rotate);
		newRotation = invrotate * newRotation;
	}

	rotation.XAxis = vec3(newRotation[0], newRotation[1], newRotation[2]);
	rotation.YAxis = vec3(newRotation[3], newRotation[4], newRotation[5]);
	rotation.ZAxis = vec3(newRotation[6], newRotation[7], newRotation[8]);
	return rotation;
}
