
#include "Precomp.h"
#include "UActor.h"
#include "Utils/Logger.h"
#include "VM/ScriptCall.h"
#include "VM/Frame.h"
#include "Packages/Engine/Resources/Mesh/USkeletalMesh.h"
#include "Engine.h"

bool UActor::HasAnim(const NameString& sequence)
{
	return Mesh() && Mesh()->GetSequence(sequence);
}

bool UActor::IsAnimating()
{
	// As AActor::IsAnimating in the Engine.dll of Deus Ex (build 1112fm): a tween to the first frame of a sequence is
	// an animation also, and it has its own rate.
	if (engine->LaunchInfo.IsDeusEx())
		return !AnimSequence().IsNone() && (AnimFrame() < 0.0f ? TweenRate() : AnimRate()) != 0.0f;

	return AnimRate() != 0.0f;
}

bool UActor::IsAnimating_HP(std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.IsAnimating_HP");
	return IsAnimating();
}

void UActor::FinishAnim()
{
	if (bAnimLoop())
	{
		bAnimLoop() = false;
		bAnimFinished() = false;
	}

	if (engine->LaunchInfo.IsDeusEx())
	{
		// As AActor::execFinishAnim in the Engine.dll of Deus Ex (build 1112fm): the state code waits only for an
		// animation that plays and that is before its last frame. If not, the code goes on in the same tick.
		// A loop that is between its last frame and its end is such a case.
		if (StateFrame && IsAnimating() && AnimFrame() < AnimLast())
			StateFrame->LatentState = LatentRunState::FinishAnim;
		return;
	}

	if (StateFrame)
		StateFrame->LatentState = LatentRunState::FinishAnim;
}

void UActor::FinishAnim_HP(std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.FinishAnim_HP");
	FinishAnim();
}

NameString UActor::GetAnimGroup(const NameString& sequence)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
			return seq->Group;
	}
	return {};
}

// UnrealScript variables controlling animation:
// 
// Tweening means animating (using vertex interpolation) from the last animation's frame to the current animation's first frame
//
// Mesh          - the mesh the animation belongs to
// AnimSequence  - current active animation sequence
// AnimFrame     - how far we've gotten in an animation 0.0 to 1.0 for current animation, negative for interpolation from old animation when tweening
// AnimLast      - end point for AnimFrame (when to stop/loop). It is zero when only tweening (don't play the animation). It is the start of the last frame (1-1/numframes) when playing an animation
// AnimRate      - how far AnimFrame moves in 1 second (AnimFrame += AnimRate * timeElapsed). If negative it is a scale factor used to convert Velocity length to animation speed
// AnimMinRate   - the minimum animation speed when AnimRate is negative (negative AnimRate means it should use length(Velocity) * abs(AnimRate) as the anim speed)
// TweenRate     - how fast to move when AnimFrame is negative (AnimFrame += TweenRate * timeElapsed)
// OldAnimRate   - AnimRate from previous call to PlayAnim/LoopAnim/TweenAnim
// bAnimLoop     - true if the animation should loop when AnimLast is reached
// bAnimNotify   - true if animation notify events should be fired when animating
// bAnimFinished - true if AnimLast was reached and there's no looping

void UActor::PlayAnim(const NameString& sequence, float rate, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;

			if (seq->NumFrames > 1)
			{
				AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
				AnimLast() = 1.0f - 1.0f / seq->NumFrames;
				AnimRate() = rate * seq->Rate / seq->NumFrames;
				TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
				bAnimNotify() = !seq->Notifys.empty();
				OldAnimRate() = AnimRate();
			}
			else
			{
				// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

				AnimFrame() = -1.0f;
				AnimLast() = 0.0f;
				AnimRate() = 0.0f;
				TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
				bAnimNotify() = false;
				OldAnimRate() = 0.0f;
				AnimMinRate() = 0.0f;
			}

			bAnimLoop() = false;
			bAnimFinished() = false;
		}
	}
}

// PlayBlendAnim and TweenBlendAnim follow AActor::execPlayBlendAnim and AActor::execTweenBlendAnim in Engine.dll of
// Deus Ex (build 1112fm). They are execPlayAnim and execTweenAnim of UE1 for one of the four blend slots.
// A frame number below zero means that the slot tweens from its last pose to the first frame of the sequence.

void UActor::PlayBlendAnim(const NameString& sequenceName, float rate, float tweenTime, int blendSlot)
{
	if (blendSlot < 0 || blendSlot > 3)
	{
		LogMessage("PlayBlendAnim: blend slot " + std::to_string(blendSlot) + " is not valid");
		return;
	}
	if (!Mesh())
	{
		LogMessage("PlayBlendAnim: no mesh for sequence '" + sequenceName.ToString() + "'");
		return;
	}

	MeshAnimSeq* sequence = Mesh()->GetSequence(sequenceName);
	if (!sequence || sequence->Name != sequenceName)
	{
		LogMessage("PlayBlendAnim: sequence '" + sequenceName.ToString() + "' not found in mesh '" + Mesh()->Name.ToString() + "'");
		return;
	}

	float numFrames = (float)sequence->NumFrames;

	// Keep the pose that the renderer tweens from
	SetTweenFromBlendAnimFrame(blendSlot);

	if (BlendAnimSequence()[blendSlot].IsNone())
		tweenTime = 0.0f;

	BlendAnimSequence()[blendSlot] = sequenceName;
	BlendAnimRate()[blendSlot] = rate * sequence->Rate / numFrames;
	BlendAnimLast()[blendSlot] = 1.0f - 1.0f / numFrames;

	if (BlendAnimLast()[blendSlot] == 0.0f)
	{
		// A sequence with one frame: show it for the tween time, or for 0.1 seconds
		BlendAnimMinRate()[blendSlot] = 0.0f;
		OldBlendAnimRate()[blendSlot] = 0.0f;
		BlendTweenRate()[blendSlot] = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
		BlendAnimFrame()[blendSlot] = -1.0f / numFrames;
		BlendAnimRate()[blendSlot] = 0.0f;
	}
	else if (tweenTime > 0.0f)
	{
		BlendTweenRate()[blendSlot] = 1.0f / (numFrames * tweenTime);
		BlendAnimFrame()[blendSlot] = -1.0f / numFrames;
	}
	else if (tweenTime == -1.0f)
	{
		// The default tween time: use the rate of the animation that played before
		BlendAnimFrame()[blendSlot] = -1.0f / numFrames;
		float oldRate = OldBlendAnimRate()[blendSlot];
		if (oldRate > 0.0f)
			BlendTweenRate()[blendSlot] = oldRate;
		else if (oldRate < 0.0f) // the old animation had a rate from the velocity
			BlendTweenRate()[blendSlot] = std::max(length(Velocity()) * oldRate * -1.0f, BlendAnimRate()[blendSlot] * 0.5f);
		else
			BlendTweenRate()[blendSlot] = 1.0f / (numFrames * 0.025f);
	}
	else
	{
		BlendTweenRate()[blendSlot] = 0.0f;
		BlendAnimFrame()[blendSlot] = 0.001f;
	}

	vec4 oldSim = SimBlendAnim()[blendSlot];
	vec4& sim = SimBlendAnim()[blendSlot];
	sim.x = BlendAnimFrame()[blendSlot] * 10000.0f;
	sim.y = BlendAnimRate()[blendSlot] * 10000.0f;
	sim.z = BlendTweenRate()[blendSlot] * 1000.0f;
	sim.w = BlendAnimLast()[blendSlot] * 10000.0f;
	if (oldSim.x == sim.x && oldSim.y == sim.y && oldSim.z == sim.z && oldSim.w == sim.w)
		sim.w += 1.0f;

	OldBlendAnimRate()[blendSlot] = BlendAnimRate()[blendSlot];
}

void UActor::TweenBlendAnim(const NameString& sequenceName, float time, int blendSlot)
{
	if (blendSlot < 0 || blendSlot > 3)
	{
		LogMessage("TweenBlendAnim: blend slot " + std::to_string(blendSlot) + " is not valid");
		return;
	}
	if (!Mesh())
	{
		LogMessage("TweenBlendAnim: no mesh for sequence '" + sequenceName.ToString() + "'");
		return;
	}

	MeshAnimSeq* sequence = Mesh()->GetSequence(sequenceName);
	if (!sequence || sequence->Name != sequenceName)
	{
		LogMessage("TweenBlendAnim: sequence '" + sequenceName.ToString() + "' not found in mesh '" + Mesh()->Name.ToString() + "'");
		return;
	}

	float numFrames = (float)sequence->NumFrames;

	// Keep the pose that the renderer tweens from
	SetTweenFromBlendAnimFrame(blendSlot);

	BlendAnimSequence()[blendSlot] = sequenceName;
	BlendAnimLast()[blendSlot] = 0.0f;
	BlendAnimMinRate()[blendSlot] = 0.0f;
	BlendAnimRate()[blendSlot] = 0.0f;
	OldBlendAnimRate()[blendSlot] = 0.0f;
	if (time <= 0.0f)
	{
		BlendTweenRate()[blendSlot] = 0.0f;
		BlendAnimFrame()[blendSlot] = 0.0f;
	}
	else
	{
		BlendTweenRate()[blendSlot] = 1.0f / (numFrames * time);
		BlendAnimFrame()[blendSlot] = -1.0f / numFrames;
	}

	vec4& sim = SimBlendAnim()[blendSlot];
	sim.x = BlendAnimFrame()[blendSlot] * 10000.0f;
	sim.y = BlendAnimRate()[blendSlot] * 10000.0f;
	sim.z = BlendTweenRate()[blendSlot] * 1000.0f;
	sim.w = BlendAnimLast()[blendSlot] * 10000.0f;
}

void UActor::LoopAnim(const NameString& sequence, float rate, float tweenTime, float minRate)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			if (AnimSequence() == sequence && IsAnimating() && bAnimLoop())
			{
				if (engine->LaunchInfo.IsDeusEx())
				{
					// As AActor::execLoopAnim in the Engine.dll of Deus Ex (build 1112fm): a loop of this sequence that
					// plays gets the new rates only. It keeps its frame and its tween rate, and it is not finished.
					// So a FinishAnim does not end while an AnimEnd event asks for the same loop again.
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate != 0.0f ? seq->Rate / seq->NumFrames * minRate : 0.0f;
					OldAnimRate() = AnimRate();
					bAnimFinished() = false;
				}
				else if (seq->NumFrames > 1)
				{
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
			}
			else
			{
				SetTweenFromAnimFrame();

				AnimSequence() = sequence;
				if (seq->NumFrames > 1)
				{
					AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
					AnimLast() = 1.0f - 1.0f / seq->NumFrames;
					bAnimNotify() = !seq->Notifys.empty();
					AnimRate() = rate * seq->Rate / seq->NumFrames;
					AnimMinRate() = minRate * seq->Rate / seq->NumFrames;
					TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
					OldAnimRate() = AnimRate();
				}
				else
				{
					// Special case for 1 frame animations. Simply keep drawing the animation for 0.1 second (or tween duration, if tweening).

					AnimFrame() = -1.0f;
					AnimLast() = 0.0f;
					AnimRate() = 0.0f;
					TweenRate() = tweenTime > 0.0f ? 1.0f / tweenTime : 10.0f;
					bAnimNotify() = false;
					OldAnimRate() = 0.0f;
					AnimMinRate() = 0.0f;
				}
				bAnimFinished() = false;
				bAnimLoop() = true;
			}
		}
	}
}

void UActor::TweenAnim(const NameString& sequence, float tweenTime)
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(sequence);
		if (seq)
		{
			SetTweenFromAnimFrame();

			AnimSequence() = sequence;
			AnimFrame() = tweenTime > 0.0f ? -1.0f / seq->NumFrames : 0.0f;
			AnimLast() = 0.0f;
			AnimRate() = 0.0f;
			AnimMinRate() = 0.0f;
			TweenRate() = tweenTime > 0.0f ? 1.0f / (tweenTime * seq->NumFrames) : 0.0f;
			OldAnimRate() = AnimRate();
			bAnimNotify() = false;
			bAnimFinished() = false;
			bAnimLoop() = false;
		}
	}
}

void UActor::PlayAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<EAnimType> Type, std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.PlayAnim_HP");
}

void UActor::LoopAnim_HP(const NameString& Sequence, std::optional<float> Rate, std::optional<float> TweenTime, std::optional<float> MinRate, std::optional<EAnimType> Type, std::optional<NameString> RootBone)
{
	LogUnimplemented("Actor.LoopAnim_HP");
}

void UActor::TickAnimation(float elapsed)
{
	if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
	{
		if (!IsAnimating() || AnimFrame() >= AnimLast())
			StateFrame->LatentState = LatentRunState::Continue;
	}

	for (int i = 0; elapsed > 0.0f && i < 10; i++)
	{
		// If AnimFrame is positive we are doing a normal animation. If it is negative we are doing a tween animation.
		float fromAnimTime = AnimFrame();
		if (fromAnimTime >= 0.0f)
		{
			// If AnimRate is positive we are animating at a fixed rate. If it is negative we animate based on velocity (using AnimRate as a speed scale factor)
			float animRate = (AnimRate() >= 0) ? AnimRate() : std::max(AnimMinRate(), -AnimRate() * length(Velocity()));
			if (animRate == 0.0f)
				break;

			// Find what time will we be at the end of the animation
			float toAnimTime = fromAnimTime + animRate * elapsed;

			// Stop at the next notify event, if any
			if (Mesh() && bAnimNotify())
			{
				MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
				if (seq)
				{
					bool foundEvent = false;
					for (const MeshAnimNotify& n : seq->Notifys)
					{
						if (n.Time > fromAnimTime && n.Time <= toAnimTime)
						{
							if (FindEventFunction(this, n.Function))
							{
								toAnimTime = n.Time;
								elapsed -= (toAnimTime - fromAnimTime) / animRate;
								AnimFrame() = toAnimTime;
								foundEvent = true;
								CallEvent(this, n.Function);
								break;
							}
						}
					}
					if (foundEvent)
						continue;
				}
			}

			// Looped animations got their AnimEnd notify event at the AnimLast point, NOT when the loop finishes!
			if (bAnimLoop() && AnimLast() > fromAnimTime && AnimLast() <= toAnimTime)
			{
				toAnimTime = AnimLast();
				elapsed -= (toAnimTime - fromAnimTime) / animRate;
				AnimFrame() = toAnimTime;

				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
				continue;
			}

			// Clamp elapsed time to the animation end. This differs for looping animations as they also have to take the last frame into account before looping.
			float animEndTime = bAnimLoop() ? 1.0f : AnimLast();
			if (toAnimTime < fromAnimTime) // This can happen if FinishAnim is called after a looping animation made it past the AnimLast point
			{
				toAnimTime = fromAnimTime;
				animEndTime = fromAnimTime;
				elapsed = 0.0f;
			}
			else if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / animRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime)
			{
				if (bAnimLoop())
				{
					AnimFrame() = 0.0f;
				}
				else
				{
					AnimRate() = 0.0f;
					bAnimFinished() = true;
				}
			}

			if (!bAnimLoop() && fromAnimTime < animEndTime && toAnimTime >= animEndTime)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				CallEvent(this, EventName::AnimEnd);
			}
		}
		else
		{
			float tweenRate = TweenRate();
			if (tweenRate == 0.0f)
				break;

			float toAnimTime = fromAnimTime + tweenRate * elapsed;

			float animEndTime = 0.0f;
			if (toAnimTime >= animEndTime)
			{
				elapsed -= (animEndTime - fromAnimTime) / tweenRate;
				toAnimTime = animEndTime;
			}
			else
			{
				elapsed = 0.0f;
			}

			AnimFrame() = toAnimTime;

			if (toAnimTime == animEndTime && AnimRate() == 0.0f)
			{
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					StateFrame->LatentState = LatentRunState::Continue;

				bAnimFinished() = true;
				//engine->LogMessage("CallEvent(AnimEnd) for " + Class->FriendlyName.ToString() + "");
				CallEvent(this, EventName::AnimEnd);
			}
		}
	}
}

// The animation step of AActor::Tick in the Engine.dll of Deus Ex (build 1112fm).
// One tick has four steps at most. A step ends at a notify function, at the end of a tween, at the last frame of a
// sequence that does not loop, or at the end of a loop. The time after that point goes to the next step, so the
// animation that an event starts gets the remaining time of the tick.
// A loop gives the AnimEnd event in the step that goes past its last frame, and it does not stop at that frame.
// bAnimFinished ends the FinishAnim of the state code (see PollLatentDeusEx).
void UActor::TickAnimationDeusEx(float elapsed)
{
	float seconds = elapsed;
	int iterations = 0;
	while (IsAnimating() && seconds > 0.0f && ++iterations <= 4)
	{
		float oldAnimFrame = AnimFrame();

		if (oldAnimFrame < 0.0f)
		{
			// A tween to the first frame of the sequence
			float frame = seconds * TweenRate() + oldAnimFrame;
			AnimFrame() = frame;
			if (frame < 0.0f)
				break;

			AnimFrame() = 0.0f;
			seconds = frame * seconds / (frame - oldAnimFrame);
			if (AnimRate() == 0.0f)
			{
				// A tween with no animation after it is complete
				bAnimFinished() = true;
				CallEvent(this, EventName::AnimEnd);
			}
			continue;
		}

		// A rate below zero is a factor for the speed of the actor
		if (AnimRate() < 0.0f)
			AnimFrame() += std::max(AnimMinRate(), -(length(Velocity()) * AnimRate())) * seconds;
		else
			AnimFrame() += seconds * AnimRate();

		// The notify function that comes first in this step
		if (bAnimNotify() && Mesh())
		{
			MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
			if (seq)
			{
				const MeshAnimNotify* best = nullptr;
				float bestFrames = 100000.0f;
				for (const MeshAnimNotify& n : seq->Notifys)
				{
					if (n.Time > oldAnimFrame && AnimFrame() >= n.Time && (!best || n.Time - oldAnimFrame < bestFrames))
					{
						best = &n;
						bestFrames = n.Time - oldAnimFrame;
					}
				}
				if (best)
				{
					NameString function = best->Function;
					seconds = (AnimFrame() - best->Time) * seconds / (AnimFrame() - oldAnimFrame);
					AnimFrame() = best->Time;
					if (FindEventFunction(this, function))
						CallEvent(this, function);
					continue;
				}
			}
		}

		if (AnimFrame() < AnimLast())
			break;

		if (!bAnimLoop())
		{
			// The sequence stops at its last frame
			seconds = (AnimFrame() - AnimLast()) * seconds / (AnimFrame() - oldAnimFrame);
			AnimFrame() = AnimLast();
			AnimRate() = 0.0f;
			bAnimFinished() = true;
			CallEvent(this, EventName::AnimEnd);
		}
		else
		{
			if (AnimFrame() >= 1.0f)
			{
				// The loop starts again
				seconds = (AnimFrame() - 1.0f) * seconds / (AnimFrame() - oldAnimFrame);
				AnimFrame() = 0.0f;
			}
			else
			{
				seconds = 0.0f;
			}

			if (oldAnimFrame < AnimLast())
			{
				// The loop went past its last frame in this step
				if (StateFrame && StateFrame->LatentState == LatentRunState::FinishAnim)
					bAnimFinished() = true;
				CallEvent(this, EventName::AnimEnd);
			}
		}
	}
}

void UActor::TickBlendAnimation(float elapsed)
{
	if (elapsed <= 0.0f)
		return;

	// Each slot has its own animation and gets the full time of the frame
	for (int i = 0; i < 4; i++)
	{
		if (BlendAnimSequence()[i].IsNone())
			continue;

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
			continue;

		float time = elapsed;

		if (BlendAnimFrame()[i] < 0.0f)
		{
			// Tween to the first frame of the sequence
			float oldFrame = BlendAnimFrame()[i];
			BlendAnimFrame()[i] += time * BlendTweenRate()[i];
			if (BlendAnimFrame()[i] < 0.0f)
				continue;

			// The tween ended in this frame. The animation gets the time that is left.
			time = time * BlendAnimFrame()[i] / (BlendAnimFrame()[i] - oldFrame);
			BlendAnimFrame()[i] = 0.0f;
			if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
				continue;
		}

		if (BlendAnimRate()[i] < 0.0f)
		{
			// The rate comes from the speed of the actor
			float adjustedRate = std::min(-length(Velocity()) * BlendAnimRate()[i], BlendAnimLast()[i]);
			BlendAnimFrame()[i] += adjustedRate * time;
		}
		else
		{
			BlendAnimFrame()[i] += BlendAnimRate()[i] * time;
		}

		if (BlendAnimFrame()[i] >= BlendAnimLast()[i])
		{
			// A blend animation does not loop. It stays on its last frame.
			BlendAnimFrame()[i] = BlendAnimLast()[i];
			BlendAnimRate()[i] = 0.0f;

			if (RemoteRole() < ENetRole::ROLE_SimulatedProxy)
			{
				SimBlendAnim()[i].x = BlendAnimFrame()[i] * 10000.0f;
				SimBlendAnim()[i].y = std::min(BlendAnimRate()[i] * 5000.0f, 32767.0f);
			}
		}
	}
}

void UActor::SetTweenFromAnimFrame()
{
	if (Mesh())
	{
		MeshAnimSeq* seq = Mesh()->GetSequence(AnimSequence());
		if (seq)
		{
			float animFrame = std::max(AnimFrame(), 0.0f) * seq->NumFrames;
			int frame0 = (int)animFrame;
			int frame1 = frame0 + 1;
			frame0 = frame0 % seq->NumFrames;
			frame1 = frame1 % seq->NumFrames;
			TweenFromAnimFrame.V0 = (seq->StartFrame + frame0) * Mesh()->FrameVerts;
			TweenFromAnimFrame.V1 = (seq->StartFrame + frame1) * Mesh()->FrameVerts;
			TweenFromAnimFrame.T = animFrame - (float)frame0;
		}
		else // For safety. Should never happen.
		{
			TweenFromAnimFrame.V0 = 0;
			TweenFromAnimFrame.V1 = 0;
			TweenFromAnimFrame.T = -1.0f;
		}
	}
}

void UActor::SetTweenFromBlendAnimFrame(int slot)
{
	if (slot < 0 || slot > 3)
		return;

	if (!Mesh())
		return;

	if (BlendAnimSequence()[slot].IsNone())
	{
		TweenFromBlendAnimFrame[slot].V0 = 0;
		TweenFromBlendAnimFrame[slot].V1 = 0;
		TweenFromBlendAnimFrame[slot].T = -1.0f;
		return;
	}

	MeshAnimSeq* seq = Mesh()->GetSequence(BlendAnimSequence()[slot]);
	if (seq)
	{
		float frame = std::max(BlendAnimFrame()[slot], 0.0f) * seq->NumFrames;
		int frame0 = (int)frame;
		int frame1 = frame0 + 1;
		frame0 = frame0 % seq->NumFrames;
		frame1 = frame1 % seq->NumFrames;
		TweenFromBlendAnimFrame[slot].V0 = (seq->StartFrame + frame0) * Mesh()->FrameVerts;
		TweenFromBlendAnimFrame[slot].V1 = (seq->StartFrame + frame1) * Mesh()->FrameVerts;
		TweenFromBlendAnimFrame[slot].T = frame - (float)frame0;
	}
	else
	{
		TweenFromBlendAnimFrame[slot].V0 = 0;
		TweenFromBlendAnimFrame[slot].V1 = 0;
		TweenFromBlendAnimFrame[slot].T = -1.0f;
	}
}

UActor* UActor::CreateAnimChannel(UClass* NewClass, EAnimType Type, const NameString& RootBone, bool bTransient)
{
	auto animChannel = Spawn(NewClass, {}, {}, {}, {});
	LogUnimplemented("Actor.CreateAnimChannel");
	return animChannel;
}

int UActor::BoneNumber(const NameString& Bone)
{
	LogUnimplemented("Actor.BoneNumber");
	return 0;
}

NameString UActor::BoneName(int Bone)
{
	LogUnimplemented("Actor.BoneName");
	return {};
}

vec3 UActor::BonePos(const NameString& Bone)
{
	LogUnimplemented("Actor.BonePos");
	return vec3(0.0f);
}

UTexture* UActor::GetMultiskin(int index)
{
	if (engine->LaunchInfo.ue1Version > 219 && index >= 0 && index < 8)
		return MultiSkins()[index];
	else
		return nullptr;
}

vec3 UActor::GetRenderExtent()
{
	LogUnimplemented("Actor.GetRenderExtent");
	return vec3(100.0f);
}
