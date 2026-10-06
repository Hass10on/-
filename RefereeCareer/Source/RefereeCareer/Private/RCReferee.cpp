#include "RCReferee.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "RCSettings.h"
#include "UObject/ConstructorHelpers.h"

ARCReferee::ARCReferee()
{
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(GetCapsuleComponent());
	CameraBoom->TargetArmLength = 420.f;
	CameraBoom->SocketOffset = FVector(0.f, 55.f, 95.f);
	CameraBoom->bUsePawnControlRotation = true;
	CameraBoom->bEnableCameraLag = true;
	CameraBoom->CameraLagSpeed = 12.f;
	CameraBoom->bDoCollisionTest = false;
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
	FollowCamera->SetFieldOfView(78.f);
	HeadCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("HeadCamera"));
	HeadCamera->SetupAttachment(GetCapsuleComponent());
	HeadCamera->SetRelativeLocation(FVector(12.f, 0.f, 72.f));
	HeadCamera->bUsePawnControlRotation = true;
	HeadCamera->SetFieldOfView(88.f);
	HeadCamera->SetActive(false);

	Flag = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Flag"));
	Flag->SetupAttachment(GetCapsuleComponent());
	Flag->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Flag->SetRelativeLocation(FVector(10.f, 30.f, -10.f));
	Flag->SetVisibility(false);
}

void ARCReferee::ConfigureOfficial(ERCOfficial InRole, float FitnessStat, float HalfLength, float HalfWidth)
{
	Official = InRole;
	FitnessFactor = FMath::Clamp(FitnessStat / 100.f, 0.f, 1.f);
	HalfLen = HalfLength;
	HalfWid = HalfWidth;
	LastLocation = GetActorLocation();
	if (Official != ERCOfficial::Centre)
	{
		if (UStaticMesh* FlagMesh = URCSettings::Get()->ARFlagMesh.LoadSynchronous())
		{
			Flag->SetStaticMesh(FlagMesh);
			Flag->SetVisibility(true);
		}
	}
}

void ARCReferee::SetFaceBall(bool bOn)
{
	bFaceBall = bOn;
	GetCharacterMovement()->bOrientRotationToMovement = !bOn;
}

void ARCReferee::ToggleCameraMode()
{
	bFirstPerson = !bFirstPerson;
	FollowCamera->SetActive(!bFirstPerson);
	HeadCamera->SetActive(bFirstPerson);
	if (bHasSkeletalArt) GetMesh()->SetOwnerNoSee(bFirstPerson);
	PrimitiveHead->SetOwnerNoSee(bFirstPerson);
	PrimitiveBody->SetOwnerNoSee(bFirstPerson);
}

void ARCReferee::RaiseFlag(bool bUp)
{
	Flag->SetRelativeLocationAndRotation(bUp ? FVector(10.f, 30.f, 95.f) : FVector(10.f, 30.f, -10.f), FRotator(bUp ? 0.f : 150.f, 0.f, 0.f));
	if (bUp) PlayTagged(TEXT("flag_raise"));
}

void ARCReferee::InputMove(const FVector2D& Axis)
{
	const AController* C = GetController();
	if (IsDown() || !C) return;
	const FRotator Yaw(0.f, C->GetControlRotation().Yaw, 0.f);
	const FVector Fwd = FRotationMatrix(Yaw).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(Yaw).GetUnitAxis(EAxis::Y);
	AddMovementInput(Fwd, Axis.Y);
	AddMovementInput(Right, Axis.X);
}

void ARCReferee::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const URCSettings* S = URCSettings::Get();
	UCharacterMovementComponent* Move = GetCharacterMovement();

	// Sprinting drains stamina faster for an unfit referee; jogging recovers it.
	const float Speed2D = GetVelocity().Size2D();
	bSprinting = bWantsSprint && Stamina > 3.f && Speed2D > 50.f;
	const float Drain = FMath::Lerp(16.f, 7.f, FitnessFactor);
	const float Regen = FMath::Lerp(5.f, 11.f, FitnessFactor);
	Stamina = FMath::Clamp(Stamina + (bSprinting ? -Drain : (Speed2D < 300.f ? Regen : Regen * 0.4f)) * DeltaSeconds, 0.f, 100.f);
	const float Tired = Stamina < 25.f ? FMath::Lerp(0.72f, 1.f, Stamina / 25.f) : 1.f;
	const float Jog = S->RefereeJogSpeed * FMath::Lerp(0.9f, 1.05f, FitnessFactor);
	const float Sprint = S->RefereeSprintSpeed * FMath::Lerp(0.88f, 1.06f, FitnessFactor);
	Move->MaxWalkSpeed = (bSprinting ? Sprint : Jog) * Tired;
	if (Stamina < 15.f) ExhaustedSeconds += DeltaSeconds;

	if (bFaceBall)
	{
		FaceTowards(BallLocation, DeltaSeconds, 10.f);
	}

	// Assistant referees run the touchline in their own half.
	FVector L = GetActorLocation();
	if (Official != ERCOfficial::Centre)
	{
		const float LineY = (Official == ERCOfficial::AssistantNear ? -1.f : 1.f) * (HalfWid + 150.f);
		const float MinX = Official == ERCOfficial::AssistantNear ? -HalfLen : 0.f;
		const float MaxX = Official == ERCOfficial::AssistantNear ? 0.f : HalfLen;
		const FVector Clamped(FMath::Clamp(L.X, MinX - 100.f, MaxX + 100.f), FMath::FInterpTo(L.Y, LineY, DeltaSeconds, 10.f), L.Z);
		if (!Clamped.Equals(L, 0.5f)) SetActorLocation(Clamped);
		L = Clamped;
	}
	Distance += FVector::Dist2D(L, LastLocation);
	LastLocation = L;
}
