
#include "Precomp.h"
#include "AIEventManager.h"
#include "Engine.h"
#include "Utils/Logger.h"
#include "GC/GC.h"
#include "Harness.h"
#include "VM/Frame.h"
#include "VM/ScriptCall.h"
#include "Packages/Core/UFunction.h"
#include "Packages/Core/Properties/UStructProperty.h"
#include "Packages/Engine/Actors/UActor.h"
#include "Packages/Engine/Actors/Pawn/UPawn.h"
#include "Packages/Engine/Actors/Inventory/UInventory.h"
#include "Packages/Engine/Actors/Info/ULevelInfo.h"
#include "Packages/Engine/Resources/Level/ULevel.h"
#include <algorithm>
#include <cmath>
#include <cstring>

AIEventManager::EventTypeNode* AIEventManager::FindType(NameString eventName, bool create)
{
	auto it = Types.find(eventName);
	if (it != Types.end())
		return it->second.get();
	if (!create)
		return nullptr;

	auto node = std::make_unique<EventTypeNode>();
	node->Name = eventName;
	EventTypeNode* result = node.get();
	Types[eventName] = std::move(node);
	return result;
}

AIEventManager::Sender* AIEventManager::FindSender(UActor* actor, NameString eventName, bool create)
{
	EventTypeNode* type = FindType(eventName, create);
	if (!type)
		return nullptr;

	for (auto& sender : type->Senders)
	{
		if (!sender->bDeleted && sender->Actor == actor)
			return sender.get();
	}
	if (!create)
		return nullptr;

	auto sender = std::make_unique<Sender>();
	sender->Actor = actor;
	Sender* result = sender.get();
	type->Senders.push_back(std::move(sender));
	return result;
}

AIEventManager::Receiver* AIEventManager::FindReceiver(UActor* actor, NameString eventName)
{
	EventTypeNode* type = FindType(eventName, false);
	if (!type)
		return nullptr;

	for (Receiver* receiver : type->Receivers)
	{
		if (!receiver->bDeleted && receiver->Actor == actor)
			return receiver;
	}
	return nullptr;
}

void AIEventManager::SetEventCallback(UActor* actor, NameString eventName, NameString callback, NameString scoreCallback, bool bCheckVisibility, bool bCheckDir, bool bCheckCylinder, bool bCheckLOS)
{
	if (!actor || eventName.IsNone())
		return;

	Receiver* receiver = FindReceiver(actor, eventName);
	if (!receiver)
	{
		EventTypeNode* type = FindType(eventName, true);
		auto newReceiver = std::make_unique<Receiver>();
		newReceiver->Type = type;
		newReceiver->Actor = actor;
		newReceiver->Stamp = CurSlot;
		receiver = newReceiver.get();
		type->Receivers.push_back(receiver);
		Receivers.push_back(std::move(newReceiver));
	}

	receiver->Callback = callback;
	receiver->ScoreCallback = scoreCallback;
	receiver->bCheckVisibility = bCheckVisibility;
	receiver->bCheckDir = bCheckDir;
	receiver->bCheckCylinder = bCheckCylinder;
	receiver->bCheckLOS = bCheckLOS;
}

void AIEventManager::ClearEventCallback(UActor* actor, NameString eventName)
{
	if (!actor || eventName.IsNone())
		return;

	Receiver* receiver = FindReceiver(actor, eventName);
	if (receiver)
	{
		receiver->bDeleted = true;
		DeleteCount++;
	}
}

void AIEventManager::SetSenderValue(Sender* sender, int eventType, bool pulse, float value, float radius)
{
	// The current slot keeps the maximum. A value that is not a pulse also becomes the persistent value.
	auto set = [pulse](float& slot, float& persist, float v)
	{
		if (slot < v)
			slot = v;
		if (!pulse)
			persist = v;
	};

	SenderValues& slot = sender->Slots[CurSlot];
	if (eventType == Audio)
	{
		set(slot.AudioVolume, sender->Persist.AudioVolume, value);
		set(slot.AudioRadius, sender->Persist.AudioRadius, radius);
	}
	else if (eventType == Visual)
	{
		set(slot.Visual, sender->Persist.Visual, value);
	}
	else if (eventType == Olfactory)
	{
		set(slot.Smell, sender->Persist.Smell, value);
	}
}

void AIEventManager::SendEvent(UActor* actor, NameString eventName, int eventType, float value, float radius)
{
	if (!actor || eventName.IsNone())
		return;
	if (actor->bDeleteMe())
	{
		value = 0.0f;
		radius = 0.0f;
	}
	SetSenderValue(FindSender(actor, eventName, true), eventType, true, value, radius);
}

void AIEventManager::StartEvent(UActor* actor, NameString eventName, int eventType, float value, float radius)
{
	if (!actor || eventName.IsNone())
		return;
	if (actor->bDeleteMe())
	{
		value = 0.0f;
		radius = 0.0f;
	}
	SetSenderValue(FindSender(actor, eventName, true), eventType, false, value, radius);
}

void AIEventManager::EndEvent(UActor* actor, NameString eventName, int eventType)
{
	if (!actor || eventName.IsNone())
		return;
	Sender* sender = FindSender(actor, eventName, false);
	if (sender)
		SetSenderValue(sender, eventType, false, 0.0f, 0.0f);
}

void AIEventManager::ClearEvent(UActor* actor, NameString eventName)
{
	if (!actor || eventName.IsNone())
		return;
	Sender* sender = FindSender(actor, eventName, false);
	if (sender)
	{
		SetSenderValue(sender, Audio, false, 0.0f, 0.0f);
		SetSenderValue(sender, Visual, false, 0.0f, 0.0f);
		SetSenderValue(sender, Olfactory, false, 0.0f, 0.0f);
	}
}

void AIEventManager::DestroyActor(UActor* actor)
{
	for (auto& it : Types)
	{
		for (auto& sender : it.second->Senders)
		{
			if (!sender->bDeleted && sender->Actor == actor)
			{
				sender->bDeleted = true;
				DeleteCount++;
			}
		}
	}

	for (auto& receiver : Receivers)
	{
		if (receiver->bDeleted)
			continue;
		if (receiver->LastBestActor == actor)
			receiver->LastBestActor = nullptr;
		if (receiver->BestActor == actor)
			receiver->BestActor = nullptr;
		if (receiver->Actor == actor)
		{
			receiver->bDeleted = true;
			DeleteCount++;
		}
	}

	if (RefProcessing == 0)
		CleanupEvents();
}

AIEventManager::SenderValues AIEventManager::GetSenderValues(const Sender* sender, int firstSlot, int lastSlot) const
{
	SenderValues result;
	int slot = firstSlot & (NumSlots - 1);
	while (true)
	{
		const SenderValues& v = sender->Slots[slot];
		result.Visual = std::max(result.Visual, v.Visual);
		result.AudioVolume = std::max(result.AudioVolume, v.AudioVolume);
		result.AudioRadius = std::max(result.AudioRadius, v.AudioRadius);
		result.Smell = std::max(result.Smell, v.Smell);
		if (slot == lastSlot)
			break;
		slot = (slot + 1) & (NumSlots - 1);
	}
	return result;
}

float AIEventManager::ComputeScore(UActor* receiver, UActor* sender, NameString scoreCallback)
{
	// The default score is the squared distance plus one. The lowest score above zero wins.
	vec3 delta = receiver->Location() - sender->Location();
	float score = dot(delta, delta) + 1.0f;

	if (!scoreCallback.IsNone())
	{
		UFunction* func = FindEventFunction(receiver, scoreCallback);
		if (func)
		{
			score = Frame::Call(func, receiver, {
				ExpressionValue::ObjectValue(receiver),
				ExpressionValue::ObjectValue(sender),
				ExpressionValue::FloatValue(score)
				}).ToFloat();
		}
	}
	return score;
}

void AIEventManager::ComputeSenseDetection(Receiver* receiver, Sender* sender, float& visibility, float& volume, float& smell)
{
	UPawn* receiverPawn = UObject::TryCast<UPawn>(receiver->Actor);

	// An inventory item that a pawn carries is sensed as that pawn, scaled by the size of the item.
	float scale = 1.0f;
	UActor* target = sender->Actor;
	if (UObject::TryCast<UInventory>(target) && target->Owner())
	{
		UActor* owner = target->Owner();
		float size = target->CollisionRadius() * target->CollisionHeight();
		float ownerSize = owner->CollisionRadius() * owner->CollisionHeight();
		if (ownerSize != 0.0f)
			size /= ownerSize;
		scale = std::sqrt(size);
		target = owner;
	}

	SenderValues values = GetSenderValues(sender, receiver->Stamp, CurSlot);

	visibility = values.Visual * scale;
	if (visibility > 0.0f)
	{
		if (receiverPawn)
		{
			visibility = receiverPawn->AICanSee(target, visibility, receiver->bCheckVisibility, receiver->bCheckDir, receiver->bCheckCylinder, receiver->bCheckLOS);
		}
		else
		{
			bool blocked = Level->Collision.TraceAnyHit(sender->Actor->Location(), receiver->Actor->Location(), receiver->Actor, false, true, true);
			if (blocked)
				visibility = 0.0f;
		}
	}

	volume = values.AudioVolume;
	if (volume > 0.0f)
	{
		if (receiverPawn)
		{
			volume = receiverPawn->AICanHear(target, volume, values.AudioRadius);
		}
		else
		{
			vec3 delta = sender->Actor->Location() - receiver->Actor->Location();
			if (values.AudioRadius * values.AudioRadius <= dot(delta, delta))
				volume = 0.0f;
		}
	}

	smell = values.Smell;
	if (smell > 0.0f)
		smell = receiverPawn ? receiverPawn->AICanSmell(target, smell) : 0.0f;
}

void AIEventManager::ProcessReceiver(Receiver* receiver)
{
	if (receiver->bDeleted)
		return;

	if (receiver->Actor->bDeleteMe())
	{
		receiver->bDeleted = true;
		DeleteCount++;
		return;
	}

	receiver->bFire = false;
	receiver->BestActor = nullptr;
	receiver->Score = 0.0f;
	receiver->Visibility = 0.0f;
	receiver->Volume = 0.0f;
	receiver->Smell = 0.0f;

	UActor* receiverActor = receiver->Actor;

	// A receiver that the player saw in the last 5 seconds, or that is near the player, senses at any range.
	// Other receivers sense within 400 units only.
	bool relevant = false;
	if (!receiverActor->InStasis())
	{
		float sinceRender = engine->LevelInfo->TimeSeconds() - receiverActor->LastRenderTime();
		relevant = sinceRender <= 5.0f || receiverActor->DistanceFromPlayer() <= 1200.0f;
	}

	std::vector<Sender*> candidates;
	if (receiver->Type)
	{
		// The score callback is script code and can add senders to this list. Use an index, not an iterator.
		auto& senders = receiver->Type->Senders;
		for (size_t i = 0; i < senders.size(); i++)
		{
			Sender* sender = senders[i].get();
			if (sender->bDeleted || sender->Actor == receiverActor)
				continue;

			if (!relevant)
			{
				vec3 delta = receiverActor->Location() - sender->Actor->Location();
				if (dot(delta, delta) > 160000.0f)
					continue;
			}

			sender->Score = ComputeScore(receiverActor, sender->Actor, receiver->ScoreCallback);
			if (sender->Score > 0.0f)
				candidates.push_back(sender);
			if (candidates.size() > 255)
				break;
		}
	}

	Sender* best = nullptr;
	float bestScore = 0.0f;
	UActor* bestActor = nullptr;
	if (!candidates.empty() && !receiver->bDeleted)
	{
		std::stable_sort(candidates.begin(), candidates.end(), [](const Sender* a, const Sender* b) { return a->Score < b->Score; });
		for (Sender* sender : candidates)
		{
			if (sender->bDeleted)
				continue;

			float visibility = 0.0f, volume = 0.0f, smell = 0.0f;
			ComputeSenseDetection(receiver, sender, visibility, volume, smell);
			if (visibility > 0.0f || volume > 0.0f || smell > 0.0f)
			{
				best = sender;
				bestActor = sender->Actor;
				bestScore = sender->Score;
				receiver->BestActor = bestActor;
				receiver->Score = bestScore;
				receiver->Visibility = visibility;
				receiver->Volume = volume;
				receiver->Smell = smell;
				break;
			}
		}
	}

	bool active = best != nullptr;
	if (!receiver->bWasActive)
	{
		if (active)
		{
			// A sender that has pulses only gives a Pulse event. A sender with a persistent value gives Begin.
			bool persistent = best->Persist.Visual > 0.0f || best->Persist.AudioVolume > 0.0f || best->Persist.Smell > 0.0f;
			receiver->bFire = true;
			receiver->State = persistent ? Begin : Pulse;
		}
	}
	else if (!active)
	{
		receiver->bFire = true;
		receiver->State = End;
	}
	else if (receiver->LastBestActor != bestActor)
	{
		receiver->bFire = true;
		receiver->State = ChangeBest;
	}

	receiver->bWasActive = active;
	if (receiver->bFire && receiver->State == Pulse)
		receiver->bWasActive = false;
	receiver->LastScore = bestScore;
	receiver->LastBestActor = bestActor;
}

void AIEventManager::FireEvent(Receiver* receiver)
{
	UActor* actor = receiver->Actor;
	if (!actor || actor->bDeleteMe() || !receiver->Type)
		return;

	NameString callback = receiver->Callback.IsNone() ? NameString("AIEvent") : receiver->Callback;
	UFunction* func = FindEventFunction(actor, callback);
	if (!func || func->Properties.size() < 3)
		return;

	// function Callback(Name event, EAIEventState state, XAIParams params)
	UStructProperty* paramsProp = UObject::TryCast<UStructProperty>(func->Properties[2]);
	if (!paramsProp || !paramsProp->Struct)
		return;

	// ExpressionValue::Variable adds the data offset of the property. The parameter property has its offset in
	// the stack frame of the callback, so use a property with offset zero for the buffer here.
	if (!CallParamsProp)
		CallParamsProp = GC::Alloc<UStructProperty>(NameString(), nullptr, ObjectFlags::NoFlags);
	CallParamsProp->Struct = paramsProp->Struct;

	std::vector<uint8_t> params(std::max<size_t>(paramsProp->Struct->StructSize, 1), 0);
	for (UProperty* prop : paramsProp->Struct->Properties)
	{
		void* data = params.data() + prop->DataOffset.DataOffset;
		if (prop->Name == "BestActor")
			*static_cast<UObject**>(data) = receiver->BestActor;
		else if (prop->Name == "Score")
			*static_cast<float*>(data) = receiver->Score;
		else if (prop->Name == "Visibility")
			*static_cast<float*>(data) = receiver->Visibility;
		else if (prop->Name == "Volume")
			*static_cast<float*>(data) = receiver->Volume;
		else if (prop->Name == "Smell")
			*static_cast<float*>(data) = receiver->Smell;
	}

	if (Harness::Get().TraceAI)
	{
		std::string text = "AI trace: event " + receiver->Type->Name.ToString() + " state " + std::to_string(receiver->State) + " -> " + actor->Name.ToString() + "." + callback.ToString() + " best=" + (receiver->BestActor ? receiver->BestActor->Name.ToString() : std::string("None")) + " structSize=" + std::to_string(paramsProp->Struct->StructSize);
		for (UProperty* prop : paramsProp->Struct->Properties)
			text += " " + prop->Name.ToString() + "@" + std::to_string(prop->DataOffset.DataOffset);
		LogMessage(text);
	}

	Frame::Call(func, actor, {
		ExpressionValue::NameValue(receiver->Type->Name),
		ExpressionValue::ByteValue(receiver->State),
		ExpressionValue::Variable(params.data(), CallParamsProp)
		});
}

void AIEventManager::Tick()
{
	if (!Level)
		return;

	RefProcessing++;

	int nextSlot = (CurSlot + 1) & (NumSlots - 1);

	// Script callbacks can register new receivers. Those start in the next tick.
	size_t count = Receivers.size();
	for (size_t i = 0; i < count; i++)
		ProcessReceiver(Receivers[i].get());

	CurSlot = nextSlot;

	// Each sender starts the new slot with its persistent values. A sender with no value left is deleted.
	for (auto& it : Types)
	{
		auto& senders = it.second->Senders;
		for (size_t i = 0; i < senders.size(); i++)
		{
			Sender* sender = senders[i].get();
			if (sender->bDeleted)
				continue;

			if (sender->Actor->bDeleteMe())
				sender->Persist = SenderValues();
			sender->Slots[nextSlot] = sender->Persist;

			bool alive = false;
			for (const SenderValues& v : sender->Slots)
			{
				if (v.Visual > 0.0f || v.AudioVolume > 0.0f || v.Smell > 0.0f)
				{
					alive = true;
					break;
				}
			}
			if (!alive)
			{
				sender->bDeleted = true;
				DeleteCount++;
			}
		}
	}

	for (size_t i = 0; i < count; i++)
	{
		Receiver* receiver = Receivers[i].get();
		if (receiver->bDeleted)
			continue;

		if (receiver->bFire && receiver->Type)
		{
			receiver->bFire = false;
			FireEvent(receiver);
		}
		receiver->Stamp = nextSlot;
	}

	RefProcessing--;
	CleanupEvents();
}

void AIEventManager::CleanupEvents()
{
	if (RefProcessing != 0 || DeleteCount == 0)
		return;

	for (auto& it : Types)
	{
		auto& senders = it.second->Senders;
		senders.erase(std::remove_if(senders.begin(), senders.end(), [](const std::unique_ptr<Sender>& s) { return s->bDeleted; }), senders.end());

		auto& receivers = it.second->Receivers;
		receivers.erase(std::remove_if(receivers.begin(), receivers.end(), [](const Receiver* r) { return r->bDeleted; }), receivers.end());
	}

	Receivers.erase(std::remove_if(Receivers.begin(), Receivers.end(), [](const std::unique_ptr<Receiver>& r) { return r->bDeleted; }), Receivers.end());
	DeleteCount = 0;
}
