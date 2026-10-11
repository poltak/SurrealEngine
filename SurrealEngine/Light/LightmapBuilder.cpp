
#include "Precomp.h"
#include "LightmapBuilder.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Actors/Info/UZoneInfo.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Core/UClass.h"
#include "RenderDevice/RenderDevice.h"
#include "Math/hsb.h"
#include <cstring>

#ifdef USE_SSE2
#include <immintrin.h>
#endif

void LightmapBuilder::Setup(UModel* model, const Coords& mapCoords, int lightMap)
{
	const LightMapIndex& lmindex = model->LightMap[lightMap];

	width = lmindex.UClamp;
	height = lmindex.VClamp;
	normal = normalize(mapCoords.ZAxis);
	base = mapCoords.Origin;

	// Stop allocations over time by building up a reserve

	size_t size = (size_t)width * height;
	if (points.size() < size)
		points.resize(size);
	if (lightcolors.size() < size)
		lightcolors.resize(size);
	if (illuminationmap.size() < size)
		illuminationmap.resize(size);

	CalcWorldLocations(mapCoords, lmindex);
	grid = GetTexelGrid();
}

LightmapTexelGrid LightmapBuilder::GetTexelGrid() const
{
	LightmapTexelGrid result;
	result.Width = width;
	result.Height = height;
	if (width >= 2 && height >= 2)
	{
		// CalcWorldLocations puts the texels on a regular grid in the plane of the surface
		result.Origin = points[0];
		result.DU = (points[(size_t)width - 1] - points[0]) * (1.0f / (width - 1));
		result.DV = (points[(size_t)(height - 1) * width] - points[0]) * (1.0f / (height - 1));
		float sum = result.Origin.x + result.Origin.y + result.Origin.z + result.DU.x + result.DU.y + result.DU.z + result.DV.x + result.DV.y + result.DV.z;
		result.Valid = std::isfinite(sum);
	}
	return result;
}

bool LightmapBuilder::GetLightRect(UActor* light, const LightmapTexelGrid& grid, LightmapTexelRect& rect)
{
	rect.x0 = 0;
	rect.y0 = 0;
	rect.x1 = grid.Width - 1;
	rect.y1 = grid.Height - 1;
	if (grid.Width <= 0 || grid.Height <= 0)
		return false;

	// A cylinder light has no limit along the Z axis. All other light effects give no light outside the light radius.
	if (!grid.Valid || light->LightEffect() == LE_Cylinder)
		return true;

	float a = dot(grid.DU, grid.DU);
	float b = dot(grid.DU, grid.DV);
	float c = dot(grid.DV, grid.DV);
	float det = a * c - b * b;
	if (!(det > 1.0e-6f * a * c))
		return true; // the grid axes are parallel, so there is no limit to calculate

	// The light location in grid coordinates, and its distance from the plane of the surface
	vec3 d = light->Location() - grid.Origin;
	float dU = dot(d, grid.DU);
	float dV = dot(d, grid.DV);
	float u = (c * dU - b * dV) / det;
	float v = (a * dV - b * dU) / det;
	vec3 offPlane = d - grid.DU * u - grid.DV * v;

	float radius = light->WorldLightRadius();
	float inPlaneRadiusSquared = radius * radius - dot(offPlane, offPlane);
	if (!std::isfinite(u) || !std::isfinite(v) || !std::isfinite(inPlaneRadiusSquared))
		return true;
	if (inPlaneRadiusSquared <= 0.0f)
		return false; // the light does not reach the plane

	// The light sphere cuts a circle from the plane. These are the limits of the circle along each grid axis, with one texel more.
	float inPlaneRadius = std::sqrt(inPlaneRadiusSquared);
	float uReach = inPlaneRadius * std::sqrt(c / det) + 1.0f;
	float vReach = inPlaneRadius * std::sqrt(a / det) + 1.0f;
	float x0 = std::floor(u - uReach);
	float x1 = std::ceil(u + uReach);
	float y0 = std::floor(v - vReach);
	float y1 = std::ceil(v + vReach);
	if (x1 < 0.0f || y1 < 0.0f || x0 > (float)(grid.Width - 1) || y0 > (float)(grid.Height - 1))
		return false;

	rect.x0 = (int)std::max(x0, 0.0f);
	rect.y0 = (int)std::max(y0, 0.0f);
	rect.x1 = (int)std::min(x1, (float)(grid.Width - 1));
	rect.y1 = (int)std::min(y1, (float)(grid.Height - 1));
	return true;
}

void LightmapBuilder::AddLight(UActor* light, const LightmapTexelRect& rect)
{
	int count = rect.x1 - rect.x0 + 1;
	for (int y = rect.y0; y <= rect.y1; y++)
	{
		size_t offset = (size_t)y * width + rect.x0;
		Effect.Run(light, count, 1, WorldLocations() + offset, base, WorldNormal(), Shadow.Pixels() + offset, illuminationmap.data() + offset);
	}
	AddLightContribution(light, rect);
}

void LightmapBuilder::SetAmbientLight(UZoneInfo* zoneActor)
{
	// Initialize lightmap with the ambient color

	vec3 ambientColor = hsbtorgb(zoneActor->AmbientHue(), zoneActor->AmbientSaturation(), zoneActor->AmbientBrightness()); // To do: is this the correct scale?
	// To do: is there more ambient light than just from the zone?

	size_t count = (size_t)width * height;
	for (size_t i = 0; i < count; i++)
		lightcolors[i] = ambientColor;

	// To do: how does polyflags affect the lightmap (if at all)?

	//bool isSpecialLit = (surface.PolyFlags & PF_SpecialLit) == PF_SpecialLit;
	//bool isTranslucent = (surface.PolyFlags & PF_Translucent) == PF_Translucent;
}

void LightmapBuilder::LoadStaticLight(const Array<vec3>& staticLightColors)
{
	if (lightcolors.size() < staticLightColors.size())
		lightcolors.resize(staticLightColors.size());
	std::memcpy(lightcolors.data(), staticLightColors.data(), staticLightColors.size() * sizeof(vec3));
}

void LightmapBuilder::SaveStaticLight(Array<vec3>& staticLightColors)
{
	staticLightColors.resize(width * height);
	std::memcpy(staticLightColors.data(), lightcolors.data(), width * height * sizeof(vec3));
}

bool LightmapBuilder::IsAnimatedLight(UActor* light)
{
	// Lights that pulse, blink, flicker or get their color from a palette
	uint8_t lightType = light->LightType();
	if (lightType > LT_Steady && lightType != LT_BackdropLight)
		return true;

	switch (light->LightEffect())
	{
	case LE_Searchlight:
		return light->LightPeriod() != 0; // it turns only with a light period
	case LE_TorchWaver:
	case LE_FireWaver:
	case LE_WateryShimmer:
	case LE_SlowWave:
	case LE_FastWave:
	case LE_Shock:
	case LE_Disco:
	case LE_Interference:
	case LE_Rotor:
		return true;
	default:
		return false;
	}
}

void LightmapBuilder::AddStaticLights(UModel* model, int lightMap, bool animated)
{
	const LightMapIndex& lmindex = model->LightMap[lightMap];
	if (lmindex.LightActors >= 0)
	{
		UActor** lightlist = &model->Lights[lmindex.LightActors];
		for (int lightindex = 0; lightlist[lightindex] != nullptr; lightindex++)
		{
			UActor* light = lightlist[lightindex];
			if (light->LightType() != LT_None && light->LightBrightness() > 0 && IsAnimatedLight(light) == animated)
			{
				// The shadow map of a large lightmap takes long to decode. Do it only for the texels that the light can reach.
				LightmapTexelRect rect;
				if (!GetLightRect(light, grid, rect))
					continue;
				Shadow.Load(model, lightMap, lightindex, rect.x0, rect.y0, rect.x1, rect.y1);
				AddLight(light, rect);
			}
		}
	}
}

void LightmapBuilder::AddDynamicLights(UModel* model, int lightMap, const Array<UActor*>& lights)
{
	Shadow.Clear(model, lightMap);
	for (UActor* light : lights)
	{
		if (light->LightType() != LT_None && light->LightBrightness() > 0)
		{
			LightmapTexelRect rect;
			if (GetLightRect(light, grid, rect))
				AddLight(light, rect);
		}
	}
}

void LightmapBuilder::AddLightContribution(UActor* light, const LightmapTexelRect& rect)
{
	vec3 lightcolor = GetLightColor(light);
	float lightcolorR = lightcolor.r;
	float lightcolorG = lightcolor.g;
	float lightcolorB = lightcolor.b;
	int size = rect.x1 - rect.x0 + 1;

	for (int y = rect.y0; y <= rect.y1; y++)
	{
		const float* src = illuminationmap.data() + (size_t)y * width + rect.x0;
		float* dest = (float*)(lightcolors.data() + (size_t)y * width + rect.x0);

#ifdef USE_SSE2
		__m128 mmlightcolor0 = _mm_setr_ps(lightcolorR, lightcolorG, lightcolorB, lightcolorR);
		__m128 mmlightcolor1 = _mm_setr_ps(lightcolorG, lightcolorB, lightcolorR, lightcolorG);
		__m128 mmlightcolor2 = _mm_setr_ps(lightcolorB, lightcolorR, lightcolorG, lightcolorB);
		int sse_size = size / 4 * 4;
		for (int i = 0; i < sse_size; i += 4)
		{
			// To do: memory align buffers
			__m128 s = _mm_loadu_ps(src);
			__m128 s0 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(1, 0, 0, 0));
			__m128 s1 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(2, 2, 1, 1));
			__m128 s2 = _mm_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 2));
			__m128 one = _mm_set_ps1(1.0f);
			_mm_storeu_ps(dest, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest), _mm_mul_ps(s0, mmlightcolor0)), one));
			_mm_storeu_ps(dest + 4, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest + 4), _mm_mul_ps(s1, mmlightcolor1)), one));
			_mm_storeu_ps(dest + 8, _mm_min_ps(_mm_add_ps(_mm_loadu_ps(dest + 8), _mm_mul_ps(s2, mmlightcolor2)), one));
			src += 4;
			dest += 3 * 4;
		}
#else
		int sse_size = 0;
#endif
		for (int i = sse_size; i < size; i++)
		{
			float s = *src;
			float r = s * lightcolorR;
			float g = s * lightcolorG;
			float b = s * lightcolorB;
			r = r <= 1.0f ? r : 1.0f;
			g = g <= 1.0f ? g : 1.0f;
			b = b <= 1.0f ? b : 1.0f;
			dest[0] += r;
			dest[1] += g;
			dest[2] += b;
			src++;
			dest += 3;
		}
	}
}

vec3 LightmapBuilder::GetLightColor(UActor* light)
{
	constexpr float phaseScale = (1.0f / 255.0f);
	constexpr float periodSpeed = 40.0f;
	constexpr float turnsToRadians = 2.0f * 3.14159265359f;
	constexpr float strobeSpeed = 10.0f;
	switch (light->LightType())
	{
	default:
	case LT_Steady:
	case LT_BackdropLight:
		return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
	case LT_Pulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		float pulse = std::sin(pulseTurns * turnsToRadians);
		float brightness = light->LightBrightness() * (0.65f + 0.35f * pulse);
		return hsbtorgb(light->LightHue(), light->LightSaturation(), (uint8_t)std::clamp(brightness, 0.0f, 255.0f));
	}
	case LT_SubtlePulse:
	{
		float pulseTurns = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		float pulse = std::sin(pulseTurns * turnsToRadians);
		float brightness = light->LightBrightness() * (0.8f + 0.2f * pulse);
		return hsbtorgb(light->LightHue(), light->LightSaturation(), (uint8_t)std::clamp(brightness, 0.0f, 255.0f));
	}
	case LT_Blink:
		if (std::fmod(light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1), 2.0f) < 1.0f)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_Strobe:
		if (std::fmod(light->Level()->TimeSeconds() * strobeSpeed, 2.0f) < 1.0f)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_Flicker:
		if (light->Light.FlickerRandom)
			return hsbtorgb(light->LightHue(), light->LightSaturation(), light->LightBrightness());
		else
			return vec3(0.0f);
	case LT_TexturePaletteOnce:
	{
		if (light->LifeSpan() <= 0.0f || !light->Skin() || !light->Skin()->Palette())
			return vec3(0.0f);

		float t = light->LifeSpan() / light->Class->GetDefaultObject<UActor>()->LifeSpan();
		UPalette* palette = light->Skin()->Palette();
		if (palette->Colors.empty())
			return vec3(0.0f);
		uint32_t color = palette->Colors[(int)(t * palette->Colors.size())];
		return vec3(
			((color >> 16) & 0xff) * (1.0f / 255.0f),
			((color >> 8) & 0xff) * (1.0f / 255.0f),
			(color & 0xff) * (1.0f / 255.0f));
	}
	case LT_TexturePaletteLoop:
	{
		if (light->LifeSpan() <= 0.0f || !light->Skin() || !light->Skin()->Palette())
			return vec3(0.0f);

		float t = light->LightPhase() * phaseScale + light->Level()->TimeSeconds() * periodSpeed / std::max(light->LightPeriod(), (uint8_t)1);
		t -= std::floor(t);

		UPalette* palette = light->Skin()->Palette();
		if (palette->Colors.empty())
			return vec3(0.0f);
		uint32_t color = palette->Colors[(int)(t * palette->Colors.size())];
		return vec3(
			((color >> 16) & 0xff) * (1.0f / 255.0f),
			((color >> 8) & 0xff) * (1.0f / 255.0f),
			(color & 0xff) * (1.0f / 255.0f));
	}
	}
}

void LightmapBuilder::CalcWorldLocations(Coords MapCoords, const LightMapIndex& lmindex)
{
	// Note: this could be simplified a lot for better performance

	// Allow optimizer to move them into registers
	int width = this->width;
	int height = this->height;

	float UDot = dot(MapCoords.XAxis, MapCoords.Origin);
	float VDot = dot(MapCoords.YAxis, MapCoords.Origin);
	float LMUPan = UDot + lmindex.PanX - 0.5f * lmindex.UScale;
	float LMVPan = VDot + lmindex.PanY - 0.5f * lmindex.VScale;
	float LMUMult = 1.0f / lmindex.UScale;
	float LMVMult = 1.0f / lmindex.VScale;

	vec3 p[3] =
	{
		MapCoords.Origin,
		MapCoords.Origin + MapCoords.XAxis,
		MapCoords.Origin + MapCoords.YAxis
	};

	vec2 uv[3];
	for (int j = 0; j < 3; j++)
	{
		uv[j] =
		{
			(dot(MapCoords.XAxis, p[j]) - LMUPan) * LMUMult,
			(dot(MapCoords.YAxis, p[j]) - LMVPan) * LMVMult
		};
	}

	float leftDX = uv[2].x - uv[0].x;
	float leftDY = uv[2].y - uv[0].y;
	float leftStep = leftDX / leftDY;
	float rightDX = uv[2].x - uv[1].x;
	float rightDY = uv[2].y - uv[1].y;
	float rightStep = rightDX / rightDY;

	for (int y = 0; y < height; y++)
	{
		float x0 = uv[0].x + leftStep * (y + 0.5f - uv[0].y) + 0.5f;
		float x1 = uv[1].x + rightStep * (y + 0.5f - uv[1].y) + 0.5f;
		float t0 = (y + 0.5f - uv[0].y) / leftDY;
		float t1 = (y + 0.5f - uv[1].y) / rightDY;
		vec3 p0 = mix(p[0], p[2], t0);
		vec3 p1 = mix(p[1], p[2], t1);
		if (x1 < x0)
		{
			std::swap(x0, x1);
			std::swap(p0, p1);
		}

		vec3* dest = &points[y * width];
		for (int i = 0; i < width; i++)
		{
			float t = (i + 0.5f - x0) / (x1 - x0);
			dest[i] = mix(p0, p1, t);
		}
	}
}
