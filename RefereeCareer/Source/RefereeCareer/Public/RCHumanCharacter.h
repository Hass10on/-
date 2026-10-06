#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RCHumanCharacter.generated.h"

class UAnimMontage;
class UMeshComponent;
class USkinnedMeshComponent;
class UPoseableMeshComponent;
class USkeletalMesh;
class UStaticMeshComponent;

/**
 * Shared body for players and match officials. Uses the skeletal mesh / animation blueprint / MetaHuman visual
 * from Project Settings > Referee Career, or a simple primitive body when no character art is set up yet.
 */
UCLASS(Abstract)
class REFEREECAREER_API ARCHumanCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ARCHumanCharacter();
	virtual void Tick(float DeltaSeconds) override;

	/** Applies the configured mesh/anim (player or referee art) and the kit colours. */
	void SetupAppearance(bool bReferee, const FLinearColor& Kit, const FLinearColor& Secondary, int32 VariantSeed);

	/** Plays the montage configured for `Tag`; returns its length, or 0 when the tag has no montage. */
	float PlayTagged(FName Tag, float Rate = 1.f);
	/** Falls to the ground (montage, ragdoll or procedural lean) and gets up after `Seconds`. */
	void FallDown(float Seconds, const FVector& Push);
	bool IsDown() const { return DownTime > 0.f; }

	/** Steers towards a point with the given max speed (cm/s); returns remaining 2D distance. */
	float MoveTowards(const FVector& Target, float MaxSpeed, float Acceptance = 60.f);
	void FaceTowards(const FVector& Point, float DeltaSeconds, float Speed = 8.f);
	void StopMoving();

	/** Replay support: snapshot of component-space bone transforms (empty for primitive bodies). */
	bool CapturePose(TArray<FTransform>& OutPose) const;
	void ShowReplayPose(const TArray<FTransform>* Pose);

	bool HasSkeletalArt() const { return bHasSkeletalArt; }

protected:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PrimitiveBody;
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> PrimitiveHead;
	UPROPERTY() TObjectPtr<UPoseableMeshComponent> ReplayGhost;
	UPROPERTY() TArray<TObjectPtr<AActor>> VisualActors;

	bool bHasSkeletalArt = false;
	bool bRagdoll = false;
	float DownTime = 0.f;
	FVector MeshRelativeLocation = FVector::ZeroVector;
	FRotator MeshRelativeRotation = FRotator::ZeroRotator;

private:
	void ApplyKitToMesh(UMeshComponent* Mesh, const FLinearColor& Kit, const FLinearColor& Secondary);
	void GetUp();
	void SetLeader(USkinnedMeshComponent* Leader);
};
