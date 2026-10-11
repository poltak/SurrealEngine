
#include "Precomp.h"
#include "UConEventRandomLabel.h"
#include "Utils/Random.h"

void UConEventRandomLabel::Load(ObjectStream* stream)
{
	UConEvent::Load(stream);
	int count = stream->ReadIndex();
	for (int i = 0; i < count; i++)
	{
		std::string label = stream->ReadString();
		labels.push_back(std::move(label));
	}
}

std::string UConEventRandomLabel::GetLabel(int labelIndex)
{
	std::string str = labels[labelIndex];
	return str;
}

int UConEventRandomLabel::GetLabelCount()
{
	return (int)labels.size();
}

std::string UConEventRandomLabel::GetRandomLabel()
{
	int count = (int)labels.size();
	if (labels.size() <= 0)
		return {};
	int index;
	if (cycleIndex() == count)
		bLabelsCycled() = true;

	if (!bCycleEvents() || (bCycleRandom() && bLabelsCycled()))
		index = RandInt(count - 1);
	else if (!bCycleOnce())
	{
		index = cycleIndex() % count;
		cycleIndex()++;
	}
	else
	{
		index = cycleIndex() % count;
		cycleIndex()++;
	}

	return labels[index];
}
