
#include "Precomp.h"
#include "NParticleIterator.h"
#include "VM/NativeFunc.h"
#include "Utils/Logger.h"
#include "Packages/DeusEx/ParticleSystem.h"

void NParticleIterator::RegisterFunctions()
{
	RegisterVMNativeFunc_1("ParticleIterator", "UpdateParticles", &NParticleIterator::UpdateParticles, 3017);
}

void NParticleIterator::UpdateParticles(UObject* Self, float DeltaTime)
{
	DeusExParticles::Update(Self, DeltaTime);
}
