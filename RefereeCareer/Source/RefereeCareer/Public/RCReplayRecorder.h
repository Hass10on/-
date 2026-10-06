#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RCReplayRecorder.generated.h"

class ARCBall;
class ARCHumanCharacter;

/**
 * Rolling recording of the last seconds of play for VAR reviews and the on-field monitor.
 * Everyone's root transform is recorded at 20 Hz; full skeletal poses only for players near the ball,
 * so the review can show the actual contact without recording 22 full skeletons.
 */
UCLASS()
class REFEREECAREER_API URCReplayRecorder : public UActorComponent
{
	GENERATED_BODY()

public:
	URCReplayRecorder();

	void SetSubjects(ARCBall* InBall, const TArray<ARCHumanCharacter*>& InCharacters);
	void Record(float Now);
	void Clear();

	bool IsPlaying() const { return bPlaying; }
	float Duration() const;
	/** Freezes the live actors and shows the recording at `Time` (0 .. Duration). */
	void BeginPlayback();
	void ShowAt(float Time);
	void EndPlayback();
	/** Recorded ball/actor location at a time, used to measure offside from the chosen frame. */
	FVector ActorLocationAt(const ARCHumanCharacter* Who, float Time) const;
	FVector BallLocationAt(float Time) const;

	float RecordSeconds = 10.f;
	float PoseRadius = 2200.f;
	int32 MaxPosed = 8;

private:
	struct FReplayFrame
	{
		float Time = 0.f;
		FVector Ball = FVector::ZeroVector;
		FQuat BallRot = FQuat::Identity;
		TArray<FTransform> Roots;
		TArray<int32> PoseIndex;           // per character: index into Poses or -1
		TArray<TArray<FTransform>> Poses;
	};
	const FReplayFrame* FrameAt(float Time, int32* OutIndex = nullptr) const;

	TArray<FReplayFrame> Frames;
	UPROPERTY() TObjectPtr<ARCBall> Ball;
	UPROPERTY() TArray<TObjectPtr<ARCHumanCharacter>> Characters;
	TArray<FTransform> LiveRoots;
	FVector LiveBall = FVector::ZeroVector;
	float LastRecord = -1.f;
	bool bPlaying = false;
};
