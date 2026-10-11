
#include "Precomp.h"
#include "UE1GameDatabase.h"
#include "Utils/SHA1Sum.h"
#include <iostream>

// True if the System folder has the script packages of Deus Ex
static bool IsDeusExFolder(const fs::path& systemPath)
{
	std::error_code ec;
	return fs::is_regular_file(systemPath / "DeusEx.u", ec) && fs::is_regular_file(systemPath / "Extension.u", ec) && fs::is_regular_file(systemPath / "DeusExUI.u", ec);
}

std::pair<KnownUE1Games, std::string> FindUE1GameInPath(const std::string& ue1_game_root_folder_path)
{
	if (ue1_game_root_folder_path.empty())
		return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");

	if (!fs::exists(ue1_game_root_folder_path) || !fs::is_directory(ue1_game_root_folder_path))
		return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");

	const auto UE1GameSystemPath = fs::path(ue1_game_root_folder_path) / "System";
	const auto UE1GameSystem64Path = fs::path(ue1_game_root_folder_path) / "System64";

	for (auto& executable_name : knownUE1ExecutableNames)
	{
		auto executablePath = UE1GameSystemPath / executable_name;
		auto executablePath64 = UE1GameSystem64Path / executable_name;

		std::string sha1sum = SHA1Sum::of_file(executablePath);
		std::string sha1sum64 = SHA1Sum::of_file(executablePath64);

		// If sha1sum is empty this means that the file doesn't exist
		if (sha1sum.empty() && sha1sum64.empty())
			continue;

		// Now check whether there is a match within the database or not
		const auto it = !sha1sum64.empty() ? SHA1Database.find(sha1sum64) : SHA1Database.find(sha1sum);
		
		if (it == SHA1Database.end())
		{
			// A mod launcher often replaces DeusEx.exe (Kentie's Deus Exe, GMDX, the community update).
			// The engine does not use the program file, so the script packages identify the game then.
			if (IsDeusExFolder(UE1GameSystemPath))
				return std::make_pair(KnownUE1Games::DEUS_EX_1112fm, "DeusEx.exe");
			return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");
		}

		// Hack: Tactical-Ops has a version that contains the exact same UTv469d executable. Handle that here
		if (it->second == KnownUE1Games::UT99_469d && executable_name == "TacticalOps.exe")
			return std::make_pair(KnownUE1Games::TACTICAL_OPS_469, executable_name);
		return std::make_pair(it->second, executable_name);
	}

	// A Deus Ex folder with no program file at all, for example files that a player copied from another computer
	if (IsDeusExFolder(UE1GameSystemPath))
		return std::make_pair(KnownUE1Games::DEUS_EX_1112fm, "DeusEx.exe");

	// We got nothing here
	return std::make_pair(KnownUE1Games::UE1_GAME_NOT_FOUND, "");
}