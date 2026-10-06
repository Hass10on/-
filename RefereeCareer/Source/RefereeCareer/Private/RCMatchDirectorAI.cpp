// Football AI for ARCMatchDirector: team shape, pressing, dribbling, passing, shooting, goalkeeping,
// and the AI-controlled officials. Deliberately simple and readable; the refereeing is the game.
#include "RCBall.h"
#include "RCFootballer.h"
#include "RCMatchDirector.h"
#include "RCPitch.h"
#include "RCReferee.h"
#include "RCSettings.h"

namespace
{
float MdAiChancePerSecond(float PerSecond, float DeltaSeconds)
{
	return 1.f - FMath::Exp(-PerSecond * DeltaSeconds);
}
}  // namespace

int32 ARCMatchDirector::PossessionTeam() const
{
	if (!Ball) return -1;
	if (const ARCFootballer* C = Ball->GetCarrier()) return C->Team;
	if (const ARCFootballer* L = Ball->GetLastTouch()) return L->Team;
	return -1;
}

ARCFootballer* ARCMatchDirector::GoalkeeperOf(int32 Team) const
{
	for (ARCFootballer* F : Players)
	{
		if (F && F->Team == Team && F->Position == ERCPosition::Goalkeeper && !F->bSentOff) return F;
	}
	return nullptr;
}

ARCFootballer* ARCMatchDirector::Nearest(int32 Team, const FVector& Where, const ARCFootballer* Except, bool bAllowGK) const
{
	ARCFootballer* Best = nullptr;
	float BestD = TNumericLimits<float>::Max();
	for (ARCFootballer* F : Players)
	{
		if (!F || F == Except || !F->IsAvailable() || (Team >= 0 && F->Team != Team)) continue;
		if (!bAllowGK && F->Position == ERCPosition::Goalkeeper) continue;
		const float D = FVector::DistSquared2D(F->GetActorLocation(), Where);
		if (D < BestD)
		{
			BestD = D;
			Best = F;
		}
	}
	return Best;
}

float ARCMatchDirector::OffsideLineX(int32 DefendingTeam, ARCFootballer** OutDefender) const
{
	// Defending team's own goal is at -AttackDir; sort its players by how deep they are.
	const int32 Own = -AttackDir(DefendingTeam);
	TArray<ARCFootballer*> Def;
	for (ARCFootballer* F : Players)
	{
		if (F && F->Team == DefendingTeam && !F->bSentOff) Def.Add(F);
	}
	Def.Sort([Own](const ARCFootballer& A, const ARCFootballer& B) { return A.GetActorLocation().X * Own > B.GetActorLocation().X * Own; });
	if (Def.Num() < 2) return 0.f;
	if (OutDefender) *OutDefender = Def[1];
	// The line is never behind the halfway line.
	const float X = Def[1]->GetActorLocation().X;
	return Own > 0 ? FMath::Max(X, 0.f) : FMath::Min(X, 0.f);
}

void ARCMatchDirector::TickTeams(float DeltaSeconds)
{
	if (!Ball) return;
	const int32 Possessing = PossessionTeam();
	// The two nearest defenders press the ball.
	TArray<ARCFootballer*> Pressers;
	for (int32 Team = 0; Team < 2; ++Team)
	{
		if (Team == Possessing && Ball->GetCarrier()) continue;
		const FVector Target = Ball->PredictLocation(0.4f);
		ARCFootballer* First = Nearest(Team, Target);
		if (First) Pressers.Add(First);
		if (Team != Possessing)
		{
			if (ARCFootballer* Second = Nearest(Team, Target, First)) Pressers.Add(Second);
		}
	}
	for (ARCFootballer* F : Players)
	{
		if (!F) continue;
		F->ActionCooldown = FMath::Max(0.f, F->ActionCooldown - DeltaSeconds);
		if (F->bSentOff || F->IsDown()) continue;
		switch (F->Mode)
		{
		case ERCPlayerMode::LeavingPitch:
			if (F->MoveTowards(F->ScriptTarget, 300.f, 100.f) < 120.f)
			{
				F->bSentOff = true;
				F->SetActorHiddenInGame(true);
				F->SetActorEnableCollision(false);
			}
			continue;
		case ERCPlayerMode::Scripted:
			F->MoveTowards(F->ScriptTarget, F->ScriptSpeed, 30.f);
			continue;
		case ERCPlayerMode::Celebrate:
		case ERCPlayerMode::Protest:
		case ERCPlayerMode::Idle:
			continue;
		default:
			break;
		}
		if (Ball->GetCarrier() == F) TickCarrier(F, DeltaSeconds);
		else if (F->Position == ERCPosition::Goalkeeper) TickGoalkeeper(F, DeltaSeconds);
		else TickOffBall(F, DeltaSeconds, Pressers);
	}
}

bool ARCMatchDirector::TryPickUp(ARCFootballer* P)
{
	if (Ball->GetCarrier() || P->IsDown()) return false;
	const FVector B = Ball->GetActorLocation();
	if (B.Z > 90.f || Ball->GetBallVelocity().Size2D() > 1500.f) return false;
	if (FVector::Dist2D(B, P->GetActorLocation()) > 85.f) return false;
	if (Ball->GetLastTouch() == P && Ball->TimeSinceTouch() < 0.35f) return false;
	Ball->GiveTo(P);
	P->ActionCooldown = 0.35f;
	return true;
}

void ARCMatchDirector::TickOffBall(ARCFootballer* P, float DeltaSeconds, const TArray<ARCFootballer*>& Pressers)
{
	const URCSettings* S = URCSettings::Get();
	const int32 Dir = AttackDir(P->Team);
	const bool bInPossession = PossessionTeam() == P->Team;
	if (TryPickUp(P)) return;

	if (Pressers.Contains(P))
	{
		const FVector Target = Ball->PredictLocation(0.3f);
		P->MoveTowards(Target, S->PlayerSprintSpeed * (0.85f + 0.15f * P->Skill), 20.f);
		// Clean tackles: win the ball without a foul (fouls only come from the incident plan).
		ARCFootballer* Carrier = Ball->GetCarrier();
		if (Carrier && Carrier->Team != P->Team && Phase == ERCMatchPhase::Play && Carrier->Mode == ERCPlayerMode::Play &&
			FVector::Dist2D(Carrier->GetActorLocation(), P->GetActorLocation()) < 130.f && Ball->TimeSinceTouch() > 0.9f)
		{
			const float Win = 0.9f * P->Skill / (P->Skill + Carrier->Skill);
			if (FMath::FRand() < MdAiChancePerSecond(Win * 2.2f, DeltaSeconds))
			{
				Ball->GiveTo(P);
				P->PlayTagged(TEXT("tackle_clean"));
				Carrier->ActionCooldown = 0.8f;
			}
		}
		return;
	}

	// Team shape: the block follows the ball and pushes up when in possession.
	const float BallAtt = Ball->GetActorLocation().X * Dir / Pitch->HalfLength();
	float X = P->Anchor.X * 0.55f + BallAtt * 0.45f + (bInPossession ? 0.14f : -0.1f);
	X = FMath::Clamp(X, -0.92f, 0.88f);
	if (bInPossession && P->Position == ERCPosition::Forward)
	{
		// Forwards play on the shoulder of the last defender (that is where offside calls come from).
		const float Line = OffsideLineX(1 - P->Team) * Dir / Pitch->HalfLength();
		X = FMath::Min(FMath::Max(X, Line - 0.03f), Line + 0.01f);
	}
	const float Y = P->Anchor.Y * 0.82f + 0.22f * Ball->GetActorLocation().Y / Pitch->HalfWidth();
	FVector Target(X * Dir * Pitch->HalfLength(), FMath::Clamp(Y, -0.95f, 0.95f) * Pitch->HalfWidth(), 0.f);
	const float Dist = FVector::Dist2D(Target, P->GetActorLocation());
	const float Speed = Dist > 900.f ? S->PlayerSprintSpeed * 0.85f : S->PlayerJogSpeed * (Dist > 300.f ? 1.f : 0.6f);
	P->MoveTowards(Target, Speed, 90.f);
	if (Dist < 120.f) P->FaceTowards(Ball->GetActorLocation(), DeltaSeconds, 4.f);
}

void ARCMatchDirector::TickCarrier(ARCFootballer* P, float DeltaSeconds)
{
	const URCSettings* S = URCSettings::Get();
	const int32 Dir = AttackDir(P->Team);
	const FVector Loc = P->GetActorLocation();
	const FVector Goal = Pitch->GoalCenter(Dir);
	const float ToGoal = FVector::Dist2D(Loc, Goal);
	const ARCFootballer* Opp = Nearest(1 - P->Team, Loc, nullptr, true);
	const float Pressure = Opp ? FVector::Dist2D(Opp->GetActorLocation(), Loc) : 5000.f;

	if (P->ActionCooldown <= 0.f && Phase == ERCMatchPhase::Play)
	{
		if (ToGoal < 2500.f && FMath::FRand() < MdAiChancePerSecond(ToGoal < 1600.f ? 1.6f : 0.6f, DeltaSeconds))
		{
			Shoot(P, false);
			return;
		}
		if ((Pressure < 380.f && FMath::FRand() < MdAiChancePerSecond(2.2f, DeltaSeconds)) || FMath::FRand() < MdAiChancePerSecond(0.35f, DeltaSeconds))
		{
			// Best option: forward progress, open space, not too far.
			ARCFootballer* Best = nullptr;
			float BestScore = -1e9f;
			for (ARCFootballer* F : Players)
			{
				if (!F || F == P || F->Team != P->Team || !F->IsAvailable() || F->Position == ERCPosition::Goalkeeper) continue;
				const FVector FL = F->GetActorLocation();
				const float D = FVector::Dist2D(FL, Loc);
				if (D < 400.f || D > 4000.f) continue;
				const ARCFootballer* Marker = Nearest(1 - P->Team, FL, nullptr, true);
				const float Space = Marker ? FMath::Min(FVector::Dist2D(Marker->GetActorLocation(), FL), 800.f) : 800.f;
				const float Rating = (FL.X - Loc.X) * Dir * 0.6f + Space * 1.2f - D * 0.25f + FMath::FRandRange(0.f, 300.f);
				if (Rating > BestScore)
				{
					BestScore = Rating;
					Best = F;
				}
			}
			if (Best)
			{
				Pass(P, Best, FVector::Dist2D(Best->GetActorLocation(), Loc) > 2600.f ? 0.5f : 0.f);
				return;
			}
		}
	}
	// Dribble towards goal, drifting away from the nearest opponent and back towards the middle near the box.
	FVector Away = FVector::ZeroVector;
	if (Opp && Pressure < 600.f) Away = (Loc - Opp->GetActorLocation()).GetSafeNormal2D() * 0.6f;
	const FVector ToG = (Goal - Loc).GetSafeNormal2D();
	FVector Dirn = (ToG + Away).GetSafeNormal2D();
	if (Dirn.IsNearlyZero()) Dirn = FVector(Dir, 0.f, 0.f);
	P->MoveTowards(Loc + Dirn * 600.f, S->PlayerJogSpeed * (1.05f + 0.25f * P->Skill), 10.f);
}

void ARCMatchDirector::Pass(ARCFootballer* From, ARCFootballer* To, float Loft)
{
	if (!From || !To) return;
	const FVector Target = To->GetActorLocation() + To->GetVelocity() * 0.6f;
	FVector D = Target - Ball->GetActorLocation();
	D.Z = 0.f;
	const float Dist = D.Size();
	const float Speed = FMath::Clamp(Dist * 1.3f + 500.f, 900.f, 2600.f);
	FVector V = D.GetSafeNormal() * Speed;
	V.Z = Loft > 0.f ? Loft * 900.f : 0.f;
	From->PlayTagged(TEXT("pass"));
	Ball->Kick(V, From);
	From->ActionCooldown = 0.6f;
}

void ARCMatchDirector::Shoot(ARCFootballer* From, bool bForceGoal)
{
	const int32 Dir = AttackDir(From->Team);
	const float HW = Pitch->GoalHalfWidth();
	const float Y = bForceGoal ? FMath::FRandRange(-HW + 60.f, HW - 60.f) : FMath::FRandRange(-HW - 150.f, HW + 150.f);
	const float Z = bForceGoal ? FMath::FRandRange(30.f, Pitch->GoalHeight() - 50.f) : FMath::FRandRange(20.f, Pitch->GoalHeight() + 80.f);
	const FVector Target(Dir * (Pitch->HalfLength() + 60.f), Y, Z);
	FVector D = Target - Ball->GetActorLocation();
	const float Speed = FMath::FRandRange(2300.f, 2900.f);
	const float T = D.Size2D() / Speed;
	FVector V = D.GetSafeNormal2D() * Speed;
	V.Z = (D.Z + 0.5f * 980.f * T * T) / FMath::Max(T, 0.05f);
	From->PlayTagged(TEXT("shot"));
	Ball->Kick(V, From);
	From->ActionCooldown = 1.f;
	if (bForceGoal)
	{
		if (ARCFootballer* GK = GoalkeeperOf(1 - From->Team)) GK->ActionCooldown = 2.f;  // beaten
	}
}

void ARCMatchDirector::TickGoalkeeper(ARCFootballer* P, float DeltaSeconds)
{
	const URCSettings* S = URCSettings::Get();
	const int32 Own = -AttackDir(P->Team);
	const FVector Goal = Pitch->GoalCenter(Own);
	const FVector B = Ball->GetActorLocation();
	if (TryPickUp(P)) return;
	// Shot coming? Try to save it.
	const FVector V = Ball->GetBallVelocity();
	if (!Ball->GetCarrier() && V.X * Own > 900.f && P->ActionCooldown <= 0.f)
	{
		const float T = (Goal.X - B.X) / V.X;
		if (T > 0.f && T < 1.4f)
		{
			const float CrossY = B.Y + V.Y * T;
			const float Reach = 230.f + 120.f * P->Skill;
			if (FMath::Abs(CrossY - P->GetActorLocation().Y) < Reach && FVector::Dist2D(B, P->GetActorLocation()) < 260.f)
			{
				P->ActionCooldown = 1.f;
				if (FMath::FRand() < 0.55f + 0.35f * P->Skill)
				{
					P->PlayTagged(TEXT("gk_dive"));
					if (FMath::FRand() < 0.5f) Ball->GiveTo(P);
					else Ball->Kick(FVector(-V.X * 0.25f, V.Y * 0.5f + FMath::FRandRange(-600.f, 600.f), 300.f), P);
					return;
				}
			}
			P->MoveTowards(FVector(Goal.X - Own * 80.f, FMath::Clamp(CrossY, -Pitch->GoalHalfWidth(), Pitch->GoalHalfWidth()), 0.f), S->PlayerSprintSpeed, 10.f);
			return;
		}
	}
	// Position on the line between ball and goal centre.
	const float BallDist = FVector::Dist2D(B, Goal);
	const float Out = FMath::Clamp(BallDist * 0.12f, 120.f, 700.f);
	const FVector ToBall = (B - Goal).GetSafeNormal2D();
	P->MoveTowards(Goal + ToBall * Out, S->PlayerJogSpeed, 30.f);
	P->FaceTowards(B, DeltaSeconds, 6.f);
	if (Ball->GetCarrier() == P && P->ActionCooldown <= 0.f)
	{
		ARCFootballer* To = nullptr;
		float Best = -1e9f;
		for (ARCFootballer* F : Players)
		{
			if (!F || F->Team != P->Team || F == P || !F->IsAvailable()) continue;
			const float Rating = F->GetActorLocation().X * -Own + FMath::FRandRange(0.f, 2000.f);
			if (Rating > Best) { Best = Rating; To = F; }
		}
		if (To) Pass(P, To, 0.55f);
	}
}

void ARCMatchDirector::TickOfficialsAI(float DeltaSeconds)
{
	const URCSettings* S = URCSettings::Get();
	if (!Ball) return;
	const FVector B = Ball->GetActorLocation();
	if (Centre && Centre != PlayerReferee)
	{
		// Diagonal system: behind and to the left of play, 15-20 m away.
		const int32 Dir = PossessionTeam() >= 0 ? AttackDir(PossessionTeam()) : 1;
		const FVector Want = Pitch->ClampToField(B + FVector(-Dir * 1300.f, (B.Y > 0.f ? -1.f : 1.f) * 700.f, 0.f), 300.f);
		const float D = FVector::Dist2D(Want, Centre->GetActorLocation());
		Centre->MoveTowards(Want, D > 1200.f ? S->RefereeSprintSpeed : S->RefereeJogSpeed, 150.f);
		Centre->FaceTowards(B, DeltaSeconds, 5.f);
	}
	for (ARCReferee* AR : {AssistantNear.Get(), AssistantFar.Get()})
	{
		if (!AR || AR == PlayerReferee) continue;
		const int32 HalfSign = AR->GetOfficial() == ERCOfficial::AssistantNear ? -1 : 1;
		const int32 Defending = AttackDir(0) == HalfSign ? 1 : 0;
		const float Line = OffsideLineX(Defending);
		const float X = HalfSign < 0 ? FMath::Min(Line, FMath::Min(B.X, 0.f)) : FMath::Max(Line, FMath::Max(B.X, 0.f));
		AR->MoveTowards(FVector(X, AR->GetActorLocation().Y, 0.f), S->RefereeSprintSpeed, 40.f);
		AR->FaceTowards(FVector(AR->GetActorLocation().X, 0.f, 0.f), DeltaSeconds, 6.f);
	}
}
