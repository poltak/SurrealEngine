
#include "Precomp.h"
#include "UFireTexture.h"
#include <cstring>

void UFireTexture::Load(ObjectStream* stream)
{
	UFractalTexture::Load(stream);

	auto& sparks = Sparks;
	int size = stream->ReadIndex();
	for (int i = 0; i < size; i++)
	{
		Spark spark;
		spark.Type = (ESpark)stream->ReadUInt8();
		spark.Heat = stream->ReadUInt8();
		spark.X = stream->ReadUInt8();
		spark.Y = stream->ReadUInt8();
		spark.ByteA = stream->ReadUInt8();
		spark.ByteB = stream->ReadUInt8();
		spark.ByteC = stream->ReadUInt8();
		spark.ByteD = stream->ReadUInt8();
		sparks.push_back(spark);
	}
}

void UFireTexture::Save(PackageStreamWriter* stream)
{
	UFractalTexture::Save(stream);

	auto& sparks = Sparks;
	stream->WriteIndex((int)sparks.size());
	for (const Spark& spark : sparks)
	{
		stream->WriteUInt8((uint8_t)spark.Type);
		stream->WriteUInt8(spark.Heat);
		stream->WriteUInt8(spark.X);
		stream->WriteUInt8(spark.Y);
		stream->WriteUInt8(spark.ByteA);
		stream->WriteUInt8(spark.ByteB);
		stream->WriteUInt8(spark.ByteC);
		stream->WriteUInt8(spark.ByteD);
	}
}

// The lookup tables of the original fire code (Fire.dll, the function that runs before the first fire texture):
// a sine wave in the range 0 to 255, the same wave 32 higher with a limit of 255, and the same wave as a signed value.
namespace
{
	struct FireTables
	{
		uint8_t Sine[256];
		uint8_t Light[256];
		int8_t SignedSine[256];

		FireTables()
		{
			for (int i = 0; i < 256; i++)
			{
				Sine[i] = (uint8_t)(int)std::round(std::sin(i * (1.0 / 256.0) * 6.2831855) * 127.5 + 127.45);
				Light[i] = (uint8_t)std::min(Sine[i] + 0x20, 255);
				SignedSine[i] = (int8_t)(uint8_t)(Sine[i] + 0x80);
			}
		}
	};

	const FireTables& GetFireTables()
	{
		static FireTables tables;
		return tables;
	}
}

void UFireTexture::UpdateFrame()
{
	if (!TextureModified)
	{
		UnrealMipmap& mipmap = UsedMipmaps.front();

		int width = mipmap.Width;
		int height = mipmap.Height;
		uint8_t* pixels = (uint8_t*)mipmap.Data.data();

		RedrawSparks(pixels, width, height);

		if (CurrentRenderHeat != RenderHeat())
		{
			CurrentRenderHeat = RenderHeat();
			float heatLoss = 1.0f - (255 - CurrentRenderHeat) / 16.0f;
			for (int i = 0; i < 4 * 256; i++)
			{
				FadeTable[i] = (uint8_t)clamp(i * 0.25f + heatLoss, 0.0f, 255.0f);
			}
		}

		WorkBuffer.resize(width * height);
		uint8_t* buffer = WorkBuffer.data();

		int rising = bRising() ? 1 : 0;
		for (int y = 0; y < height; y++)
		{
			uint8_t* destLine = buffer + y * width;
			uint8_t* srcLine = pixels + ((y + rising) % height) * width;
			uint8_t* nextLine = pixels + ((y + rising + 1) % height) * width;
			{
				int left = srcLine[width - 1];
				int center = srcLine[0];
				int right = srcLine[1];
				int bottom = nextLine[0];
				destLine[0] = FadeTable[left + center + right + bottom];
			}
			for (int x = 1; x < width - 1; x++)
			{
				int left = srcLine[x - 1];
				int center = srcLine[x];
				int right = srcLine[x + 1];
				int bottom = nextLine[x];
				destLine[x] = FadeTable[left + center + right + bottom];
			}
			{
				int left = srcLine[width - 2];
				int center = srcLine[width - 1];
				int right = srcLine[0];
				int bottom = nextLine[width - 1];
				destLine[width - 1] = FadeTable[left + center + right + bottom];
			}
		}
		std::memcpy(pixels, buffer, width * height);

		PostDrawSparks(pixels, width, height);

		TextureModified = true;
	}
}

// This function does the same as UFireTexture::RedrawSparks of the original Fire.dll. Each spark has 8 bytes, and
// what the four last bytes mean depends on the type. The sizes of a fire texture are powers of two.
void UFireTexture::RedrawSparks(uint8_t* pixels, int width, int height)
{
	const FireTables& tables = GetFireTables();
	const int umask = width - 1;
	const int vmask = height - 1;
	const int limit = SparksLimit();

	FrameCount++;

	auto rnd = [this]() -> int { return RandomByteValue(); };
	auto put = [=](int x, int y, int heat) { pixels[(y & vmask) * width + (x & umask)] = (uint8_t)heat; };

	// A speed is a signed byte. It is the chance out of 128 that the spark moves one pixel in this frame.
	auto moveX = [&](Spark& s, int speed)
	{
		if (speed < 0)
		{
			if ((rnd() & 0x7f) < -speed)
				s.X = (uint8_t)((s.X - 1) & umask);
		}
		else if ((rnd() & 0x7f) < speed)
		{
			s.X = (uint8_t)((s.X + 1) & umask);
		}
	};
	auto moveY = [&](Spark& s, int speed)
	{
		if (speed < 0)
		{
			if ((rnd() & 0x7f) < -speed)
				s.Y = (uint8_t)((s.Y - 1) & vmask);
		}
		else if ((rnd() & 0x7f) < speed)
		{
			s.Y = (uint8_t)((s.Y + 1) & vmask);
		}
	};
	// The slow random walk of some spark types
	auto wander = [&](Spark& s)
	{
		if (rnd() & 1)
			s.X = (uint8_t)(((rnd() & 0xf) + s.X - 7) & umask);
		if (rnd() & 1)
			s.Y = (uint8_t)(((rnd() & 0xf) + s.Y - 7) & vmask);
	};
	auto jitter = [&](Spark& s)
	{
		int dx = (rnd() & 7) - (rnd() & 7);
		s.X = (uint8_t)((dx + s.X) & umask);
		int dy = (rnd() & 7) - (rnd() & 7);
		s.Y = (uint8_t)((dy + s.Y) & vmask);
	};
	auto makeSpark = [](uint8_t type, int heat, int x, int y, int a, int b, int c, int d)
	{
		Spark n = {};
		n.Type = (ESpark)type;
		n.Heat = (uint8_t)heat;
		n.X = (uint8_t)x;
		n.Y = (uint8_t)y;
		n.ByteA = (uint8_t)a;
		n.ByteB = (uint8_t)b;
		n.ByteC = (uint8_t)c;
		n.ByteD = (uint8_t)d;
		return n;
	};

	Array<Spark>& sparks = Sparks;

	// A new spark goes to the end of the list, so this loop also moves it in the frame that made it
	for (size_t i = 0; i < sparks.size(); i++)
	{
		Spark s = sparks[i];
		bool canEmit = (int)sparks.size() < limit;
		bool remove = false;

		switch ((uint8_t)s.Type)
		{
		case (uint8_t)ESpark::Burn:
			put(s.X, s.Y, rnd());
			break;
		case (uint8_t)ESpark::Sparkle:
		{
			int x = s.X + ((s.ByteA * rnd()) >> 8);
			int y = s.Y + ((s.ByteB * rnd()) >> 8);
			put(x, y, s.Heat);
			break;
		}
		case (uint8_t)ESpark::Pulse:
			put(s.X, s.Y, s.Heat);
			s.Heat = (uint8_t)(s.Heat + s.ByteD);
			break;
		case (uint8_t)ESpark::Signal:
			if (s.ByteC < s.Heat)
				put(s.X, s.Y, s.Heat);
			s.Heat = (uint8_t)(s.Heat + s.ByteD);
			if (s.Heat < s.ByteD)
				s.Heat = (uint8_t)rnd();
			break;
		case (uint8_t)ESpark::Blaze:
			if (canEmit && rnd() < 0x80)
			{
				int a = rnd();
				int b = rnd();
				sparks.push_back(makeSpark(DynamicSpark::Blaze, s.Heat, s.X, s.Y, a, b, s.ByteC, s.ByteD));
			}
			break;
		case (uint8_t)ESpark::OzHasSpoken:
			if (canEmit && rnd() < 0x80)
				sparks.push_back(makeSpark(DynamicSpark::Drift, s.Heat, s.X, s.Y, (rnd() & 0x7f) - 0x3f, 0x81, 0, 2));
			break;
		case (uint8_t)ESpark::Cone:
			if (canEmit && rnd() < 0x40)
				sparks.push_back(makeSpark(DynamicSpark::Fall, s.Heat, s.X, s.Y, (rnd() & 0x7f) - 0x3f, 0, 0x32, 0));
			break;
		case (uint8_t)ESpark::BlazeRight:
			if (canEmit && rnd() < 0x40)
				sparks.push_back(makeSpark(DynamicSpark::Fall, s.Heat, s.X, s.Y, (rnd() & 0x3f) + 0x3f, 0xe3, s.ByteC, 0));
			break;
		case (uint8_t)ESpark::BlazeLeft:
			if (canEmit && rnd() < 0x40)
				sparks.push_back(makeSpark(DynamicSpark::Fall, s.Heat, s.X, s.Y, (rnd() & 0x3f) + 0x80, 0xe3, s.ByteC, 0));
			break;
		case (uint8_t)ESpark::Cylinder:
		{
			int heat = std::min(tables.Sine[(s.ByteA + 0x40) & 0xff] + s.Heat, 255);
			put((uint8_t)(s.X + ((tables.Sine[s.ByteA] * s.ByteB) >> 8)), s.Y, heat);
			s.ByteA = (uint8_t)(s.ByteA + s.ByteD);
			break;
		}
		case (uint8_t)ESpark::Cylinder3D:
		{
			int phase = (s.ByteA + 0x40) & 0xff;
			if (phase < 0x80)
			{
				int heat = std::min(tables.Sine[phase] + s.Heat, 255);
				put((uint8_t)(s.X + ((tables.Sine[s.ByteA] * s.ByteB) >> 8)), s.Y, heat);
			}
			s.ByteA = (uint8_t)(s.ByteA + s.ByteD);
			break;
		}
		case (uint8_t)ESpark::Lissajous:
		{
			int x = (uint8_t)(s.X + ((tables.Sine[s.ByteA] * s.Heat) >> 8));
			int y = (uint8_t)(s.Y + ((tables.Sine[s.ByteB] * s.Heat) >> 8));
			put(x, y, tables.Light[(uint8_t)(s.ByteA + 0x40)]);
			s.ByteA = (uint8_t)(s.ByteA + s.ByteC);
			s.ByteB = (uint8_t)(s.ByteB + s.ByteD);
			break;
		}
		case (uint8_t)ESpark::Jugglers:
		{
			int heat = std::min(tables.Sine[(s.ByteA + 0x40) & 0xff] + s.Heat, 255);
			put(s.X, (uint8_t)(s.Y + ((tables.Sine[s.ByteA] * s.ByteB) >> 8)), heat);
			s.ByteA = (uint8_t)(s.ByteA + s.ByteD);
			break;
		}
		case (uint8_t)ESpark::Emit:
			if (canEmit && rnd() < 0x40)
				sparks.push_back(makeSpark(DynamicSpark::Drift, s.Heat, s.X, s.Y, s.ByteA, s.ByteB, 0, s.ByteD));
			break;
		case (uint8_t)ESpark::Fountain:
			if (canEmit && rnd() < 0x40)
				sparks.push_back(makeSpark(DynamicSpark::FountainDrop, s.Heat, s.X, s.Y, s.ByteA, s.ByteB, 0, s.ByteD));
			break;
		case (uint8_t)ESpark::Flocks:
			if (canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::Twirl, s.Heat, x, y, 0, s.ByteA, s.ByteB, s.ByteD));
				s.ByteA = (uint8_t)(s.ByteA + s.ByteC);
			}
			jitter(s);
			break;
		case (uint8_t)ESpark::Eels:
			if (rnd() < 0x14 && canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				int a = rnd();
				int b = rnd();
				sparks.push_back(makeSpark(DynamicSpark::Eel, s.Heat, x, y, a, b, s.ByteC, 0));
			}
			wander(s);
			break;
		case (uint8_t)ESpark::Organic:
			if (canEmit && rnd() < 0x80)
			{
				int x = (((rnd() * s.ByteC) >> 8) + s.X) & umask;
				int y = (((rnd() * s.ByteC) >> 8) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::OrganicRise, 0, x, y, rnd() - 0x7f, 0x81, 0xff, 0));
			}
			break;
		case (uint8_t)ESpark::WanderOrganic:
			if (canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::OrganicRise, 0, x, y, rnd() - 0x7f, 0x81, 0xff, 0));
			}
			wander(s);
			break;
		case (uint8_t)ESpark::RandmCloud:
			if (canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::CloudRise, 0, x, y, (rnd() & 0x1f) - 0xf, 0x81, 0, 0));
			}
			wander(s);
			break;
		case (uint8_t)ESpark::CustomCloud:
			if (canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::CloudDrift, 0, x, y, s.ByteA, s.ByteB, s.ByteD, 0));
			}
			jitter(s);
			break;
		case (uint8_t)ESpark::LocalCloud:
			if (canEmit)
			{
				int x = (((rnd() * s.ByteC) >> 8) + s.X) & umask;
				int y = (((rnd() * s.ByteC) >> 8) + s.Y) & vmask;
				sparks.push_back(makeSpark(DynamicSpark::CloudDrift, 0, x, y, s.ByteA, s.ByteB, s.ByteD, 0));
			}
			break;
		case (uint8_t)ESpark::Stars:
			put(s.X, s.Y, s.ByteB);
			break;
		case (uint8_t)ESpark::LineLightning:
		case (uint8_t)ESpark::RampLightning:
			// ByteC counts the frames of one flash. ByteD is how seldom a flash starts.
			if (s.Heat != 0)
			{
				if (s.ByteC == 0)
				{
					if (s.ByteD <= rnd())
						s.ByteC = (uint8_t)((rnd() + 1) & 5);
				}
				else
				{
					s.ByteC--;
					int heatEnd = (s.Type == ESpark::LineLightning) ? s.Heat : (s.Heat >> 3);
					DrawFlashRamp(pixels, width, height, s.X, s.Y, s.ByteA, s.ByteB, s.Heat, heatEnd);
				}
			}
			break;
		case (uint8_t)ESpark::SphereLightning:
			if (s.ByteD <= rnd())
			{
				// A flash from the center to a random point on a circle with the diameter ByteC
				int angle = rnd();
				int radius = s.ByteC;
				int dx = ((tables.Sine[angle] * radius) >> 8) - (radius >> 1);
				int dy = ((tables.Sine[(angle + 0x40) & 0xff] * radius) >> 8) - (radius >> 1);
				int lengthX = (dx < 1) ? ((uint8_t)(-dx) | 1) : ((uint8_t)dx & 0xfe);
				int lengthY = (dy < 1) ? ((uint8_t)(-dy) | 1) : ((uint8_t)dy & 0xfe);
				DrawFlashRamp(pixels, width, height, s.X, s.Y, lengthX, lengthY, s.Heat, s.Heat >> 2);
			}
			break;
		case (uint8_t)ESpark::Wheel:
			if (canEmit)
				sparks.push_back(makeSpark(DynamicSpark::Twirl, s.Heat, s.X, s.Y, 0, s.ByteA, s.ByteB, s.ByteD));
			s.ByteA = (uint8_t)(s.ByteA + s.ByteC);
			break;
		case (uint8_t)ESpark::Gametes:
			if (rnd() < 0x14 && canEmit)
			{
				int x = ((rnd() & 0x1f) + s.X) & umask;
				int y = ((rnd() & 0x1f) + s.Y) & vmask;
				int a = rnd();
				int d = rnd();
				sparks.push_back(makeSpark(DynamicSpark::Gamete, s.Heat, x, y, a, 0, s.ByteC, d));
			}
			wander(s);
			break;
		case (uint8_t)ESpark::Sprinkler:
			if (canEmit)
				sparks.push_back(makeSpark(DynamicSpark::Sprinkle, s.Heat, s.X, s.Y, s.ByteA, s.ByteB, s.ByteC, 2));
			s.ByteA = (uint8_t)(s.ByteA + s.ByteD);
			break;
		case 0x1d: // A point that swings from left to right
			put((uint8_t)(s.X + ((tables.Sine[s.ByteA] * s.Heat) >> 8)), s.Y, tables.Light[(uint8_t)(s.ByteA + 0x40)]);
			s.ByteA = (uint8_t)(s.ByteA + s.ByteC);
			break;
		case 0x1e: // A point that swings up and down
			put(s.X, (uint8_t)(s.Y + ((tables.Sine[s.ByteB] * s.Heat) >> 8)), tables.Light[(uint8_t)(s.ByteB + 0x40)]);
			s.ByteB = (uint8_t)(s.ByteB + s.ByteD);
			break;

		case DynamicSpark::Blaze:
			s.Heat = (uint8_t)(s.Heat - 5);
			if (s.Heat < 0xfb)
			{
				put(s.X, s.Y, s.Heat);
				moveX(s, (int8_t)s.ByteA);
				moveY(s, (int8_t)s.ByteB);
			}
			else
			{
				remove = true;
			}
			break;
		case DynamicSpark::Drift:
			s.Heat = (uint8_t)(s.Heat - s.ByteD);
			if (s.Heat <= s.ByteD)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			break;
		case DynamicSpark::Fall:
			s.ByteC--;
			if (s.ByteC == 0)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			if ((int8_t)s.ByteB < 0x7a)
				s.ByteB = (uint8_t)(s.ByteB + 3);
			break;
		case DynamicSpark::OrganicRise:
		case DynamicSpark::CloudRise:
			if ((uint8_t)s.Type == DynamicSpark::OrganicRise)
			{
				s.ByteC = (uint8_t)(s.ByteC - 3);
				remove = s.ByteC < 0xbf;
			}
			else
			{
				s.ByteC = (uint8_t)(s.ByteC + 4);
				remove = s.ByteC > 0xf9;
			}
			if (remove)
				break;
			put(s.X, s.Y, s.ByteC);
			moveX(s, (int8_t)s.ByteA);
			s.Y = (uint8_t)((s.Y - 2) & vmask);
			break;
		case DynamicSpark::CloudDrift:
			s.ByteC = (uint8_t)(s.ByteC + 4);
			if (s.ByteC > 0xf9)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.ByteC);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			break;
		case DynamicSpark::Eel:
			s.ByteC--;
			if (s.ByteC == 0xff)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			break;
		case DynamicSpark::Twirl:
		{
			s.ByteC--;
			if (s.ByteC == 0xff)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			int speedY = tables.SignedSine[s.ByteB];
			int speedX = tables.SignedSine[(s.ByteB + 0x40) & 0xff];
			// ByteA and ByteB together are the direction as a 16 bit value
			int direction = (s.ByteA | (s.ByteB << 8)) + s.ByteD * 0x10;
			s.ByteA = (uint8_t)direction;
			s.ByteB = (uint8_t)(direction >> 8);
			moveX(s, speedX);
			moveY(s, speedY);
			break;
		}
		case DynamicSpark::Sprinkle:
		{
			s.ByteC--;
			if (s.ByteC == 0xff)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			int speedX = (int8_t)(uint8_t)(tables.Sine[(s.ByteA + 0x40) & 0xff] - 0x80);
			int speedY = (int8_t)s.ByteB;
			s.ByteA = (uint8_t)(s.ByteA + s.ByteD);
			moveX(s, speedX);
			moveY(s, speedY);
			break;
		}
		case DynamicSpark::HeatDrift:
			s.Heat = (uint8_t)(s.Heat - s.ByteC);
			if (s.Heat > 0xf9)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			break;
		case DynamicSpark::FountainDrop:
			s.Heat = (uint8_t)(s.Heat - s.ByteD);
			if (s.Heat < 0x33)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			moveX(s, (int8_t)s.ByteA);
			moveY(s, (int8_t)s.ByteB);
			if ((FrameCount & 1) != 0 && (int8_t)s.ByteB < 0x7c)
				s.ByteB = (uint8_t)(s.ByteB + 3);
			break;
		case DynamicSpark::Gamete:
		{
			s.ByteC--;
			if (s.ByteC == 0xff)
			{
				remove = true;
				break;
			}
			put(s.X, s.Y, s.Heat);
			s.ByteA = (uint8_t)(s.ByteA + 7);
			int swing = s.ByteA & 0x7f;
			if (swing > 0x3f)
				swing = 0x7f - swing;
			int direction = (uint8_t)(s.ByteD + swing);
			int speedY = (int8_t)(uint8_t)(tables.Sine[(uint8_t)(direction + 0x40)] - 0x7f);
			int speedX = (int8_t)(uint8_t)(tables.Sine[direction] - 0x7f);
			moveX(s, speedX);
			moveY(s, speedY);
			break;
		}
		default:
			break;
		}

		if (remove)
		{
			// The last spark of the list takes this place. It moves again in the next frame.
			sparks[i] = sparks.back();
			sparks.pop_back();
		}
		else
		{
			sparks[i] = s;
		}
	}
}

// A star keeps its place bright after the heat of the texture went down there
void UFireTexture::PostDrawSparks(uint8_t* pixels, int width, int height)
{
	const int umask = width - 1;
	const int vmask = height - 1;
	for (Spark& s : Sparks)
	{
		if (s.Type == ESpark::Stars)
		{
			uint8_t& pixel = pixels[(s.Y & vmask) * width + (s.X & umask)];
			s.ByteB = pixel;
			if (pixel < 0x26)
				pixel = s.ByteA;
		}
	}
}

// A flash of lightning: a line with random steps to the side, and a heat that changes from heatStart to heatEnd.
// lengthX and lengthY are the lengths in pixels. Bit 0 of a length is the direction: 1 is to the left or up.
// This does the same as UFireTexture::DrawFlashRamp of the original Fire.dll.
void UFireTexture::DrawFlashRamp(uint8_t* pixels, int width, int height, int x, int y, int lengthX, int lengthY, int heatStart, int heatEnd)
{
	const int umask = width - 1;
	const int vmask = height - 1;

	int steps = std::max(lengthX, lengthY) | 1;
	uint8_t side[256];
	int sideSum = 0;
	for (int i = 0; i < steps; i++)
	{
		side[i] = (uint8_t)RandomByteValue();
		sideSum += side[i];
	}

	int stepX = (lengthX & 1) ? -1 : 1;
	int stepY = (lengthY & 1) ? -1 : 1;
	int heat = heatStart << 23;
	int heatStep = ((heatEnd - heatStart) * 0x800000) / steps;

	if (lengthX < lengthY)
	{
		// The line goes more up or down than to the side. The random steps move X, and they end at the X of the end point.
		int sideStep = (stepX * lengthX * 0x40 - sideSum) / steps;
		int fixedX = x << 6;
		for (int i = 0; i < lengthY; i++)
		{
			fixedX += side[i] + sideStep;
			heat += heatStep;
			pixels[(y & vmask) * width + ((fixedX >> 6) & umask)] = (uint8_t)(heat >> 23);
			y += stepY;
		}
	}
	else
	{
		int sideStep = (stepY * lengthY * 0x40 - sideSum) / steps;
		int fixedY = y << 6;
		for (int i = 0; i < lengthX; i++)
		{
			fixedY += side[i] + sideStep;
			heat += heatStep;
			pixels[((fixedY >> 6) & vmask) * width + (x & umask)] = (uint8_t)(heat >> 23);
			x += stepX;
		}
	}
}
