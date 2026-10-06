#pragma once

#include "CoreMinimal.h"
#include "RCHumanCharacter.h"
#include "RCReferee.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UStaticMeshComponent;

UENUM()
enum class ERCOfficial : uint8 { Centre, AssistantNear, AssistantFar };

/**
 * A match official. The player controls one (centre referee or assistant); the others are AI-driven by the
 * match director. Tracks stamina (from the career fitness), distance covered and the camera.
 */
UCLASS()
class REFEREECAREER_API ARCReferee : public ARCHumanCharacter
{
	GENERATED_BODY()

public:
	ARCReferee();
	virtual void Tick(float DeltaSeconds) override;

	void ConfigureOfficial(ERCOfficial InRole, float FitnessStat, float HalfLength, float HalfWidth);
	void InputMove(const FVector2D& Axis);
	void SetSprinting(bool bOn) { bWantsSprint = bOn; }
	void SetFaceBall(bool bOn);
	bool IsFacingBall() const { return bFaceBall; }
	void ToggleCameraMode();
	void SetBallLocation(const FVector& Ball) { BallLocation = Ball; }
	/** Assistant flag raised / lowered (visual). */
	void RaiseFlag(bool bUp);

	ERCOfficial GetOfficial() const { return Official; }
	float GetStamina01() const { return Stamina / 100.f; }
	float GetDistanceMetres() const { return Distance / 100.f; }
	float GetExhaustedSeconds() const { return ExhaustedSeconds; }
	bool IsSprinting() const { return bSprinting; }
	/** Eye position and view direction used for sight-line checks. */
	FVector EyeLocation() const { return GetActorLocation() + FVector(0.f, 0.f, 75.f); }

	UPROPERTY(VisibleAnywhere) TObjectPtr<USpringArmComponent> CameraBoom;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> FollowCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> HeadCamera;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Flag;

private:
	ERCOfficial Official = ERCOfficial::Centre;
	float Stamina = 100.f;
	float FitnessFactor = 0.5f;
	float Distance = 0.f;
	float ExhaustedSeconds = 0.f;
	bool bWantsSprint = false;
	bool bSprinting = false;
	bool bFaceBall = false;
	bool bFirstPerson = false;
	float HalfLen = 5250.f;
	float HalfWid = 3400.f;
	FVector BallLocation = FVector::ZeroVector;
	FVector LastLocation = FVector::ZeroVector;
};
