#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RCBall.generated.h"

class ARCFootballer;
class UStaticMeshComponent;

/** Match ball with simple deterministic flight (gravity, drag, bounce, rolling friction) and dribbling. */
UCLASS()
class REFEREECAREER_API ARCBall : public AActor
{
	GENERATED_BODY()

public:
	ARCBall();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	void Kick(const FVector& Velocity, ARCFootballer* By);
	void GiveTo(ARCFootballer* Player);
	void Release();
	void PlaceAt(const FVector& GroundLocation);

	ARCFootballer* GetCarrier() const { return Carrier; }
	ARCFootballer* GetLastTouch() const { return LastTouch; }
	/** Ball velocity (AActor::GetVelocity is the engine one and stays untouched). */
	const FVector& GetBallVelocity() const { return Velocity; }
	bool IsInFlight() const { return !Carrier && Velocity.SizeSquared() > 25.f; }
	/** Where a rolling/flying ball will be in `Seconds` (ignores bounces). */
	FVector PredictLocation(float Seconds) const;
	/** Seconds since the carrier last changed (used to stop instant re-tackles). */
	float TimeSinceTouch() const { return SinceTouch; }

	static constexpr float Radius = 11.f;

private:
	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Mesh;
	UPROPERTY() TObjectPtr<ARCFootballer> Carrier;
	UPROPERTY() TObjectPtr<ARCFootballer> LastTouch;
	FVector Velocity = FVector::ZeroVector;
	float SinceTouch = 0.f;
	float DribblePhase = 0.f;
};
