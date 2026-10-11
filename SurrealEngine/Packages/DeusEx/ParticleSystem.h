#pragma once

#include "Math/vec.h"
#include "Math/bbox.h"

class UObject;
class UActor;
class VisibleFrame;

// Deus Ex particle effects (smoke, fire, sparks, water). A ParticleGenerator actor has a ParticleIterator
// object as its render interface. The iterator holds up to 64 particles, and the renderer draws the
// ParticleProxy sprite of the generator one time for each particle.
// It follows UParticleIterator in DeusEx.dll and the render iterator loop of the original renderer.
class DeusExParticles
{
public:
	static const int MaxParticles = 64;

	// The iterator object of an actor that has a render iterator class. The renderer makes it, as in the original.
	static UObject* GetIterator(UActor* actor, bool create);

	// ParticleIterator.UpdateParticles
	static void Update(UObject* iterator, float deltaTime);

	// The box around the generator and its particles. False if the actor has no particle iterator.
	static bool GetBounds(UActor* generator, BBox& bbox);

	// Calls the Init function of the iterator for this frame. Returns true if the frame must draw particles.
	static bool Prepare(VisibleFrame* frame, UActor* generator);

	static void Draw(VisibleFrame* frame, UActor* generator);
};
