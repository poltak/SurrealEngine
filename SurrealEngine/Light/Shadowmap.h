#pragma once

class UModel;
class BspSurface;

class Shadowmap
{
public:
	Shadowmap();

	// Loads the shadow map of one light for a block of texels (the limits are inclusive). The texels outside the block keep old values.
	void Load(UModel* model, int lightMap, int lightindex, int x0, int y0, int x1, int y1);
	void Clear(UModel* model, int lightMap);

	int Width() const { return width; }
	int Height() const { return height; }
	const float* Pixels() const { return pixels.data(); }

private:
	int width = 0;
	int height = 0;
	Array<float> pixels;
	Array<float> tempbuf;
	Array<float> blurTable;
};
