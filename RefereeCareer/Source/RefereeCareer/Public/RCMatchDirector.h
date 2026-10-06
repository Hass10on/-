#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "RefCore/RcMatch.h"

#include "RCMatchDirector.generated.h"

class ACameraActor;
class APlayerController;
class ARCBall;
class ARCFootballer;
class ARCPitch;
class ARCReferee;
class UAudioComponent;
class UStaticMeshComponent;
class URCReplayRecorder;
class USoundBase;

UENUM()
enum class ERCMatchPhase : uint8 { None, Play, Restart, Staging, Decision, VarCheck, Review, Dissent, Goal, HalfTime, FullTime };

enum class ERCRestart : uint8 { KickOff, ThrowIn, GoalKick, Corner, FreeKick, IndirectFreeKick, Penalty, DropBall };

/** Everything the HUD shows; rebuilt by the director every frame. */
struct FRCHudState
{
	FString HomeName, AwayName, Competition, RoleLabel, Clock, PhaseLabel;
	FLinearColor HomeColor = FLinearColor::Red, AwayColor = FLinearColor::Blue;
	int32 HomeScore = 0, AwayScore = 0;
	float Heat = 0.f, Calm = 1.f, Stamina = 1.f;
	int32 Breaths = 0;
	bool bShowStamina = true;
	// Minimap (normalised -1..1 pitch coordinates)
	TArray<FVector2D> HomeDots, AwayDots;
	FVector2D BallDot = FVector2D::ZeroVector, RefDot = FVector2D::ZeroVector;
	bool bHasRefDot = false;
	// Decision prompt
	bool bDecision = false;
	FString DecisionTitle, DecisionNote;
	TArray<FString> Cues, Options;
	float Clarity = 0.f, TimeLeft = 0.f, TimeTotal = 1.f;
	// Dissent prompt
	bool bDissent = false;
	FString DissentText;
	TArray<FString> DissentOptions;
	// Replay / VAR review
	bool bReview = false;
	float ReviewTime = 0.f, ReviewDuration = 0.f;
	FString ReviewCamera;
	bool bReviewPlaying = false, bLines = false;
	// Feedback
	FString BigText;
	float BigTextTime = 0.f;
	int32 CardShown = 0;  // 1 yellow, 2 red
	float CardTime = 0.f;
	TArray<TPair<FString, float>> Toasts;
	bool bFinished = false;
};

/**
 * Runs a refereed match in the world: builds pitch, teams and officials, plays the football (team AI, restarts,
 * goals), stages the incidents planned by RefCore in front of the player, measures what the player could see,
 * collects the call, and handles dissent, the on-field monitor and the VAR room.
 */
UCLASS()
class REFEREECAREER_API ARCMatchDirector : public AActor
{
	GENERATED_BODY()

public:
	ARCMatchDirector();
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Builds the match from the current RefCore match session and gives the player their official (or the VAR room). */
	bool StartMatch(APlayerController* PC);

	// Player input (routed by ARCPlayerController)
	void OnOption(int32 Index);
	void OnBreathe();
	void OnCycleCamera(int32 Dir);
	void OnScrub(float Axis);
	void OnTogglePlayback();
	void OnToggleLines();

	const FRCHudState& GetHud() const { return Hud; }
	bool IsFinished() const { return Phase == ERCMatchPhase::FullTime && PhaseTime > 4.f; }
	bool IsPlayerVar() const;
	ARCReferee* GetPlayerReferee() const { return PlayerReferee; }
	/** Keeps the venue alive after the match (as the menu backdrop); the caller owns it from then on. */
	ARCPitch* DetachPitch();

private:
	// --- setup & flow (RCMatchDirector.cpp)
	void SpawnTeams();
	void SpawnOfficials(APlayerController* PC);
	void SpawnCameras();
	void SetPhase(ERCMatchPhase NewPhase);
	void UpdateClock(float RealDelta);
	float MatchMinute() const;
	void BeginRestart(ERCRestart Type, int32 Team, const FVector& Spot);
	void TickRestart(float DeltaSeconds);
	void ExecuteRestart();
	void OnGoal(int32 ScoringTeam);
	void CheckBoundaries();
	void PlaceTeamsForKickOff();
	void UpdateHud(float RealDelta);
	void UpdateAudio();
	void UpdateBroadcastCamera(float DeltaSeconds);
	void Whistle(int32 Kind);
	void Toast(const FString& Text, float Seconds = 3.f);
	void BigText(const FString& Text, float Seconds = 2.f);
	void SampleOfficiating(float RealDelta);
	void FinishMatch();

	// --- football AI (RCMatchDirectorAI.cpp)
	void TickTeams(float DeltaSeconds);
	void TickCarrier(ARCFootballer* P, float DeltaSeconds);
	void TickGoalkeeper(ARCFootballer* P, float DeltaSeconds);
	void TickOffBall(ARCFootballer* P, float DeltaSeconds, const TArray<ARCFootballer*>& Pressers);
	void TickOfficialsAI(float DeltaSeconds);
	void Pass(ARCFootballer* From, ARCFootballer* To, float Loft = 0.f);
	void Shoot(ARCFootballer* From, bool bForceGoal);
	bool TryPickUp(ARCFootballer* P);
	ARCFootballer* Nearest(int32 Team, const FVector& Where, const ARCFootballer* Except = nullptr, bool bAllowGK = false) const;
	ARCFootballer* GoalkeeperOf(int32 Team) const;
	/** Second-last defender X (world) of `DefendingTeam`, the offside line. */
	float OffsideLineX(int32 DefendingTeam, ARCFootballer** OutDefender = nullptr) const;
	int32 AttackDir(int32 Team) const;
	int32 PossessionTeam() const;
	FVector ToWorld(int32 Team, const FVector2D& Attack) const;

	// --- incidents, decisions, VAR (RCMatchDirectorIncidents.cpp)
	void TryBeginIncident();
	void TickStaging(float DeltaSeconds);
	void Contact();
	refcore::ViewSample MeasureView(const FVector& Point) const;
	void OpenDecision();
	void TickDecision(float RealDelta);
	void ResolveDecision(int32 OptionIndex, bool bTimeout);
	void ApplyVerdict(const refcore::DecisionResult& R);
	void ApplyCard(ARCFootballer* Who, refcore::Card Card);
	void SendOff(ARCFootballer* Who);
	void BeginDissent();
	void ResolveDissent(int32 OptionIndex);
	void BeginReview(bool bVarRoom);
	void TickReview(float RealDelta);
	void EndReview();
	void UpdateOffsideLines();
	void ContinueAfterIncident();

	// Actors
	UPROPERTY() TObjectPtr<ARCPitch> Pitch;
	UPROPERTY() TObjectPtr<ARCBall> Ball;
	UPROPERTY() TArray<TObjectPtr<ARCFootballer>> Players;
	UPROPERTY() TObjectPtr<ARCReferee> Centre;
	UPROPERTY() TObjectPtr<ARCReferee> AssistantNear;
	UPROPERTY() TObjectPtr<ARCReferee> AssistantFar;
	UPROPERTY() TObjectPtr<ARCReferee> PlayerReferee;
	UPROPERTY() TArray<TObjectPtr<ACameraActor>> Cameras;
	UPROPERTY() TObjectPtr<URCReplayRecorder> Recorder;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LineAttacker;
	UPROPERTY() TObjectPtr<UStaticMeshComponent> LineDefender;
	UPROPERTY() TObjectPtr<UAudioComponent> CrowdAudio;
	UPROPERTY() TObjectPtr<UAudioComponent> HeartAudio;
	UPROPERTY() TObjectPtr<APlayerController> PlayerPC;

	refcore::MatchSession* Session = nullptr;
	FRCHudState Hud;
	ERCMatchPhase Phase = ERCMatchPhase::None;
	float PhaseTime = 0.f;
	int32 Half = 1;
	float HalfElapsed = 0.f;
	float HalfSeconds = 360.f;
	float PlayedSeconds = 0.f;
	float LastRealTime = 0.f;
	float SampleTimer = 0.f;
	float RecordClock = 0.f;
	int32 Score[2] = {0, 0};
	int32 KickOffTeam = 0;
	int32 BroadcastCam = 0;

	// Restart in progress
	ERCRestart RestartType = ERCRestart::KickOff;
	int32 RestartTeam = 0;
	FVector RestartSpot = FVector::ZeroVector;
	TObjectPtr<ARCFootballer> RestartTaker = nullptr;
	bool bRestartWhistled = false;

	// Incident in progress
	int32 IncidentIndex = -1;
	FString Stage;
	TObjectPtr<ARCFootballer> Offender = nullptr;
	TObjectPtr<ARCFootballer> Victim = nullptr;
	TObjectPtr<ARCFootballer> Runner = nullptr;
	TObjectPtr<ARCFootballer> LineDefenderPlayer = nullptr;
	FVector ContactPoint = FVector::ZeroVector;
	float StageTime = 0.f;
	bool bContactDone = false;
	bool bBallLaunched = false;
	float OffsideLineAtPass = 0.f;
	refcore::ViewSample ContactView;
	float ContactRecordTime = 0.f;
	float DecisionOpenedAt = 0.f;
	float DecisionWindow = 5.f;
	bool bDecisionIsReview = false;
	refcore::DecisionResult LastResult;
	bool bPendingGoalCheck = false;
	int32 PendingGoalTeam = -1;
	TArray<TObjectPtr<ARCFootballer>> Protesters;

	// Replay review
	float ReviewTime = 0.f;
	bool bReviewPlaying = true;
	bool bShowLines = false;
	int32 ReviewCam = 0;
	TArray<FVector> ReviewCamOffsets;
	FTransform SavedCamXf;
};
