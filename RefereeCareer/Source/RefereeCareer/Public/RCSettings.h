#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "RCSettings.generated.h"

class UAnimInstance;
class UAnimMontage;
class UMaterialInterface;
class UMaterialParameterCollection;
class USkeletalMesh;
class USoundBase;
class UStaticMesh;

/**
 * Project Settings > Game > Referee Career.
 * Every asset is optional: when one is missing the game falls back to engine primitives, so it stays
 * playable while art (Blender venues, MetaHumans, Game Animation Sample motion matching) is added.
 */
UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Referee Career"))
class REFEREECAREER_API URCSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static const URCSettings* Get() { return GetDefault<URCSettings>(); }
	virtual FName GetCategoryName() const override { return TEXT("Game"); }

	// ---- Venues (Tools/Blender) ----
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> StadiumMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> StadiumFloorMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> CommunityMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> CommunityFenceMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> CommunityGroundMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TSoftObjectPtr<UStaticMesh> SeatMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Venues") TArray<TSoftObjectPtr<UStaticMesh>> CrowdMeshes;

	// ---- Props ----
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> GoalMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> GoalNetMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> SmallGoalMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> SmallGoalNetMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> CornerFlagMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> BallMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> ARFlagMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> YellowCardMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> RedCardMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> WhistleMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Props") TSoftObjectPtr<UStaticMesh> VARMonitorMesh;

	// ---- Materials ----
	UPROPERTY(Config, EditAnywhere, Category = "Materials") TSoftObjectPtr<UMaterialInterface> GrassMaterial;
	UPROPERTY(Config, EditAnywhere, Category = "Materials") TSoftObjectPtr<UMaterialInterface> LineMaterial;
	/** Material for kits; needs vector parameters named KitColorParameter / KitSecondaryParameter. */
	UPROPERTY(Config, EditAnywhere, Category = "Materials") TSoftObjectPtr<UMaterialInterface> KitMaterial;
	/** Scalar "Excitement" drives the crowd bounce in the crowd material. */
	UPROPERTY(Config, EditAnywhere, Category = "Materials") TSoftObjectPtr<UMaterialParameterCollection> CrowdParameters;

	// ---- Characters (MetaHuman / Game Animation Sample) ----
	/** Skeletal mesh used for players (e.g. SKM_Manny from the Game Animation Sample). */
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TSoftObjectPtr<USkeletalMesh> PlayerMesh;
	/** Animation blueprint for players. The motion-matching AnimBP from the Game Animation Sample works as is. */
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TSoftClassPtr<UAnimInstance> PlayerAnimClass;
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TSoftObjectPtr<USkeletalMesh> RefereeMesh;
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TSoftClassPtr<UAnimInstance> RefereeAnimClass;
	/**
	 * Optional visual actors (e.g. MetaHuman blueprints). One is spawned per player and its skeletal meshes follow
	 * the animated character mesh through leader-pose, so MetaHumans get motion-matched locomotion.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TArray<TSoftClassPtr<AActor>> PlayerVisualClasses;
	UPROPERTY(Config, EditAnywhere, Category = "Characters") TSoftClassPtr<AActor> RefereeVisualClass;
	UPROPERTY(Config, EditAnywhere, Category = "Characters") FName KitColorParameter = TEXT("KitColor");
	UPROPERTY(Config, EditAnywhere, Category = "Characters") FName KitSecondaryParameter = TEXT("KitSecondary");
	UPROPERTY(Config, EditAnywhere, Category = "Characters") FVector MeshOffset = FVector(0.f, 0.f, -90.f);
	UPROPERTY(Config, EditAnywhere, Category = "Characters") FRotator MeshRotation = FRotator(0.f, -90.f, 0.f);
	/**
	 * Montages by tag. Tags used by the game: tackle_clean, tackle_careless, tackle_reckless, tackle_studs, push,
	 * holding, elbow_aerial, elbow_strike, handball_deliberate, handball_natural, shoulder, shove, dive, fall,
	 * get_up, protest, protest_angry, celebrate, kick, pass, shot, throw_in, gk_dive, whistle, show_card,
	 * point_spot, signal_advantage, flag_raise, var_signal, breathe.
	 * Missing tags fall back to ragdoll falls (characters with a physics asset) or a procedural lean.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Animation") TMap<FName, TSoftObjectPtr<UAnimMontage>> Montages;

	// ---- Audio (Tools/Audio) ----
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> WhistleShort;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> WhistleLong;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> WhistleTriple;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> CrowdLoop;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> CrowdCheer;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> CrowdBoo;
	UPROPERTY(Config, EditAnywhere, Category = "Audio") TSoftObjectPtr<USoundBase> Heartbeat;

	// ---- Gameplay ----
	/** Real minutes per half; the clock is scaled to 45 match minutes. */
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay", meta = (ClampMin = "2", ClampMax = "45")) float HalfLengthMinutes = 6.f;
	/** Time dilation while the referee decides. */
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay", meta = (ClampMin = "0.02", ClampMax = "1")) float IncidentTimeDilation = 0.15f;
	/** Show right/wrong immediately (training mode). Off = like real refereeing, you learn in the assessor report. */
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay") bool bRevealCallsDuringMatch = false;
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay", meta = (ClampMin = "0")) int32 MaxCrowdInstances = 30000;
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay") float RefereeJogSpeed = 420.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay") float RefereeSprintSpeed = 760.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay") float PlayerJogSpeed = 480.f;
	UPROPERTY(Config, EditAnywhere, Category = "Gameplay") float PlayerSprintSpeed = 820.f;
};
