#pragma once

#include "CoreMinimal.h"
#include "RCHumanCharacter.h"
#include "RCFootballer.generated.h"

UENUM()
enum class ERCPosition : uint8 { Goalkeeper, Defender, Midfielder, Forward };

/** What the match director currently wants from a player. */
UENUM()
enum class ERCPlayerMode : uint8 { Play, Scripted, Protest, LeavingPitch, Celebrate, Idle };

/** An outfield player or goalkeeper. All decisions are made by ARCMatchDirector; this class holds state. */
UCLASS()
class REFEREECAREER_API ARCFootballer : public ARCHumanCharacter
{
	GENERATED_BODY()

public:
	ARCFootballer();

	int32 Team = 0;
	int32 Number = 1;
	ERCPosition Position = ERCPosition::Midfielder;
	/** Formation slot in "attacking" space: X -1 (own goal) .. +1 (opponent goal), Y -1..1 across the pitch. */
	FVector2D Anchor = FVector2D::ZeroVector;
	ERCPlayerMode Mode = ERCPlayerMode::Play;
	int32 Yellows = 0;
	bool bSentOff = false;
	float Skill = 0.5f;
	float ActionCooldown = 0.f;
	/** Scripted target (incident staging, set pieces). */
	FVector ScriptTarget = FVector::ZeroVector;
	float ScriptSpeed = 600.f;

	bool IsAvailable() const { return !bSentOff && !IsDown() && Mode != ERCPlayerMode::LeavingPitch; }
};
