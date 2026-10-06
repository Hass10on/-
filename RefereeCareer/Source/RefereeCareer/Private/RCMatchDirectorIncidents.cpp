// Incidents for ARCMatchDirector: staging the planned incident in the world, measuring what the player could
// see, the slow-motion decision, applying the call (cards, restarts, send-offs), dissent, the on-field monitor
// and the VAR room with replay angles and offside lines.
#include "Camera/CameraActor.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "RCBall.h"
#include "RCCareerSubsystem.h"
#include "RCFootballer.h"
#include "RCMatchDirector.h"
#include "RCPitch.h"
#include "RCReferee.h"
#include "RCReplayRecorder.h"
#include "RCSettings.h"
#include "RefereeCareer.h"
#include "Sound/SoundBase.h"

void ARCMatchDirector::TryBeginIncident()
{
	if (IncidentIndex >= 0 || !Ball) return;
	const int32 Idx = Session->dueIncident(MatchMinute());
	if (Idx < 0) return;
	const refcore::PlannedIncident& P = Session->plan()[static_cast<size_t>(Idx)];
	IncidentIndex = Idx;
	Stage = URCCareerSubsystem::ToF(P.tpl->stage);
	StageTime = 0.f;
	bContactDone = false;
	bBallLaunched = false;
	Offender = Victim = Runner = LineDefenderPlayer = nullptr;
	Protesters.Reset();
	const int32 OffSide = P.offendingSide;

	if (Stage == TEXT("confrontation"))
	{
		// Players from both sides converge on a point near the ball.
		ContactPoint = Pitch->ClampToField(Ball->GetActorLocation(), 400.f);
		Offender = Nearest(OffSide, ContactPoint);
		Victim = Nearest(1 - OffSide, ContactPoint);
		int32 Count = 0;
		for (ARCFootballer* F : Players)
		{
			if (!F || !F->IsAvailable() || F->Position == ERCPosition::Goalkeeper) continue;
			if (FVector::Dist2D(F->GetActorLocation(), ContactPoint) > 2500.f || Count >= 7) continue;
			F->Mode = ERCPlayerMode::Scripted;
			F->ScriptTarget = ContactPoint + FVector(FMath::FRandRange(-180.f, 180.f), FMath::FRandRange(-180.f, 180.f), 0.f);
			F->ScriptSpeed = 650.f;
			++Count;
		}
		Ball->Release();
		SetPhase(ERCMatchPhase::Staging);
		return;
	}

	if (Stage == TEXT("offside") || Stage == TEXT("goal_check"))
	{
		// Attack by `OffSide` (the side that may be offside / scoring).
		const int32 Att = OffSide;
		ARCFootballer* Carrier = Ball->GetCarrier();
		if (!Carrier || Carrier->Team != Att)
		{
			Carrier = Nearest(Att, Ball->GetActorLocation());
			if (Carrier) Ball->GiveTo(Carrier);
		}
		Victim = Carrier;  // the passer
		for (ARCFootballer* F : Players)
		{
			if (F && F->Team == Att && F->IsAvailable() && F->Position == ERCPosition::Forward && F != Carrier) Runner = F;
		}
		if (!Runner) Runner = Nearest(Att, Pitch->GoalCenter(AttackDir(Att)), Carrier);
		if (!Runner || !Carrier)
		{
			IncidentIndex = -1;
			return;
		}
		Runner->Mode = ERCPlayerMode::Scripted;
		Runner->ScriptSpeed = 600.f;
		Carrier->Mode = ERCPlayerMode::Scripted;
		Carrier->ScriptSpeed = 380.f;
		SetPhase(ERCMatchPhase::Staging);
		return;
	}

	// Foul-type incidents: the victim's side has the ball, the offender closes in.
	const int32 VictimSide = 1 - OffSide;
	ARCFootballer* Carrier = Ball->GetCarrier();
	if (!Carrier || Carrier->Team != VictimSide)
	{
		Carrier = Nearest(VictimSide, Ball->GetActorLocation());
		if (Carrier) Ball->GiveTo(Carrier);
	}
	Victim = Carrier;
	Offender = Victim ? Nearest(OffSide, Victim->GetActorLocation()) : nullptr;
	if (!Victim || !Offender)
	{
		IncidentIndex = -1;
		return;
	}
	Victim->Mode = ERCPlayerMode::Scripted;
	Victim->ScriptSpeed = 560.f;
	Offender->Mode = ERCPlayerMode::Scripted;
	Offender->ScriptSpeed = URCSettings::Get()->PlayerSprintSpeed;
	SetPhase(ERCMatchPhase::Staging);
}

void ARCMatchDirector::TickStaging(float DeltaSeconds)
{
	StageTime += DeltaSeconds;
	const refcore::PlannedIncident& P = Session->plan()[static_cast<size_t>(IncidentIndex)];

	if (Stage == TEXT("confrontation"))
	{
		if (StageTime > 1.8f && !bContactDone)
		{
			for (ARCFootballer* F : Players)
			{
				if (F && F->Mode == ERCPlayerMode::Scripted) F->PlayTagged(F == Offender ? TEXT("shove") : TEXT("protest_angry"));
			}
			Contact();
		}
		return;
	}

	if (Stage == TEXT("offside") || Stage == TEXT("goal_check"))
	{
		const int32 Att = P.offendingSide;
		const int32 Dir = AttackDir(Att);
		ARCFootballer* Def = nullptr;
		const float Line = OffsideLineX(1 - Att, &Def);
		LineDefenderPlayer = Def;
		const float Margin = static_cast<float>(P.tpl->offsideMargin) * RC_M;
		if (!bBallLaunched)
		{
			// The runner holds the line at the planned margin while the passer advances.
			Runner->ScriptTarget = FVector(Line + Dir * Margin, FMath::Clamp(Runner->GetActorLocation().Y, -Pitch->HalfWidth() * 0.6f, Pitch->HalfWidth() * 0.6f), 0.f);
			if (Victim) Victim->ScriptTarget = Victim->GetActorLocation() + FVector(Dir * 300.f, 0.f, 0.f);
			const float Off = FMath::Abs(Runner->GetActorLocation().X - (Line + Dir * Margin));
			if ((StageTime > 1.6f && Off < 90.f) || StageTime > 6.f)
			{
				// Moment of the pass: lock the margin exactly and play the through ball.
				FVector L = Runner->GetActorLocation();
				L.X = Line + Dir * Margin;
				Runner->SetActorLocation(L);
				OffsideLineAtPass = Line;
				const FVector Space = L + FVector(Dir * 900.f, 0.f, 0.f);
				FVector V = (Space - Ball->GetActorLocation()).GetSafeNormal2D() * FMath::Clamp(FVector::Dist2D(Space, Ball->GetActorLocation()) * 1.1f + 600.f, 900.f, 2400.f);
				if (Victim) Victim->PlayTagged(TEXT("pass"));
				Ball->Kick(V, Victim);
				Runner->ScriptTarget = Space;
				Runner->ScriptSpeed = URCSettings::Get()->PlayerSprintSpeed;
				bBallLaunched = true;
				ContactPoint = L;
				if (Stage == TEXT("offside"))
				{
					Contact();
					return;
				}
				if (Victim) Victim->Mode = ERCPlayerMode::Play;
				// The defence is caught flat: they trail the runner instead of intercepting.
				for (ARCFootballer* F : Players)
				{
					if (F && F->Team != Att && F->IsAvailable() && F->Position != ERCPosition::Goalkeeper)
					{
						F->Mode = ERCPlayerMode::Scripted;
						F->ScriptTarget = F->GetActorLocation() + FVector(Dir * 600.f, 0.f, 0.f);
						F->ScriptSpeed = 320.f;
					}
				}
			}
			return;
		}
		// Goal check: the runner collects and scores; the AI referee rules, then the VAR checks.
		if (!Ball->GetCarrier() && FVector::Dist2D(Ball->GetActorLocation(), Runner->GetActorLocation()) < 110.f && !bContactDone)
		{
			Ball->GiveTo(Runner);
		}
		if (Ball->GetCarrier() == Runner)
		{
			Runner->ScriptTarget = Pitch->PenaltySpot(Dir);
			if (FVector::Dist2D(Runner->GetActorLocation(), Pitch->GoalCenter(Dir)) < 1700.f)
			{
				if (P.tpl->id == "goal_foul_buildup")
				{
					if (ARCFootballer* Pushed = Nearest(1 - Att, Runner->GetActorLocation()))
					{
						Runner->PlayTagged(TEXT("push"));
						Pushed->FallDown(2.5f, (Pushed->GetActorLocation() - Runner->GetActorLocation()).GetSafeNormal2D() * 300.f);
						Offender = Runner;
						Victim = Pushed;
					}
				}
				Shoot(Runner, true);
			}
		}
		if (Pitch->Classify(Ball->GetActorLocation()) == (Dir > 0 ? ERCBoundary::Goal1 : ERCBoundary::Goal0) && !bContactDone)
		{
			Score[Att] += 1;
			Session->goalScored(Att);
			if (USoundBase* Cheer = URCSettings::Get()->CrowdCheer.LoadSynchronous()) UGameplayStatics::PlaySound2D(this, Cheer, 1.f);
			ContactPoint = Runner->GetActorLocation();
			PendingGoalTeam = Att;
			Contact();
		}
		else if (StageTime > 14.f && !bContactDone)
		{
			// The attack broke down: drop the check quietly (counts as a correct "goal stands" style no-call).
			PendingGoalTeam = -1;
			ContactPoint = Runner->GetActorLocation();
			Contact();
		}
		return;
	}

	// Foul-type staging.
	if (!Victim || !Offender) return;
	const bool bInBox = P.tpl->inBox;
	const int32 AttDir = AttackDir(Victim->Team);
	if (bInBox)
	{
		Victim->ScriptTarget = Pitch->PenaltySpot(AttDir) + FVector(-AttDir * 150.f, FMath::Sin(StageTime) * 300.f, 0.f);
	}
	else
	{
		Victim->ScriptTarget = Victim->GetActorLocation() + FVector(AttDir * 500.f, 0.f, 0.f);
	}
	Offender->ScriptTarget = Victim->GetActorLocation() + Victim->GetVelocity() * 0.25f;
	const float Gap = FVector::Dist2D(Victim->GetActorLocation(), Offender->GetActorLocation());
	const bool bZoneOk = !bInBox || Pitch->IsInPenaltyArea(Victim->GetActorLocation(), AttDir);
	if (StageTime > 14.f && !bZoneOk)
	{
		// Could not reach the box in time: bring the offender alongside wherever play is.
		Offender->SetActorLocation(Victim->GetActorLocation() - Victim->GetActorForwardVector() * 120.f + FVector(0.f, 60.f, 0.f));
	}
	const float Trigger = Stage == TEXT("dive") ? 260.f : Stage == TEXT("handball") ? 900.f : 140.f;
	if (Stage == TEXT("handball") && Gap < Trigger && !bBallLaunched && (bZoneOk || StageTime > 14.f))
	{
		// The attacker plays the ball into the defender.
		const FVector At = Offender->GetActorLocation() + FVector(0.f, 25.f, 40.f);
		FVector V = (At - Ball->GetActorLocation()).GetSafeNormal() * 1800.f;
		V.Z += 250.f;
		Victim->PlayTagged(TEXT("pass"));
		Ball->Kick(V, Victim);
		Offender->PlayTagged(FName(*URCCareerSubsystem::ToF(P.tpl->anim)));
		bBallLaunched = true;
		return;
	}
	if (Stage == TEXT("handball"))
	{
		if (bBallLaunched && FVector::Dist(Ball->GetActorLocation(), Offender->GetActorLocation() + FVector(0.f, 0.f, 30.f)) < 120.f)
		{
			Ball->Kick(-Ball->GetBallVelocity() * 0.3f + FVector(0.f, 0.f, 150.f), Offender);
			ContactPoint = Offender->GetActorLocation();
			Contact();
		}
		else if (StageTime > 18.f)
		{
			ContactPoint = Offender->GetActorLocation();
			Contact();
		}
		return;
	}
	if ((Gap < Trigger && bZoneOk) || StageTime > 18.f)
	{
		ContactPoint = (Victim->GetActorLocation() + Offender->GetActorLocation()) * 0.5f;
		const FVector Push = (Victim->GetActorLocation() - Offender->GetActorLocation()).GetSafeNormal2D();
		Offender->PlayTagged(FName(*URCCareerSubsystem::ToF(P.tpl->anim)));
		if (Stage == TEXT("elbow"))
		{
			Victim->Jump();
			Offender->Jump();
		}
		const bool bClean = P.tpl->id == "tackle_clean" || P.tpl->id == "pen_shoulder_fair" || P.tpl->id == "handball_natural";
		if (Stage == TEXT("dive"))
		{
			Victim->PlayTagged(TEXT("dive"));
			Victim->FallDown(2.2f, Push * 250.f);
			Offender->StopMoving();
		}
		else if (!bClean || FMath::FRand() < 0.5f)
		{
			Victim->FallDown(Stage == TEXT("holding") ? 1.8f : 2.6f, Push * (P.tpl->truth.card == refcore::Card::Red ? 650.f : 400.f));
		}
		if (bClean) Ball->GiveTo(Offender);
		else Ball->Kick(Push * 350.f + FVector(0.f, 0.f, 80.f), Victim);
		Contact();
	}
}

refcore::ViewSample ARCMatchDirector::MeasureView(const FVector& Point) const
{
	refcore::ViewSample V;
	const ARCReferee* Viewer = PlayerReferee ? PlayerReferee.Get() : Centre.Get();
	if (!Viewer) return V;
	const FVector Eye = Viewer->EyeLocation();
	const FVector Target = Point + FVector(0.f, 0.f, 60.f);
	V.distance = FVector::Dist(Eye, Target) / RC_M;
	// Side-on views of the challenge are best: compare the line of sight with the challenge direction.
	FVector Challenge = FVector::ForwardVector;
	if (Victim && Offender) Challenge = (Victim->GetActorLocation() - Offender->GetActorLocation()).GetSafeNormal2D();
	const FVector Sight = (Target - Eye).GetSafeNormal2D();
	V.angleQuality = FMath::Abs(FVector::CrossProduct(Sight, Challenge).Z);
	// Sight lines to five points around the contact, blocked by other players' capsules.
	int32 Blocked = 0;
	const FVector Offsets[5] = {FVector::ZeroVector, FVector(0.f, 0.f, 50.f), FVector(0.f, 0.f, -40.f), FVector(40.f, 40.f, 0.f), FVector(-40.f, -40.f, 0.f)};
	FCollisionQueryParams Q(SCENE_QUERY_STAT(RefereeSight), false, Viewer);
	if (Victim) Q.AddIgnoredActor(Victim);
	if (Offender) Q.AddIgnoredActor(Offender);
	if (Runner) Q.AddIgnoredActor(Runner);
	for (const FVector& O : Offsets)
	{
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit, Eye, Target + O, ECC_Pawn, Q) && Cast<ARCFootballer>(Hit.GetActor())) ++Blocked;
	}
	V.occlusion = Blocked / 5.f;
	V.alignment = FMath::Abs(Viewer->GetActorLocation().X - OffsideLineAtPass) / RC_M;
	V.replay = false;
	return V;
}

void ARCMatchDirector::Contact()
{
	bContactDone = true;
	ContactRecordTime = Recorder->Duration();
	const refcore::PlannedIncident& P = Session->plan()[static_cast<size_t>(IncidentIndex)];
	if (IsPlayerVar())
	{
		// The AI referee makes the on-field call first, then the VAR checks it.
		if (P.onField.restart != refcore::Restart::PlayOn && P.onField.restart != refcore::Restart::Advantage && P.onField.restart != refcore::Restart::Goal)
		{
			Whistle(0);
		}
		URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
		Toast(Sub->Fmt("ui.var.onfield", {{"decision", URCCareerSubsystem::ToF(Sub->GetContent().verdictLabel(P.onField, refcore::Role::Center))}}), 4.f);
		BeginReview(true);
		return;
	}
	ContactView = MeasureView(ContactPoint);
	UGameplayStatics::SetGlobalTimeDilation(this, URCSettings::Get()->IncidentTimeDilation);
	OpenDecision();
}

void ARCMatchDirector::OpenDecision()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	const refcore::Perception Per = Session->perceive(IncidentIndex, ContactView);
	Hud.bDecision = true;
	Hud.DecisionTitle = Sub->Str(bDecisionIsReview ? "ui.decision.review" : (Session->setup().role == refcore::Role::Assistant ? "ui.decision.ar" : "ui.decision.title"));
	Hud.DecisionNote = URCCareerSubsystem::ToF(Per.note);
	Hud.Cues.Reset();
	for (const std::string& C : Per.cues) Hud.Cues.Add(URCCareerSubsystem::ToF(C));
	Hud.Options.Reset();
	for (const refcore::DecisionOption& O : Session->options(IncidentIndex)) Hud.Options.Add(URCCareerSubsystem::ToF(O.label));
	Hud.Clarity = static_cast<float>(Per.clarity);
	DecisionWindow = static_cast<float>(Session->decisionWindowSeconds(IncidentIndex));
	if (bDecisionIsReview) DecisionWindow = FMath::Max(DecisionWindow, 30.f);
	Hud.TimeTotal = DecisionWindow;
	Hud.TimeLeft = DecisionWindow;
	DecisionOpenedAt = static_cast<float>(GetWorld()->GetRealTimeSeconds());
	SetPhase(ERCMatchPhase::Decision);
}

void ARCMatchDirector::TickDecision(float RealDelta)
{
	Hud.TimeLeft = FMath::Max(0.f, Hud.TimeLeft - RealDelta);
	if (Hud.TimeLeft <= 0.f) ResolveDecision(-1, true);
}

void ARCMatchDirector::OnOption(int32 Index)
{
	if (Phase == ERCMatchPhase::Decision || ((Phase == ERCMatchPhase::VarCheck || Phase == ERCMatchPhase::Review) && Hud.bDecision))
	{
		if (Index >= 0 && Index < Hud.Options.Num()) ResolveDecision(Index, false);
	}
	else if (Phase == ERCMatchPhase::Dissent)
	{
		ResolveDissent(Index);
	}
}

void ARCMatchDirector::ResolveDecision(int32 OptionIndex, bool bTimeout)
{
	const std::vector<refcore::DecisionOption> Opts = Session->options(IncidentIndex);
	const float Reaction = static_cast<float>(GetWorld()->GetRealTimeSeconds()) - DecisionOpenedAt;
	Hud.bDecision = false;
	UGameplayStatics::SetGlobalTimeDilation(this, 1.f);
	refcore::DecisionResult R;
	const bool bWasReview = Phase == ERCMatchPhase::Review || Phase == ERCMatchPhase::VarCheck;
	if (bWasReview) EndReview();
	if (bDecisionIsReview && !IsPlayerVar())
	{
		// Centre referee at the monitor after a VAR recommendation.
		bDecisionIsReview = false;
		R = Session->onFieldReview(IncidentIndex, bTimeout ? std::string("keep") : Opts[static_cast<size_t>(OptionIndex)].key);
	}
	else if (bTimeout)
	{
		R = Session->timeout(IncidentIndex, ContactView);
	}
	else
	{
		refcore::ViewSample V = ContactView;
		if (IsPlayerVar())
		{
			V.replay = true;
			V.angleQuality = 0.8;
		}
		R = Session->decide(IncidentIndex, Opts[static_cast<size_t>(OptionIndex)].key, Reaction, V);
	}
	if (R.varReviewOffered)
	{
		// The VAR asks the centre referee to look at the monitor.
		URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
		BigText(Sub->Str("ui.var.recommend"), 2.5f);
		bDecisionIsReview = true;
		BeginReview(false);
		return;
	}
	LastResult = R;
	ApplyVerdict(R);
}

void ARCMatchDirector::ApplyVerdict(const refcore::DecisionResult& R)
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	const refcore::PlannedIncident& P = Session->plan()[static_cast<size_t>(IncidentIndex)];
	const refcore::Verdict& V = R.finalVerdict;
	const FString Label = URCCareerSubsystem::ToF(Sub->GetContent().verdictLabel(V, refcore::Role::Center));
	if (URCSettings::Get()->bRevealCallsDuringMatch) Toast(URCCareerSubsystem::ToF(R.feedback) + TEXT(" ") + Label, 4.f);

	// Assistant: flag up for offside / fouls in his zone.
	if (PlayerReferee && PlayerReferee->GetOfficial() != ERCOfficial::Centre && V.restart != refcore::Restart::PlayOn)
	{
		PlayerReferee->RaiseFlag(true);
	}

	// Disciplinary action.
	if (V.card != refcore::Card::None)
	{
		ARCFootballer* Target = V.cardTo == refcore::CardTo::Victim ? Victim : Offender;
		if (P.tpl->stage == "confrontation" && !Target) Target = Nearest(P.offendingSide, ContactPoint);
		ApplyCard(Target, V.card);
		if (V.cardTo == refcore::CardTo::Both) ApplyCard(Victim, V.card);
	}

	// Goal checks: disallowing removes the goal.
	if (P.tpl->stage == "goal_check")
	{
		if (V.restart == refcore::Restart::NoGoal && PendingGoalTeam >= 0)
		{
			Score[PendingGoalTeam] = FMath::Max(0, Score[PendingGoalTeam] - 1);
			Toast(Sub->Str("ui.var.goal_disallowed"), 3.5f);
			if (USoundBase* Boo = URCSettings::Get()->CrowdBoo.LoadSynchronous()) UGameplayStatics::PlaySound2D(this, Boo, 1.f);
		}
		if (PendingGoalTeam >= 0)
		{
			KickOffTeam = V.restart == refcore::Restart::NoGoal ? PendingGoalTeam : 1 - PendingGoalTeam;
		}
	}

	switch (V.restart)
	{
	case refcore::Restart::PlayOn:
		break;
	case refcore::Restart::Advantage:
		BigText(Sub->Str("ui.advantage"), 1.5f);
		if (PlayerReferee) PlayerReferee->PlayTagged(TEXT("signal_advantage"));
		break;
	case refcore::Restart::Penalty:
		Whistle(0);
		if (PlayerReferee) PlayerReferee->PlayTagged(TEXT("point_spot"));
		break;
	case refcore::Restart::Goal:
	case refcore::Restart::NoGoal:
		break;
	default:
		Whistle(0);
		break;
	}

	// Dissent from the side that lost the call.
	if (Session->dissent().active && !IsPlayerVar())
	{
		BeginDissent();
		return;
	}
	ContinueAfterIncident();
}

void ARCMatchDirector::ContinueAfterIncident()
{
	const refcore::Verdict& V = LastResult.finalVerdict;
	const refcore::PlannedIncident& P = Session->plan()[static_cast<size_t>(IncidentIndex)];
	const int32 Benefit = LastResult.benefitSide;
	if (PlayerReferee) PlayerReferee->RaiseFlag(false);
	for (ARCFootballer* F : Players)
	{
		if (F && (F->Mode == ERCPlayerMode::Scripted || F->Mode == ERCPlayerMode::Protest)) F->Mode = ERCPlayerMode::Play;
	}
	const FVector Spot = Pitch->ClampToField(ContactPoint, 50.f);
	IncidentIndex = -1;
	if (P.tpl->stage == "goal_check")
	{
		if (PendingGoalTeam >= 0)
		{
			PendingGoalTeam = -1;
			BeginRestart(ERCRestart::KickOff, KickOffTeam, FVector::ZeroVector);
		}
		else
		{
			SetPhase(ERCMatchPhase::Play);
		}
		return;
	}
	switch (V.restart)
	{
	case refcore::Restart::FreeKick:
		BeginRestart(V.cardTo == refcore::CardTo::Victim ? ERCRestart::IndirectFreeKick : ERCRestart::FreeKick, Benefit, Spot);
		break;
	case refcore::Restart::Penalty:
		BeginRestart(ERCRestart::Penalty, Benefit, Spot);
		break;
	case refcore::Restart::Offside:
		BeginRestart(ERCRestart::IndirectFreeKick, Benefit, Runner ? Runner->GetActorLocation() : Spot);
		break;
	default:
		if (P.tpl->stage == "confrontation") BeginRestart(ERCRestart::DropBall, Benefit, Spot);
		else SetPhase(ERCMatchPhase::Play);
		break;
	}
}

void ARCMatchDirector::ApplyCard(ARCFootballer* Who, refcore::Card Card)
{
	if (!Who || Who->bSentOff) return;
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	ARCReferee* Ref = Centre;
	if (Ref) Ref->PlayTagged(TEXT("show_card"));
	refcore::Card Shown = Card;
	if (Card == refcore::Card::Yellow)
	{
		Who->Yellows += 1;
		if (Who->Yellows >= 2) Shown = refcore::Card::Red;
	}
	Hud.CardShown = Shown == refcore::Card::Red ? 2 : 1;
	Hud.CardTime = 2.5f;
	const FString Team = Who->Team == 0 ? Hud.HomeName : Hud.AwayName;
	Toast(Sub->Fmt(Shown == refcore::Card::Red ? "ui.card.red" : "ui.card.yellow", {{"number", FString::FromInt(Who->Number)}, {"team", Team}}), 4.f);
	if (Shown == refcore::Card::Red) SendOff(Who);
}

void ARCMatchDirector::SendOff(ARCFootballer* Who)
{
	if (!Who) return;
	if (Ball->GetCarrier() == Who) Ball->Release();
	Who->Mode = ERCPlayerMode::LeavingPitch;
	Who->ScriptTarget = FVector(0.f, -Pitch->HalfWidth() - 700.f, 0.f);
}

void ARCMatchDirector::BeginDissent()
{
	const refcore::DissentEpisode& D = Session->dissent();
	Protesters.Reset();
	const FVector RefLoc = PlayerReferee ? PlayerReferee->GetActorLocation() : Centre->GetActorLocation();
	const int32 Count = D.anger > 60.0 ? 3 : 1;
	ARCFootballer* Except = nullptr;
	for (int32 i = 0; i < Count; ++i)
	{
		ARCFootballer* F = Nearest(D.side, RefLoc, Except);
		if (!F) break;
		F->Mode = ERCPlayerMode::Protest;
		F->PlayTagged(D.anger > 55.0 ? TEXT("protest_angry") : TEXT("protest"));
		Protesters.Add(F);
		Except = F;
	}
	Hud.bDissent = true;
	Hud.DissentText = URCCareerSubsystem::ToF(D.text);
	Hud.DissentOptions.Reset();
	for (refcore::DissentResponse R : Session->dissentOptions())
	{
		Hud.DissentOptions.Add(URCCareerSubsystem::ToF(Session->dissentOptionLabel(R)));
	}
	if (USoundBase* Boo = URCSettings::Get()->CrowdBoo.LoadSynchronous()) UGameplayStatics::PlaySound2D(this, Boo, 0.6f);
	SetPhase(ERCMatchPhase::Dissent);
}

void ARCMatchDirector::ResolveDissent(int32 OptionIndex)
{
	const std::vector<refcore::DissentResponse> Opts = Session->dissentOptions();
	if (OptionIndex < 0 || OptionIndex >= static_cast<int32>(Opts.size())) return;
	const refcore::DissentResponse Choice = Opts[static_cast<size_t>(OptionIndex)];
	const refcore::DissentResult R = Session->respondDissent(Choice);
	Hud.bDissent = false;
	Toast(URCCareerSubsystem::ToF(R.text), 3.5f);
	if (R.cardShown && Protesters.Num() > 0 && Session->dissent().who != "coach") ApplyCard(Protesters[0], refcore::Card::Yellow);
	if (R.coachSentOff) BigText(URCCareerSubsystem::Get(this)->Str("ui.coach_sent_off"), 2.f);
	ContinueAfterIncident();
}

void ARCMatchDirector::BeginReview(bool bVarRoom)
{
	ReviewTime = FMath::Max(0.f, ContactRecordTime - 3.f);
	bReviewPlaying = true;
	bShowLines = false;
	ReviewCam = 0;
	Recorder->BeginPlayback();
	if (Cameras.Num() > 0) SavedCamXf = Cameras[0]->GetActorTransform();
	if (!bVarRoom && PlayerReferee)
	{
		// Walk to the monitor is skipped for pace; the screen shows the replay.
		PlayerReferee->PlayTagged(TEXT("var_signal"));
	}
	if (PlayerPC && Cameras.Num() > 0) PlayerPC->SetViewTarget(Cameras[0]);
	SetPhase(bVarRoom ? ERCMatchPhase::VarCheck : ERCMatchPhase::Review);
	// The decision panel is shown alongside the replay controls.
	ContactView = refcore::ViewSample();
	ContactView.replay = true;
	ContactView.angleQuality = 0.8;
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	const refcore::Perception Per = Session->perceive(IncidentIndex, ContactView);
	Hud.bDecision = true;
	Hud.DecisionTitle = Sub->Str(bVarRoom ? "ui.decision.var" : "ui.decision.review");
	Hud.DecisionNote = URCCareerSubsystem::ToF(Per.note);
	Hud.Cues.Reset();
	for (const std::string& C : Per.cues) Hud.Cues.Add(URCCareerSubsystem::ToF(C));
	Hud.Options.Reset();
	std::vector<refcore::DecisionOption> Opts = Session->options(IncidentIndex);
	for (const refcore::DecisionOption& O : Opts) Hud.Options.Add(URCCareerSubsystem::ToF(O.label));
	Hud.Clarity = static_cast<float>(Per.clarity);
	DecisionWindow = bVarRoom ? static_cast<float>(Session->decisionWindowSeconds(IncidentIndex)) : 30.f;
	Hud.TimeTotal = DecisionWindow;
	Hud.TimeLeft = DecisionWindow;
	DecisionOpenedAt = static_cast<float>(GetWorld()->GetRealTimeSeconds());
}

void ARCMatchDirector::TickReview(float RealDelta)
{
	const float Dur = Recorder->Duration();
	if (bReviewPlaying)
	{
		ReviewTime += RealDelta * 0.5f;  // half-speed replay
		if (ReviewTime > Dur) ReviewTime = FMath::Max(0.f, ContactRecordTime - 3.f);
	}
	Recorder->ShowAt(ReviewTime);
	// Review camera orbits the contact point.
	if (Cameras.Num() > 0 && ReviewCamOffsets.Num() > 0)
	{
		ACameraActor* Cam = Cameras[0];
		const FVector Focus = Recorder->BallLocationAt(ReviewTime) * 0.3f + ContactPoint * 0.7f;
		const FVector Pos = Focus + ReviewCamOffsets[ReviewCam];
		Cam->SetActorLocationAndRotation(Pos, (Focus - Pos).Rotation());
	}
	UpdateOffsideLines();
	Hud.TimeLeft = FMath::Max(0.f, Hud.TimeLeft - RealDelta);
	if (Hud.TimeLeft <= 0.f && Hud.bDecision) ResolveDecision(-1, true);
}

void ARCMatchDirector::EndReview()
{
	Recorder->EndPlayback();
	LineAttacker->SetVisibility(false);
	LineDefender->SetVisibility(false);
	// Put the broadcast camera back where it belongs and return the view to the player.
	if (Cameras.Num() > 0) Cameras[0]->SetActorTransform(SavedCamXf);
	if (PlayerPC && PlayerReferee) PlayerPC->SetViewTarget(PlayerReferee);
	else if (PlayerPC && Cameras.IsValidIndex(BroadcastCam)) PlayerPC->SetViewTarget(Cameras[BroadcastCam]);
}

void ARCMatchDirector::UpdateOffsideLines()
{
	const bool bLineStage = Stage == TEXT("offside") || Stage == TEXT("goal_check");
	const bool bShow = bShowLines && bLineStage && Runner && LineDefenderPlayer;
	LineAttacker->SetVisibility(bShow);
	LineDefender->SetVisibility(bShow);
	if (!bShow) return;
	const float AttX = Recorder->ActorLocationAt(Runner, ReviewTime).X;
	const float DefX = Recorder->ActorLocationAt(LineDefenderPlayer, ReviewTime).X;
	const FVector Scale(0.04f, Pitch->HalfWidth() * 2.f / 100.f, 0.012f);
	LineAttacker->SetWorldLocationAndRotation(FVector(AttX, 0.f, 1.5f), FRotator::ZeroRotator);
	LineAttacker->SetWorldScale3D(Scale);
	LineDefender->SetWorldLocationAndRotation(FVector(DefX, 0.f, 1.6f), FRotator::ZeroRotator);
	LineDefender->SetWorldScale3D(Scale);
}

void ARCMatchDirector::OnScrub(float Axis)
{
	if (Phase != ERCMatchPhase::VarCheck && Phase != ERCMatchPhase::Review) return;
	if (FMath::Abs(Axis) < 0.1f) return;
	bReviewPlaying = false;
	ReviewTime = FMath::Clamp(ReviewTime + Axis * 0.02f, 0.f, Recorder->Duration());
}

void ARCMatchDirector::OnTogglePlayback()
{
	bReviewPlaying = !bReviewPlaying;
}

void ARCMatchDirector::OnToggleLines()
{
	bShowLines = !bShowLines;
}
