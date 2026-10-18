
#include "Precomp.h"
#include "ParticleSystem.h"
#include "Engine.h"
#include "Utils/Random.h"
#include "VM/ScriptCall.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Textures/UTexture.h"
#include "Packages/Engine/UViewport.h"
#include "Render/VisibleFrame.h"
#include "Render/VisibleSprite.h"
#include "Render/VisibleMesh.h"
#include "Math/coords.h"
#include <map>

namespace
{
	// Where the fields of the script struct sParticle are in one element of ParticleIterator.Particles
	struct ParticleLayout
	{
		bool Valid = false;
		UProperty* ArrayProp = nullptr;
		UBoolProperty* Active = nullptr;
		size_t InitVel = 0, Velocity = 0, Location = 0, DrawScale = 0, ScaleGlow = 0, LifeSpan = 0;
	};

	ParticleLayout& GetLayout(UObject* iterator)
	{
		static std::map<UClass*, ParticleLayout> layouts;
		ParticleLayout& layout = layouts[iterator->Class];
		if (layout.ArrayProp)
			return layout;

		for (UProperty* prop : iterator->PropertyData.Class->Properties)
		{
			if (prop->Name == "Particles")
				layout.ArrayProp = prop;
		}

		auto structProp = UObject::TryCast<UStructProperty>(layout.ArrayProp);
		if (!structProp || !structProp->Struct)
			return layout;

		int found = 0;
		for (UField* field = structProp->Struct->Children; field; field = field->Next)
		{
			UProperty* prop = UObject::TryCast<UProperty>(field);
			if (!prop)
				continue;
			size_t offset = prop->DataOffset.DataOffset;
			if (prop->Name == "bActive") { layout.Active = UObject::TryCast<UBoolProperty>(prop); found++; }
			else if (prop->Name == "initVel") { layout.InitVel = offset; found++; }
			else if (prop->Name == "Velocity") { layout.Velocity = offset; found++; }
			else if (prop->Name == "Location") { layout.Location = offset; found++; }
			else if (prop->Name == "DrawScale") { layout.DrawScale = offset; found++; }
			else if (prop->Name == "ScaleGlow") { layout.ScaleGlow = offset; found++; }
			else if (prop->Name == "LifeSpan") { layout.LifeSpan = offset; found++; }
		}
		layout.Valid = found == 7 && layout.Active && layout.ArrayProp->ArrayDimension >= DeusExParticles::MaxParticles;
		if (!layout.Valid)
			LogMessage("Deus Ex particles: the struct sParticle does not have the expected fields");
		return layout;
	}

	// One particle of an iterator
	struct Particle
	{
		Particle(UObject* iterator, const ParticleLayout& layout, int index) : layout(layout)
		{
			data = static_cast<uint8_t*>(layout.ArrayProp->GetElement(iterator->PropertyData.Ptr(layout.ArrayProp), index));
		}

		bool Active() const { return layout.Active->GetBool(data + layout.Active->DataOffset.DataOffset); }
		void SetActive(bool value) { layout.Active->SetBool(data + layout.Active->DataOffset.DataOffset, value); }
		vec3& InitVel() { return *reinterpret_cast<vec3*>(data + layout.InitVel); }
		vec3& Velocity() { return *reinterpret_cast<vec3*>(data + layout.Velocity); }
		vec3& Location() { return *reinterpret_cast<vec3*>(data + layout.Location); }
		float& DrawScale() { return *reinterpret_cast<float*>(data + layout.DrawScale); }
		float& ScaleGlow() { return *reinterpret_cast<float*>(data + layout.ScaleGlow); }
		float& LifeSpan() { return *reinterpret_cast<float*>(data + layout.LifeSpan); }

		const ParticleLayout& layout;
		uint8_t* data = nullptr;
	};

	UActor* GetProxy(UObject* iterator)
	{
		return UObject::TryCast<UActor>(iterator->Value<UObject*>(PropOffsets_ParticleIterator.proxy));
	}

	bool IsParticleIterator(UObject* iterator)
	{
		return iterator && iterator->IsA("ParticleIterator");
	}

	float SpriteExtent(UActor* proxy, float drawScale)
	{
		UTexture* texture = proxy ? proxy->Texture() : nullptr;
		if (!texture || texture->UsedMipmaps.empty())
			return 16.0f * drawScale;
		return (float)std::max(texture->UsedMipmaps[0].Width, texture->UsedMipmaps[0].Height) * 0.5f * drawScale;
	}

	// Where the fields of the script struct sBeam are in one element of LaserIterator.Beams
	struct BeamLayout
	{
		bool Valid = false;
		UProperty* ArrayProp = nullptr;
		UBoolProperty* Active = nullptr;
		size_t Location = 0, Rotation = 0, Length = 0, NumSegments = 0;
	};

	const int MaxBeams = 8;

	BeamLayout& GetBeamLayout(UObject* iterator)
	{
		static std::map<UClass*, BeamLayout> layouts;
		BeamLayout& layout = layouts[iterator->Class];
		if (layout.ArrayProp)
			return layout;

		for (UProperty* prop : iterator->PropertyData.Class->Properties)
		{
			if (prop->Name == "Beams")
				layout.ArrayProp = prop;
		}

		auto structProp = UObject::TryCast<UStructProperty>(layout.ArrayProp);
		if (!structProp || !structProp->Struct)
			return layout;

		int found = 0;
		for (UField* field = structProp->Struct->Children; field; field = field->Next)
		{
			UProperty* prop = UObject::TryCast<UProperty>(field);
			if (!prop)
				continue;
			size_t offset = prop->DataOffset.DataOffset;
			if (prop->Name == "bActive") { layout.Active = UObject::TryCast<UBoolProperty>(prop); found++; }
			else if (prop->Name == "Location") { layout.Location = offset; found++; }
			else if (prop->Name == "Rotation") { layout.Rotation = offset; found++; }
			else if (prop->Name == "Length") { layout.Length = offset; found++; }
			else if (prop->Name == "numSegments") { layout.NumSegments = offset; found++; }
		}
		layout.Valid = found == 5 && layout.Active && layout.ArrayProp->ArrayDimension >= MaxBeams;
		if (!layout.Valid)
			LogMessage("Deus Ex lasers: the struct sBeam does not have the expected fields");
		return layout;
	}

	// One beam of a laser iterator
	struct Beam
	{
		Beam(UObject* iterator, const BeamLayout& layout, int index) : layout(layout)
		{
			data = static_cast<uint8_t*>(layout.ArrayProp->GetElement(iterator->PropertyData.Ptr(layout.ArrayProp), index));
		}

		bool Active() const { return layout.Active->GetBool(data + layout.Active->DataOffset.DataOffset); }
		const vec3& Location() const { return *reinterpret_cast<const vec3*>(data + layout.Location); }
		const Rotator& Rotation() const { return *reinterpret_cast<const Rotator*>(data + layout.Rotation); }
		float Length() const { return *reinterpret_cast<const float*>(data + layout.Length); }
		int NumSegments() const { return *reinterpret_cast<const int*>(data + layout.NumSegments); }

		const BeamLayout& layout;
		uint8_t* data = nullptr;
	};

	bool IsLaserIterator(UObject* iterator)
	{
		return iterator && iterator->IsA("LaserIterator");
	}

	UActor* GetLaserProxy(UObject* iterator)
	{
		return UObject::TryCast<UActor>(iterator->Value<UObject*>(PropOffsets_LaserIterator.proxy));
	}

	// The box around the emitter and its beams
	bool GetLaserBounds(UObject* iterator, UActor* emitter, BBox& bbox)
	{
		// The beam mesh is 16 units long, and an electric arc moves its segments up to 2 units to the side
		const float extent = 16.0f;
		bbox.min = emitter->Location() - vec3(extent);
		bbox.max = emitter->Location() + vec3(extent);

		BeamLayout& layout = GetBeamLayout(iterator);
		if (!layout.Valid)
			return true;

		for (int i = 0; i < MaxBeams; i++)
		{
			Beam beam(iterator, layout, i);
			if (!beam.Active())
				continue;
			vec3 start = beam.Location();
			vec3 end = start + Coords::Rotation(beam.Rotation()).XAxis * beam.Length();
			for (const vec3& p : { start, end })
			{
				bbox.min.x = std::min(bbox.min.x, p.x - extent);
				bbox.min.y = std::min(bbox.min.y, p.y - extent);
				bbox.min.z = std::min(bbox.min.z, p.z - extent);
				bbox.max.x = std::max(bbox.max.x, p.x + extent);
				bbox.max.y = std::max(bbox.max.y, p.y + extent);
				bbox.max.z = std::max(bbox.max.z, p.z + extent);
			}
		}
		return true;
	}

	// The render iterator loop of the original renderer for a LaserIterator, with ULaserIterator::CurrentItem of
	// DeusEx.dll (build 1112fm). The script function Init puts the number of beam segments into MaxItems, plus one.
	// Each item puts the proxy mesh (LaserProxy, a beam piece of 16 units) at the next segment of a beam, and the
	// renderer draws it there. An electric arc (bRandomBeam) has segments of about 15 units, and each segment moves
	// by the sum of two random unit vectors. The proxy then points back to a mirror point of the segment before it.
	// After the last item, the proxy stays at a random segment of the beams.
	void DrawLaser(VisibleFrame* frame, UObject* iterator, UActor* emitter)
	{
		BeamLayout& layout = GetBeamLayout(iterator);
		UActor* proxy = GetLaserProxy(iterator);
		if (!layout.Valid || !proxy || !proxy->Mesh())
			return;

		int maxItems = iterator->Value<int>(PropOffsets_RenderIterator.MaxItems);
		int& index = iterator->Value<int>(PropOffsets_RenderIterator.Index);
		int& nextItem = iterator->Value<int>(PropOffsets_LaserIterator.NextItem);
		vec3& prevLoc = iterator->Value<vec3>(PropOffsets_LaserIterator.prevloc);
		vec3& prevRand = iterator->Value<vec3>(PropOffsets_LaserIterator.prevRand);
		vec3& savedLoc = iterator->Value<vec3>(PropOffsets_LaserIterator.savedLoc);
		Rotator& savedRot = iterator->Value<Rotator>(PropOffsets_LaserIterator.SavedRot);
		bool randomBeam = iterator->BoolValue(PropOffsets_LaserIterator.bRandomBeam);

		VisibleMesh vismesh;
		for (index = 0; index < maxItems; index++)
		{
			// The beam of this item. The last item, one after all segments, takes the end of the first beam.
			int segmentsSoFar = 0;
			int beamIndex = 0;
			for (int i = 0; i < MaxBeams; i++)
			{
				Beam beam(iterator, layout, i);
				if (!beam.Active())
					continue;
				segmentsSoFar += beam.NumSegments();
				if (segmentsSoFar > index)
				{
					beamIndex = i;
					break;
				}
			}

			Beam beam(iterator, layout, beamIndex);
			int numSegments = beam.NumSegments();
			float alpha = numSegments > 0 ? 1.0f - (float)(segmentsSoFar - index) / (float)numSegments : 0.0f;
			alpha = clamp(alpha, 0.0f, 1.0f);

			vec3 start = beam.Location();
			vec3 end = start + Coords::Rotation(beam.Rotation()).XAxis * beam.Length();
			vec3 location = start + (end - start) * alpha;
			Rotator rotation = beam.Rotation();

			if (randomBeam)
			{
				vec3 v;
				do
				{
					v = vec3(VisualFRand() * 2.0f - 1.0f, VisualFRand() * 2.0f - 1.0f, VisualFRand() * 2.0f - 1.0f);
				} while (dot(v, v) > 1.0f || dot(v, v) == 0.0f);
				vec3 randomDir = v * (1.0f / std::sqrt(dot(v, v)));

				location += prevRand + randomDir;
				vec3 mirror = location * 2.0f - prevLoc;
				rotation = Rotator::FromVector(prevLoc - mirror);
				prevLoc = mirror;
				prevRand = randomDir;
			}

			proxy->Location() = location;
			proxy->Rotation() = rotation;

			// Keep a random segment of this frame for the place of the proxy after the last item
			if (savedLoc == emitter->Location() && VisualFRand() < (float)nextItem / (float)maxItems)
			{
				savedLoc = location;
				savedRot = rotation;
			}

			nextItem++;
			if (nextItem == maxItems)
			{
				proxy->Location() = savedLoc;
				proxy->Rotation() = savedRot;
			}

			// All segments use the light list of the first segment of this frame. The light list keeps the lights
			// that have a free line to the actor, and a new list for each segment took most of the frame time.
			// Each vertex still gets the light of its own place.
			if (index > 0)
				proxy->TouchingLights.Location = proxy->Location();

			if (vismesh.DrawMesh(frame, proxy, false, false))
				vismesh.DrawMesh(frame, proxy, false, true);
		}
	}
}

UObject* DeusExParticles::GetIterator(UActor* actor, bool create)
{
	UObject*& iterator = actor->Value<UObject*>(PropOffsets_Actor.RenderInterface);
	if (iterator || !create)
		return iterator;

	UClass* cls = actor->RenderIteratorClass();
	if (!cls || !actor->package)
		return nullptr;

	// The render interface is transient. It is not part of a save game, so it is in the transient package.
	iterator = engine->packages->GetTransientPackage()->NewObject(cls->Name, cls, ObjectFlags::Transient);
	iterator->Outer() = actor;
	return iterator;
}

void DeusExParticles::Update(UObject* iterator, float deltaTime)
{
	ParticleLayout& layout = GetLayout(iterator);
	if (!layout.Valid)
		return;

	bool ownerUsesGravity = iterator->BoolValue(PropOffsets_ParticleIterator.bOwnerUsesGravity);
	bool ownerScales = iterator->BoolValue(PropOffsets_ParticleIterator.bOwnerScales);
	bool ownerFades = iterator->BoolValue(PropOffsets_ParticleIterator.bOwnerFades);
	float ownerZoneGravity = iterator->Value<float>(PropOffsets_ParticleIterator.OwnerZoneGravity);
	float ownerRiseRate = iterator->Value<float>(PropOffsets_ParticleIterator.OwnerRiseRate);
	float ownerLifeSpan = iterator->Value<float>(PropOffsets_ParticleIterator.OwnerLifeSpan);
	float ownerDrawScale = iterator->Value<float>(PropOffsets_ParticleIterator.OwnerDrawScale);

	bool noneActive = true;
	for (int i = 0; i < MaxParticles; i++)
	{
		Particle p(iterator, layout, i);
		if (!p.Active())
			continue;

		if (p.LifeSpan() > 0.0f)
		{
			p.LifeSpan() -= deltaTime;
			if (p.LifeSpan() <= 0.0f)
				p.LifeSpan() = -1.0f;
		}

		if (p.LifeSpan() < 0.0f)
		{
			// The particle is at its end
			p.SetActive(false);
			iterator->Value<int>(PropOffsets_ParticleIterator.nextFreeParticle) = i;
			continue;
		}

		noneActive = false;

		p.Velocity().x = p.InitVel().x + 2.0f - FRand() * 5.0f;
		p.Velocity().y = p.InitVel().y + 2.0f - FRand() * 5.0f;
		if (ownerUsesGravity)
			p.Velocity().z += deltaTime * ownerZoneGravity * 0.2f;
		else
			p.Velocity().z = (ownerLifeSpan - p.LifeSpan()) * ownerRiseRate * (FRand() * 0.2f + 0.9f) + p.InitVel().z;

		if (ownerScales)
			p.DrawScale() = clamp((ownerLifeSpan - p.LifeSpan() + 1.0f) * ownerDrawScale, 0.01f, 3.0f);
		else
			p.DrawScale() = ownerDrawScale;

		if (ownerFades)
			p.ScaleGlow() = ownerLifeSpan != 0.0f ? p.LifeSpan() / ownerLifeSpan : 0.0f;
		else
			p.ScaleGlow() = 1.0f;

		p.Location() += p.Velocity() * deltaTime;
	}

	// The proxy sprite is not visible while the generator has no particle
	if (UActor* proxy = GetProxy(iterator))
		proxy->bHidden() = noneActive;
}

bool DeusExParticles::GetBounds(UActor* generator, BBox& bbox)
{
	UObject* iterator = GetIterator(generator, false);
	if (IsLaserIterator(iterator))
		return GetLaserBounds(iterator, generator, bbox);
	if (!IsParticleIterator(iterator))
		return false;

	ParticleLayout& layout = GetLayout(iterator);
	UActor* proxy = GetProxy(iterator);

	// The position of the generator is always in the box. The script stops a generator that the player cannot see,
	// and it starts it again when the position of the generator comes into view.
	float extent = SpriteExtent(proxy, 1.0f);
	bbox.min = generator->Location() - vec3(extent);
	bbox.max = generator->Location() + vec3(extent);

	if (layout.Valid)
	{
		for (int i = 0; i < MaxParticles; i++)
		{
			Particle p(iterator, layout, i);
			if (!p.Active())
				continue;
			vec3 size = vec3(SpriteExtent(proxy, p.DrawScale()));
			bbox.min.x = std::min(bbox.min.x, p.Location().x - size.x);
			bbox.min.y = std::min(bbox.min.y, p.Location().y - size.y);
			bbox.min.z = std::min(bbox.min.z, p.Location().z - size.z);
			bbox.max.x = std::max(bbox.max.x, p.Location().x + size.x);
			bbox.max.y = std::max(bbox.max.y, p.Location().y + size.y);
			bbox.max.z = std::max(bbox.max.z, p.Location().z + size.z);
		}
	}
	return true;
}

bool DeusExParticles::Prepare(VisibleFrame* frame, UActor* generator)
{
	UObject* iterator = GetIterator(generator, false);
	if (IsLaserIterator(iterator))
	{
		// URenderIterator::Init: the script counts the beam segments and sets the proxy
		UActor* camera = engine->viewport ? engine->viewport->Actor() : nullptr;
		CallEvent(iterator, "Init", { ExpressionValue::ObjectValue(camera) });
		iterator->Value<UObject*>(PropOffsets_RenderIterator.Observer) = camera;

		UActor* proxy = GetLaserProxy(iterator);
		if (!proxy)
			return false;

		// The player sees the beam. LaserEmitter.Tick reads this time and stops a beam that the player did not see.
		proxy->LastRenderTime() = engine->LevelInfo->TimeSeconds();

		int maxItems = iterator->Value<int>(PropOffsets_RenderIterator.MaxItems);
		return maxItems > 0 && !proxy->bHidden() && proxy->Mesh();
	}
	if (!IsParticleIterator(iterator))
		return false;

	// URenderIterator::Init: the script sets the proxy and the number of items for this frame
	UActor* camera = engine->viewport ? engine->viewport->Actor() : nullptr;
	CallEvent(iterator, "Init", { ExpressionValue::ObjectValue(camera) });
	iterator->Value<UObject*>(PropOffsets_RenderIterator.Observer) = camera;

	UActor* proxy = GetProxy(iterator);
	if (!proxy)
		return false;

	// The player sees the place of the generator. The script reads this time to keep the generator in operation.
	proxy->LastRenderTime() = engine->LevelInfo->TimeSeconds();

	int maxItems = iterator->Value<int>(PropOffsets_RenderIterator.MaxItems);
	return maxItems > 0 && !proxy->bHidden() && proxy->Texture();
}

void DeusExParticles::Draw(VisibleFrame* frame, UActor* generator)
{
	UObject* iterator = GetIterator(generator, false);
	if (IsLaserIterator(iterator))
	{
		DrawLaser(frame, iterator, generator);
		return;
	}
	if (!IsParticleIterator(iterator))
		return;

	ParticleLayout& layout = GetLayout(iterator);
	UActor* proxy = GetProxy(iterator);
	if (!layout.Valid || !proxy || !proxy->Texture())
		return;

	int maxItems = std::min(iterator->Value<int>(PropOffsets_RenderIterator.MaxItems), MaxParticles);
	VisibleSprite sprite;
	for (int i = 0; i < maxItems; i++)
	{
		Particle p(iterator, layout, i);
		if (p.Active())
			sprite.Draw(frame, proxy, p.Location(), p.DrawScale(), p.ScaleGlow());
	}
}
