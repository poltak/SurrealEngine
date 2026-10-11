
#include "Precomp.h"
#include "RenderSubsystem.h"
#include "RenderDevice/RenderDevice.h"
#include "GameWindow.h"
#include "Packages/Engine/USurrealClient.h"
#include "Packages/Engine/UConsole.h"
#include "Packages/Engine/UViewport.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Resources/UPalette.h"
#include "Packages/Engine/Resources/Level/UModel.h"
#include "Packages/Engine/Resources/Level/UPolys.h"
#include "VM/ScriptCall.h"
#include "Engine.h"
#include "Utils/CommandLine.h"
#include "Utils/Logger.h"

RenderSubsystem::RenderSubsystem(RenderDevice* renderdevice) : Device(renderdevice)
{
}

void RenderSubsystem::DrawGame(float levelTimeElapsed)
{
	LevelTimeElapsed = levelTimeElapsed;
	AutoUV += levelTimeElapsed * 64.0f;
	engine->Level->Light.Tick(levelTimeElapsed);

	Stats.Frames = 0;
	Stats.Surfaces = 0;
	Stats.Actors = 0;
	Stats.LightmapsUpdated = 0;

	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);

	UPlayerPawn* player = UObject::TryCast<UPlayerPawn>(engine->CameraActor);
	if (player)
	{
		flashScale = player->FlashScale();
		flashFog = player->FlashFog();
	}

	Device->Brightness = engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);

	ResetCanvas();
	PreRender();

	if (engine->LaunchInfo.ue1Version <= 219 || engine->console->bNoDrawWorld() == false)
	{
		DrawScene();
		RenderOverlays();
		if (engine->LaunchInfo.IsDeusEx())
			PostRenderFlash();
		Device->EndFlash();
	}

	PostRender();

	Device->Unlock(true);

	// Local test harness (fork only): write the frame as a 32 bit BMP at a fixed interval.
	// ReadPixels submits the command buffer, so this must run after Unlock (outside the scene render pass).
	if (commandline)
	{
		static const std::string shotDir = commandline->GetArg({}, "--shots");
		static const double shotInterval = commandline->GetArgDouble({}, "--shot-interval", 5.0);
		static double nextShot = commandline->GetArgDouble({}, "--shot-first", 2.0);
		static int shotIndex = 0;
		if (!shotDir.empty() && engine->TotalTime >= nextShot)
		{
			nextShot = engine->TotalTime + shotInterval;
			int w = engine->window->GetPixelWidth();
			int h = engine->window->GetPixelHeight();
			std::vector<TextureColor> pixels((size_t)w * h);
			Device->ReadPixels(pixels.data());
			char name[64];
			snprintf(name, sizeof(name), "/shot%03d.bmp", shotIndex++);
			if (FILE* f = fopen((shotDir + name).c_str(), "wb"))
			{
				uint32_t dataSize = (uint32_t)w * h * 4;
				uint8_t hdr[54] = { 'B', 'M' };
				auto put32 = [&](int o, uint32_t v) { memcpy(hdr + o, &v, 4); };
				auto put16 = [&](int o, uint16_t v) { memcpy(hdr + o, &v, 2); };
				put32(2, 54 + dataSize); put32(10, 54); put32(14, 40);
				put32(18, (uint32_t)w); put32(22, (uint32_t)-h);
				put16(26, 1); put16(28, 32); put32(34, dataSize);
				fwrite(hdr, 1, 54, f);
				fwrite(pixels.data(), 1, dataSize, f);
				fclose(f);
				LogMessage(std::string("Harness: wrote ") + name);
			}
		}
	}
}

void RenderSubsystem::DrawEditorViewport()
{
	Device->Brightness = engine->client->Brightness;
	DrawScene();
}

void RenderSubsystem::DrawVideoFrame(TextureInfo* frame, TextureInfo* background)
{
	vec3 flashScale = 0.5f;
	vec3 flashFog = vec3(0.0f, 0.0f, 0.0f);
	Device->Brightness = 0.4f;// engine->client->Brightness;
	Device->Lock(vec4(flashScale, 1.0f), vec4(flashFog, 1.0f), vec4(0.0f), nullptr, nullptr);
	ResetCanvas();
	Device->SetSceneNode(&Canvas.Frame);

	float sizeX = (float)(int)(engine->viewport->ViewportWidth() / (float)Canvas.uiscale);
	float sizeY = (float)(int)(engine->viewport->ViewportHeight() / (float)Canvas.uiscale);

	Rectf clipBox = Rectf::xywh(0.0f, 0.0f, sizeX, sizeY);
	Rectf dest = clipBox;

	if (frame)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)frame->USize, (float)frame->VSize);
		DrawTile(*frame, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided);
	}

	if (background)
	{
		Rectf src = Rectf::xywh(0.0f, 0.0f, (float)background->USize, (float)background->VSize);
		DrawTile(*background, dest, src, clipBox, 1.0f, vec4(1.0f), vec4(0.0f), PF_TwoSided | PF_Highlighted);
	}

	Device->EndFlash();
	Device->Unlock(true);
}

void RenderSubsystem::UpdateTexture(UTexture* tex)
{
	if (tex && tex->FrameCounter != TextureFrameCounter)
	{
		tex->Update(LevelTimeElapsed);
		tex->FrameCounter = TextureFrameCounter;
	}
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, BspSurface& surface, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)surface.PanU;
	info.Pan.y = -(float)surface.PanV;
	if (surface.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (surface.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, const Poly& poly, UTexture* texture, float ZoneUPanSpeed, float ZoneVPanSpeed)
{
	UpdateTextureInfo(info, texture);

	info.Pan.x = -(float)poly.PanU;
	info.Pan.y = -(float)poly.PanV;
	if (poly.PolyFlags & PF_AutoUPan) info.Pan.x -= AutoUV * ZoneUPanSpeed;
	if (poly.PolyFlags & PF_AutoVPan) info.Pan.y -= AutoUV * ZoneVPanSpeed;
}

void RenderSubsystem::UpdateTextureInfo(TextureInfo& info, UTexture* texture)
{
	info.Texture = texture;
	info.CacheID = (uint64_t)(ptrdiff_t)texture;

	if (!info.Texture)
		return;

	info.UScale = texture->DrawScale();
	info.VScale = texture->DrawScale();
	info.Format = texture->UsedFormat;
	info.Mips = texture->UsedMipmaps.data();
	info.NumMips = (int)texture->UsedMipmaps.size();
	info.USize = texture->USize();
	info.VSize = texture->VSize();
	if (texture->Palette())
		info.Palette = (TextureColor*)texture->Palette()->Colors.data();

	info.bRealtimeChanged = texture->TextureModified;
	if (texture->TextureModified)
		texture->TextureModified = false;
}

void RenderSubsystem::OnMapLoaded()
{
	Device->Flush(true);
	engine->Level->Light.OnMapLoaded();
}
