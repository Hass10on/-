#include "RCReplayRecorder.h"

#include "RCBall.h"
#include "RCHumanCharacter.h"

URCReplayRecorder::URCReplayRecorder()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void URCReplayRecorder::SetSubjects(ARCBall* InBall, const TArray<ARCHumanCharacter*>& InCharacters)
{
	Ball = InBall;
	Characters.Reset();
	for (ARCHumanCharacter* C : InCharacters) Characters.Add(C);
	Clear();
}

void URCReplayRecorder::Clear()
{
	Frames.Reset();
	LastRecord = -1.f;
}

float URCReplayRecorder::Duration() const
{
	return Frames.Num() < 2 ? 0.f : Frames.Last().Time - Frames[0].Time;
}

void URCReplayRecorder::Record(float Now)
{
	if (bPlaying || !Ball || Now - LastRecord < 0.05f) return;
	LastRecord = Now;
	FReplayFrame F;
	F.Time = Now;
	F.Ball = Ball->GetActorLocation();
	F.BallRot = Ball->GetActorQuat();
	F.Roots.Reserve(Characters.Num());
	F.PoseIndex.Init(-1, Characters.Num());
	// Pose the players closest to the ball.
	TArray<TPair<float, int32>> Near;
	for (int32 i = 0; i < Characters.Num(); ++i)
	{
		const ARCHumanCharacter* C = Characters[i];
		F.Roots.Add(C ? C->GetActorTransform() : FTransform::Identity);
		if (C && C->HasSkeletalArt())
		{
			const float D = FVector::Dist2D(C->GetActorLocation(), F.Ball);
			if (D < PoseRadius) Near.Add(TPair<float, int32>(D, i));
		}
	}
	Near.Sort([](const TPair<float, int32>& A, const TPair<float, int32>& B) { return A.Key < B.Key; });
	for (int32 k = 0; k < Near.Num() && k < MaxPosed; ++k)
	{
		TArray<FTransform> Pose;
		if (Characters[Near[k].Value]->CapturePose(Pose))
		{
			F.PoseIndex[Near[k].Value] = F.Poses.Num();
			F.Poses.Add(MoveTemp(Pose));
		}
	}
	Frames.Add(MoveTemp(F));
	while (Frames.Num() > 2 && Frames.Last().Time - Frames[0].Time > RecordSeconds)
	{
		Frames.RemoveAt(0, 1, EAllowShrinking::No);
	}
}

const URCReplayRecorder::FReplayFrame* URCReplayRecorder::FrameAt(float Time, int32* OutIndex) const
{
	if (Frames.Num() == 0) return nullptr;
	const float Abs = Frames[0].Time + FMath::Clamp(Time, 0.f, Duration());
	int32 Best = 0;
	for (int32 i = 0; i < Frames.Num(); ++i)
	{
		if (Frames[i].Time <= Abs) Best = i;
		else break;
	}
	if (OutIndex) *OutIndex = Best;
	return &Frames[Best];
}

void URCReplayRecorder::BeginPlayback()
{
	if (bPlaying || Frames.Num() < 2) return;
	bPlaying = true;
	LiveRoots.Reset();
	for (ARCHumanCharacter* C : Characters)
	{
		LiveRoots.Add(C ? C->GetActorTransform() : FTransform::Identity);
		if (C) C->CustomTimeDilation = 0.f;
	}
	LiveBall = Ball ? Ball->GetActorLocation() : FVector::ZeroVector;
	if (Ball) Ball->CustomTimeDilation = 0.f;
}

void URCReplayRecorder::ShowAt(float Time)
{
	if (!bPlaying) return;
	const FReplayFrame* F = FrameAt(Time);
	if (!F) return;
	if (Ball) Ball->SetActorLocationAndRotation(F->Ball, F->BallRot);
	for (int32 i = 0; i < Characters.Num() && i < F->Roots.Num(); ++i)
	{
		ARCHumanCharacter* C = Characters[i];
		if (!C) continue;
		C->SetActorTransform(F->Roots[i], false, nullptr, ETeleportType::TeleportPhysics);
		const int32 P = F->PoseIndex.IsValidIndex(i) ? F->PoseIndex[i] : -1;
		C->ShowReplayPose(P >= 0 ? &F->Poses[P] : nullptr);
	}
}

void URCReplayRecorder::EndPlayback()
{
	if (!bPlaying) return;
	bPlaying = false;
	for (int32 i = 0; i < Characters.Num(); ++i)
	{
		ARCHumanCharacter* C = Characters[i];
		if (!C) continue;
		C->ShowReplayPose(nullptr);
		if (LiveRoots.IsValidIndex(i)) C->SetActorTransform(LiveRoots[i], false, nullptr, ETeleportType::TeleportPhysics);
		C->CustomTimeDilation = 1.f;
	}
	if (Ball)
	{
		Ball->SetActorLocation(LiveBall);
		Ball->CustomTimeDilation = 1.f;
	}
}

FVector URCReplayRecorder::ActorLocationAt(const ARCHumanCharacter* Who, float Time) const
{
	const FReplayFrame* F = FrameAt(Time);
	int32 Idx = INDEX_NONE;
	for (int32 i = 0; i < Characters.Num(); ++i)
	{
		if (Characters[i].Get() == Who) Idx = i;
	}
	return (F && Idx != INDEX_NONE && F->Roots.IsValidIndex(Idx)) ? F->Roots[Idx].GetLocation() : FVector::ZeroVector;
}

FVector URCReplayRecorder::BallLocationAt(float Time) const
{
	const FReplayFrame* F = FrameAt(Time);
	return F ? F->Ball : FVector::ZeroVector;
}
