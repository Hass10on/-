#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RCPitch.generated.h"

class UHierarchicalInstancedStaticMeshComponent;
class UInstancedStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;

/** Out-of-play classification used for restarts. */
enum class ERCBoundary : uint8 { InPlay, Touchline, GoalLineLeft, GoalLineRight, Goal0, Goal1 };

/**
 * Builds the playing field for a tier: grass, markings, goals, corner flags, the venue (Blender stadium or
 * neighbourhood ground), seats and crowd, floodlights and the sky. Works on an empty level.
 * Pitch axes: X = length (team 0 defends -X in the first half), Y = width, origin = centre spot.
 */
UCLASS()
class REFEREECAREER_API ARCPitch : public AActor
{
	GENERATED_BODY()

public:
	ARCPitch();

	/** Lengths in metres, venue "stadium" / "stadium_small" / "community", crowd 0..1, kit colours for the stands. */
	void Build(float LengthM, float WidthM, const FString& Venue, float CrowdDensity, const FLinearColor& HomeColor,
		const FLinearColor& AwayColor, bool bNight);
	void SetCrowdExcitement(float Excitement01);

	float HalfLength() const { return HalfLen; }
	float HalfWidth() const { return HalfWid; }
	bool IsSmallPitch() const { return HalfLen < 4000.f; }
	/** Centre of the goal line defended by the given end (-1 = -X end, +1 = +X end). */
	FVector GoalCenter(int32 EndSign) const { return FVector(EndSign * HalfLen, 0.f, 0.f); }
	FVector PenaltySpot(int32 EndSign) const;
	float PenaltyAreaDepth() const;
	float PenaltyAreaHalfWidth() const;
	bool IsInPenaltyArea(const FVector& Location, int32 EndSign) const;
	float GoalHalfWidth() const;
	float GoalHeight() const;
	ERCBoundary Classify(const FVector& BallLocation) const;
	FVector ClampToField(const FVector& Location, float Margin = 0.f) const;
	/** Venue camera positions (world space, cm) from the Blender venue data. */
	const TMap<FName, FVector>& GetCameraSpots() const { return CameraSpots; }

private:
	void ClearBuilt();
	void BuildMarkings();
	void BuildGoals();
	void BuildVenue(const FString& Venue, float CrowdDensity, const FLinearColor& HomeColor, const FLinearColor& AwayColor);
	void BuildLighting(bool bNight, const TArray<FVector>& Floodlights);
	void AddLine(const FVector2D& A, const FVector2D& B);
	void AddArc(const FVector2D& Center, float Radius, float StartDeg, float EndDeg, int32 Segments);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FTransform& Xf, UMaterialInterface* Material = nullptr);

	UPROPERTY() TObjectPtr<UStaticMeshComponent> Grass;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Lines;
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Built;
	UPROPERTY() TArray<TObjectPtr<UHierarchicalInstancedStaticMeshComponent>> CrowdComponents;
	UPROPERTY() TArray<TObjectPtr<AActor>> SpawnedActors;

	float HalfLen = 5250.f;
	float HalfWid = 3400.f;
	TMap<FName, FVector> CameraSpots;
};
