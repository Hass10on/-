#include "RCBall.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "RCFootballer.h"
#include "RCSettings.h"
#include "RefereeCareer.h"
#include "UObject/ConstructorHelpers.h"

ARCBall::ARCBall()
{
	PrimaryActorTick.bCanEverTick = true;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(true);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		Mesh->SetStaticMesh(Sphere.Object);
		Mesh->SetRelativeScale3D(FVector(0.22f));
	}
}

void ARCBall::BeginPlay()
{
	Super::BeginPlay();
	if (UStaticMesh* Art = URCSettings::Get()->BallMesh.LoadSynchronous())
	{
		Mesh->SetStaticMesh(Art);
		Mesh->SetRelativeScale3D(FVector(1.f));
	}
}

void ARCBall::Kick(const FVector& InVelocity, ARCFootballer* By)
{
	Carrier = nullptr;
	Velocity = InVelocity;
	LastTouch = By;
	SinceTouch = 0.f;
	FVector L = GetActorLocation();
	L.Z = FMath::Max(L.Z, Radius + 1.f);
	SetActorLocation(L);
}

void ARCBall::GiveTo(ARCFootballer* Player)
{
	if (Carrier != Player)
	{
		SinceTouch = 0.f;
	}
	Carrier = Player;
	LastTouch = Player;
	Velocity = FVector::ZeroVector;
}

void ARCBall::Release()
{
	if (Carrier)
	{
		Velocity = Carrier->GetVelocity();
		Velocity.Z = 0.f;
	}
	Carrier = nullptr;
}

void ARCBall::PlaceAt(const FVector& GroundLocation)
{
	Carrier = nullptr;
	Velocity = FVector::ZeroVector;
	SetActorLocation(FVector(GroundLocation.X, GroundLocation.Y, Radius));
}

FVector ARCBall::PredictLocation(float Seconds) const
{
	if (Carrier)
	{
		return GetActorLocation() + Carrier->GetVelocity() * Seconds;
	}
	// Rolling deceleration ~ 2.2 m/s^2.
	const FVector Flat(Velocity.X, Velocity.Y, 0.f);
	const float Speed = Flat.Size();
	if (Speed < 1.f)
	{
		return GetActorLocation();
	}
	const float Decel = 220.f;
	const float T = FMath::Min(Seconds, Speed / Decel);
	const float Dist = Speed * T - 0.5f * Decel * T * T;
	return GetActorLocation() + Flat / Speed * Dist;
}

void ARCBall::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SinceTouch += DeltaSeconds;
	if (Carrier)
	{
		// Dribble: the ball runs a little ahead of the carrier's feet and is tapped forward.
		const FVector Fwd = Carrier->GetActorForwardVector();
		const float Speed = Carrier->GetVelocity().Size2D();
		DribblePhase += DeltaSeconds * (2.f + Speed / 150.f);
		const float Ahead = 45.f + 25.f * FMath::Abs(FMath::Sin(DribblePhase));
		FVector Target = Carrier->GetActorLocation() + Fwd * Ahead;
		Target.Z = Radius;
		const FVector NewLoc = FMath::VInterpTo(GetActorLocation(), Target, DeltaSeconds, 14.f);
		const FVector Delta = NewLoc - GetActorLocation();
		SetActorLocation(NewLoc);
		if (Delta.SizeSquared() > 0.01f)
		{
			const FVector Axis = FVector::CrossProduct(FVector::UpVector, Delta.GetSafeNormal());
			const float Angle = Delta.Size() / Radius;
			AddActorWorldRotation(FQuat(Axis, Angle));
		}
		return;
	}
	FVector Loc = GetActorLocation();
	const float Gravity = -980.f;
	const bool bOnGround = Loc.Z <= Radius + 0.5f && FMath::Abs(Velocity.Z) < 60.f;
	if (bOnGround)
	{
		Velocity.Z = 0.f;
		Loc.Z = Radius;
		const FVector Flat(Velocity.X, Velocity.Y, 0.f);
		const float Speed = Flat.Size();
		const float NewSpeed = FMath::Max(0.f, Speed - 220.f * DeltaSeconds);
		Velocity = Speed > 0.f ? Flat * (NewSpeed / Speed) : FVector::ZeroVector;
	}
	else
	{
		Velocity.Z += Gravity * DeltaSeconds;
		Velocity *= FMath::Max(0.f, 1.f - 0.05f * DeltaSeconds);  // air drag
	}
	Loc += Velocity * DeltaSeconds;
	if (Loc.Z < Radius)
	{
		Loc.Z = Radius;
		if (Velocity.Z < -60.f)
		{
			Velocity.Z = -Velocity.Z * 0.55f;
			Velocity.X *= 0.85f;
			Velocity.Y *= 0.85f;
		}
		else
		{
			Velocity.Z = 0.f;
		}
	}
	const FVector Delta = Loc - GetActorLocation();
	SetActorLocation(Loc);
	const FVector FlatDelta(Delta.X, Delta.Y, 0.f);
	if (FlatDelta.SizeSquared() > 0.01f)
	{
		const FVector Axis = FVector::CrossProduct(FVector::UpVector, FlatDelta.GetSafeNormal());
		AddActorWorldRotation(FQuat(Axis, FlatDelta.Size() / Radius));
	}
}
