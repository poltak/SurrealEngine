
#include "Precomp.h"
#include "UGameDirectory.h"
#include "Utils/Logger.h"
#include "Utils/StrTools.h"
#include "Engine.h"
#include "GC/GC.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include <algorithm>

void UDXGameDirectory::GetGameDirectory()
{
	directoryList.clear();

	std::error_code ec;
	if (GameDirectoryType() == EGameDirectoryTypes::GD_Maps)
	{
		// The map files, with their extension
		std::string mapExt = "." + engine->packages->GetMapExtension();
		fs::path mapsFolder = fs::path(engine->LaunchInfo.gameRootFolder) / "Maps";
		if (fs::is_directory(mapsFolder, ec))
		{
			for (const auto& entry : fs::directory_iterator(mapsFolder, ec))
			{
				if (entry.is_regular_file(ec) && StrTools::equals_ignore_case(entry.path().extension().string(), mapExt))
					directoryList.push_back(entry.path().filename().string());
			}
		}
	}
	else
	{
		// The folders of the numbered save games. QuickSave and Current are not in this list.
		fs::path saveFolder = engine->packages->GetSaveFolderPath();
		if (fs::is_directory(saveFolder, ec))
		{
			for (const auto& entry : fs::directory_iterator(saveFolder, ec))
			{
				if (entry.is_directory(ec) && StrTools::startswith(entry.path().filename().string(), "Save", true))
					directoryList.push_back(entry.path().filename().string());
			}
		}
	}

	// The order of a folder listing is not the same on each file system
	std::sort(directoryList.begin(), directoryList.end());
}

int UDXGameDirectory::GetNewSaveFileIndex()
{
	// The highest number of a save folder in the list, plus one
	int highest = 0;
	for (const std::string& name : directoryList)
	{
		if (name.size() > 4 && StrTools::startswith(name, "Save", true))
			highest = std::max(highest, std::atoi(name.c_str() + 4));
	}
	return highest + 1;
}

std::string UDXGameDirectory::GenerateSaveFilename(int saveIndex)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "Save%04d", saveIndex);
	return buf;
}

std::string UDXGameDirectory::GenerateNewSaveFileName(std::optional<int> newIndex)
{
	return GenerateSaveFilename((newIndex && *newIndex > 0) ? *newIndex : GetNewSaveFileIndex());
}

int UDXGameDirectory::GetDirCount()
{
	return (int)directoryList.size();
}

std::string UDXGameDirectory::GetDirFilename(int fileIndex)
{
	if (fileIndex < 0 || (size_t)fileIndex >= directoryList.size())
		return {};
	return directoryList[fileIndex];
}

void UDXGameDirectory::SetDirType(EGameDirectoryTypes newDirType)
{
	GameDirectoryType() = newDirType;
}

void UDXGameDirectory::SetDirFilter(const std::string& strFilter)
{
	CurrentFilter() = strFilter;
}

UDXSaveInfo* UDXGameDirectory::LoadSaveInfo(const fs::path& saveFolder)
{
	fs::path file = saveFolder / ("SaveInfo." + engine->packages->GetSaveExtension());
	std::error_code ec;
	if (!fs::is_regular_file(file, ec))
		return nullptr;

	UDXSaveInfo* saveInfo = nullptr;
	try
	{
		Package* package = GC::Alloc<Package>(engine->packages.get(), "SaveInfo", file.string());
		saveInfo = TryCast<UDXSaveInfo>(package->GetUObject("DeusExSaveInfo", "MyDeusExSaveInfo"));
		// The file must not stay open. A save to the same slot replaces it.
		engine->packages->UnloadPackage(package);
	}
	catch (const std::exception& e)
	{
		LogMessage("Could not read " + file.string() + ": " + e.what());
		return nullptr;
	}

	if (saveInfo)
		loadedSaveInfos.push_back(saveInfo);
	return saveInfo;
}

// fileIndex is the number of the save game. -1 is the quick save.
UDXSaveInfo* UDXGameDirectory::GetSaveInfo(int fileIndex)
{
	return LoadSaveInfo(engine->DXSaveDirectory(fileIndex));
}

// DirectoryIndex is a position in the folder list
UDXSaveInfo* UDXGameDirectory::GetSaveInfoFromDirectoryIndex(int DirectoryIndex)
{
	if (DirectoryIndex < 0 || (size_t)DirectoryIndex >= directoryList.size())
		return nullptr;
	return LoadSaveInfo(engine->packages->GetSaveFolderPath() / directoryList[DirectoryIndex]);
}

UDXSaveInfo* UDXGameDirectory::GetTempSaveInfo()
{
	if (!TempSaveInfo())
	{
		UClass* cls = engine->packages->FindClass("DeusEx.DeusExSaveInfo");
		if (cls)
			TempSaveInfo() = Cast<UDXSaveInfo>(engine->packages->GetTransientPackage()->NewObject("DeusExSaveInfo", cls, ObjectFlags::Transient));
	}
	return TempSaveInfo();
}

// Releases a save info object that GetSaveInfo returned. The save game stays on the disk.
void UDXGameDirectory::DeleteSaveInfo(UDXSaveInfo* saveInfo)
{
	if (!saveInfo)
		return;
	auto it = std::find(loadedSaveInfos.begin(), loadedSaveInfos.end(), saveInfo);
	if (it != loadedSaveInfos.end())
		loadedSaveInfos.erase(it);
}

void UDXGameDirectory::PurgeAllSaveInfo()
{
	loadedSaveInfos.clear();
}

// The free space of the save disk in kilobytes
int UDXGameDirectory::GetSaveFreeSpace()
{
	std::error_code ec;
	fs::space_info info = fs::space(engine->packages->GetSaveFolderPath(), ec);
	if (ec)
		return 0;
	return (int)std::min<uintmax_t>(info.available / 1024, 0x7fffffff);
}

// The size of one save game in kilobytes
int UDXGameDirectory::GetSaveDirectorySize(int saveIndex)
{
	std::error_code ec;
	fs::path folder = engine->DXSaveDirectory(saveIndex);
	if (!fs::is_directory(folder, ec))
		return 0;

	uintmax_t size = 0;
	for (const auto& entry : fs::directory_iterator(folder, ec))
	{
		if (entry.is_regular_file(ec))
			size += entry.file_size(ec) / 1024;
	}
	return (int)std::min<uintmax_t>(size, 0x7fffffff);
}
