#include "Precomp.h"
#include "UFlagBase.h"
#include "UFlag.h"
#include "Engine.h"
#include "Package/PackageManager.h"
#include "Packages/Extension/Flags/UFlagBool.h"
#include "Packages/Extension/Flags/UFlagByte.h"
#include "Packages/Extension/Flags/UFlagFloat.h"
#include "Packages/Extension/Flags/UFlagInt.h"
#include "Packages/Extension/Flags/UFlagName.h"
#include "Packages/Extension/Flags/UFlagRotator.h"
#include "Packages/Extension/Flags/UFlagVector.h"

// The flag store follows XFlagBase in Extension.dll (build 1112fm):
//
// - hashTable has 64 buckets. The bucket index is the low 6 bits of the flag hash.
// - Each bucket is a chain through Flag.nextFlag. A chain is in ascending order of the signed hash.
//   Flags with the same name are in ascending order of the flag type.
// - A flag with expiration 0 does not expire. DeleteExpiredFlags(n) removes each flag that has an
//   expiration from 1 to n.

namespace
{
	// appStrihash of the original engine: a CRC of the upper case name, two steps for each 16 bit character
	uint32_t FlagNameHash(const NameString& name)
	{
		static uint32_t table[256];
		static bool tableReady = false;
		if (!tableReady)
		{
			for (uint32_t i = 0; i < 256; i++)
			{
				uint32_t c = i << 24;
				for (int j = 0; j < 8; j++)
					c = (c & 0x80000000) ? (c << 1) ^ 0x04c11db7 : (c << 1);
				table[i] = c;
			}
			tableReady = true;
		}

		uint32_t hash = 0;
		for (char ch : name.ToString())
		{
			uint32_t c = (uint8_t)ch;
			if (c >= 'a' && c <= 'z')
				c -= 0x20;
			hash = table[(hash ^ c) & 0xff] ^ (hash >> 8);
			hash = table[(hash ^ (c >> 8)) & 0xff] ^ (hash >> 8);
		}
		return hash;
	}
}

// Returns the flag with this name and type, or null. "link" gets the chain position where a new flag with this name and type belongs.
UFlag* UFlagBase::FindName(const NameString& flagName, uint8_t flagType, UFlag**& link)
{
	uint32_t hash = FlagNameHash(flagName);
	UFlag** bucket = &hashTable()[hash & (HashTableSize - 1)];

	UFlag* found = nullptr;
	UFlag* prev = nullptr;
	for (UFlag* cur = *bucket; cur; prev = cur, cur = cur->nextFlag())
	{
		if ((int32_t)hash < cur->flagHash())
			break;

		if ((uint32_t)cur->flagHash() == hash && cur->FlagName() == flagName)
		{
			if (cur->flagType() > flagType)
				break;
			if (cur->flagType() == flagType)
			{
				found = cur;
				break;
			}
		}
	}

	link = prev ? &prev->nextFlag() : bucket;
	return found;
}

UFlag* UFlagBase::GetFlag(const NameString& flagName, uint8_t flagType)
{
	UFlag** link = nullptr;
	return FindName(flagName, flagType, link);
}

void UFlagBase::AddFlag(UFlag* flag)
{
	if (!flag || flag->FlagBase() == this)
		return;

	if (flag->FlagBase())
		flag->FlagBase()->RemoveFlag(flag);

	UFlag** link = nullptr;
	while (UFlag* existing = FindName(flag->FlagName(), flag->flagType(), link))
	{
		if (existing == flag)
			return;
		RemoveFlag(existing);
	}

	flag->nextFlag() = *link;
	*link = flag;
	flag->FlagBase() = this;
}

void UFlagBase::RemoveFlag(UFlag* flag)
{
	if (!flag || flag->FlagBase() != this)
		return;

	UFlag** link = &hashTable()[(uint32_t)flag->flagHash() & (HashTableSize - 1)];
	while (*link && *link != flag)
		link = &(*link)->nextFlag();

	if (*link == flag)
	{
		*link = flag->nextFlag();
		flag->nextFlag() = nullptr;
		flag->FlagBase() = nullptr;
	}
}

bool UFlagBase::DeleteFlag(const NameString& FlagName, uint8_t flagType)
{
	UFlag* flag = GetFlag(FlagName, flagType);
	if (!flag)
		return false;
	RemoveFlag(flag);
	return true;
}

void UFlagBase::DeleteAllFlags()
{
	auto table = hashTable();
	for (int i = 0; i < HashTableSize; i++)
	{
		while (table[i])
		{
			UFlag* flag = table[i];
			table[i] = flag->nextFlag();
			flag->nextFlag() = nullptr;
			flag->FlagBase() = nullptr;
		}
	}
}

void UFlagBase::DeleteExpiredFlags(int criteria)
{
	int iterator = CreateIterator((uint8_t)AllFlagTypes);

	NameString flagName;
	uint8_t flagType = 0;
	bool more = GetNextFlag(iterator, flagName, flagType);
	while (more)
	{
		// Get the next flag before the current flag goes out of its chain
		NameString curName = flagName;
		uint8_t curType = flagType;
		more = GetNextFlag(iterator, flagName, flagType);

		UFlag* flag = GetFlag(curName, curType);
		if (flag && flag->expiration() != 0 && flag->expiration() <= criteria)
			RemoveFlag(flag);
	}

	DestroyIterator(iterator);
}

bool UFlagBase::CheckFlag(const NameString& FlagName, uint8_t flagType)
{
	return GetFlag(FlagName, flagType) != nullptr;
}

int UFlagBase::CreateIterator(std::optional<uint8_t> flagType)
{
	int handle = NextIterator++;
	FlagIterator flagIt;
	flagIt.FlagType = flagType ? *flagType : AllFlagTypes;
	FlagIterators[handle] = flagIt;
	return handle;
}

void UFlagBase::DestroyIterator(int Iterator)
{
	FlagIterators.erase(Iterator);
}

bool UFlagBase::GetNextFlag(int Iterator, NameString& FlagName, uint8_t& flagType)
{
	FlagName = {}; // ResetFlags() expects this to be set to '' if no more flags were found
	flagType = 0;

	auto it = FlagIterators.find(Iterator);
	if (it == FlagIterators.end())
		return false;

	FlagIterator& flagIt = it->second;
	auto table = hashTable();

	// A flag that left its chain has no next flag. The search then continues with the next bucket, as in the original.
	UFlag* cur = flagIt.Current ? flagIt.Current->nextFlag() : nullptr;
	int bucket = flagIt.Bucket;
	bool found = false;

	while (bucket < HashTableSize)
	{
		if (!cur)
		{
			bucket++;
			if (bucket >= HashTableSize)
				break;
			cur = table[bucket];
			continue;
		}

		if (flagIt.FlagType < 0 || flagIt.FlagType >= AllFlagTypes || cur->flagType() == flagIt.FlagType)
		{
			found = true;
			break;
		}

		cur = cur->nextFlag();
	}

	flagIt.Bucket = bucket;
	flagIt.Current = found ? cur : nullptr;

	if (found)
	{
		FlagName = cur->FlagName();
		flagType = cur->flagType();
	}
	return found;
}

bool UFlagBase::GetNextFlagName(int Iterator, NameString& FlagName)
{
	uint8_t flagType = 0;
	return GetNextFlag(Iterator, FlagName, flagType);
}

bool UFlagBase::GetBool(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Bool))
		return UObject::Cast<UFlagBool>(flag)->bValue();
	return false;
}

uint8_t UFlagBase::GetByte(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Byte))
		return UObject::Cast<UFlagByte>(flag)->byteValue();
	return 0;
}

int UFlagBase::GetExpiration(const NameString& FlagName, uint8_t flagType)
{
	if (UFlag* flag = GetFlag(FlagName, flagType))
		return flag->expiration();
	return -1;
}

float UFlagBase::GetFloat(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Float))
		return UObject::Cast<UFlagFloat>(flag)->floatValue();
	return 0.0f;
}

int UFlagBase::GetInt(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Int))
		return UObject::Cast<UFlagInt>(flag)->intValue();
	return 0;
}

NameString UFlagBase::GetName(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Name))
		return UObject::Cast<UFlagName>(flag)->nameValue();
	return {};
}

Rotator UFlagBase::GetRotator(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Rotator))
		return UObject::Cast<UFlagRotator>(flag)->rotatorValue();
	return {};
}

vec3 UFlagBase::GetVector(const NameString& FlagName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)EFlagType::Vector))
		return UObject::Cast<UFlagVector>(flag)->vectorValue();
	return vec3(0.0f);
}

void UFlagBase::SetDefaultExpiration(int expiration)
{
	defaultFlagExpiration() = expiration;
}

void UFlagBase::SetExpiration(const NameString& FlagName, uint8_t flagType, int expiration)
{
	if (UFlag* flag = GetFlag(FlagName, flagType))
		flag->expiration() = expiration;
}

// The expiration -1 (also the value of an omitted argument) selects the default expiration
void UFlagBase::SetExpiration(UFlag* flag, std::optional<int> expiration)
{
	flag->expiration() = (expiration && *expiration != -1) ? *expiration : defaultFlagExpiration();
}

template<typename T>
T* UFlagBase::GetOrCreateFlag(const NameString& FlagName, std::optional<bool> bAdd, EFlagType flagType, const char* flagClassName)
{
	if (UFlag* flag = GetFlag(FlagName, (uint8_t)flagType))
		return UObject::Cast<T>(flag);

	if (bAdd && !*bAdd)
		return nullptr;

	size_t classIndex = (size_t)flagType;
	if (classIndex >= FlagClasses.size())
		FlagClasses.resize(classIndex + 1);

	UClass*& cls = FlagClasses[classIndex];
	if (!cls)
	{
		cls = engine->packages->FindClass(flagClassName);
		if (cls == nullptr)
			throw std::runtime_error(std::string("Could not find class ") + flagClassName);
	}

	// A flag is part of the package of its flag base. A save of the level then includes the flags.
	auto flag = UObject::Cast<T>(package->NewObject(FlagName, cls, ObjectFlags::NoFlags));
	flag->Outer() = this;
	flag->FlagName() = FlagName;
	flag->flagHash() = (int)FlagNameHash(FlagName);
	flag->flagType() = (uint8_t)flagType;
	flag->expiration() = 0;
	flag->nextFlag() = nullptr;
	flag->FlagBase() = nullptr;
	AddFlag(flag);
	return flag;
}

bool UFlagBase::SetBool(const NameString& FlagName, bool NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagBool>(FlagName, bAdd, EFlagType::Bool, "Extension.FlagBool"))
	{
		flag->bValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetByte(const NameString& FlagName, uint8_t NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagByte>(FlagName, bAdd, EFlagType::Byte, "Extension.FlagByte"))
	{
		flag->byteValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetFloat(const NameString& FlagName, float NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagFloat>(FlagName, bAdd, EFlagType::Float, "Extension.FlagFloat"))
	{
		flag->floatValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetInt(const NameString& FlagName, int NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagInt>(FlagName, bAdd, EFlagType::Int, "Extension.FlagInt"))
	{
		flag->intValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetName(const NameString& FlagName, const NameString& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagName>(FlagName, bAdd, EFlagType::Name, "Extension.FlagName"))
	{
		flag->nameValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetRotator(const NameString& FlagName, const Rotator& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagRotator>(FlagName, bAdd, EFlagType::Rotator, "Extension.FlagRotator"))
	{
		flag->rotatorValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}

bool UFlagBase::SetVector(const NameString& FlagName, const vec3& NewValue, std::optional<bool> bAdd, std::optional<int> expiration)
{
	if (auto flag = GetOrCreateFlag<UFlagVector>(FlagName, bAdd, EFlagType::Vector, "Extension.FlagVector"))
	{
		flag->vectorValue() = NewValue;
		SetExpiration(flag, expiration);
		return true;
	}
	return false;
}
