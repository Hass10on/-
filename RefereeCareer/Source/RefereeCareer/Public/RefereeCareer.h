#pragma once

#include "CoreMinimal.h"

DECLARE_LOG_CATEGORY_EXTERN(LogReferee, Log, All);

// Unreal works in centimetres; the rules and the Blender data use metres.
constexpr float RC_M = 100.f;
