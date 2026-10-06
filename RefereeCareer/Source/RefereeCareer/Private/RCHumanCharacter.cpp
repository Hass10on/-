#include "RCHumanCharacter.h"

#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RCSettings.h"
#include "RefereeCareer.h"
#include "UObject/ConstructorHelpers.h"

ARCHumanCharacter::ARCHumanCharacter()
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
	bUseControllerRotationYaw = false;
	GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);

	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bOrientRotationToMovement = true;
	Move->RotationRate = FRotator(0.f, 600.f, 0.f);
	Move->MaxWalkSpeed = 480.f;
	Move->MaxAcceleration = 1700.f;
	Move->BrakingDecelerationWalking = 1400.f;
	Move->bUseSeparateBrakingFriction = true;
	Move->BrakingFriction = 4.f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sph(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	PrimitiveBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrimitiveBody"));
	PrimitiveBody->SetupAttachment(GetCapsuleComponent());
	PrimitiveBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrimitiveBody->SetRelativeLocation(FVector(0.f, 0.f, -25.f));
	PrimitiveBody->SetRelativeScale3D(FVector(0.42f, 0.3f, 1.3f));
	PrimitiveHead = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PrimitiveHead"));
	PrimitiveHead->SetupAttachment(GetCapsuleComponent());
	PrimitiveHead->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PrimitiveHead->SetRelativeLocation(FVector(0.f, 0.f, 62.f));
	PrimitiveHead->SetRelativeScale3D(FVector(0.24f));
	if (Cyl.Succeeded()) PrimitiveBody->SetStaticMesh(Cyl.Object);
	if (Sph.Succeeded()) PrimitiveHead->SetStaticMesh(Sph.Object);
}

void ARCHumanCharacter::SetupAppearance(bool bReferee, const FLinearColor& Kit, const FLinearColor& Secondary, int32 VariantSeed)
{
	const URCSettings* S = URCSettings::Get();
	USkeletalMesh* Mesh = nullptr;
	UClass* Anim = nullptr;
	if (bReferee)
	{
		Mesh = S->RefereeMesh.LoadSynchronous();
		Anim = S->RefereeAnimClass.LoadSynchronous();
	}
	if (!Mesh) Mesh = S->PlayerMesh.LoadSynchronous();
	if (!Anim) Anim = S->PlayerAnimClass.LoadSynchronous();

	USkeletalMeshComponent* Skel = GetMesh();
	if (Mesh)
	{
		Skel->SetSkeletalMeshAsset(Mesh);
		if (Anim) Skel->SetAnimInstanceClass(Anim);
		Skel->SetRelativeLocationAndRotation(S->MeshOffset, S->MeshRotation);
		Skel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		MeshRelativeLocation = S->MeshOffset;
		MeshRelativeRotation = S->MeshRotation;
		bHasSkeletalArt = true;
		PrimitiveBody->SetVisibility(false);
		PrimitiveHead->SetVisibility(false);

		// Optional MetaHuman (or any) visual actor that follows the animated mesh via leader pose.
		UClass* VisualClass = nullptr;
		if (bReferee) VisualClass = S->RefereeVisualClass.LoadSynchronous();
		else if (S->PlayerVisualClasses.Num() > 0)
			VisualClass = S->PlayerVisualClasses[FMath::Abs(VariantSeed) % S->PlayerVisualClasses.Num()].LoadSynchronous();
		if (VisualClass)
		{
			FActorSpawnParameters P;
			P.Owner = this;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			if (AActor* Visual = GetWorld()->SpawnActor<AActor>(VisualClass, GetActorTransform(), P))
			{
				Visual->AttachToComponent(Skel, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
				Visual->SetActorEnableCollision(false);
				VisualActors.Add(Visual);
				SetLeader(Skel);
				Skel->SetVisibility(false, false);
			}
		}
		ApplyKitToMesh(Skel, Kit, Secondary);
		for (AActor* Visual : VisualActors)
		{
			TArray<UMeshComponent*> Meshes;
			Visual->GetComponents<UMeshComponent>(Meshes);
			for (UMeshComponent* M : Meshes) ApplyKitToMesh(M, Kit, Secondary);
		}
	}
	else
	{
		bHasSkeletalArt = false;
		PrimitiveBody->SetVisibility(true);
		PrimitiveHead->SetVisibility(true);
		if (UMaterialInterface* KitMat = S->KitMaterial.LoadSynchronous()) PrimitiveBody->SetMaterial(0, KitMat);
		ApplyKitToMesh(PrimitiveBody, Kit, Secondary);
		if (UMaterialInstanceDynamic* Head = PrimitiveHead->CreateDynamicMaterialInstance(0))
		{
			const float Tone = 0.25f + 0.5f * float(FMath::Abs(VariantSeed * 7919) % 100) / 100.f;
			Head->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.55f, 0.38f, 0.27f) * (0.6f + Tone));
		}
	}
}

void ARCHumanCharacter::ApplyKitToMesh(UMeshComponent* Mesh, const FLinearColor& Kit, const FLinearColor& Secondary)
{
	if (!Mesh) return;
	const URCSettings* S = URCSettings::Get();
	for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i)
	{
		if (UMaterialInstanceDynamic* M = Mesh->CreateDynamicMaterialInstance(i))
		{
			M->SetVectorParameterValue(S->KitColorParameter, Kit);
			M->SetVectorParameterValue(S->KitSecondaryParameter, Secondary);
			M->SetVectorParameterValue(TEXT("Color"), Kit);  // engine BasicShapeMaterial fallback
		}
	}
}

void ARCHumanCharacter::SetLeader(USkinnedMeshComponent* Leader)
{
	for (AActor* Visual : VisualActors)
	{
		TArray<USkinnedMeshComponent*> Parts;
		Visual->GetComponents<USkinnedMeshComponent>(Parts);
		for (USkinnedMeshComponent* Part : Parts)
		{
			if (Part != Leader) Part->SetLeaderPoseComponent(Leader, true);
		}
	}
}

float ARCHumanCharacter::PlayTagged(FName Tag, float Rate)
{
	const TSoftObjectPtr<UAnimMontage>* Entry = URCSettings::Get()->Montages.Find(Tag);
	if (!Entry || !bHasSkeletalArt) return 0.f;
	if (UAnimMontage* Montage = Entry->LoadSynchronous())
	{
		return PlayAnimMontage(Montage, Rate);
	}
	return 0.f;
}

void ARCHumanCharacter::FallDown(float Seconds, const FVector& Push)
{
	if (IsDown()) return;
	DownTime = Seconds;
	StopMoving();
	if (PlayTagged(TEXT("fall")) > 0.f) return;
	USkeletalMeshComponent* Skel = GetMesh();
	if (bHasSkeletalArt && Skel->GetPhysicsAsset())
	{
		GetCharacterMovement()->DisableMovement();
		Skel->SetCollisionProfileName(TEXT("Ragdoll"));
		Skel->SetAllBodiesSimulatePhysics(true);
		Skel->SetSimulatePhysics(true);
		Skel->WakeAllRigidBodies();
		Skel->AddImpulse(Push, NAME_None, true);
		bRagdoll = true;
		return;
	}
	// Primitive body: lie down along the push.
	const FVector Dir = Push.GetSafeNormal2D();
	PrimitiveBody->SetWorldRotation(FRotationMatrix::MakeFromZX(Dir.IsNearlyZero() ? GetActorForwardVector() : Dir, FVector::UpVector).Rotator());
	PrimitiveBody->SetRelativeLocation(FVector(0.f, 0.f, -75.f));
	PrimitiveHead->SetRelativeLocation(GetActorTransform().InverseTransformVectorNoScale(Dir) * 70.f + FVector(0.f, 0.f, -75.f));
}

void ARCHumanCharacter::GetUp()
{
	DownTime = 0.f;
	USkeletalMeshComponent* Skel = GetMesh();
	if (bRagdoll)
	{
		const FVector Pelvis = Skel->GetComponentLocation();
		Skel->SetAllBodiesSimulatePhysics(false);
		Skel->SetSimulatePhysics(false);
		Skel->SetCollisionProfileName(TEXT("CharacterMesh"));
		Skel->AttachToComponent(GetCapsuleComponent(), FAttachmentTransformRules::KeepRelativeTransform);
		Skel->SetRelativeLocationAndRotation(MeshRelativeLocation, MeshRelativeRotation);
		SetActorLocation(FVector(Pelvis.X, Pelvis.Y, GetActorLocation().Z), false, nullptr, ETeleportType::TeleportPhysics);
		GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		bRagdoll = false;
		PlayTagged(TEXT("get_up"));
		return;
	}
	PrimitiveBody->SetRelativeRotation(FRotator::ZeroRotator);
	PrimitiveBody->SetRelativeLocation(FVector(0.f, 0.f, -25.f));
	PrimitiveHead->SetRelativeLocation(FVector(0.f, 0.f, 62.f));
}

void ARCHumanCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (DownTime > 0.f)
	{
		DownTime -= DeltaSeconds;
		if (DownTime <= 0.f) GetUp();
	}
}

float ARCHumanCharacter::MoveTowards(const FVector& Target, float MaxSpeed, float Acceptance)
{
	const FVector Delta = (Target - GetActorLocation()) * FVector(1.f, 1.f, 0.f);
	const float Dist = Delta.Size();
	if (IsDown()) return Dist;
	GetCharacterMovement()->MaxWalkSpeed = MaxSpeed;
	if (Dist > Acceptance)
	{
		AddMovementInput(Delta / Dist, FMath::Clamp(Dist / 250.f, 0.35f, 1.f));
	}
	return Dist;
}

void ARCHumanCharacter::FaceTowards(const FVector& Point, float DeltaSeconds, float Speed)
{
	const FVector D = (Point - GetActorLocation()).GetSafeNormal2D();
	if (D.IsNearlyZero()) return;
	const FRotator Want(0.f, D.Rotation().Yaw, 0.f);
	SetActorRotation(FMath::RInterpTo(GetActorRotation(), Want, DeltaSeconds, Speed));
}

void ARCHumanCharacter::StopMoving()
{
	GetCharacterMovement()->StopMovementImmediately();
	ConsumeMovementInputVector();
}

bool ARCHumanCharacter::CapturePose(TArray<FTransform>& OutPose) const
{
	if (!bHasSkeletalArt) return false;
	OutPose = GetMesh()->GetComponentSpaceTransforms();
	return OutPose.Num() > 0;
}

void ARCHumanCharacter::ShowReplayPose(const TArray<FTransform>* Pose)
{
	USkeletalMeshComponent* Skel = GetMesh();
	if (!Pose || !bHasSkeletalArt)
	{
		if (ReplayGhost)
		{
			ReplayGhost->SetVisibility(false);
			if (VisualActors.Num() > 0) SetLeader(Skel);
			else Skel->SetVisibility(true);
		}
		return;
	}
	if (!ReplayGhost)
	{
		ReplayGhost = NewObject<UPoseableMeshComponent>(this);
		ReplayGhost->SetSkinnedAssetAndUpdate(Skel->GetSkinnedAsset());
		ReplayGhost->SetupAttachment(GetCapsuleComponent());
		ReplayGhost->SetRelativeLocationAndRotation(MeshRelativeLocation, MeshRelativeRotation);
		ReplayGhost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ReplayGhost->RegisterComponent();
		for (int32 i = 0; i < Skel->GetNumMaterials(); ++i) ReplayGhost->SetMaterial(i, Skel->GetMaterial(i));
	}
	// Bones are stored parent-first, so setting them in order in component space is exact.
	const int32 NumBones = FMath::Min(Pose->Num(), ReplayGhost->GetNumBones());
	for (int32 i = 0; i < NumBones; ++i)
	{
		ReplayGhost->SetBoneTransformByName(ReplayGhost->GetBoneName(i), (*Pose)[i], EBoneSpaces::ComponentSpace);
	}
	ReplayGhost->RefreshBoneTransforms();
	ReplayGhost->SetVisibility(VisualActors.Num() == 0);
	if (VisualActors.Num() > 0) SetLeader(ReplayGhost);
	else Skel->SetVisibility(false);
}
