#pragma once

#include "UDeusExSaveInfo.h"
#include "Utils/Array.h"

#include <filesystem>

namespace fs = std::filesystem;

enum class EGameDirectoryTypes : uint8_t
{
	GD_Maps,
	GD_SaveGames
};

// The list of map files or of save game folders. It follows XGameDirectory in DeusEx.dll.
class UDXGameDirectory : public UObject
{
public:
	using UObject::UObject;

	EGameDirectoryTypes& GameDirectoryType() { return Value<EGameDirectoryTypes>(PropOffsets_GameDirectory.gameDirectoryType); }
	std::string& CurrentFilter() { return Value<std::string>(PropOffsets_GameDirectory.currentFilter); }
	UDXSaveInfo*& TempSaveInfo() { return Value<UDXSaveInfo*>(PropOffsets_GameDirectory.tempSaveInfo); }

	void GetGameDirectory(); // Depends on what GameDirectoryType is set
	int GetNewSaveFileIndex();
	std::string GenerateSaveFilename(int saveIndex);
	std::string GenerateNewSaveFileName(std::optional<int> newIndex);
	int GetDirCount();
	std::string GetDirFilename(int fileIndex);
	void SetDirType(EGameDirectoryTypes newDirType);
	void SetDirFilter(const std::string& strFilter);
	UDXSaveInfo* GetSaveInfo(int fileIndex);
	UDXSaveInfo* GetSaveInfoFromDirectoryIndex(int DirectoryIndex);
	UDXSaveInfo* GetTempSaveInfo();
	void DeleteSaveInfo(UDXSaveInfo* saveInfo);
	void PurgeAllSaveInfo();
	int GetSaveFreeSpace();
	int GetSaveDirectorySize(int saveIndex);

private:
	UDXSaveInfo* LoadSaveInfo(const fs::path& saveFolder);

	// The script declares directoryList and loadedSaveInfoPointers as native arrays. A script cannot read them,
	// so the lists are members here.
	Array<std::string> directoryList;
	Array<UDXSaveInfo*> loadedSaveInfos;
};
