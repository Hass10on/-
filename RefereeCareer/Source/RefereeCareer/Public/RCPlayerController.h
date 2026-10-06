#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "RefCore/RcCommon.h"

#include "RCPlayerController.generated.h"

class ACameraActor;
class ARCMatchDirector;
class ARCPitch;
class SBox;
class SWidget;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/**
 * Drives the whole game: the career screens (Slate), the transition into and out of matches, and all input.
 * Input actions and mappings are created in code, so the project needs no input assets.
 */
UCLASS()
class REFEREECAREER_API ARCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARCPlayerController();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void SetupInputComponent() override;

private:
	// Screens
	void SetScreen(const TSharedRef<SWidget>& Widget, bool bGameInput = false);
	void ShowTitle();
	void ShowNewCareer();
	void ShowCareerPhase();
	void ShowSeasonStart();
	void ShowPlanning();
	void ShowWeekLog(const TArray<FString>& Lines);
	void ShowEvent();
	void ShowResult(const FString& Title, const FString& Body);
	void ShowAppointment();
	void ShowReport();
	void ShowPress();
	void ShowSeasonReview();
	void ShowEnding();
	void ShowPause();
	void ShowError(const FString& Message);

	// Match
	void StartMatch();
	void EndMatch();
	void EnsureBackdrop();
	void UpdateBackdropCamera(float DeltaTime);
	bool InMatch() const;

	// Input
	void CreateInputActions();
	void OnMove(const FInputActionValue& Value);
	void OnMoveStop(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnSprint(const FInputActionValue& Value);
	void OnSprintStop(const FInputActionValue& Value);
	void OnFaceBall(const FInputActionValue& Value);
	void OnFaceBallStop(const FInputActionValue& Value);
	void OnCamera(const FInputActionValue& Value);
	void OnBreathe(const FInputActionValue& Value);
	void OnPause(const FInputActionValue& Value);
	void OnCamPrev(const FInputActionValue& Value);
	void OnCamNext(const FInputActionValue& Value);
	void OnLines(const FInputActionValue& Value);
	void OnPlayPause(const FInputActionValue& Value);
	void OnOption(int32 Index);
	void OnOption1(const FInputActionValue& Value);
	void OnOption2(const FInputActionValue& Value);
	void OnOption3(const FInputActionValue& Value);
	void OnOption4(const FInputActionValue& Value);
	void OnOption5(const FInputActionValue& Value);
	void OnOption6(const FInputActionValue& Value);

	FString S(const std::string& Key) const;

	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> SprintAction;
	UPROPERTY() TObjectPtr<UInputAction> FaceBallAction;
	UPROPERTY() TObjectPtr<UInputAction> CameraAction;
	UPROPERTY() TObjectPtr<UInputAction> BreatheAction;
	UPROPERTY() TObjectPtr<UInputAction> PauseAction;
	UPROPERTY() TObjectPtr<UInputAction> CamPrevAction;
	UPROPERTY() TObjectPtr<UInputAction> CamNextAction;
	UPROPERTY() TObjectPtr<UInputAction> LinesAction;
	UPROPERTY() TObjectPtr<UInputAction> PlayPauseAction;
	UPROPERTY() TArray<TObjectPtr<UInputAction>> OptionActions;

	UPROPERTY() TObjectPtr<ARCMatchDirector> Director;
	UPROPERTY() TObjectPtr<ARCPitch> Backdrop;
	UPROPERTY() TObjectPtr<ACameraActor> BackdropCamera;

	TSharedPtr<SBox> Root;
	TSharedPtr<SWidget> RootWidget;
	float BackdropAngle = 0.f;
	FVector2D MoveAxis = FVector2D::ZeroVector;
	bool bPaused = false;

	// Planning screen state
	FString PendingName;
	refcore::Role PendingRole = refcore::Role::Center;
	TArray<FString> PlannedActivities;
	refcore::Role PreferredRole = refcore::Role::Center;
};
