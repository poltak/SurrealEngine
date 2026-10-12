
#include "Precomp.h"
#include "VisibleActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "VisibleFrame.h"
#include "VisibleBrush.h"
#include "VisibleMesh.h"
#include "VisibleSprite.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Brush/UMover.h"
#include "Packages/Core/UClass.h"
#include "Packages/DeusEx/ParticleSystem.h"

void VisibleActor::Process(VisibleFrame* frame, UActor* actor)
{
	if (actor->bCorona())
	{
		if (engine->LaunchInfo.IsDeusEx())
		{
			// There seems to be more to this check than simply the distance. But this will do for now.
			vec3 v = actor->Location() - frame->ViewLocation.xyz();
			float d = 2000.0f;
			if (dot(v, v) < d * d)
				frame->Coronas.push_back(actor);
		}
		else
		{
			frame->Coronas.push_back(actor);
		}
	}

	if (engine->LaunchInfo.IsDeusEx())
	{
		// A particle generator has no draw type of its own. Its render iterator gives the sprites to draw.
		if (actor->RenderIteratorClass())
		{
			if (frame->Clipper.IsAABBVisible(actor->BspInfo.BoundingBox) && DeusExParticles::Prepare(frame, actor))
			{
				Type = DT_None;
				Actor = actor;
				Particles = true;
				frame->Actors.push_back(*this);
				Particles = false;
			}
			return;
		}

		// The proxy sprite is drawn for each particle of its generator only
		if (actor->Class && actor->Class->Name == "ParticleProxy")
			return;
	}

	if (actor->bHidden())
		return;

	UActor* viewportActor = engine->viewport->Actor();
	bool isOwnedByViewport = actor->IsOwnedBy(viewportActor);

	bool behindView = frame->PortalDepth > 0;
	if (UPawn* pawn = UObject::TryCast<UPawn>(viewportActor))
	{
		if (pawn->bBehindView())
			behindView = true;
	}

	if ((behindView || !isOwnedByViewport) && actor->bOnlyOwnerSee())
		return;

	if (engine->LaunchInfo.ue1Version > 219)
	{
		if (!behindView && isOwnedByViewport && actor->bOwnerNoSee())
			return;
	}

	if (!behindView && actor == engine->CameraActor) // Hide CameraActor as if bOwnerNoSee was set for it
		return;

	if (!frame->Clipper.IsAABBVisible(actor->BspInfo.BoundingBox))
		return;

	// Deus Ex: the AI uses the time of the last frame in which the player could see the actor. The box test above is
	// too early for that, because the walls below this node are not in the clipper yet.
	if (engine->LaunchInfo.IsDeusEx())
		frame->AddRenderTimeTest(actor);

	EDrawType dt = (EDrawType)actor->DrawType();
	if (dt == DT_Mesh && actor->Mesh())
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
	else if ((dt == DT_Sprite || dt == DT_SpriteAnimOnce) && actor->Texture())
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
	else if (dt == DT_Brush && actor->Brush() && UActor::TryCast<UMover>(actor))
	{
		Type = dt;
		Actor = actor;
		frame->Actors.push_back(*this);
	}
}

void VisibleActor::DrawOpaque(VisibleFrame* frame)
{
	if (Particles)
	{
		vec3 v = Actor->Location() - frame->ViewLocation.xyz();
		frame->Translucents.emplace_back(*this, dot(v, v));
	}
	else if (Type == DT_Mesh)
	{
		VisibleMesh vismesh;
		if (vismesh.DrawMesh(frame, Actor, false, false))
		{
			vec3 v = Actor->Location() - frame->ViewLocation.xyz();
			frame->Translucents.emplace_back(*this, dot(v, v));
		}
	}
	else if (Type == DT_Sprite || Type == DT_SpriteAnimOnce)
	{
		// Assume all sprites are translucent for now
		vec3 v = Actor->Location() - frame->ViewLocation.xyz();
		frame->Translucents.emplace_back(*this, dot(v, v));
	}
	else if (Type == DT_Brush)
	{
		VisibleBrush visbrush;
		if (visbrush.Draw(frame, Actor, false))
		{
			vec3 v = Actor->Location() - frame->ViewLocation.xyz();
			frame->Translucents.emplace_back(*this, dot(v, v));
		}
	}
}

void VisibleActor::DrawTranslucent(VisibleFrame* frame)
{
	if (Particles)
	{
		DeusExParticles::Draw(frame, Actor);
	}
	else if (Type == DT_Mesh)
	{
		VisibleMesh vismesh;
		vismesh.DrawMesh(frame, Actor, false, true);
	}
	else if (Type == DT_Sprite || Type == DT_SpriteAnimOnce)
	{
		VisibleSprite vissprite;
		vissprite.Draw(frame, Actor);
	}
	else if (Type == DT_Brush)
	{
		VisibleBrush visbrush;
		visbrush.Draw(frame, Actor, true);
	}
}
