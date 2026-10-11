#pragma once

#include "Math/vec.h"

class UActor;
class VisibleFrame;

class VisibleSprite
{
public:
	void Draw(VisibleFrame* frame, UActor* actor);
	void Draw(VisibleFrame* frame, UActor* actor, const vec3& location, float drawscale, float scaleglow);
};
