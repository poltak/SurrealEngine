
#include "Precomp.h"
#include "DeusExTravel.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "Package/Package.h"
#include "Package/PackageManager.h"
#include "Packages/Core/UClass.h"
#include "Packages/Core/Properties/UProperty.h"
#include "Packages/Core/Properties/UBoolProperty.h"
#include "Packages/Core/Properties/UObjectProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include <map>
#include <memory>
#include <new>
#include <set>

// The original writes the travel data as text:
//
//   Class=DeusEx.JCDentonMale Name=JCDentonMale0 Ptr=0a1b2c3d
//   {
//   Credits=250
//   Inventory=0b2c3d4e WeaponPistol3
//   }
//
// - The export starts at the player pawn. Each object property with the travel flag adds its object to the list.
// - An actor travels only if bTravel is true. An object that is not an actor always travels.
// - A property goes into the data only if its value is different from the class default.
// - The import makes each object again (an actor with the pawn as its owner at the location of the pawn)
//   and then connects the object properties through the Ptr values.
//
// This code keeps the same rules, but it holds the values as copies in memory and not as text.
// A text form needs an escape rule for each property type, and the data does not go to a file.

namespace
{
	struct TravelValue
	{
		UProperty* Prop = nullptr;
		int Index = 0;

		bool IsObject = false;
		UObject* Reference = nullptr; // The object in the old map. Used as a key only.

		bool IsBool = false;
		bool BoolValue = false;

		std::shared_ptr<void> Element; // A copy of the element for all other property types
	};

	struct TravelObject
	{
		UClass* Class = nullptr;
		NameString Name;
		UObject* Id = nullptr; // The object in the old map. Used as a key only.
		std::vector<TravelValue> Values;
	};

	struct TravelSnapshot
	{
		std::vector<TravelObject> Objects;
	};

	const std::string KeyPrefix = "deusextravel:";
	const size_t MaxSnapshots = 4; // The current map can restart with its own snapshot while a newer one is in use

	std::map<int, std::shared_ptr<TravelSnapshot>> Snapshots;
	int NextSnapshot = 1;

	std::shared_ptr<void> CopyElement(UProperty* prop, const void* src)
	{
		size_t align = std::max(prop->ElementAlignment(), (size_t)alignof(std::max_align_t));
		void* memory = ::operator new(std::max(prop->ElementSize(), (size_t)1), std::align_val_t(align));
		prop->CopyConstructElement(memory, src);
		return std::shared_ptr<void>(memory, [prop, align](void* p)
			{
				prop->DestructElement(p);
				::operator delete(p, std::align_val_t(align));
			});
	}

	bool IsChildOf(UClass* cls, const NameString& baseName)
	{
		for (UStruct* cur = cls; cur; cur = cur->BaseStruct)
		{
			if (cur->Name == baseName)
				return true;
		}
		return false;
	}

	// Finds the property of the target object for a value of the snapshot.
	UProperty* FindTargetProperty(UObject* target, UClass* sourceClass, UProperty* sourceProp)
	{
		if (target->Class == sourceClass)
			return sourceProp;

		// The class of the pawn in the new map can be different from the class in the old map
		for (UProperty* prop : target->PropertyData.Class->Properties)
		{
			if (prop == sourceProp)
				return prop;
		}
		for (UProperty* prop : target->PropertyData.Class->Properties)
		{
			if (prop->Name == sourceProp->Name && prop->Class == sourceProp->Class && prop->ElementSize() == sourceProp->ElementSize() && prop->ArrayDimension == sourceProp->ArrayDimension)
				return prop;
		}
		return nullptr;
	}
}

std::string DeusExTravel::Create(UPlayerPawn* pawn)
{
	auto snapshot = std::make_shared<TravelSnapshot>();

	std::vector<UObject*> objects = { pawn };
	std::set<UObject*> known = { pawn };

	for (size_t i = 0; i < objects.size(); i++)
	{
		UObject* object = objects[i];

		if (UActor* actor = UObject::TryCast<UActor>(object))
		{
			if (!actor->bTravel() || actor->bDeleteMe())
				continue;
		}

		TravelObject info;
		info.Class = object->Class;
		info.Name = object->Name;
		info.Id = object;

		PropertyDataBlock& defaults = object->Class->PropertyData;
		for (UProperty* prop : object->PropertyData.Class->Properties)
		{
			if (!AllFlags(prop->PropFlags, PropertyFlags::Travel))
				continue;

			void* data = object->PropertyData.Ptr(prop);
			void* defaultData = defaults.Ptr(prop);

			for (int index = 0; index < prop->ArrayDimension; index++)
			{
				void* element = prop->GetElement(data, index);
				if (prop->CompareElement(element, prop->GetElement(defaultData, index)))
					continue;

				TravelValue value;
				value.Prop = prop;
				value.Index = index;

				UObject* reference = UObject::TryCast<UObjectProperty>(prop) ? *static_cast<UObject**>(element) : nullptr;
				if (reference && reference->package == pawn->package)
				{
					// An object of the old map. It must travel, or the new map cannot use it.
					value.IsObject = true;
					value.Reference = reference;
					if (known.insert(reference).second)
						objects.push_back(reference);
				}
				else if (auto boolProp = UObject::TryCast<UBoolProperty>(prop))
				{
					value.IsBool = true;
					value.BoolValue = boolProp->GetBool(element);
				}
				else
				{
					value.Element = CopyElement(prop, element);
				}

				info.Values.push_back(std::move(value));
			}
		}

		snapshot->Objects.push_back(std::move(info));
	}

	if (snapshot->Objects.empty())
		LogMessage("Deus Ex travel: the player pawn does not travel (bTravel is false)");

	int id = NextSnapshot++;
	Snapshots[id] = snapshot;
	while (Snapshots.size() > MaxSnapshots)
		Snapshots.erase(Snapshots.begin());

	LogMessage("Deus Ex travel: " + std::to_string(snapshot->Objects.size()) + " objects travel with the player");
	return KeyPrefix + std::to_string(id);
}

Array<UObject*> DeusExTravel::Accept(UPlayerPawn* pawn, const std::string& key)
{
	Array<UObject*> accepted;

	if (key.compare(0, KeyPrefix.size(), KeyPrefix) != 0)
	{
		LogMessage("Deus Ex travel: unknown travel info, nothing accepted");
		return accepted;
	}

	auto it = Snapshots.find(std::atoi(key.c_str() + KeyPrefix.size()));
	if (it == Snapshots.end())
	{
		LogMessage("Deus Ex travel: the travel snapshot is gone, nothing accepted");
		return accepted;
	}
	std::shared_ptr<TravelSnapshot> snapshot = it->second;

	// Make the objects

	std::vector<UObject*> targets(snapshot->Objects.size(), nullptr);
	std::map<UObject*, UObject*> idToTarget;

	for (size_t i = 0; i < snapshot->Objects.size(); i++)
	{
		const TravelObject& info = snapshot->Objects[i];

		UObject* target = nullptr;
		if (IsChildOf(info.Class, "PlayerPawn"))
		{
			target = pawn;
		}
		else if (IsChildOf(info.Class, "Actor"))
		{
			target = pawn->Spawn(info.Class, pawn, NameString(), pawn->Location(), Rotator(0, 0, 0), true);
		}
		else
		{
			target = pawn->package->NewObject(info.Name, info.Class, ObjectFlags::NoFlags);
			target->Outer() = pawn;
		}

		if (!target)
		{
			LogMessage("Deus Ex travel: could not make the object " + info.Name.ToString());
			continue;
		}

		targets[i] = target;
		idToTarget[info.Id] = target;
	}

	// Set the travel properties

	std::vector<std::pair<UObject**, UObject*>> references;

	for (size_t i = 0; i < snapshot->Objects.size(); i++)
	{
		const TravelObject& info = snapshot->Objects[i];
		UObject* target = targets[i];
		if (!target)
			continue;

		for (const TravelValue& value : info.Values)
		{
			UProperty* prop = FindTargetProperty(target, info.Class, value.Prop);
			if (!prop)
				continue;

			void* element = prop->GetElement(target->PropertyData.Ptr(prop), value.Index);
			if (value.IsObject)
				references.push_back({ static_cast<UObject**>(element), value.Reference });
			else if (value.IsBool)
				static_cast<UBoolProperty*>(prop)->SetBool(element, value.BoolValue);
			else
				prop->CopyElement(element, value.Element.get());
		}
	}

	// Connect the objects. A reference to an object that did not travel stays as it is in the new map.

	for (auto& reference : references)
	{
		auto found = idToTarget.find(reference.second);
		if (found != idToTarget.end())
			*reference.first = found->second;
	}

	for (UObject* target : targets)
	{
		if (target && target != pawn)
			accepted.push_back(target);
	}

	LogMessage("Deus Ex travel: the player accepted " + std::to_string(accepted.size()) + " objects");
	return accepted;
}
