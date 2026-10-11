#pragma once

#include "Package/NameString.h"
#include <map>
#include <memory>
#include <vector>

class UActor;
class ULevel;
class UFunction;
class UStructProperty;

// The Deus Ex AI event manager.
//
// Senders announce what they emit: something to see, to hear or to smell. Receivers (mostly ScriptedPawns)
// register a script callback for an event name. Each level tick the manager finds, for each receiver, the
// best sender that the receiver can sense and calls the script callback when that state changes.
//
// This follows UEventManager in Engine.dll (build 1112fm), source file UnEventManager.cpp:
//
// - Values are kept in a ring of 16 time slots, one slot per manager tick. AISendEvent puts a pulse in the
//   current slot only. AIStartEvent also sets a value that fills each new slot until AIEndEvent clears it.
// - A receiver reads the maximum of all slots since it was last processed, so it does not miss a pulse.
// - A sender with no value left in any slot is deleted.
// - The original stops work on the receiver list when a time budget is used up and continues in the next
//   tick. This version processes all receivers in each tick.
class AIEventManager
{
public:
	enum EventType { Visual = 0, Audio = 1, Olfactory = 2 };
	enum EventState { Begin = 0, End = 1, Pulse = 2, ChangeBest = 3 };

	explicit AIEventManager(ULevel* level) : Level(level) { }

	void SetEventCallback(UActor* actor, NameString eventName, NameString callback, NameString scoreCallback, bool bCheckVisibility, bool bCheckDir, bool bCheckCylinder, bool bCheckLOS);
	void ClearEventCallback(UActor* actor, NameString eventName);
	void SendEvent(UActor* actor, NameString eventName, int eventType, float value, float radius);
	void StartEvent(UActor* actor, NameString eventName, int eventType, float value, float radius);
	void EndEvent(UActor* actor, NameString eventName, int eventType);
	void ClearEvent(UActor* actor, NameString eventName);
	void DestroyActor(UActor* actor);
	void Tick();

private:
	static const int NumSlots = 16;

	struct SenderValues
	{
		float Visual = 0.0f;
		float AudioVolume = 0.0f;
		float AudioRadius = 0.0f;
		float Smell = 0.0f;
	};

	struct Sender
	{
		UActor* Actor = nullptr;
		bool bDeleted = false;
		float Score = 0.0f;
		SenderValues Slots[NumSlots];
		SenderValues Persist;
	};

	struct EventTypeNode;

	struct Receiver
	{
		EventTypeNode* Type = nullptr;
		UActor* Actor = nullptr;
		bool bDeleted = false;
		NameString Callback;
		NameString ScoreCallback;
		bool bFire = false;
		uint8_t State = Begin;
		bool bCheckVisibility = true;
		bool bCheckDir = true;
		bool bCheckCylinder = false;
		bool bCheckLOS = true;
		bool bWasActive = false;
		float LastScore = 0.0f;
		UActor* LastBestActor = nullptr;
		int Stamp = 0;

		// XAIParams of the script callback
		UActor* BestActor = nullptr;
		float Score = 0.0f;
		float Visibility = 0.0f;
		float Volume = 0.0f;
		float Smell = 0.0f;
	};

	struct EventTypeNode
	{
		NameString Name;
		std::vector<std::unique_ptr<Sender>> Senders;
		std::vector<Receiver*> Receivers;
	};

	EventTypeNode* FindType(NameString eventName, bool create);
	Sender* FindSender(UActor* actor, NameString eventName, bool create);
	Receiver* FindReceiver(UActor* actor, NameString eventName);
	void SetSenderValue(Sender* sender, int eventType, bool pulse, float value, float radius);
	SenderValues GetSenderValues(const Sender* sender, int firstSlot, int lastSlot) const;
	float ComputeScore(UActor* receiver, UActor* sender, NameString scoreCallback);
	void ComputeSenseDetection(Receiver* receiver, Sender* sender, float& visibility, float& volume, float& smell);
	void ProcessReceiver(Receiver* receiver);
	void FireEvent(Receiver* receiver);
	void CleanupEvents();

	ULevel* Level = nullptr;
	UStructProperty* CallParamsProp = nullptr;
	int RefProcessing = 0;
	int DeleteCount = 0;
	int CurSlot = 0;
	std::map<NameString, std::unique_ptr<EventTypeNode>> Types;
	std::vector<std::unique_ptr<Receiver>> Receivers; // in the order of registration, as the process list of the original
};
