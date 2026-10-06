#include "RCGameMode.h"

#include "RCPlayerController.h"

ARCGameMode::ARCGameMode()
{
	PlayerControllerClass = ARCPlayerController::StaticClass();
	DefaultPawnClass = nullptr;
	HUDClass = nullptr;
}
