#pragma once

#include "Math/vec.h"
#include "LightEffect.h"
#include "Shadowmap.h"

class BspSurface;
class LightMapIndex;
class UModel;
class UZoneInfo;
class Coords;
struct Poly;

// The world location of each lightmap texel: texel (x, y) is at Origin + x * DU + y * DV
struct LightmapTexelGrid
{
	vec3 Origin = vec3(0.0f);
	vec3 DU = vec3(0.0f);
	vec3 DV = vec3(0.0f);
	int Width = 0;
	int Height = 0;
	bool Valid = false;
};

// A block of lightmap texels. The limits are inclusive.
struct LightmapTexelRect
{
	int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

class LightmapBuilder
{
public:
	void Setup(UModel* model, const Coords& mapCoords, int lightMap);

	LightmapTexelGrid GetTexelGrid() const;

	// Gets the texels that a light can reach. Returns false if the light reaches no texel of the lightmap.
	// A light changes only the texels in its radius, and a lightmap of a large surface has 50000 texels,
	// so this keeps the work for a small light small.
	static bool GetLightRect(UActor* light, const LightmapTexelGrid& grid, LightmapTexelRect& rect);
	void SetAmbientLight(UZoneInfo* zoneActor);
	void AddStaticLights(UModel* model, int lightMap);
	void AddDynamicLights(UModel* model, int lightMap, const Array<UActor*>& lights);

	void LoadStaticLight(const Array<vec3>& staticLightColors);
	void SaveStaticLight(Array<vec3>& staticLightColors);

	int Width() const { return width; }
	int Height() const { return height; }
	const vec3* Pixels() const { return lightcolors.data(); }

	static vec3 GetLightColor(UActor* light);

private:
	const vec3* WorldLocations() const { return points.data(); }
	const vec3& WorldNormal() const { return normal; }

	void CalcWorldLocations(Coords MapCoords, const LightMapIndex& lmindex);

	void AddLight(UActor* light);
	void AddLightContribution(UActor* light, const LightmapTexelRect& rect);

	LightmapTexelGrid grid;

	int width = 0;
	int height = 0;
	Array<vec3> lightcolors;

	Array<vec3> points;
	vec3 normal;
	vec3 base;

	Shadowmap Shadow;
	LightEffect Effect;
	Array<float> illuminationmap;
};
