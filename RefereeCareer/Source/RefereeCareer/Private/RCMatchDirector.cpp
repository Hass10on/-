#include "RCMatchDirector.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RCBall.h"
#include "RCCareerSubsystem.h"
#include "RCFootballer.h"
#include "RCPitch.h"
#include "RCReferee.h"
#include "RCReplayRecorder.h"
#include "RCSettings.h"
#include "RefereeCareer.h"
#include "Sound/SoundBase.h"

namespace
{
FLinearColor MdSetupHex(const std::string& Hex)
{
	return FLinearColor(FColor::FromHex(URCCareerSubsystem::ToF(Hex)));
}

// Formation slots in attacking space (X: -1 own goal .. +1 opponent goal).
struct FMdSlot { float X, Y; ERCPosition Pos; };
const FMdSlot MdSetupEleven[] = {
	{-0.95f, 0.f, ERCPosition::Goalkeeper},
	{-0.62f, -0.72f, ERCPosition::Defender}, {-0.68f, -0.25f, ERCPosition::Defender}, {-0.68f, 0.25f, ERCPosition::Defender}, {-0.62f, 0.72f, ERCPosition::Defender},
	{-0.22f, -0.66f, ERCPosition::Midfielder}, {-0.3f, -0.2f, ERCPosition::Midfielder}, {-0.3f, 0.2f, ERCPosition::Midfielder}, {-0.22f, 0.66f, ERCPosition::Midfielder},
	{0.18f, -0.24f, ERCPosition::Forward}, {0.24f, 0.22f, ERCPosition::Forward}};
const FMdSlot MdSetupSeven[] = {
	{-0.95f, 0.f, ERCPosition::Goalkeeper},
	{-0.6f, -0.45f, ERCPosition::Defender}, {-0.6f, 0.45f, ERCPosition::Defender},
	{-0.2f, -0.62f, ERCPosition::Midfielder}, {-0.26f, 0.f, ERCPosition::Midfielder}, {-0.2f, 0.62f, ERCPosition::Midfielder},
	{0.24f, 0.f, ERCPosition::Forward}};
const int32 MdSetupNumbers[] = {1, 2, 4, 5, 3, 7, 8, 6, 11, 9, 10};
}  // namespace

ARCMatchDirector::ARCMatchDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Recorder = CreateDefaultSubobject<URCReplayRecorder>(TEXT("Replay"));
	LineAttacker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OffsideLineAttacker"));
	LineDefender = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("OffsideLineDefender"));
	for (UStaticMeshComponent* L : {LineAttacker.Get(), LineDefender.Get()})
	{
		L->SetupAttachment(RootComponent);
		L->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		L->SetCastShadow(false);
		L->SetVisibility(false);
	}
}

bool ARCMatchDirector::IsPlayerVar() const
{
	return Session && Session->setup().role == refcore::Role::Var;
}

bool ARCMatchDirector::StartMatch(APlayerController* PC)
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	Session = Sub ? Sub->GetMatch() : nullptr;
	if (!Session || !Sub->GetCareer()) return false;
	PlayerPC = PC;
	const refcore::MatchSetup& Setup = Session->setup();
	const refcore::TierDef& Tier = Session->tier();
	const refcore::TeamDef& Home = Tier.teams[static_cast<size_t>(Setup.home)];
	const refcore::TeamDef& Away = Tier.teams[static_cast<size_t>(Setup.away)];
	Hud.HomeName = URCCareerSubsystem::ToF(Home.shortName);
	Hud.AwayName = URCCareerSubsystem::ToF(Away.shortName);
	Hud.HomeColor = MdSetupHex(Home.kitPrimary);
	Hud.AwayColor = MdSetupHex(Away.kitPrimary);
	Hud.Competition = URCCareerSubsystem::ToF(Tier.competition);
	Hud.RoleLabel = Sub->Str(std::string("role.") + refcore::roleId(Setup.role));
	HalfSeconds = URCSettings::Get()->HalfLengthMinutes * 60.f;

	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Pitch = GetWorld()->SpawnActor<ARCPitch>(ARCPitch::StaticClass(), FTransform::Identity, P);
	const bool bNight = Tier.venue != "community";
	const float Crowd = static_cast<float>(FMath::Clamp(Tier.crowd + (Setup.importance >= 4 ? 0.2 : 0.0), 0.0, 1.0));
	Pitch->Build(static_cast<float>(Tier.pitchLength), static_cast<float>(Tier.pitchWidth), URCCareerSubsystem::ToF(Tier.venue), Crowd,
		Hud.HomeColor, Hud.AwayColor, bNight);
	Ball = GetWorld()->SpawnActor<ARCBall>(ARCBall::StaticClass(), FTransform(FVector(0.f, 0.f, ARCBall::Radius)), P);

	SpawnTeams();
	SpawnOfficials(PC);
	SpawnCameras();

	TArray<ARCHumanCharacter*> Everyone;
	for (ARCFootballer* F : Players) Everyone.Add(F);
	for (ARCReferee* R : {Centre.Get(), AssistantNear.Get(), AssistantFar.Get()}) Everyone.Add(R);
	Recorder->SetSubjects(Ball, Everyone);

	if (UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")))
	{
		LineAttacker->SetStaticMesh(Cube);
		LineDefender->SetStaticMesh(Cube);
		if (UMaterialInstanceDynamic* M = LineAttacker->CreateDynamicMaterialInstance(0)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(1.f, 0.1f, 0.1f));
		if (UMaterialInstanceDynamic* M = LineDefender->CreateDynamicMaterialInstance(0)) M->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.1f, 0.4f, 1.f));
	}

	const URCSettings* S = URCSettings::Get();
	if (USoundBase* Loop = S->CrowdLoop.LoadSynchronous())
	{
		CrowdAudio = UGameplayStatics::SpawnSound2D(this, Loop, 0.4f + 0.6f * Crowd, 1.f, 0.f, nullptr, true, false);
	}
	if (USoundBase* Heart = S->Heartbeat.LoadSynchronous())
	{
		HeartAudio = UGameplayStatics::SpawnSound2D(this, Heart, 0.f, 1.f, 0.f, nullptr, true, false);
	}

	LastRealTime = static_cast<float>(GetWorld()->GetRealTimeSeconds());
	KickOffTeam = 0;
	BeginRestart(ERCRestart::KickOff, KickOffTeam, FVector::ZeroVector);
	BigText(Hud.HomeName + TEXT(" \u00D7 ") + Hud.AwayName, 3.f);
	UE_LOG(LogReferee, Log, TEXT("Match started: %s, %d incidents planned"), *URCCareerSubsystem::ToF(Setup.fixtureName), (int32)Session->plan().size());
	return true;
}

void ARCMatchDirector::SpawnTeams()
{
	const refcore::MatchSetup& Setup = Session->setup();
	const refcore::TierDef& Tier = Session->tier();
	const int32 PerSide = FMath::Clamp(Tier.playersPerSide, 5, 11);
	const bool bSeven = PerSide <= 7;
	const FLinearColor Shorts[2] = {MdSetupHex(Tier.teams[static_cast<size_t>(Setup.home)].kitSecondary),
		MdSetupHex(Tier.teams[static_cast<size_t>(Setup.away)].kitSecondary)};
	const FLinearColor Kits[2] = {Hud.HomeColor, Hud.AwayColor};
	const FLinearColor Keepers[2] = {FLinearColor(0.9f, 0.85f, 0.05f), FLinearColor(0.1f, 0.75f, 0.3f)};
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	FRandomStream Rand(static_cast<int32>(Setup.seed & 0x7fffffff));
	for (int32 Team = 0; Team < 2; ++Team)
	{
		const int32 Strength = Tier.teams[static_cast<size_t>(Team == 0 ? Setup.home : Setup.away)].strength;
		for (int32 i = 0; i < PerSide; ++i)
		{
			const FMdSlot& Slot = bSeven ? MdSetupSeven[i] : MdSetupEleven[i];
			const FVector2D Anchor(Slot.X, Slot.Y);
			ARCFootballer* F = GetWorld()->SpawnActor<ARCFootballer>(ARCFootballer::StaticClass(),
				FTransform(FVector(0.f, 0.f, 95.f)), P);
			if (!F) continue;
			F->Team = Team;
			F->Number = bSeven ? (i == 0 ? 1 : i + 1) : MdSetupNumbers[i];
			F->Position = Slot.Pos;
			F->Anchor = Anchor;
			F->Skill = FMath::Clamp(Strength / 100.f + Rand.FRandRange(-0.15f, 0.15f), 0.2f, 0.95f);
			const bool bKeeper = Slot.Pos == ERCPosition::Goalkeeper;
			F->SetupAppearance(false, bKeeper ? Keepers[Team] : Kits[Team], Shorts[Team], Team * 31 + i);
			Players.Add(F);
		}
	}
	PlaceTeamsForKickOff();
}

void ARCMatchDirector::SpawnOfficials(APlayerController* PC)
{
	refcore::Career* Career = URCCareerSubsystem::Get(this)->GetCareer();
	const float Fitness = static_cast<float>(Career->stats().get(refcore::Stat::Fitness));
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto Make = [&](ERCOfficial Role, const FVector& At) -> ARCReferee*
	{
		ARCReferee* R = GetWorld()->SpawnActor<ARCReferee>(ARCReferee::StaticClass(), FTransform(At), P);
		if (!R) return nullptr;
		// Referee kit avoids both team colours: black, or yellow if a team plays in dark colours.
		const bool bDarkTeam = Hud.HomeColor.GetLuminance() < 0.05f || Hud.AwayColor.GetLuminance() < 0.05f;
		const FLinearColor Kit = bDarkTeam ? FLinearColor(0.95f, 0.8f, 0.05f) : FLinearColor(0.02f, 0.02f, 0.02f);
		R->SetupAppearance(true, Kit, FLinearColor(0.02f, 0.02f, 0.02f), static_cast<int32>(Role));
		R->ConfigureOfficial(Role, Role == ERCOfficial::Centre ? Fitness : 75.f, Pitch->HalfLength(), Pitch->HalfWidth());
		return R;
	};
	Centre = Make(ERCOfficial::Centre, FVector(-600.f, 900.f, 95.f));
	AssistantNear = Make(ERCOfficial::AssistantNear, FVector(-Pitch->HalfLength() * 0.4f, -Pitch->HalfWidth() - 150.f, 95.f));
	AssistantFar = Make(ERCOfficial::AssistantFar, FVector(Pitch->HalfLength() * 0.4f, Pitch->HalfWidth() + 150.f, 95.f));

	const refcore::Role Role = Session->setup().role;
	PlayerReferee = Role == refcore::Role::Center ? Centre.Get() : Role == refcore::Role::Assistant ? AssistantNear.Get() : nullptr;
	if (PlayerReferee && PlayerReferee->GetOfficial() != ERCOfficial::Centre)
	{
		// Assistant fitness comes from the career as well.
		PlayerReferee->ConfigureOfficial(PlayerReferee->GetOfficial(), Fitness, Pitch->HalfLength(), Pitch->HalfWidth());
	}
	if (PC && PlayerReferee)
	{
		if (AController* Old = PlayerReferee->GetController())
		{
			Old->UnPossess();
			Old->Destroy();
		}
		PC->Possess(PlayerReferee);
		PC->SetControlRotation(FRotator(-12.f, PlayerReferee->GetOfficial() == ERCOfficial::Centre ? 0.f : 90.f, 0.f));
	}
}

void ARCMatchDirector::SpawnCameras()
{
	FActorSpawnParameters P;
	const TMap<FName, FVector>& Spots = Pitch->GetCameraSpots();
	TArray<FVector> Positions;
	if (const FVector* Main = Spots.Find(TEXT("main"))) Positions.Add(*Main);
	for (const TPair<FName, FVector>& KV : Spots)
	{
		if (KV.Key != TEXT("main")) Positions.Add(KV.Value);
	}
	if (Positions.Num() == 0)
	{
		Positions.Add(FVector(0.f, -Pitch->HalfWidth() - 2500.f, 2200.f));
		Positions.Add(FVector(Pitch->HalfLength() + 1800.f, 0.f, 1200.f));
	}
	for (const FVector& Pos : Positions)
	{
		ACameraActor* Cam = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform((FVector::ZeroVector - Pos).Rotation(), Pos), P);
		if (Cam)
		{
			Cam->GetCameraComponent()->SetFieldOfView(Cameras.Num() == 0 ? 38.f : 30.f);
			Cam->GetCameraComponent()->SetConstraintAspectRatio(false);
			Cameras.Add(Cam);
		}
	}
	// Review angles around the incident: side-on low, side-on high, behind, reverse.
	ReviewCamOffsets = {FVector(0.f, -1400.f, 350.f), FVector(0.f, -2600.f, 1800.f), FVector(-1800.f, 0.f, 600.f), FVector(0.f, 1500.f, 400.f)};
	if (IsPlayerVar() && PlayerPC && Cameras.Num() > 0)
	{
		PlayerPC->SetViewTarget(Cameras[0]);
	}
}

int32 ARCMatchDirector::AttackDir(int32 Team) const
{
	return (Team == 0 ? 1 : -1) * (Half == 1 ? 1 : -1);
}

FVector ARCMatchDirector::ToWorld(int32 Team, const FVector2D& Attack) const
{
	return FVector(Attack.X * AttackDir(Team) * Pitch->HalfLength(), Attack.Y * Pitch->HalfWidth(), 95.f);
}

void ARCMatchDirector::PlaceTeamsForKickOff()
{
	for (ARCFootballer* F : Players)
	{
		if (!F || F->bSentOff) continue;
		FVector2D A = F->Anchor;
		A.X = FMath::Min(A.X * 0.9f, -0.04f);  // everyone in their own half
		F->SetActorLocation(ToWorld(F->Team, A), false, nullptr, ETeleportType::TeleportPhysics);
		F->SetActorRotation(FRotator(0.f, AttackDir(F->Team) > 0 ? 0.f : 180.f, 0.f));
		F->Mode = ERCPlayerMode::Play;
	}
}

void ARCMatchDirector::SetPhase(ERCMatchPhase NewPhase)
{
	Phase = NewPhase;
	PhaseTime = 0.f;
}

float ARCMatchDirector::MatchMinute() const
{
	return (Half - 1) * 45.f + 45.f * FMath::Clamp(HalfElapsed / HalfSeconds, 0.f, 1.f);
}

void ARCMatchDirector::UpdateClock(float RealDelta)
{
	const bool bRunning = Phase == ERCMatchPhase::Play || Phase == ERCMatchPhase::Restart || Phase == ERCMatchPhase::Staging;
	if (!bRunning) return;
	HalfElapsed += RealDelta;
	PlayedSeconds += RealDelta;
	Session->advance(MatchMinute());
	if (HalfElapsed >= HalfSeconds && Phase == ERCMatchPhase::Play)
	{
		if (Half == 1)
		{
			Whistle(1);
			URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
			BigText(Sub->Str("ui.halftime"), 3.f);
			SetPhase(ERCMatchPhase::HalfTime);
		}
		else
		{
			FinishMatch();
		}
	}
}

void ARCMatchDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Session || !Pitch) return;
	const float Now = static_cast<float>(GetWorld()->GetRealTimeSeconds());
	const float RealDelta = FMath::Clamp(Now - LastRealTime, 0.f, 0.1f);
	LastRealTime = Now;
	PhaseTime += RealDelta;
	if (Ball && Centre) Centre->SetBallLocation(Ball->GetActorLocation());
	if (PlayerReferee && Ball) PlayerReferee->SetBallLocation(Ball->GetActorLocation());

	switch (Phase)
	{
	case ERCMatchPhase::Play:
		TickTeams(DeltaSeconds);
		CheckBoundaries();
		TryBeginIncident();
		break;
	case ERCMatchPhase::Staging:
		TickTeams(DeltaSeconds);
		TickStaging(DeltaSeconds);
		break;
	case ERCMatchPhase::Restart:
		TickRestart(DeltaSeconds);
		break;
	case ERCMatchPhase::Decision:
		TickDecision(RealDelta);
		break;
	case ERCMatchPhase::VarCheck:
	case ERCMatchPhase::Review:
		TickReview(RealDelta);
		break;
	case ERCMatchPhase::Dissent:
		for (ARCFootballer* F : Protesters)
		{
			if (F && PlayerReferee) F->MoveTowards(PlayerReferee->GetActorLocation(), 520.f, 170.f);
			else if (F && Centre) F->MoveTowards(Centre->GetActorLocation(), 520.f, 170.f);
		}
		break;
	case ERCMatchPhase::Goal:
		for (ARCFootballer* F : Players)
		{
			if (F && F->Mode == ERCPlayerMode::Celebrate) F->MoveTowards(F->ScriptTarget, 650.f, 120.f);
		}
		if (PhaseTime > 4.5f) BeginRestart(ERCRestart::KickOff, KickOffTeam, FVector::ZeroVector);
		break;
	case ERCMatchPhase::HalfTime:
		if (PhaseTime > 3.f)
		{
			Half = 2;
			HalfElapsed = 0.f;
			KickOffTeam = 1;
			PlaceTeamsForKickOff();
			BeginRestart(ERCRestart::KickOff, KickOffTeam, FVector::ZeroVector);
		}
		break;
	default:
		break;
	}

	if (Phase != ERCMatchPhase::VarCheck && Phase != ERCMatchPhase::Review && Phase != ERCMatchPhase::FullTime)
	{
		TickOfficialsAI(DeltaSeconds);
		RecordClock += DeltaSeconds;
		Recorder->Record(RecordClock);
	}
	UpdateClock(RealDelta);
	SampleOfficiating(RealDelta);
	UpdateBroadcastCamera(DeltaSeconds);
	UpdateAudio();
	UpdateHud(RealDelta);
}

void ARCMatchDirector::CheckBoundaries()
{
	if (!Ball || Ball->GetCarrier()) return;
	const ERCBoundary B = Pitch->Classify(Ball->GetActorLocation());
	if (B == ERCBoundary::InPlay) return;
	const ARCFootballer* Last = Ball->GetLastTouch();
	const int32 LastTeam = Last ? Last->Team : 0;
	const FVector L = Ball->GetActorLocation();
	switch (B)
	{
	case ERCBoundary::Goal0:
	case ERCBoundary::Goal1:
	{
		const int32 EndSign = B == ERCBoundary::Goal1 ? 1 : -1;
		OnGoal(AttackDir(0) == EndSign ? 0 : 1);
		break;
	}
	case ERCBoundary::Touchline:
		Whistle(0);
		BeginRestart(ERCRestart::ThrowIn, 1 - LastTeam, FVector(FMath::Clamp(L.X, -Pitch->HalfLength(), Pitch->HalfLength()), FMath::Sign(L.Y) * Pitch->HalfWidth(), 0.f));
		break;
	default:
	{
		const int32 EndSign = B == ERCBoundary::GoalLineRight ? 1 : -1;
		const int32 Defending = AttackDir(0) == EndSign ? 1 : 0;
		if (LastTeam == Defending)
		{
			BeginRestart(ERCRestart::Corner, 1 - Defending, FVector(EndSign * Pitch->HalfLength(), FMath::Sign(L.Y) * Pitch->HalfWidth(), 0.f));
		}
		else
		{
			BeginRestart(ERCRestart::GoalKick, Defending, FVector(EndSign * (Pitch->HalfLength() - 550.f), FMath::Sign(L.Y) * 500.f, 0.f));
		}
		break;
	}
	}
}

void ARCMatchDirector::OnGoal(int32 ScoringTeam)
{
	Score[ScoringTeam] += 1;
	Session->goalScored(ScoringTeam);
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	BigText(Sub->Str("ui.goal"), 2.5f);
	if (USoundBase* Cheer = URCSettings::Get()->CrowdCheer.LoadSynchronous()) UGameplayStatics::PlaySound2D(this, Cheer, 1.f);
	Pitch->SetCrowdExcitement(1.f);
	for (ARCFootballer* F : Players)
	{
		if (!F || !F->IsAvailable()) continue;
		if (F->Team == ScoringTeam && F->Position != ERCPosition::Goalkeeper)
		{
			F->Mode = ERCPlayerMode::Celebrate;
			F->ScriptTarget = FVector(AttackDir(ScoringTeam) * Pitch->HalfLength() * 0.8f, -Pitch->HalfWidth() * 0.9f, 0.f);
			F->PlayTagged(TEXT("celebrate"));
		}
	}
	Ball->PlaceAt(FVector::ZeroVector);
	KickOffTeam = 1 - ScoringTeam;
	if (bPendingGoalCheck)
	{
		PendingGoalTeam = ScoringTeam;
	}
	SetPhase(ERCMatchPhase::Goal);
}

void ARCMatchDirector::BeginRestart(ERCRestart Type, int32 Team, const FVector& Spot)
{
	RestartType = Type;
	RestartTeam = Team;
	FVector S = Spot;
	if (Type == ERCRestart::KickOff)
	{
		S = FVector::ZeroVector;
		PlaceTeamsForKickOff();
	}
	else if (Type == ERCRestart::Penalty)
	{
		S = Pitch->PenaltySpot(AttackDir(Team));
	}
	else if (Type == ERCRestart::FreeKick || Type == ERCRestart::IndirectFreeKick || Type == ERCRestart::DropBall)
	{
		S = Pitch->ClampToField(Spot, 50.f);
	}
	RestartSpot = FVector(S.X, S.Y, 0.f);
	Ball->PlaceAt(RestartSpot);
	if (Type == ERCRestart::GoalKick) RestartTaker = GoalkeeperOf(Team);
	else if (Type == ERCRestart::Penalty)
	{
		RestartTaker = nullptr;
		for (ARCFootballer* F : Players)
		{
			if (F && F->Team == Team && F->IsAvailable() && F->Position == ERCPosition::Forward) RestartTaker = F;
		}
	}
	else RestartTaker = nullptr;
	if (!RestartTaker) RestartTaker = Nearest(Team, RestartSpot, nullptr, Type == ERCRestart::GoalKick);
	for (ARCFootballer* F : Players)
	{
		if (F && F->Mode != ERCPlayerMode::LeavingPitch && !F->bSentOff) F->Mode = ERCPlayerMode::Play;
	}
	bRestartWhistled = false;
	SetPhase(ERCMatchPhase::Restart);
}

void ARCMatchDirector::TickRestart(float DeltaSeconds)
{
	const URCSettings* S = URCSettings::Get();
	if (!RestartTaker || !RestartTaker->IsAvailable())
	{
		RestartTaker = Nearest(RestartTeam, RestartSpot);
	}
	if (!RestartTaker)
	{
		SetPhase(ERCMatchPhase::Play);
		return;
	}
	const int32 Dir = AttackDir(RestartTeam);
	// Taker walks behind the ball, everyone else drifts to shape (outside the area for penalties, 9.15 m away).
	const FVector Behind = RestartSpot - FVector(Dir * 120.f, 0.f, 0.f);
	const float TakerDist = RestartTaker->MoveTowards(Behind, S->PlayerJogSpeed, 40.f);
	RestartTaker->FaceTowards(RestartSpot + FVector(Dir * 1000.f, 0.f, 0.f), DeltaSeconds);
	for (ARCFootballer* F : Players)
	{
		if (!F || F == RestartTaker || !F->IsAvailable()) continue;
		FVector Target = ToWorld(F->Team, F->Anchor);
		const float BallAtt = RestartSpot.X * AttackDir(F->Team) / Pitch->HalfLength();
		Target.X = AttackDir(F->Team) * Pitch->HalfLength() * FMath::Clamp(F->Anchor.X * 0.55f + BallAtt * 0.45f, -0.92f, 0.85f);
		if (RestartType == ERCRestart::Penalty && F->Position != ERCPosition::Goalkeeper)
		{
			Target.X = FMath::Clamp(Target.X, -Pitch->HalfLength() + 2200.f, Pitch->HalfLength() - 2200.f);
		}
		if (RestartType == ERCRestart::Penalty && F->Position == ERCPosition::Goalkeeper && F->Team != RestartTeam)
		{
			Target = FVector(Dir * Pitch->HalfLength(), 0.f, 95.f);
		}
		if (FVector::Dist2D(Target, RestartSpot) < 915.f && F->Team != RestartTeam)
		{
			Target = RestartSpot + (Target - RestartSpot).GetSafeNormal2D() * 950.f;
		}
		F->MoveTowards(Target, S->PlayerJogSpeed, 80.f);
	}
	const float Wait = RestartType == ERCRestart::Penalty ? 3.5f : RestartType == ERCRestart::KickOff ? 2.5f : 1.6f;
	if ((TakerDist < 80.f && PhaseTime > Wait) || PhaseTime > 9.f)
	{
		if (!bRestartWhistled && (RestartType == ERCRestart::Penalty || RestartType == ERCRestart::KickOff || RestartType == ERCRestart::FreeKick))
		{
			Whistle(0);
		}
		bRestartWhistled = true;
		ExecuteRestart();
	}
}

void ARCMatchDirector::ExecuteRestart()
{
	ARCFootballer* Taker = RestartTaker;
	SetPhase(ERCMatchPhase::Play);
	if (!Taker) return;
	Ball->PlaceAt(RestartSpot);
	switch (RestartType)
	{
	case ERCRestart::Penalty:
		Taker->PlayTagged(TEXT("shot"));
		Shoot(Taker, FMath::FRand() < 0.76f);
		break;
	case ERCRestart::Corner:
	{
		// Cross towards the penalty spot area.
		const int32 Dir = AttackDir(RestartTeam);
		const FVector Target = Pitch->PenaltySpot(Dir) + FVector(-Dir * FMath::FRandRange(-300.f, 300.f), FMath::FRandRange(-500.f, 500.f), 0.f);
		const FVector D = Target - RestartSpot;
		const float T = 1.2f;
		Ball->Kick(FVector(D.X / T, D.Y / T, 0.5f * 980.f * T), Taker);
		Taker->PlayTagged(TEXT("pass"));
		break;
	}
	case ERCRestart::GoalKick:
	{
		ARCFootballer* To = nullptr;
		float Best = -1e9f;
		for (ARCFootballer* F : Players)
		{
			if (!F || F->Team != RestartTeam || F == Taker || !F->IsAvailable()) continue;
			const float Fwd = F->GetActorLocation().X * AttackDir(RestartTeam);
			if (Fwd > Best) { Best = Fwd; To = F; }
		}
		if (To) Pass(Taker, To, 0.6f);
		break;
	}
	default:
	{
		ARCFootballer* To = Nearest(RestartTeam, Taker->GetActorLocation() + FVector(AttackDir(RestartTeam) * 600.f, 0.f, 0.f), Taker);
		if (RestartType == ERCRestart::FreeKick && Pitch && FMath::Abs(RestartSpot.X - AttackDir(RestartTeam) * Pitch->HalfLength()) < 2600.f && FMath::FRand() < 0.5f)
		{
			Shoot(Taker, false);
		}
		else if (To)
		{
			Pass(Taker, To);
		}
		break;
	}
	}
}

void ARCMatchDirector::SampleOfficiating(float RealDelta)
{
	if (Phase != ERCMatchPhase::Play || !PlayerReferee || !Ball) return;
	SampleTimer += RealDelta;
	if (SampleTimer < 1.f) return;
	SampleTimer = 0.f;
	float Q = 0.7f;
	const FVector R = PlayerReferee->GetActorLocation();
	if (PlayerReferee->GetOfficial() == ERCOfficial::Centre)
	{
		const float D = FVector::Dist2D(R, Ball->GetActorLocation()) / RC_M;
		Q = D < 4.f ? 0.4f : D < 8.f ? FMath::Lerp(0.4f, 1.f, (D - 4.f) / 4.f) : D < 24.f ? 1.f : D < 45.f ? FMath::Lerp(1.f, 0.25f, (D - 24.f) / 21.f) : 0.25f;
	}
	else
	{
		// The assistant should stay level with the second-last defender of the team defending his half.
		const bool bNearHalf = PlayerReferee->GetOfficial() == ERCOfficial::AssistantNear;
		const int32 HalfSign = bNearHalf ? -1 : 1;
		const int32 Defending = AttackDir(0) == HalfSign ? 1 : 0;
		const float Line = OffsideLineX(Defending);
		const float Target = HalfSign < 0 ? FMath::Min(Line, FMath::Min(Ball->GetActorLocation().X, 0.f)) : FMath::Max(Line, FMath::Max(Ball->GetActorLocation().X, 0.f));
		Q = FMath::Clamp(1.f - FMath::Abs(R.X - Target) / 900.f, 0.f, 1.f);
	}
	Session->addPositionSample(Q);
}

void ARCMatchDirector::FinishMatch()
{
	Whistle(2);
	if (PlayerReferee)
	{
		const float Expected = (PlayerReferee->GetOfficial() == ERCOfficial::Centre ? 2.4f : 1.6f) * FMath::Max(1.f, PlayedSeconds);
		Session->setFitnessData(PlayerReferee->GetDistanceMetres() / Expected, PlayerReferee->GetExhaustedSeconds() / FMath::Max(1.f, PlayedSeconds));
	}
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	BigText(Sub->Str("ui.fulltime"), 4.f);
	Hud.bFinished = true;
	SetPhase(ERCMatchPhase::FullTime);
}

void ARCMatchDirector::Whistle(int32 Kind)
{
	const URCSettings* S = URCSettings::Get();
	USoundBase* Sound = Kind == 0 ? S->WhistleShort.LoadSynchronous() : Kind == 1 ? S->WhistleLong.LoadSynchronous() : S->WhistleTriple.LoadSynchronous();
	if (Sound) UGameplayStatics::PlaySound2D(this, Sound, 0.9f);
	ARCReferee* Ref = PlayerReferee ? PlayerReferee.Get() : Centre.Get();
	if (Ref && Ref->GetOfficial() == ERCOfficial::Centre) Ref->PlayTagged(TEXT("whistle"));
}

void ARCMatchDirector::Toast(const FString& Text, float Seconds)
{
	Hud.Toasts.Add(TPair<FString, float>(Text, Seconds));
	if (Hud.Toasts.Num() > 4) Hud.Toasts.RemoveAt(0);
}

void ARCMatchDirector::BigText(const FString& Text, float Seconds)
{
	Hud.BigText = Text;
	Hud.BigTextTime = Seconds;
}

void ARCMatchDirector::UpdateAudio()
{
	const float Heat = static_cast<float>(Session->heat()) / 100.f;
	if (CrowdAudio) CrowdAudio->SetVolumeMultiplier(0.35f + 0.65f * Heat);
	if (HeartAudio) HeartAudio->SetVolumeMultiplier(Session->calm() < 35.0 ? static_cast<float>(1.0 - Session->calm() / 35.0) : 0.f);
	if (Pitch && Phase == ERCMatchPhase::Play) Pitch->SetCrowdExcitement(0.25f + 0.6f * Heat);
}

void ARCMatchDirector::UpdateBroadcastCamera(float DeltaSeconds)
{
	if (!Ball || Cameras.Num() == 0 || Phase == ERCMatchPhase::VarCheck || Phase == ERCMatchPhase::Review) return;
	ACameraActor* Cam = Cameras[FMath::Clamp(BroadcastCam, 0, Cameras.Num() - 1)];
	const FVector Look = Ball->GetActorLocation() * FVector(0.85f, 0.6f, 0.f);
	const FRotator Want = (Look - Cam->GetActorLocation()).Rotation();
	Cam->SetActorRotation(FMath::RInterpTo(Cam->GetActorRotation(), Want, DeltaSeconds, 2.5f));
}

void ARCMatchDirector::UpdateHud(float RealDelta)
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	Hud.HomeScore = Score[0];
	Hud.AwayScore = Score[1];
	const int32 Minute = FMath::Min(FMath::FloorToInt(MatchMinute()) + 1, Half * 45);
	Hud.Clock = FString::Printf(TEXT("%d'"), Minute);
	Hud.Heat = static_cast<float>(Session->heat()) / 100.f;
	Hud.Calm = static_cast<float>(Session->calm()) / 100.f;
	Hud.Breaths = Session->breathsLeft();
	Hud.bShowStamina = PlayerReferee != nullptr;
	Hud.Stamina = PlayerReferee ? PlayerReferee->GetStamina01() : 1.f;
	Hud.PhaseLabel = Sub ? Sub->Str(std::string("ui.phase.") + (Phase == ERCMatchPhase::VarCheck ? "var" : Phase == ERCMatchPhase::Review ? "review" : "play")) : FString();
	Hud.HomeDots.Reset();
	Hud.AwayDots.Reset();
	const float HL = Pitch->HalfLength(), HW = Pitch->HalfWidth();
	for (const ARCFootballer* F : Players)
	{
		if (!F || F->bSentOff) continue;
		const FVector2D D(F->GetActorLocation().X / HL, F->GetActorLocation().Y / HW);
		(F->Team == 0 ? Hud.HomeDots : Hud.AwayDots).Add(D);
	}
	if (Ball) Hud.BallDot = FVector2D(Ball->GetActorLocation().X / HL, Ball->GetActorLocation().Y / HW);
	Hud.bHasRefDot = PlayerReferee != nullptr;
	if (PlayerReferee) Hud.RefDot = FVector2D(PlayerReferee->GetActorLocation().X / HL, PlayerReferee->GetActorLocation().Y / HW);
	Hud.BigTextTime = FMath::Max(0.f, Hud.BigTextTime - RealDelta);
	Hud.CardTime = FMath::Max(0.f, Hud.CardTime - RealDelta);
	for (int32 i = Hud.Toasts.Num() - 1; i >= 0; --i)
	{
		Hud.Toasts[i].Value -= RealDelta;
		if (Hud.Toasts[i].Value <= 0.f) Hud.Toasts.RemoveAt(i);
	}
	Hud.bReview = Phase == ERCMatchPhase::VarCheck || Phase == ERCMatchPhase::Review;
	Hud.ReviewTime = ReviewTime;
	Hud.ReviewDuration = Recorder->Duration();
	Hud.bReviewPlaying = bReviewPlaying;
	Hud.bLines = bShowLines;
	Hud.ReviewCamera = Sub ? Sub->Fmt("ui.review.camera", {{"n", FString::FromInt(ReviewCam + 1)}, {"total", FString::FromInt(ReviewCamOffsets.Num())}}) : FString();
}

void ARCMatchDirector::OnBreathe()
{
	if (Session && Session->breathe())
	{
		Toast(URCCareerSubsystem::Get(this)->Str("ui.breathe"), 2.f);
		if (PlayerReferee) PlayerReferee->PlayTagged(TEXT("breathe"));
	}
}

void ARCMatchDirector::OnCycleCamera(int32 Dir)
{
	if (Phase == ERCMatchPhase::VarCheck || Phase == ERCMatchPhase::Review)
	{
		ReviewCam = (ReviewCam + Dir + ReviewCamOffsets.Num()) % FMath::Max(1, ReviewCamOffsets.Num());
		return;
	}
	if (Cameras.Num() == 0) return;
	BroadcastCam = (BroadcastCam + Dir + Cameras.Num()) % Cameras.Num();
	if (IsPlayerVar() && PlayerPC) PlayerPC->SetViewTarget(Cameras[BroadcastCam]);
}

void ARCMatchDirector::EndPlay(const EEndPlayReason::Type Reason)
{
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	if (CrowdAudio) CrowdAudio->Stop();
	if (HeartAudio) HeartAudio->Stop();
	// The director owns everything it spawned for the match.
	for (ARCFootballer* F : Players)
	{
		if (F) F->Destroy();
	}
	for (ARCReferee* R : {Centre.Get(), AssistantNear.Get(), AssistantFar.Get()})
	{
		if (R) R->Destroy();
	}
	for (ACameraActor* C : Cameras)
	{
		if (C) C->Destroy();
	}
	if (Ball) Ball->Destroy();
	if (Pitch) Pitch->Destroy();
	Super::EndPlay(Reason);
}

ARCPitch* ARCMatchDirector::DetachPitch()
{
	ARCPitch* Out = Pitch;
	Pitch = nullptr;
	return Out;
}
