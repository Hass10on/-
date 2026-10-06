#include "RCFootballer.h"

#include "GameFramework/CharacterMovementComponent.h"

ARCFootballer::ARCFootballer()
{
	GetCharacterMovement()->MaxWalkSpeed = 480.f;
}
