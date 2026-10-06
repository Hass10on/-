#include "RCPitch.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"
#include "RCCareerSubsystem.h"
#include "RCSettings.h"
#include "RefereeCareer.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
UStaticMesh* PitchEngineMesh(const TCHAR* Path)
{
	return LoadObject<UStaticMesh>(nullptr, Path);
}

// Blender (metres, right-handed) -> Unreal (centimetres, left-handed): the Y axis flips.
FVector PitchFromBlender(double X, double Y, double Z)
{
	return FVector(X * RC_M, -Y * RC_M, Z * RC_M);
}

FVector PitchFromBlender(const refcore::Json& Arr)
{
	return PitchFromBlender(Arr.at(0).asNumber(), Arr.at(1).asNumber(), Arr.at(2).asNumber());
}

/** Points every `Spacing` cm along a rounded rectangle (half-extents A, B, corner radius R), with aisle gaps. */
void PitchRoundedRectSeats(float A, float B, float R, float Spacing, int32 AisleEvery, float AisleGap, TArray<FVector2D>& Out)
{
	R = FMath::Clamp(R, 1.f, FMath::Min(A, B));
	TArray<FVector2D> Poly;
	const int32 Seg = 14;
	const FVector2D Centers[4] = {{A - R, B - R}, {-(A - R), B - R}, {-(A - R), -(B - R)}, {A - R, -(B - R)}};
	for (int32 c = 0; c < 4; ++c)
	{
		for (int32 i = 0; i <= Seg; ++i)
		{
			const float Ang = UE_HALF_PI * c + UE_HALF_PI * i / Seg;
			Poly.Add(Centers[c] + FVector2D(FMath::Cos(Ang), FMath::Sin(Ang)) * R);
		}
	}
	float Carry = 0.f;
	int32 InBlock = 0;
	for (int32 i = 0; i < Poly.Num(); ++i)
	{
		const FVector2D P = Poly[i];
		const FVector2D Q = Poly[(i + 1) % Poly.Num()];
		const float Len = FVector2D::Distance(P, Q);
		float T = Carry;
		while (T < Len)
		{
			Out.Add(P + (Q - P) * (T / Len));
			++InBlock;
			if (InBlock >= AisleEvery)
			{
				InBlock = 0;
				T += AisleGap;
			}
			else
			{
				T += Spacing;
			}
		}
		Carry = T - Len;
	}
}

FLinearColor PitchSeatTint(const FLinearColor& Team)
{
	return FLinearColor::LerpUsingHSV(Team, FLinearColor(0.08f, 0.08f, 0.09f), 0.25f);
}
}  // namespace

ARCPitch::ARCPitch()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Grass = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Grass"));
	Grass->SetupAttachment(RootComponent);
	Grass->SetCollisionProfileName(TEXT("BlockAll"));
	Lines = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Lines"));
	Lines->SetupAttachment(RootComponent);
	Lines->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lines->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Plane.Succeeded()) Grass->SetStaticMesh(Plane.Object);
	if (Cube.Succeeded()) Lines->SetStaticMesh(Cube.Object);
}

void ARCPitch::ClearBuilt()
{
	for (UStaticMeshComponent* C : Built)
	{
		if (C) C->DestroyComponent();
	}
	Built.Reset();
	for (UHierarchicalInstancedStaticMeshComponent* C : CrowdComponents)
	{
		if (C) C->DestroyComponent();
	}
	CrowdComponents.Reset();
	for (AActor* A : SpawnedActors)
	{
		if (A) A->Destroy();
	}
	SpawnedActors.Reset();
	Lines->ClearInstances();
	CameraSpots.Reset();
}

UStaticMeshComponent* ARCPitch::AddMesh(UStaticMesh* Mesh, const FTransform& Xf, UMaterialInterface* Material)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetRelativeTransform(Xf);
	C->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	C->SetCollisionResponseToAllChannels(ECR_Ignore);
	C->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	if (Material) C->SetMaterial(0, Material);
	C->RegisterComponent();
	Built.Add(C);
	return C;
}

void ARCPitch::Build(float LengthM, float WidthM, const FString& Venue, float CrowdDensity, const FLinearColor& HomeColor,
	const FLinearColor& AwayColor, bool bNight)
{
	ClearBuilt();
	HalfLen = LengthM * RC_M * 0.5f;
	HalfWid = WidthM * RC_M * 0.5f;
	const URCSettings* S = URCSettings::Get();

	// Grass: the run-off around the lines is part of the playing surface.
	Grass->SetRelativeLocation(FVector::ZeroVector);
	Grass->SetRelativeScale3D(FVector((HalfLen + 700.f) * 2.f / 100.f, (HalfWid + 600.f) * 2.f / 100.f, 1.f));
	if (UMaterialInterface* GrassMat = S->GrassMaterial.LoadSynchronous())
	{
		Grass->SetMaterial(0, GrassMat);
	}
	else if (UMaterialInstanceDynamic* M = Grass->CreateDynamicMaterialInstance(0))
	{
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.07f, 0.26f, 0.05f));
	}
	if (UMaterialInterface* LineMat = S->LineMaterial.LoadSynchronous())
	{
		Lines->SetMaterial(0, LineMat);
	}
	else if (UMaterialInstanceDynamic* M = Lines->CreateDynamicMaterialInstance(0))
	{
		M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.9f, 0.9f, 0.9f));
	}
	BuildMarkings();
	BuildGoals();
	BuildVenue(Venue, CrowdDensity, HomeColor, AwayColor);
	TArray<FVector> Flood;
	refcore::Json VenueJson;
	if (URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this))
	{
		const FString File = Venue == TEXT("community") ? TEXT("venue_community.json") : TEXT("venue_stadium.json");
		if (Sub->LoadJson(File, VenueJson))
		{
			for (const refcore::Json& P : VenueJson["floodlights"].items()) Flood.Add(PitchFromBlender(P));
		}
	}
	BuildLighting(bNight, Flood);
}

void ARCPitch::AddLine(const FVector2D& A, const FVector2D& B)
{
	const FVector2D D = B - A;
	const float Len = D.Size();
	if (Len < 1.f) return;
	const FVector2D Mid = (A + B) * 0.5f;
	const FRotator Rot(0.f, FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X)), 0.f);
	// BasicShapes/Cube is 100 cm; lines are 12 cm wide and 1 cm proud of the grass.
	Lines->AddInstance(FTransform(Rot, FVector(Mid.X, Mid.Y, 0.5f), FVector(Len / 100.f, 0.12f, 0.01f)));
}

void ARCPitch::AddArc(const FVector2D& Center, float Radius, float StartDeg, float EndDeg, int32 Segments)
{
	FVector2D Prev = Center + FVector2D(FMath::Cos(FMath::DegreesToRadians(StartDeg)), FMath::Sin(FMath::DegreesToRadians(StartDeg))) * Radius;
	for (int32 i = 1; i <= Segments; ++i)
	{
		const float Deg = FMath::Lerp(StartDeg, EndDeg, float(i) / Segments);
		const FVector2D P = Center + FVector2D(FMath::Cos(FMath::DegreesToRadians(Deg)), FMath::Sin(FMath::DegreesToRadians(Deg))) * Radius;
		AddLine(Prev, P);
		Prev = P;
	}
}

FVector ARCPitch::PenaltySpot(int32 EndSign) const
{
	const float Dist = IsSmallPitch() ? 700.f : 1100.f;
	return FVector(EndSign * (HalfLen - Dist), 0.f, 0.f);
}

float ARCPitch::PenaltyAreaDepth() const { return IsSmallPitch() ? 900.f : 1650.f; }
float ARCPitch::PenaltyAreaHalfWidth() const { return IsSmallPitch() ? 1200.f : 2016.f; }
float ARCPitch::GoalHalfWidth() const { return IsSmallPitch() ? 250.f : 366.f; }
float ARCPitch::GoalHeight() const { return IsSmallPitch() ? 200.f : 244.f; }

bool ARCPitch::IsInPenaltyArea(const FVector& L, int32 EndSign) const
{
	const float X = L.X * EndSign;
	return X >= HalfLen - PenaltyAreaDepth() && X <= HalfLen + 50.f && FMath::Abs(L.Y) <= PenaltyAreaHalfWidth();
}

ERCBoundary ARCPitch::Classify(const FVector& B) const
{
	if (FMath::Abs(B.Y) > HalfWid + 11.f) return ERCBoundary::Touchline;
	if (B.X > HalfLen + 11.f)
	{
		if (FMath::Abs(B.Y) < GoalHalfWidth() && B.Z < GoalHeight()) return ERCBoundary::Goal1;
		return ERCBoundary::GoalLineRight;
	}
	if (B.X < -HalfLen - 11.f)
	{
		if (FMath::Abs(B.Y) < GoalHalfWidth() && B.Z < GoalHeight()) return ERCBoundary::Goal0;
		return ERCBoundary::GoalLineLeft;
	}
	return ERCBoundary::InPlay;
}

FVector ARCPitch::ClampToField(const FVector& L, float Margin) const
{
	return FVector(FMath::Clamp(L.X, -HalfLen + Margin, HalfLen - Margin), FMath::Clamp(L.Y, -HalfWid + Margin, HalfWid - Margin), L.Z);
}

void ARCPitch::BuildMarkings()
{
	const float L = HalfLen, W = HalfWid;
	const bool bSmall = IsSmallPitch();
	AddLine({-L, -W}, {L, -W});
	AddLine({-L, W}, {L, W});
	AddLine({-L, -W}, {-L, W});
	AddLine({L, -W}, {L, W});
	AddLine({0.f, -W}, {0.f, W});
	const float CircleR = bSmall ? 600.f : 915.f;
	AddArc({0.f, 0.f}, CircleR, 0.f, 360.f, 64);
	AddArc({0.f, 0.f}, 15.f, 0.f, 360.f, 8);
	const float BoxD = PenaltyAreaDepth(), BoxW = PenaltyAreaHalfWidth();
	const float SixD = bSmall ? 300.f : 550.f, SixW = bSmall ? 600.f : 916.f;
	for (const int32 Sign : {-1, 1})
	{
		const float GoalX = Sign * L;
		const float BoxX = Sign * (L - BoxD);
		AddLine({GoalX, -BoxW}, {BoxX, -BoxW});
		AddLine({GoalX, BoxW}, {BoxX, BoxW});
		AddLine({BoxX, -BoxW}, {BoxX, BoxW});
		const float SixX = Sign * (L - SixD);
		AddLine({GoalX, -SixW}, {SixX, -SixW});
		AddLine({GoalX, SixW}, {SixX, SixW});
		AddLine({SixX, -SixW}, {SixX, SixW});
		const FVector Spot = PenaltySpot(Sign);
		AddArc({Spot.X, 0.f}, 12.f, 0.f, 360.f, 8);
		// Penalty arc: the part of the 9.15 m circle outside the area.
		const float Dx = FMath::Abs(BoxX - Spot.X);
		if (CircleR > Dx)
		{
			const float Half = FMath::RadiansToDegrees(FMath::Acos(Dx / CircleR));
			const float Facing = Sign > 0 ? 180.f : 0.f;
			AddArc({Spot.X, 0.f}, CircleR, Facing - Half, Facing + Half, 20);
		}
		for (const int32 SY : {-1, 1})
		{
			const float Start = Sign > 0 ? (SY > 0 ? 180.f : 90.f) : (SY > 0 ? 270.f : 0.f);
			AddArc({GoalX, SY * W}, 100.f, Start, Start + 90.f, 6);
		}
	}
}

void ARCPitch::BuildGoals()
{
	const URCSettings* S = URCSettings::Get();
	const bool bSmall = IsSmallPitch();
	UStaticMesh* Goal = (bSmall ? S->SmallGoalMesh : S->GoalMesh).LoadSynchronous();
	UStaticMesh* Net = (bSmall ? S->SmallGoalNetMesh : S->GoalNetMesh).LoadSynchronous();
	UStaticMesh* Cyl = PitchEngineMesh(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	for (const int32 Sign : {-1, 1})
	{
		const FTransform Xf(FRotator(0.f, Sign > 0 ? 0.f : 180.f, 0.f), FVector(Sign * HalfLen, 0.f, 0.f));
		if (Goal)
		{
			AddMesh(Goal, Xf);
			AddMesh(Net, Xf);
		}
		else if (Cyl)
		{
			const float HW = GoalHalfWidth(), H = GoalHeight();
			for (const float Y : {-HW, HW})
			{
				AddMesh(Cyl, FTransform(FRotator::ZeroRotator, FVector(Sign * HalfLen, Y, H * 0.5f), FVector(0.12f, 0.12f, H / 100.f)));
			}
			AddMesh(Cyl, FTransform(FRotator(0.f, 0.f, 90.f), FVector(Sign * HalfLen, 0.f, H), FVector(0.12f, 0.12f, HW * 2.f / 100.f)));
		}
		if (UStaticMesh* Flag = S->CornerFlagMesh.LoadSynchronous())
		{
			AddMesh(Flag, FTransform(FVector(Sign * HalfLen, -HalfWid, 0.f)));
			AddMesh(Flag, FTransform(FVector(Sign * HalfLen, HalfWid, 0.f)));
		}
	}
}

void ARCPitch::BuildVenue(const FString& Venue, float CrowdDensity, const FLinearColor& HomeColor, const FLinearColor& AwayColor)
{
	const URCSettings* S = URCSettings::Get();
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	const bool bCommunity = Venue == TEXT("community");
	refcore::Json V;
	const bool bHaveJson = Sub && Sub->LoadJson(bCommunity ? TEXT("venue_community.json") : TEXT("venue_stadium.json"), V);
	if (bHaveJson)
	{
		for (const auto& KV : V["cameras"].members())
		{
			CameraSpots.Add(FName(UTF8_TO_TCHAR(KV.first.c_str())), PitchFromBlender(KV.second));
		}
	}
	if (bCommunity)
	{
		AddMesh(S->CommunityMesh.LoadSynchronous(), FTransform::Identity);
		AddMesh(S->CommunityFenceMesh.LoadSynchronous(), FTransform::Identity);
		AddMesh(S->CommunityGroundMesh.LoadSynchronous(), FTransform::Identity);
	}
	else
	{
		AddMesh(S->StadiumMesh.LoadSynchronous(), FTransform::Identity);
		AddMesh(S->StadiumFloorMesh.LoadSynchronous(), FTransform::Identity);
	}
	if (!bHaveJson) return;

	// Seat positions (Blender space, metres), then seats and fans as instances.
	struct FSeat { FVector Location; float Yaw; };
	TArray<FSeat> Seats;
	if (bCommunity)
	{
		const float Spacing = static_cast<float>(V["seatSpacing"].asNumber(0.55));
		for (const refcore::Json& Row : V["rows"].items())
		{
			const double X0 = Row["x0"].asNumber(), X1 = Row["x1"].asNumber(), Y = Row["y"].asNumber(), Z = Row["z"].asNumber();
			for (double X = X0; X <= X1; X += Spacing)
			{
				const FVector L = PitchFromBlender(X, Y, Z);
				Seats.Add({L, L.Y > 0.f ? -90.f : 90.f});
			}
		}
	}
	else
	{
		const float A = static_cast<float>(V["path"]["a"].asNumber(62)), B = static_cast<float>(V["path"]["b"].asNumber(44));
		const float R = static_cast<float>(V["path"]["r"].asNumber(16));
		const float Spacing = static_cast<float>(V["seatSpacing"].asNumber(0.52));
		const int32 AisleEvery = V["aisleEvery"].asInt(16);
		const float AisleGap = static_cast<float>(V["aisleGap"].asNumber(1.2));
		for (const refcore::Json& Row : V["rows"].items())
		{
			const float Off = static_cast<float>(Row["offset"].asNumber());
			TArray<FVector2D> Pts;
			PitchRoundedRectSeats(A + Off, B + Off, R + Off, Spacing, AisleEvery, AisleGap, Pts);
			for (const FVector2D& P : Pts)
			{
				const FVector L = PitchFromBlender(P.X, P.Y, Row["z"].asNumber());
				// Face the pitch: away from the nearest point of the bowl's inner rectangle.
				const FVector Inner(FMath::Clamp(L.X, -(A - R) * RC_M, (A - R) * RC_M), FMath::Clamp(L.Y, -(B - R) * RC_M, (B - R) * RC_M), L.Z);
				const FVector In = (Inner - L).GetSafeNormal2D();
				Seats.Add({L, static_cast<float>(FMath::RadiansToDegrees(FMath::Atan2(In.Y, In.X)))});
			}
		}
	}

	auto MakeHism = [this](UStaticMesh* Mesh, int32 CustomFloats) -> UHierarchicalInstancedStaticMeshComponent*
	{
		UHierarchicalInstancedStaticMeshComponent* H = NewObject<UHierarchicalInstancedStaticMeshComponent>(this);
		H->SetStaticMesh(Mesh);
		H->SetupAttachment(RootComponent);
		H->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		H->SetCastShadow(false);
		H->NumCustomDataFloats = CustomFloats;
		H->RegisterComponent();
		CrowdComponents.Add(H);
		return H;
	};

	FRandomStream Rand(1234);
	if (UStaticMesh* SeatMesh = S->SeatMesh.LoadSynchronous())
	{
		UHierarchicalInstancedStaticMeshComponent* H = MakeHism(SeatMesh, 3);
		TArray<FTransform> Xfs;
		Xfs.Reserve(Seats.Num());
		for (const FSeat& Seat : Seats) Xfs.Add(FTransform(FRotator(0.f, Seat.Yaw, 0.f), Seat.Location));
		H->AddInstances(Xfs, false);
		for (int32 i = 0; i < Seats.Num(); ++i)
		{
			const FLinearColor C = Seats[i].Location.X > 0.f ? PitchSeatTint(HomeColor) : PitchSeatTint(AwayColor);
			H->SetCustomData(i, TArray<float>{C.R, C.G, C.B}, false);
		}
		H->MarkRenderStateDirty();
	}

	TArray<UStaticMesh*> FanMeshes;
	for (const TSoftObjectPtr<UStaticMesh>& M : S->CrowdMeshes)
	{
		if (UStaticMesh* Loaded = M.LoadSynchronous()) FanMeshes.Add(Loaded);
	}
	if (FanMeshes.Num() == 0 || CrowdDensity <= 0.f) return;
	TArray<UHierarchicalInstancedStaticMeshComponent*> Fans;
	for (UStaticMesh* M : FanMeshes) Fans.Add(MakeHism(M, 4));
	TArray<TArray<FTransform>> Xfs;
	TArray<TArray<float>> Data;
	Xfs.SetNum(Fans.Num());
	Data.SetNum(Fans.Num());
	const int32 MaxFans = S->MaxCrowdInstances;
	int32 Count = 0;
	for (const FSeat& Seat : Seats)
	{
		if (Count >= MaxFans || Rand.FRand() > CrowdDensity) continue;
		const float Pick = Rand.FRand();
		const int32 Variant = Fans.Num() == 1 ? 0 : Pick < 0.7f ? 0 : FMath::Min(Fans.Num() - 1, 1 + (Pick > 0.9f ? 1 : 0));
		const bool bHomeEnd = Seat.Location.X > 0.f;
		FLinearColor Shirt = Rand.FRand() < 0.8f ? (bHomeEnd ? HomeColor : AwayColor) : FLinearColor(0.85f, 0.85f, 0.85f);
		Shirt = FLinearColor::LerpUsingHSV(Shirt, FLinearColor::Black, Rand.FRandRange(0.f, 0.25f));
		Xfs[Variant].Add(FTransform(FRotator(0.f, Seat.Yaw + Rand.FRandRange(-8.f, 8.f), 0.f), Seat.Location));
		Data[Variant].Append({Shirt.R, Shirt.G, Shirt.B, Rand.FRand()});
		++Count;
	}
	for (int32 v = 0; v < Fans.Num(); ++v)
	{
		Fans[v]->AddInstances(Xfs[v], false);
		for (int32 i = 0; i < Xfs[v].Num(); ++i)
		{
			Fans[v]->SetCustomData(i, TArray<float>{Data[v][i * 4], Data[v][i * 4 + 1], Data[v][i * 4 + 2], Data[v][i * 4 + 3]}, false);
		}
		Fans[v]->MarkRenderStateDirty();
	}
	UE_LOG(LogReferee, Log, TEXT("Venue %s: %d seats, %d fans"), *Venue, Seats.Num(), Count);
}

void ARCPitch::BuildLighting(bool bNight, const TArray<FVector>& Floodlights)
{
	UWorld* World = GetWorld();
	bool bLevelHasSun = false;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		bLevelHasSun = true;
		break;
	}
	auto Register = [this](USceneComponent* C)
	{
		C->SetMobility(EComponentMobility::Movable);
		C->SetupAttachment(RootComponent);
		C->RegisterComponent();
	};
	if (!bLevelHasSun)
	{
		UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
		Sun->SetIntensity(bNight ? 0.08f : 8.f);
		Sun->SetLightColor(bNight ? FLinearColor(0.55f, 0.65f, 1.f) : FLinearColor(1.f, 0.93f, 0.82f));
		Sun->SetAtmosphereSunLight(true);
		Sun->SetWorldRotation(FRotator(bNight ? -35.f : -24.f, bNight ? 60.f : 215.f, 0.f));
		Register(Sun);
		USkyAtmosphereComponent* Sky = NewObject<USkyAtmosphereComponent>(this);
		Register(Sky);
		USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this);
		SkyLight->bRealTimeCapture = true;
		SkyLight->SetIntensity(bNight ? 0.6f : 1.f);
		Register(SkyLight);
		UExponentialHeightFogComponent* Fog = NewObject<UExponentialHeightFogComponent>(this);
		Fog->SetFogDensity(bNight ? 0.004f : 0.01f);
		Fog->SetFogHeightFalloff(0.05f);
		Register(Fog);
		UPostProcessComponent* Post = NewObject<UPostProcessComponent>(this);
		Post->bUnbound = true;
		Post->Settings.bOverride_BloomIntensity = true;
		Post->Settings.BloomIntensity = bNight ? 0.9f : 0.5f;
		Post->Settings.bOverride_VignetteIntensity = true;
		Post->Settings.VignetteIntensity = 0.35f;
		Register(Post);
	}
	if (!bNight) return;
	// Floodlights from the venue data, aimed at the pitch. Only every fourth casts shadows.
	for (int32 i = 0; i < Floodlights.Num(); ++i)
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
		const FVector Pos = Floodlights[i];
		const FVector Target(Pos.X * 0.25f, Pos.Y * 0.25f, 0.f);
		Spot->SetWorldLocationAndRotation(Pos, (Target - Pos).Rotation());
		Spot->SetIntensityUnits(ELightUnits::Candelas);
		Spot->SetIntensity(Floodlights.Num() > 8 ? 14000.f : 9000.f);
		Spot->SetAttenuationRadius(16000.f);
		Spot->SetOuterConeAngle(55.f);
		Spot->SetInnerConeAngle(25.f);
		Spot->SetLightColor(FLinearColor(1.f, 0.97f, 0.92f));
		Spot->SetCastShadows(i % 4 == 0);
		Register(Spot);
	}
}

void ARCPitch::SetCrowdExcitement(float Excitement01)
{
	if (UMaterialParameterCollection* MPC = URCSettings::Get()->CrowdParameters.LoadSynchronous())
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, MPC, TEXT("Excitement"), FMath::Clamp(Excitement01, 0.f, 1.f));
	}
}
