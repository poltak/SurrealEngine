
#include "Precomp.h"
#include "UConversation.h"
#include "Package/PackageManager.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Resources/USound.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include "Packages/ConSys/UConAudioList.h"
#include "Packages/ConSys/UConFlagRef.h"
#include "Packages/ConSys/Events/UConEvent.h"
#include "Packages/ConSys/Events/UConEventSpeech.h"
#include "Packages/ConSys/Events/UConEventTransferObject.h"
#include "Packages/ConSys/Events/UConEventCheckObject.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Pawn/UPlayerPawn.h"
#include "Packages/Engine/Actors/Decoration/UDecoration.h"
#include "Packages/ConSys/Events/UConEventMoveCamera.h"
#include "Packages/ConSys/Events/UConEventAnimation.h"
#include "Packages/ConSys/Events/UConEventTrade.h"
#include "Packages/ConSys/Events/UConEventJump.h"
#include "Engine.h"

// The bind functions follow ConSys.dll of build 1112fm: DConversation::BindEvents, BindEventsToActor, BindActorEvents,
// AddBoundActor and the BindActor function of each event class.
//
// Offsets in the original: an actor has BindName at 0x2ac and BarkBindName at 0x2b8. A conversation has bFirstPerson as
// bit 4 of the flags at 0x50. All name tests are appStricmp, so two empty names are equal.

static bool SameText(const std::string& a, const std::string& b)
{
	if (a.size() != b.size())
		return false;
	for (size_t i = 0; i < a.size(); i++)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
			return false;
	}
	return true;
}

// The original calls StaticLoadObject(UClass, package "DeusEx", name, LOAD_NoWarn): the class with that name in DeusEx.u, or null
static UClass* FindDeusExClass(const std::string& name)
{
	if (name.empty())
		return nullptr;
	return engine->packages->FindClass("DeusEx." + name);
}

static void AddBoundActor(UObject** conBoundActors, UActor* actor)
{
	for (int i = 0; i < 10; i++)
	{
		if (conBoundActors[i] == actor)
			return;
		if (!conBoundActors[i])
		{
			conBoundActors[i] = actor;
			return;
		}
	}
}

// DConEventTransferObject::BindActor and DConEventCheckObject::BindActor load the object class from its name each time
// they run. The result does not depend on the actor, so this does it one time for each bind.
// Without it, giveObject and checkObject are null: a conversation gives the player no object, and a check for an
// object in the inventory of the player always fails.
void UConversation::LoadEventObjectClasses()
{
	for (UConEvent* e = eventList(); e; e = e->nextEvent())
	{
		switch ((EEventType)e->eventType())
		{
		case EEventType::TransferObject:
			if (auto transfer = UObject::TryCast<UConEventTransferObject>(e))
				transfer->giveObject() = FindDeusExClass(transfer->ObjectName());
			break;
		case EEventType::CheckObject:
			if (auto check = UObject::TryCast<UConEventCheckObject>(e))
			{
				// A name that starts with "NK_" is a nano key. The script looks for it in the key ring.
				if (SameText(check->ObjectName().substr(0, 3), "NK_"))
					check->checkObject() = nullptr;
				else
					check->checkObject() = FindDeusExClass(check->ObjectName());
			}
			break;
		default:
			break;
		}
	}
}

// The BindActor function of the event classes. Returns true if the event now refers to the actor.
bool UConversation::BindEventToActor(UConEvent* e, UActor* invokeActor, UActor* actor)
{
	const std::string& bindName = actor->BindName();
	const std::string& barkBindName = actor->BarkBindName();

	switch ((EEventType)e->eventType())
	{
	case EEventType::Speech:
		if (auto speech = UObject::TryCast<UConEventSpeech>(e))
		{
			// The bark name is only for the actor that invoked the conversation
			bool bound = false;
			if (SameText(bindName, speech->speakerName()) || (invokeActor == actor && SameText(barkBindName, speech->speakerName())))
			{
				speech->speaker() = actor;
				bound = true;
			}
			if (SameText(bindName, speech->speakingToName()) || (invokeActor == actor && SameText(barkBindName, speech->speakingToName())))
			{
				speech->speakingTo() = actor;
				bound = true;
			}
			return bound;
		}
		break;

	case EEventType::TransferObject:
		if (auto transfer = UObject::TryCast<UConEventTransferObject>(e))
		{
			bool bound = false;
			if ((SameText(barkBindName, transfer->fromName()) && bFirstPerson()) || SameText(bindName, transfer->fromName()))
			{
				transfer->fromActor() = actor;
				bound = true;
			}
			if ((SameText(barkBindName, transfer->toName()) && bFirstPerson()) || SameText(bindName, transfer->toName()))
			{
				transfer->toActor() = actor;
				bound = true;
			}
			return bound;
		}
		break;

	case EEventType::Animation:
		if (auto animation = UObject::TryCast<UConEventAnimation>(e))
		{
			if ((SameText(barkBindName, animation->eventOwnerName()) && bFirstPerson()) || SameText(bindName, animation->eventOwnerName()))
			{
				animation->eventOwner() = actor;
				return true;
			}
		}
		break;

	case EEventType::Trade:
		// DConEventTrade uses the function of DConEventAnimation
		if (auto trade = UObject::TryCast<UConEventTrade>(e))
		{
			if ((SameText(barkBindName, trade->eventOwnerName()) && bFirstPerson()) || SameText(bindName, trade->eventOwnerName()))
			{
				trade->eventOwner() = actor;
				return true;
			}
		}
		break;

	default:
		// All other events, also MoveCamera, bind no actor
		break;
	}
	return false;
}

void UConversation::BindEventsToActor(UObject** conBoundActors, UActor* invokeActor, UActor* actor)
{
	if (actor->BindName().empty())
		return;

	// Many actors can have the same bind name. The events then go only to the actor that invoked the conversation.
	if (invokeActor && invokeActor != actor && SameText(invokeActor->BindName(), actor->BindName()))
		return;

	for (UConEvent* e = eventList(); e; e = e->nextEvent())
	{
		if (BindEventToActor(e, invokeActor, actor))
			AddBoundActor(conBoundActors, actor);
	}
}

void UConversation::BindActorEvents(UObject* actorToBind)
{
	UActor* actor = UObject::TryCast<UActor>(actorToBind);
	if (!actor)
		return;

	LoadEventObjectClasses();
	for (UConEvent* e = eventList(); e; e = e->nextEvent())
		BindEventToActor(e, actor, actor);
}

void UConversation::BindEvents(UObject** conBoundActors, UObject* invokeActorObject)
{
	// conBoundActors is the array "ConActorsBound[10]" of the script. It gets each actor that an event refers to.
	// ConPlayBase.ActorDestroyed stops the conversation when one of them goes away.
	for (int i = 0; i < 10; i++)
		conBoundActors[i] = nullptr;

	UActor* invokeActor = UObject::TryCast<UActor>(invokeActorObject);

	LoadEventObjectClasses();

	// The original goes through all pawns, then all decorations, then all player pawns. A later actor replaces an earlier one.
	for (int pass = 0; pass < 3; pass++)
	{
		for (UActor* actor : engine->Level->Actors)
		{
			if (!actor || actor->bDeleteMe())
				continue;

			bool inPass = false;
			if (pass == 0)
				inPass = UObject::IsType<UPawn>(actor);
			else if (pass == 1)
				inPass = UObject::IsType<UDecoration>(actor);
			else
				inPass = UObject::IsType<UPlayerPawn>(actor);

			if (inPass)
				BindEventsToActor(conBoundActors, invokeActor, actor);
		}
	}
}

// Note: DConversation::ClearBindEvents does nothing in the original. Its loop calls the ClearBindActors function of the
// event base class, which is empty and not virtual. The original engine sets references to destroyed actors to null in
// its garbage collection. This engine has no such step, so this function removes the actors from the events.
void UConversation::ClearBindEvents()
{
	for (UConEvent* e = eventList(); e; e = e->nextEvent())
	{
		switch ((EEventType)e->eventType())
		{
		default:
		case EEventType::Choice:
		case EEventType::SetFlag:
		case EEventType::CheckFlag:
		case EEventType::CheckObject:
		case EEventType::Random:
		case EEventType::Trigger:
		case EEventType::AddGoal:
		case EEventType::AddNote:
		case EEventType::AddSkillPoints:
		case EEventType::AddCredits:
		case EEventType::CheckPersona:
		case EEventType::Comment:
		case EEventType::End:
			break;
		case EEventType::Speech:
			if (auto speech = UObject::Cast<UConEventSpeech>(e))
			{
				speech->speaker() = nullptr;
				speech->speakingTo() = nullptr;
			}
			break;
			break;
		case EEventType::TransferObject:
			if (auto transfer = UObject::Cast<UConEventTransferObject>(e))
			{
				transfer->fromActor() = nullptr;
				transfer->toActor() = nullptr;
			}
			break;
		case EEventType::MoveCamera:
			if (auto moveCamera = UObject::Cast<UConEventMoveCamera>(e))
			{
				moveCamera->cameraActor() = nullptr;
			}
			break;
		case EEventType::Animation:
			if (auto animation = UObject::Cast<UConEventAnimation>(e))
			{
				animation->eventOwner() = nullptr;
			}
			break;
		case EEventType::Trade:
			if (auto trade = UObject::Cast<UConEventTrade>(e))
			{
				trade->eventOwner() = nullptr;
			}
			break;
		case EEventType::Jump:
			if (auto jump = UObject::Cast<UConEventJump>(e))
			{
				// jump->jumpCon() = nullptr;
			}
			break;
		}
	}
}

UObject* UConversation::CreateConCamera()
{
	UClass* cls = engine->packages->FindClass("ConSys.ConCamera");
	NameString name;
	return engine->LevelPackage->NewObject(name, cls, ObjectFlags::Transient, true);
}

UObject* UConversation::CreateFlagRef(const NameString& FlagName, bool flagValue)
{
	UClass* cls = engine->packages->FindClass("ConSys.ConFlagRef");
	NameString name;
	UObject* obj = engine->LevelPackage->NewObject(name, cls, ObjectFlags::Transient, true);
	UConFlagRef* flagObj = UObject::Cast<UConFlagRef>(obj);
	flagObj->FlagName() = FlagName;
	flagObj->Value() = flagValue;
	return obj;
}

UObject* UConversation::GetSpeechAudio(int soundID)
{
	auto package = engine->packages->GetPackage("DeusExConAudio" + audioPackageName());
	auto audioList = UObject::Cast<UConAudioList>(package->GetUObject("ConAudioList", "ConAudioList_" + audioPackageName()));

	if (!audioList)
	{
		LogMessage("Could not find ConAudioList in Conversation.GetSpeechAudio");
		return nullptr;
	}

	if (soundID < 0 || (size_t)soundID >= audioList->conAudioList.size())
	{
		LogMessage("SoundID out of bounds in Conversation.GetSpeechAudio");
		return nullptr;
	}

	return audioList->conAudioList[soundID];
}

float UConversation::GetSpeechLength(int soundID)
{
	USound* sound = UObject::Cast<USound>(GetSpeechAudio(soundID));
	return sound ? sound->GetDuration() : 1.0f;
}
