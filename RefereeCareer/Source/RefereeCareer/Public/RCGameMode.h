#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RCGameMode.generated.h"

/** Global game mode: no default pawn (the controller possesses an official when a match starts). */
UCLASS()
class REFEREECAREER_API ARCGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARCGameMode();
};
