#pragma once

#include "UFractalTexture.h"
#include "Utils/Random.h"

enum class ESpark : uint8_t
{
	Burn,
	Sparkle,
	Pulse,
	Signal,
	Blaze,
	OzHasSpoken,
	Cone,
	BlazeRight,
	BlazeLeft,
	Cylinder,
	Cylinder3D,
	Lissajous,
	Jugglers,
	Emit,
	Fountain,
	Flocks,
	Eels,
	Organic,
	WanderOrganic,
	RandmCloud,
	CustomCloud,
	LocalCloud,
	Stars,
	LineLightning,
	RampLightning,
	SphereLightning,
	Wheel,
	Gametes,
	Sprinkler
};

enum class FireDrawMode : uint8_t
{
	Normal,
	Lathe,
	Lathe_2,
	Lathe_3,
	Lathe_4,
};

struct Spark
{
	ESpark Type;
	uint8_t Heat;
	uint8_t X;
	uint8_t Y;
	union
	{
		struct { uint8_t ByteA, ByteB, ByteC, ByteD; };
		struct { uint8_t A, B, C, Speed; } Pulse;
		struct { uint8_t A, B, Frequency, Speed; } Signal;
		struct { uint8_t A, B, C, D; } Burn, OzHasSpoken;
		struct { uint8_t Angle, TwirlAge, RotSpeed, TwirlRotSpeed; } Wheel;
		struct { uint8_t SpeedX, SpeedY, C, HeatDecay; } Emit;
		struct { uint8_t A, B, C, D; } Blaze;
		struct { uint8_t A, B, Age, D; } BlazeLeftRight;
		struct { uint8_t A, B, Radius, Frequency; } SphereLightning;
	};
};

// Spark types above the list of ESpark are short-lived sparks. A spark of the texture makes them, and they move and
// lose heat until they go out. The original engine keeps them in the same list as the sparks of the texture.
namespace DynamicSpark
{
	enum : uint8_t
	{
		Blaze = 0x20,         // loses 5 heat for each frame
		Drift = 0x21,         // loses ByteD heat for each frame
		Fall = 0x22,          // lives ByteC frames, falls faster with time
		OrganicRise = 0x23,   // heat is ByteC and goes down, rises 2 pixels for each frame
		CloudRise = 0x24,     // heat is ByteC and goes up, rises 2 pixels for each frame
		CloudDrift = 0x25,    // heat is ByteC and goes up, moves with the speed ByteA and ByteB
		Eel = 0x26,           // lives ByteC frames
		Twirl = 0x27,         // lives ByteC frames, the direction ByteB turns with the speed ByteD
		Sprinkle = 0x28,      // lives ByteC frames, the horizontal speed swings
		HeatDrift = 0x29,     // loses ByteC heat for each frame
		FountainDrop = 0x2a,  // loses ByteD heat for each frame, falls faster with time
		Gamete = 0x2b         // lives ByteC frames, the direction swings around ByteD
	};
}

class UFireTexture : public UFractalTexture
{
public:
	using UFractalTexture::UFractalTexture;

	void Load(ObjectStream* stream) override;
	void Save(PackageStreamWriter* stream) override;

	void UpdateFrame() override;

	FireDrawMode& DrawMode() { return Value<FireDrawMode>(PropOffsets_FireTexture.DrawMode); }
	uint8_t& FX_Area() { return Value<uint8_t>(PropOffsets_FireTexture.FX_Area); }
	uint8_t& FX_AuxSize() { return Value<uint8_t>(PropOffsets_FireTexture.FX_AuxSize); }
	uint8_t& FX_Frequency() { return Value<uint8_t>(PropOffsets_FireTexture.FX_Frequency); }
	uint8_t& FX_Heat() { return Value<uint8_t>(PropOffsets_FireTexture.FX_Heat); }
	uint8_t& FX_HorizSpeed() { return Value<uint8_t>(PropOffsets_FireTexture.FX_HorizSpeed); }
	uint8_t& FX_Phase() { return Value<uint8_t>(PropOffsets_FireTexture.FX_Phase); }
	uint8_t& FX_Size() { return Value<uint8_t>(PropOffsets_FireTexture.FX_Size); }
	uint8_t& FX_VertSpeed() { return Value<uint8_t>(PropOffsets_FireTexture.FX_VertSpeed); }
	int& NumSparks() { return Value<int>(PropOffsets_FireTexture.NumSparks); }
	int& OldRenderHeat() { return Value<int>(PropOffsets_FireTexture.OldRenderHeat); }
	uint8_t& PenDownX() { return Value<uint8_t>(PropOffsets_FireTexture.PenDownX); }
	uint8_t& PenDownY() { return Value<uint8_t>(PropOffsets_FireTexture.PenDownY); }
	uint8_t& RenderHeat() { return Value<uint8_t>(PropOffsets_FireTexture.RenderHeat); }
	FixedArrayView<uint8_t, 1028> RenderTable() { return FixedArray<uint8_t, 1028>(PropOffsets_FireTexture.RenderTable); }
	uint8_t& SparkType() { return Value<uint8_t>(PropOffsets_FireTexture.SparkType); }
	//TypedScriptArray<Spark> Sparks() { return DynamicArray<Spark>(PropOffsets_FireTexture.Sparks); }
	int& SparksLimit() { return Value<int>(PropOffsets_FireTexture.SparksLimit); }
	uint8_t& StarStatus() { return Value<uint8_t>(PropOffsets_FireTexture.StarStatus); }
	BitfieldBool bRising() { return BoolValue(PropOffsets_FireTexture.bRising); }

private:
	int RandomByteValue() { return VisualRandInt(255); }

	void RedrawSparks(uint8_t* pixels, int width, int height);
	void PostDrawSparks(uint8_t* pixels, int width, int height);
	void DrawFlashRamp(uint8_t* pixels, int width, int height, int x, int y, int lengthX, int lengthY, int heatStart, int heatEnd);

	Array<uint8_t> WorkBuffer;
	uint8_t FadeTable[4 * 256];
	int CurrentRenderHeat = -1;
	uint32_t FrameCount = 0;
	Array<Spark> Sparks; // The sparks of the texture first, then the short-lived sparks
};
