
#include "Precomp.h"
#include "GameWindow.h"
#include "LauncherSettings.h"
#include "RenderDevice/RenderDevice.h"
#include <surrealgpu/vulkansurface.h>
#include <surrealgpu/vulkancompatibledevice.h>
#include <surrealgpu/vulkanbuilders.h>

GameWindow::GameWindow(GameWindowHost* windowHost, RenderAPI renderAPI) : Widget(nullptr, WidgetType::Window, renderAPI), windowHost(windowHost)
{
	SetWindowIcon({
		Image::LoadResource("surreal-engine-icon-16.png"),
		Image::LoadResource("surreal-engine-icon-24.png"),
		Image::LoadResource("surreal-engine-icon-32.png"),
		Image::LoadResource("surreal-engine-icon-48.png"),
		Image::LoadResource("surreal-engine-icon-64.png"),
		Image::LoadResource("surreal-engine-icon-128.png"),
		Image::LoadResource("surreal-engine-icon-256.png")
		});

	device = RenderDevice::Create(this, renderAPI);
	SetCanvas(std::make_unique<RenderDeviceCanvas>(device.get()));
	SetFocus();
}

RenderDevice* GameWindow::GetRenderDevice()
{
	return device.get();
}

int GameWindow::GetPixelWidth()
{
	return GetNativePixelWidth();
}

int GameWindow::GetPixelHeight()
{
	return GetNativePixelHeight();
}

void GameWindow::ToggleWindowFullscreen(Size newResolution)
{
#ifdef __APPLE__
	// A window on macOS goes to a fullscreen space of its own and always fills the screen there. macOS keeps the
	// frame of the window and puts it back later. A change of the size here would replace that frame.
	if (this->IsFullscreen())
		this->ShowNormal();
	else
		this->ShowFullscreen();
	return;
#endif

	if (this->IsFullscreen())
	{
		// First switch to normal
		this->ShowNormal();

		// Then resize the window
		auto geometry = this->GetFrameGeometry();
		geometry.width = newResolution.width;
		geometry.height = newResolution.height;
		this->SetFrameGeometry(geometry);
	}
	else
	{
		// Get the nearest valid fullscreen resolution first
		auto selectedResolution = GetClosestResolution(newResolution);

		// Then resize the window to the resolution
		auto geometry = this->GetFrameGeometry();
		geometry.width = selectedResolution.width;
		geometry.height = selectedResolution.height;
		this->SetFrameGeometry(geometry);

		// Finally switch to fullscreen
		this->ShowFullscreen();
	}
}

bool GameWindow::GetKeyState(EInputKey key)
{
	return Widget::GetKeyState((InputKey)key);
}

Array<Size> GameWindow::QueryAvailableResolutions() const
{
	return { DisplayBackend::Get()->GetScreenSize() };
}

void GameWindow::OnPaint(Canvas* canvas)
{
	windowHost->OnWindowPaint();
}

bool GameWindow::OnMouseDown(const Point& pos, InputKey key)
{
	windowHost->OnWindowMouseDown(pos, (EInputKey)key);
	return true;
}

bool GameWindow::OnMouseDoubleclick(const Point& pos, InputKey key)
{
	windowHost->OnWindowMouseDoubleclick(pos, (EInputKey)key);
	return true;
}

bool GameWindow::OnMouseUp(const Point& pos, InputKey key)
{
	windowHost->OnWindowMouseUp(pos, (EInputKey)key);
	return true;
}

bool GameWindow::OnMouseWheel(const Point& pos, InputKey key)
{
	windowHost->OnWindowMouseWheel(pos, (EInputKey)key);
	return true;
}

void GameWindow::OnMouseMove(const Point& pos)
{
	windowHost->OnWindowMouseMove(pos);
}

void GameWindow::OnRawMouseMove(int dx, int dy)
{
	windowHost->OnWindowRawMouseMove(dx, dy);
}

void GameWindow::OnKeyChar(std::string chars)
{
	windowHost->OnWindowKeyChar(chars);
}

void GameWindow::OnKeyDown(InputKey key)
{
	windowHost->OnWindowKeyDown((EInputKey)key);
}

void GameWindow::OnKeyUp(InputKey key)
{
	windowHost->OnWindowKeyUp((EInputKey)key);
}

void GameWindow::OnGeometryChanged()
{
	windowHost->OnWindowGeometryChanged();
}

void GameWindow::OnClose()
{
	windowHost->OnWindowClose();
}

void GameWindow::OnSetFocus()
{
	windowHost->OnWindowActivated();
}

void GameWindow::OnLostFocus()
{
	windowHost->OnWindowDeactivated();
}

std::unique_ptr<GameWindow> GameWindow::Create(GameWindowHost* windowHost)
{
	RenderAPI api;
	switch (LauncherSettings::Get().RenderDevice.Type)
	{
	default:
	case RenderDeviceType::Vulkan: api = RenderAPI::Vulkan; break;
	case RenderDeviceType::D3D11: api = RenderAPI::D3D11; break;
	case RenderDeviceType::D3D12: api = RenderAPI::D3D12; break;
	case RenderDeviceType::OpenGL: api = RenderAPI::OpenGL; break;
	}
	return std::make_unique<GameWindow>(windowHost, api);
}

void GameWindow::ProcessEvents()
{
	DisplayBackend::Get()->ProcessEvents();
}

void GameWindow::RunLoop()
{
	DisplayBackend::Get()->RunLoop();
}

void GameWindow::ExitLoop()
{
	DisplayBackend::Get()->ExitLoop();
}

// The largest N for which a window with a content area of N x 640 by N x 480 pixels fits on the screen (1 or more)
int GameWindow::GetLargestWindowMultiple()
{
	Size screen = DisplayBackend::Get()->GetScreenSize();
	double dpiScale = std::max(GetDpiScale(), 1.0);
	const double reservedHeight = 110.0; // menu bar or task bar, title bar of the window and a part of the Dock, in points
	int multiple = (int)std::min(std::floor(screen.width * dpiScale / 640.0), std::floor((screen.height - reservedHeight) * dpiScale / 480.0));
	return std::max(multiple, 1);
}

// The list for the display settings screen of a game, in pixels, with a space between the entries.
// It has each whole multiple of 640x480 that fits on the screen as a window, and the size of the window now.
// The settings screen of Deus Ex shows an empty value if the size of the window is not in the list.
std::string GameWindow::GetAvailableResolutions()
{
	Array<Size> resolutions;
	int largest = GetLargestWindowMultiple();
	for (int multiple = 1; multiple <= largest; multiple++)
		AddResolutionIfNotAdded(resolutions, Size(multiple * 640, multiple * 480));
	if (GetPixelWidth() >= 640 && GetPixelHeight() >= 480)
		AddResolutionIfNotAdded(resolutions, Size(GetPixelWidth(), GetPixelHeight()));

	std::sort(resolutions.begin(), resolutions.end(), [](const Size& a, const Size& b) { return a.width != b.width ? a.width < b.width : a.height < b.height; });

	std::string result;
	for (const Size& res : resolutions)
	{
		if (!result.empty())
			result += " ";
		result += std::to_string(int(res.width)) + "x" + std::to_string(int(res.height));
	}
	return result;
}

void GameWindow::AddResolutionIfNotAdded(Array<Size>& resList, Size resolution) const
{
	// Skip over the current resolution if it is already inserted
	// (in case of multiple refresh rates being available for the display)
	for (auto& res : resList)
	{
		if (resolution == res)
			return;
	}

	// Add the resolution, as it is not added before
	resList.push_back(resolution);
}

Size GameWindow::ParseResolutionString(const std::string& resolutionString) const
{
	if (resolutionString.empty())
		return Size(0, 0);

#ifdef WIN32
	int width, height;
	int parsedDataCount = sscanf_s(resolutionString.c_str(), "%dx%d", &width, &height);
#else
	int width, height;
	int parsedDataCount = sscanf(resolutionString.c_str(), "%dx%d", &width, &height);
#endif

	// Handle incorrect parsings
	// sscanf() returns EOF on input failure, or the amount of "data processed" otherwise
	// Since we want to process width and height of a window, we expect it to return 2
	if (parsedDataCount == EOF || parsedDataCount != 2)
	{
		return Size(0, 0);
	}

	// Resolution shouldn't be smaller than 640x480
	if (width < 640 || height < 480)
	{
		// Maybe produce a log here too?
		return Size(0, 0);
	}

	return Size(width, height);
}

Size GameWindow::GetClosestResolution(Size resolution) const
{
	auto resolutions = QueryAvailableResolutions();

	if (resolutions.empty())
		return resolution;

	if (resolutions.size() == 1)
		return resolutions[0];

	int index = 0;
	double minDist = std::abs(pow(resolutions[0].width - resolution.width, 2) + pow(resolutions[0].height - resolution.height, 2));

	for (int i = 1; i < resolutions.size(); i++)
	{
		auto& currRes = resolutions[i];

		double dist = std::abs(pow(currRes.width - resolution.width, 2) + pow(currRes.height - resolution.height, 2));

		if (currRes == resolution)
			return resolutions[i];

		if (dist < minDist)
		{
			minDist = dist;
			index = i;
		}
	}

	return resolutions[index];
}

void GameWindow::SetResolution(const std::string& resolutionString)
{
	Size parsedResolution = ParseResolutionString(resolutionString);
	if (parsedResolution == Size(0, 0))
		return;

#ifdef __APPLE__
	// A fullscreen window on macOS always has the size of the screen
	if (IsFullscreen())
		return;
#endif

	Rect windowRect = GetFrameGeometry();

	if (IsFullscreen())
	{
		parsedResolution = GetClosestResolution(parsedResolution);
		windowRect.x = 0;
		windowRect.y = 0;
	}
		
	auto dpi = GetDpiScale();
	windowRect.width = parsedResolution.width / dpi;
	windowRect.height = parsedResolution.height / dpi;

	SetFrameGeometry(windowRect);
}
