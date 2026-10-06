#include "RCPlayerController.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "RCCareerSubsystem.h"
#include "RCMatchDirector.h"
#include "RCPitch.h"
#include "RCReferee.h"
#include "RefereeCareer.h"
#include "UI/RCUI.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

using namespace RCUI;

namespace
{
FString PcNum(double V, int32 Decimals = 0)
{
	return Decimals > 0 ? FString::Printf(TEXT("%.*f"), Decimals, V) : FString::FromInt(FMath::RoundToInt(V));
}

FString PcSigned(double V)
{
	const int32 R = FMath::RoundToInt(V);
	return R > 0 ? FString::Printf(TEXT("+%d"), R) : FString::FromInt(R);
}

UInputAction* PcMakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
{
	UInputAction* A = NewObject<UInputAction>(Outer, Name);
	A->ValueType = Type;
	return A;
}
}  // namespace

ARCPlayerController::ARCPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

FString ARCPlayerController::S(const std::string& Key) const
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	return Sub ? Sub->Str(Key) : URCCareerSubsystem::ToF(Key);
}

// ---------------------------------------------------------------- input

void ARCPlayerController::CreateInputActions()
{
	if (Mapping) return;
	Mapping = NewObject<UInputMappingContext>(this, TEXT("IMC_Referee"));
	MoveAction = PcMakeAction(this, TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = PcMakeAction(this, TEXT("IA_Look"), EInputActionValueType::Axis2D);
	SprintAction = PcMakeAction(this, TEXT("IA_Sprint"), EInputActionValueType::Boolean);
	FaceBallAction = PcMakeAction(this, TEXT("IA_FaceBall"), EInputActionValueType::Boolean);
	CameraAction = PcMakeAction(this, TEXT("IA_Camera"), EInputActionValueType::Boolean);
	BreatheAction = PcMakeAction(this, TEXT("IA_Breathe"), EInputActionValueType::Boolean);
	PauseAction = PcMakeAction(this, TEXT("IA_Pause"), EInputActionValueType::Boolean);
	CamPrevAction = PcMakeAction(this, TEXT("IA_CamPrev"), EInputActionValueType::Boolean);
	CamNextAction = PcMakeAction(this, TEXT("IA_CamNext"), EInputActionValueType::Boolean);
	LinesAction = PcMakeAction(this, TEXT("IA_Lines"), EInputActionValueType::Boolean);
	PlayPauseAction = PcMakeAction(this, TEXT("IA_PlayPause"), EInputActionValueType::Boolean);

	auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle = false, bool bNegateX = false, bool bNegateY = false)
	{
		FEnhancedActionKeyMapping& M = Mapping->MapKey(Action, Key);
		if (bSwizzle) M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		if (bNegateX || bNegateY)
		{
			UInputModifierNegate* N = NewObject<UInputModifierNegate>(this);
			N->bX = bNegateX;
			N->bY = bNegateY;
			N->bZ = false;
			M.Modifiers.Add(N);
		}
	};
	// Movement: WASD / arrows / left stick. Swizzle turns a 1D key into the Y axis.
	Map(MoveAction, EKeys::W, true);
	Map(MoveAction, EKeys::S, true, false, true);
	Map(MoveAction, EKeys::D);
	Map(MoveAction, EKeys::A, false, true);
	Map(MoveAction, EKeys::Up, true);
	Map(MoveAction, EKeys::Down, true, false, true);
	Map(MoveAction, EKeys::Right);
	Map(MoveAction, EKeys::Left, false, true);
	Map(MoveAction, EKeys::Gamepad_Left2D);
	Map(LookAction, EKeys::Mouse2D, false, false, true);
	Map(LookAction, EKeys::Gamepad_Right2D, false, false, true);
	Map(SprintAction, EKeys::LeftShift);
	Map(SprintAction, EKeys::Gamepad_LeftTrigger);
	Map(FaceBallAction, EKeys::RightMouseButton);
	Map(FaceBallAction, EKeys::Gamepad_LeftShoulder);
	Map(CameraAction, EKeys::V);
	Map(CameraAction, EKeys::Gamepad_RightThumbstick);
	Map(BreatheAction, EKeys::B);
	Map(BreatheAction, EKeys::Gamepad_RightShoulder);
	Map(PauseAction, EKeys::Escape);
	Map(PauseAction, EKeys::P);
	Map(PauseAction, EKeys::Gamepad_Special_Right);
	Map(CamPrevAction, EKeys::Q);
	Map(CamPrevAction, EKeys::Gamepad_DPad_Left);
	Map(CamNextAction, EKeys::E);
	Map(CamNextAction, EKeys::Gamepad_DPad_Right);
	Map(LinesAction, EKeys::L);
	Map(LinesAction, EKeys::Gamepad_LeftThumbstick);
	Map(PlayPauseAction, EKeys::SpaceBar);
	Map(PlayPauseAction, EKeys::Gamepad_RightTrigger);

	const FKey OptionKeys[6] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six};
	const FKey OptionPad[6] = {EKeys::Gamepad_FaceButton_Bottom, EKeys::Gamepad_FaceButton_Right, EKeys::Gamepad_FaceButton_Left,
		EKeys::Gamepad_FaceButton_Top, EKeys::Gamepad_DPad_Up, EKeys::Gamepad_DPad_Down};
	for (int32 i = 0; i < 6; ++i)
	{
		UInputAction* A = PcMakeAction(this, *FString::Printf(TEXT("IA_Option%d"), i + 1), EInputActionValueType::Boolean);
		Map(A, OptionKeys[i]);
		Map(A, OptionPad[i]);
		OptionActions.Add(A);
	}
}

void ARCPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	CreateInputActions();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogReferee, Error, TEXT("Enhanced Input is not the default input component (see Config/DefaultInput.ini)."));
		return;
	}
	EIC->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ARCPlayerController::OnMove);
	EIC->BindAction(MoveAction, ETriggerEvent::Completed, this, &ARCPlayerController::OnMoveStop);
	EIC->BindAction(LookAction, ETriggerEvent::Triggered, this, &ARCPlayerController::OnLook);
	EIC->BindAction(SprintAction, ETriggerEvent::Started, this, &ARCPlayerController::OnSprint);
	EIC->BindAction(SprintAction, ETriggerEvent::Completed, this, &ARCPlayerController::OnSprintStop);
	EIC->BindAction(FaceBallAction, ETriggerEvent::Started, this, &ARCPlayerController::OnFaceBall);
	EIC->BindAction(FaceBallAction, ETriggerEvent::Completed, this, &ARCPlayerController::OnFaceBallStop);
	EIC->BindAction(CameraAction, ETriggerEvent::Started, this, &ARCPlayerController::OnCamera);
	EIC->BindAction(BreatheAction, ETriggerEvent::Started, this, &ARCPlayerController::OnBreathe);
	EIC->BindAction(PauseAction, ETriggerEvent::Started, this, &ARCPlayerController::OnPause);
	EIC->BindAction(CamPrevAction, ETriggerEvent::Started, this, &ARCPlayerController::OnCamPrev);
	EIC->BindAction(CamNextAction, ETriggerEvent::Started, this, &ARCPlayerController::OnCamNext);
	EIC->BindAction(LinesAction, ETriggerEvent::Started, this, &ARCPlayerController::OnLines);
	EIC->BindAction(PlayPauseAction, ETriggerEvent::Started, this, &ARCPlayerController::OnPlayPause);
	if (OptionActions.Num() == 6)
	{
		EIC->BindAction(OptionActions[0], ETriggerEvent::Started, this, &ARCPlayerController::OnOption1);
		EIC->BindAction(OptionActions[1], ETriggerEvent::Started, this, &ARCPlayerController::OnOption2);
		EIC->BindAction(OptionActions[2], ETriggerEvent::Started, this, &ARCPlayerController::OnOption3);
		EIC->BindAction(OptionActions[3], ETriggerEvent::Started, this, &ARCPlayerController::OnOption4);
		EIC->BindAction(OptionActions[4], ETriggerEvent::Started, this, &ARCPlayerController::OnOption5);
		EIC->BindAction(OptionActions[5], ETriggerEvent::Started, this, &ARCPlayerController::OnOption6);
	}
}

void ARCPlayerController::OnOption1(const FInputActionValue& Value) { OnOption(0); }
void ARCPlayerController::OnOption2(const FInputActionValue& Value) { OnOption(1); }
void ARCPlayerController::OnOption3(const FInputActionValue& Value) { OnOption(2); }
void ARCPlayerController::OnOption4(const FInputActionValue& Value) { OnOption(3); }
void ARCPlayerController::OnOption5(const FInputActionValue& Value) { OnOption(4); }
void ARCPlayerController::OnOption6(const FInputActionValue& Value) { OnOption(5); }

void ARCPlayerController::OnMove(const FInputActionValue& Value)
{
	MoveAxis = Value.Get<FVector2D>();
	if (!InMatch() || bPaused) return;
	if (Director) Director->OnScrub(MoveAxis.X);
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->InputMove(MoveAxis);
}

void ARCPlayerController::OnMoveStop(const FInputActionValue& Value)
{
	MoveAxis = FVector2D::ZeroVector;
}

void ARCPlayerController::OnLook(const FInputActionValue& Value)
{
	if (!InMatch() || bPaused || !GetPawn()) return;
	const FVector2D V = Value.Get<FVector2D>();
	AddYawInput(V.X);
	AddPitchInput(V.Y);
}

void ARCPlayerController::OnSprint(const FInputActionValue& Value)
{
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->SetSprinting(true);
}

void ARCPlayerController::OnSprintStop(const FInputActionValue& Value)
{
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->SetSprinting(false);
}

void ARCPlayerController::OnFaceBall(const FInputActionValue& Value)
{
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->SetFaceBall(true);
}

void ARCPlayerController::OnFaceBallStop(const FInputActionValue& Value)
{
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->SetFaceBall(false);
}

void ARCPlayerController::OnCamera(const FInputActionValue& Value)
{
	if (ARCReferee* Ref = Cast<ARCReferee>(GetPawn())) Ref->ToggleCameraMode();
}

void ARCPlayerController::OnBreathe(const FInputActionValue& Value)
{
	if (InMatch() && !bPaused && Director) Director->OnBreathe();
}

void ARCPlayerController::OnCamPrev(const FInputActionValue& Value)
{
	if (InMatch() && Director) Director->OnCycleCamera(-1);
}

void ARCPlayerController::OnCamNext(const FInputActionValue& Value)
{
	if (InMatch() && Director) Director->OnCycleCamera(1);
}

void ARCPlayerController::OnLines(const FInputActionValue& Value)
{
	if (InMatch() && Director) Director->OnToggleLines();
}

void ARCPlayerController::OnPlayPause(const FInputActionValue& Value)
{
	if (InMatch() && Director) Director->OnTogglePlayback();
}

void ARCPlayerController::OnOption(int32 Index)
{
	if (InMatch() && !bPaused && Director) Director->OnOption(Index);
}

void ARCPlayerController::OnPause(const FInputActionValue& Value)
{
	if (!InMatch()) return;
	if (bPaused)
	{
		bPaused = false;
		SetPause(false);
		SetScreen(SNew(SRCMatchHud).Director(Director.Get()), true);
	}
	else
	{
		ShowPause();
	}
}

// ---------------------------------------------------------------- lifecycle

void ARCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController()) return;
	if (UEnhancedInputLocalPlayerSubsystem* EIS = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
	{
		CreateInputActions();
		EIS->AddMappingContext(Mapping, 0);
	}
	SAssignNew(Root, SBox);
	RootWidget = Root;
	if (GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->AddViewportWidgetContent(RootWidget.ToSharedRef(), 10);
	}
	EnsureBackdrop();
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	if (!Sub || !Sub->IsContentLoaded())
	{
		ShowError(Sub ? Sub->GetLoadError() : FString(TEXT("Career subsystem missing")));
		return;
	}
	ShowTitle();
}

void ARCPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (GEngine && GEngine->GameViewport && RootWidget.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(RootWidget.ToSharedRef());
	}
	Root.Reset();
	RootWidget.Reset();
	Super::EndPlay(Reason);
}

void ARCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	if (InMatch())
	{
		if (Director->IsFinished()) EndMatch();
		return;
	}
	UpdateBackdropCamera(DeltaTime);
}

bool ARCPlayerController::InMatch() const
{
	return Director != nullptr;
}

void ARCPlayerController::SetScreen(const TSharedRef<SWidget>& Widget, bool bGameInput)
{
	if (!Root.IsValid()) return;
	Root->SetContent(Widget);
	if (bGameInput)
	{
		FInputModeGameOnly Mode;
		SetInputMode(Mode);
		bShowMouseCursor = false;
	}
	else
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Widget);
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		SetInputMode(Mode);
		bShowMouseCursor = true;
	}
}

void ARCPlayerController::EnsureBackdrop()
{
	if (!BackdropCamera)
	{
		BackdropCamera = GetWorld()->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), FTransform(FVector(-7000.f, -7000.f, 3000.f)));
		if (BackdropCamera) BackdropCamera->GetCameraComponent()->SetFieldOfView(55.f);
	}
	if (!Backdrop)
	{
		Backdrop = GetWorld()->SpawnActor<ARCPitch>(ARCPitch::StaticClass(), FTransform::Identity);
		if (Backdrop) Backdrop->Build(105.f, 68.f, TEXT("stadium"), 0.6f, FLinearColor(0.6f, 0.03f, 0.05f), FLinearColor(0.04f, 0.15f, 0.55f), true);
	}
	if (BackdropCamera) SetViewTarget(BackdropCamera);
}

void ARCPlayerController::UpdateBackdropCamera(float DeltaTime)
{
	if (!BackdropCamera) return;
	BackdropAngle += DeltaTime * 3.f;
	const float Rad = FMath::DegreesToRadians(BackdropAngle);
	const FVector Pos(FMath::Cos(Rad) * 7800.f, FMath::Sin(Rad) * 5600.f, 2400.f);
	BackdropCamera->SetActorLocationAndRotation(Pos, (FVector(0.f, 0.f, 300.f) - Pos).Rotation());
}

// ---------------------------------------------------------------- match

void ARCPlayerController::StartMatch()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	if (!Sub || !Sub->BeginMatch())
	{
		ShowCareerPhase();
		return;
	}
	if (Backdrop)
	{
		Backdrop->Destroy();
		Backdrop = nullptr;
	}
	Director = GetWorld()->SpawnActor<ARCMatchDirector>(ARCMatchDirector::StaticClass(), FTransform::Identity);
	if (!Director || !Director->StartMatch(this))
	{
		ShowError(TEXT("Could not start the match."));
		return;
	}
	bPaused = false;
	SetScreen(SNew(SRCMatchHud).Director(Director.Get()), true);
}

void ARCPlayerController::EndMatch()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	if (Sub) Sub->FinishMatch();
	if (GetPawn()) UnPossess();
	Backdrop = Director->DetachPitch();
	Director->Destroy();
	Director = nullptr;
	SetPause(false);
	bPaused = false;
	EnsureBackdrop();
	ShowCareerPhase();
}

// ---------------------------------------------------------------- screens

void ARCPlayerController::ShowError(const FString& Message)
{
	SetScreen(Dialog(T(TEXT("Referee Career")), T(TEXT("Could not load the game data (Content/Data)")), T(Message), {}));
}

void ARCPlayerController::ShowTitle()
{
	const FStyle& St = Style();
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	TArray<FChoice> Choices;
	if (Sub->HasSave())
	{
		FChoice C;
		C.Label = T(S("ui.title.continue"));
		C.bPrimary = true;
		C.OnClick = [this]()
		{
			URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
			if (Sub2 && Sub2->LoadCareer()) ShowCareerPhase();
			else ShowNewCareer();
		};
		Choices.Add(C);
	}
	FChoice New;
	New.Label = T(S("ui.title.new"));
	New.bPrimary = !Sub->HasSave();
	New.OnClick = [this]() { ShowNewCareer(); };
	Choices.Add(New);
	FChoice Quit;
	Quit.Label = T(S("ui.title.quit"));
	Quit.OnClick = [this]() { UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false); };
	Choices.Add(Quit);

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Text(T(S("ui.title.eyebrow")), St.Small, St.Muted)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)[Text(T(S("ui.title.name")), St.Display, St.Chalk)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 18.f)[Text(T(S("ui.title.pitch")), St.Body, St.Chalk)]
		+ SVerticalBox::Slot().AutoHeight()[ChoiceList(Choices)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 18.f, 0.f, 0.f)[Text(T(S("ui.controls.full")), St.Small, St.Muted)];
	SetScreen(Card(Body, 720.f));
}

void ARCPlayerController::ShowNewCareer()
{
	const FStyle& St = Style();
	TArray<FChoice> Roles;
	for (refcore::Role R : {refcore::Role::Center, refcore::Role::Assistant})
	{
		FChoice C;
		C.Label = T(S(std::string("role.") + refcore::roleId(R)));
		C.Hint = T(S(std::string("ui.role_desc.") + refcore::roleId(R)));
		C.bSelected = PendingRole == R;
		C.OnClick = [this, R]() { PendingRole = R; ShowNewCareer(); };
		Roles.Add(C);
	}
	FChoice Start;
	Start.Label = T(S("ui.new.start"));
	Start.bPrimary = true;
	Start.OnClick = [this]()
	{
		URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
		Sub->NewCareer(PendingName.TrimStartAndEnd(), PendingRole);
		PreferredRole = PendingRole;
		PlannedActivities.Reset();
		ShowCareerPhase();
	};
	FChoice Back;
	Back.Label = T(S("ui.back"));
	Back.OnClick = [this]() { ShowTitle(); };

	TSharedRef<SWidget> Body = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Text(T(S("ui.new.title")), St.Title, St.Chalk)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(S("ui.new.name")), St.Small, St.Muted)]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SEditableTextBox)
			.Font(St.Body)
			.Text(T(PendingName))
			.HintText(T(S("ui.new.name_hint")))
			.OnTextChanged_Lambda([this](const FText& NewText) { PendingName = NewText.ToString(); })
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 14.f, 0.f, 4.f)[Text(T(S("ui.new.role")), St.Small, St.Muted)]
		+ SVerticalBox::Slot().AutoHeight()[ChoiceList(Roles)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 12.f)[Text(T(S("ui.new.var_note")), St.Small, St.Muted)]
		+ SVerticalBox::Slot().AutoHeight()[ChoiceList({Start, Back})];
	SetScreen(Card(Body, 720.f));
}

void ARCPlayerController::ShowCareerPhase()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub ? Sub->GetCareer() : nullptr;
	if (!C)
	{
		ShowTitle();
		return;
	}
	switch (C->phase())
	{
	case refcore::Phase::SeasonStart: ShowSeasonStart(); break;
	case refcore::Phase::Planning: ShowPlanning(); break;
	case refcore::Phase::Event: ShowEvent(); break;
	case refcore::Phase::Appointment: ShowAppointment(); break;
	case refcore::Phase::Match: StartMatch(); break;
	case refcore::Phase::PostMatch: ShowReport(); break;
	case refcore::Phase::Press: ShowPress(); break;
	case refcore::Phase::SeasonReview: ShowSeasonReview(); break;
	case refcore::Phase::Over: ShowEnding(); break;
	}
}

void ARCPlayerController::ShowSeasonStart()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	FChoice Go;
	Go.Label = T(S("ui.season.start"));
	Go.bPrimary = true;
	Go.OnClick = [this]()
	{
		URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
		Sub2->GetCareer()->confirmSeasonStart();
		Sub2->SaveCareer();
		ShowCareerPhase();
	};
	SetScreen(Dialog(T(URCCareerSubsystem::ToF(C->name())), T(Sub->Fmt("ui.season.title", {{"season", FString::FromInt(C->season())}})),
		T(URCCareerSubsystem::ToF(C->seasonIntro())), {Go}));
}

void ARCPlayerController::ShowPlanning()
{
	const FStyle& St = Style();
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const refcore::Content& Content = Sub->GetContent();

	// ---- left column: who you are and how the federation sees you
	TSharedRef<SVerticalBox> Left = SNew(SVerticalBox);
	Left->AddSlot().AutoHeight()[Text(T(URCCareerSubsystem::ToF(C->name())), St.Title, St.Chalk)];
	Left->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)[Text(T(Sub->Fmt("ui.hub.subtitle", {
		{"age", FString::FromInt(C->age())},
		{"tier", URCCareerSubsystem::ToF(C->tier().name)},
		{"season", FString::FromInt(C->season())},
		{"week", FString::FromInt(C->week())},
		{"weeks", FString::FromInt(Content.weeksPerSeason)},
		{"money", PcNum(C->money())}})), St.Small, St.Muted)];
	Left->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Text(T(S("ui.hub.reputation")), St.BodyBold, St.Yellow)];
	const refcore::Stat Order[] = {refcore::Stat::Fairness, refcore::Stat::Personality, refcore::Stat::Trust, refcore::Stat::Fitness,
		refcore::Stat::Laws, refcore::Stat::Media, refcore::Stat::Mental};
	for (int32 i = 0; i < 7; ++i)
	{
		const refcore::Stat St7 = Order[i];
		const double V = C->stats().get(St7);
		const FLinearColor Col = i < 3 ? St.Yellow : St.Grass;
		Left->AddSlot().AutoHeight().Padding(0.f, 2.f)[StatBar(T(S(std::string("stat.") + refcore::statId(St7))), static_cast<float>(V / 100.0), T(PcNum(V)), Col)];
	}
	Left->AddSlot().AutoHeight().Padding(0.f, 8.f, 0.f, 2.f)[StatBar(T(S("ui.hub.app_index")), static_cast<float>(C->appointmentIndex() / 100.0),
		T(PcNum(C->appointmentIndex())), FLinearColor(0.3f, 0.7f, 1.f))];
	Left->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[Text(T(S("ui.hub.app_index_hint")), St.Small, St.Muted)];
	// Career ladder
	Left->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 4.f)[Text(T(S("ui.hub.ladder")), St.BodyBold, St.Yellow)];
	for (int32 t = static_cast<int32>(Content.tiers.size()) - 1; t >= 0; --t)
	{
		const bool bNow = t == C->tierIndex();
		const bool bDone = t < C->tierIndex();
		const FString Mark = bNow ? TEXT("\u25C6 ") : bDone ? TEXT("\u2713 ") : TEXT("\u25CB ");
		Left->AddSlot().AutoHeight()[Text(T(Mark + URCCareerSubsystem::ToF(Content.tiers[static_cast<size_t>(t)].name)), bNow ? St.BodyBold : St.Small,
			bNow ? St.Yellow : bDone ? St.Muted : St.Chalk, false)];
	}
	// News
	Left->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(S("ui.hub.news")), St.BodyBold, St.Yellow)];
	for (size_t n = 0; n < C->news().size() && n < 5; ++n)
	{
		Left->AddSlot().AutoHeight().Padding(0.f, 1.f)[Text(T(URCCareerSubsystem::ToF(C->news()[n])), St.Small, St.Chalk)];
	}

	// ---- right column: this week's plan
	TSharedRef<SVerticalBox> Right = SNew(SVerticalBox);
	Right->AddSlot().AutoHeight()[Text(T(S("ui.plan.title")), St.Title, St.Chalk)];
	Right->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 8.f)[Text(T(Sub->Fmt("ui.plan.slots", {
		{"used", FString::FromInt(PlannedActivities.Num())}, {"slots", FString::FromInt(C->slots())}})), St.Small, St.Muted)];
	TArray<FChoice> Acts;
	for (const refcore::ActivityStatus& A : C->activities())
	{
		const FString Id = URCCareerSubsystem::ToF(A.def->id);
		FChoice Ch;
		Ch.Label = T(URCCareerSubsystem::ToF(A.def->name) + (A.def->cost > 0 ? FString::Printf(TEXT("  (%s)"), *PcNum(A.def->cost)) : FString()));
		Ch.Hint = T(A.available ? URCCareerSubsystem::ToF(A.def->desc) : URCCareerSubsystem::ToF(A.reason));
		Ch.bEnabled = A.available;
		Ch.bSelected = PlannedActivities.Contains(Id);
		Ch.OnClick = [this, Id, Slots = C->slots()]()
		{
			if (PlannedActivities.Contains(Id)) PlannedActivities.Remove(Id);
			else if (PlannedActivities.Num() < Slots) PlannedActivities.Add(Id);
			ShowPlanning();
		};
		Acts.Add(Ch);
	}
	Right->AddSlot().AutoHeight()[ChoiceList(Acts)];
	Right->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(S("ui.plan.role")), St.BodyBold, St.Yellow)];
	TArray<FChoice> Roles;
	for (refcore::Role R : {refcore::Role::Center, refcore::Role::Assistant, refcore::Role::Var})
	{
		std::string Why;
		FChoice Ch;
		Ch.Label = T(S(std::string("role.") + refcore::roleId(R)));
		Ch.bEnabled = C->canWorkAs(R, &Why);
		Ch.Hint = Ch.bEnabled ? FText::GetEmpty() : T(URCCareerSubsystem::ToF(Why));
		Ch.bSelected = PreferredRole == R;
		Ch.OnClick = [this, R]() { PreferredRole = R; ShowPlanning(); };
		Roles.Add(Ch);
	}
	Right->AddSlot().AutoHeight()[ChoiceList(Roles)];
	FChoice Confirm;
	Confirm.Label = T(S("ui.plan.confirm"));
	Confirm.bPrimary = true;
	Confirm.OnClick = [this]()
	{
		URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
		std::vector<std::string> Ids;
		for (const FString& Id : PlannedActivities) Ids.push_back(URCCareerSubsystem::ToStd(Id));
		const std::vector<std::string> Log = Sub2->GetCareer()->commitWeek(Ids, PreferredRole);
		PlannedActivities.Reset();
		Sub2->SaveCareer();
		TArray<FString> Lines;
		for (const std::string& L : Log) Lines.Add(URCCareerSubsystem::ToF(L));
		ShowWeekLog(Lines);
	};
	FChoice Quit;
	Quit.Label = T(S("ui.save_quit"));
	Quit.OnClick = [this]() { URCCareerSubsystem::Get(this)->SaveCareer(); ShowTitle(); };
	Right->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)[ChoiceList({Confirm, Quit})];

	TSharedRef<SWidget> Body = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0.f, 0.f, 24.f, 0.f)[Left]
		+ SHorizontalBox::Slot().FillWidth(1.1f)[Right];
	SetScreen(Card(Body, 1240.f));
}

void ARCPlayerController::ShowWeekLog(const TArray<FString>& Lines)
{
	FString Body;
	for (const FString& L : Lines) Body += L + TEXT("\n");
	if (Body.IsEmpty()) Body = S("ui.week.rest");
	FChoice Next;
	Next.Label = T(S("ui.continue"));
	Next.bPrimary = true;
	Next.OnClick = [this]() { ShowCareerPhase(); };
	SetScreen(Dialog(T(S("ui.week.eyebrow")), T(S("ui.week.title")), T(Body.TrimEnd()), {Next}));
}

void ARCPlayerController::ShowEvent()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const refcore::EventDef* E = C->currentEvent();
	if (!E)
	{
		ShowCareerPhase();
		return;
	}
	const std::vector<refcore::EventChoiceStatus> Status = C->eventChoices();
	TArray<FChoice> Choices;
	for (size_t i = 0; i < E->choices.size(); ++i)
	{
		FChoice Ch;
		Ch.Label = T(URCCareerSubsystem::ToF(E->choices[i].text));
		Ch.bEnabled = Status[i].available;
		if (!Ch.bEnabled) Ch.Hint = T(URCCareerSubsystem::ToF(Status[i].reason));
		const int32 Index = static_cast<int32>(i);
		const FString Title = URCCareerSubsystem::ToF(E->title);
		Ch.OnClick = [this, Index, Title]()
		{
			URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
			const FString Result = URCCareerSubsystem::ToF(Sub2->GetCareer()->chooseEvent(Index));
			Sub2->SaveCareer();
			ShowResult(Title, Result);
		};
		Choices.Add(Ch);
	}
	SetScreen(Dialog(T(S("ui.event.eyebrow")), T(URCCareerSubsystem::ToF(E->title)),
		T(URCCareerSubsystem::ToF(refcore::Content::substitute(E->text, {{"name", C->name()}}))), Choices));
}

void ARCPlayerController::ShowResult(const FString& Title, const FString& Body)
{
	FChoice Next;
	Next.Label = T(S("ui.continue"));
	Next.bPrimary = true;
	Next.OnClick = [this]() { ShowCareerPhase(); };
	SetScreen(Dialog(FText::GetEmpty(), T(Title), T(Body), {Next}));
}

void ARCPlayerController::ShowAppointment()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const refcore::Appointment& A = C->appointment();
	FString Body = URCCareerSubsystem::ToF(A.reason);
	if (!A.blocked.empty()) Body += TEXT("\n\n") + URCCareerSubsystem::ToF(A.blocked);
	if (!A.otherFixtures.empty())
	{
		Body += TEXT("\n\n") + S("ui.app.others");
		for (const std::string& F : A.otherFixtures) Body += TEXT("\n") + URCCareerSubsystem::ToF(F);
	}
	if (C->hasFlag("fix_accepted") && A.appointed)
	{
		Body += TEXT("\n\n") + S("ui.app.fix_warning");
	}
	FChoice Go;
	Go.bPrimary = true;
	Go.Label = T(S(A.appointed ? "ui.app.go" : "ui.continue"));
	Go.OnClick = [this]()
	{
		URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
		const bool bMatch = Sub2->GetCareer()->appointment().appointed;
		Sub2->GetCareer()->acceptAppointment();
		if (!bMatch) Sub2->SaveCareer();
		ShowCareerPhase();
	};
	const FString Title = A.appointed ? URCCareerSubsystem::ToF(C->matchSetup().fixtureName) : S("ui.app.none_title");
	SetScreen(Dialog(T(Sub->Fmt("ui.app.eyebrow", {{"week", FString::FromInt(C->week())}, {"tier", URCCareerSubsystem::ToF(C->tier().competition)}})),
		T(Title), T(Body), {Go}));
}

void ARCPlayerController::ShowReport()
{
	const FStyle& St = Style();
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const refcore::MatchReport& R = C->lastReport();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight()[Text(T(S("ui.report.eyebrow")), St.Small, St.Muted)];
	Box->AddSlot().AutoHeight()
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()[Text(T(PcNum(R.mark, 1)), St.Display, R.kmiErrors > 0 ? St.Red : St.Grass, false)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f)[Text(T(URCCareerSubsystem::ToF(R.grade)), St.Title, St.Chalk, false)]
	];
	Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 10.f)[Text(T(Sub->Fmt("ui.report.summary", {
		{"kmi", FString::FromInt(R.kmiCorrect)}, {"kmitotal", FString::FromInt(R.kmiTotal)},
		{"y", FString::FromInt(R.yellowCards)}, {"r", FString::FromInt(R.redCards)},
		{"score", FString::Printf(TEXT("%d - %d"), R.score[0], R.score[1])}})), St.Body, St.Chalk)];
	struct FPart { const char* Key; double Value; };
	const FPart Parts[] = {{"ui.report.decisions", R.decisionScore}, {"ui.report.positioning", R.positioningScore},
		{"ui.report.control", R.controlScore}, {"ui.report.fitness", R.fitnessScore}, {"ui.report.personality", R.personalityScore}};
	for (const FPart& P : Parts)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 2.f)[StatBar(T(S(P.Key)), static_cast<float>(P.Value), T(PcNum(P.Value * 100.0)), St.Grass)];
	}
	Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(S("ui.report.calls")), St.BodyBold, St.Yellow)];
	for (const refcore::IncidentLog& L : R.incidents)
	{
		const FString Mark = L.acceptable ? TEXT("\u2713") : TEXT("\u2717");
		const FString Line = FString::Printf(TEXT("%s %d' %s%s \u2014 %s"), *Mark, static_cast<int32>(L.minute) + 1, L.kmi ? *S("ui.report.kmi") : TEXT(""),
			*URCCareerSubsystem::ToF(L.name), *URCCareerSubsystem::ToF(L.chosenLabel));
		Box->AddSlot().AutoHeight()[Text(T(Line), St.Small, L.acceptable ? St.Chalk : St.Red)];
	}
	Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(S("ui.report.assessor")), St.BodyBold, St.Yellow)];
	for (const std::string& N : R.assessorNotes)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 1.f)[Text(T(URCCareerSubsystem::ToF(N)), St.Small, St.Chalk)];
	}
	FString Deltas;
	for (int32 i = 0; i < refcore::kStatCount; ++i)
	{
		const double D = R.statDeltas[static_cast<size_t>(i)];
		if (FMath::Abs(D) >= 0.5) Deltas += S(std::string("stat.") + refcore::statId(static_cast<refcore::Stat>(i))) + TEXT(" ") + PcSigned(D) + TEXT("   ");
	}
	Box->AddSlot().AutoHeight().Padding(0.f, 12.f, 0.f, 4.f)[Text(T(Deltas), St.Small, St.Muted)];
	for (const std::string& N : C->lastMatchNews())
	{
		Box->AddSlot().AutoHeight()[Text(T(URCCareerSubsystem::ToF(N)), St.Small, St.Chalk)];
	}
	FChoice Next;
	Next.Label = T(S("ui.continue"));
	Next.bPrimary = true;
	Next.OnClick = [this]()
	{
		URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
		Sub2->GetCareer()->continueAfterReport();
		Sub2->SaveCareer();
		ShowCareerPhase();
	};
	Box->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 0.f)[ChoiceList({Next})];
	SetScreen(Card(Box, 900.f));
}

void ARCPlayerController::ShowPress()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const int32 Index = C->pressIndex();
	if (Index >= static_cast<int32>(C->pressQuestions().size()))
	{
		ShowCareerPhase();
		return;
	}
	const refcore::PressQuestion& Q = C->pressQuestions()[static_cast<size_t>(Index)];
	TArray<FChoice> Answers;
	for (size_t i = 0; i < Q.answers.size(); ++i)
	{
		FChoice Ch;
		Ch.Label = T(URCCareerSubsystem::ToF(Q.answers[i].text));
		const int32 A = static_cast<int32>(i);
		Ch.OnClick = [this, A]()
		{
			URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
			const FString Result = URCCareerSubsystem::ToF(Sub2->GetCareer()->answerPress(A));
			Sub2->SaveCareer();
			ShowResult(S("ui.press.title"), Result);
		};
		Answers.Add(Ch);
	}
	SetScreen(Dialog(T(Sub->Fmt("ui.press.eyebrow", {{"n", FString::FromInt(Index + 1)}, {"total", FString::FromInt(static_cast<int32>(C->pressQuestions().size()))}})),
		T(S("ui.press.title")), T(URCCareerSubsystem::ToF(Q.text)), Answers));
}

void ARCPlayerController::ShowSeasonReview()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	const refcore::SeasonReview& R = C->review();
	FString Body = URCCareerSubsystem::ToF(R.text) + TEXT("\n");
	for (const std::string& Line : R.checks) Body += TEXT("\n") + URCCareerSubsystem::ToF(Line);
	FChoice Next;
	Next.Label = T(S("ui.continue"));
	Next.bPrimary = true;
	Next.OnClick = [this]()
	{
		URCCareerSubsystem* Sub2 = URCCareerSubsystem::Get(this);
		Sub2->GetCareer()->continueAfterReview();
		Sub2->SaveCareer();
		ShowCareerPhase();
	};
	SetScreen(Dialog(T(Sub->Fmt("ui.season.end", {{"season", FString::FromInt(C->season())}})), T(URCCareerSubsystem::ToF(R.title)), T(Body), {Next}));
}

void ARCPlayerController::ShowEnding()
{
	URCCareerSubsystem* Sub = URCCareerSubsystem::Get(this);
	refcore::Career* C = Sub->GetCareer();
	FChoice New;
	New.Label = T(S("ui.title.new"));
	New.bPrimary = true;
	New.OnClick = [this]() { ShowNewCareer(); };
	FChoice Title;
	Title.Label = T(S("ui.back"));
	Title.OnClick = [this]() { ShowTitle(); };
	SetScreen(Dialog(T(S("ui.ending.eyebrow")), T(URCCareerSubsystem::ToF(C->endingTitle())), T(URCCareerSubsystem::ToF(C->endingText())), {New, Title}));
}

void ARCPlayerController::ShowPause()
{
	bPaused = true;
	SetPause(true);
	FChoice Resume;
	Resume.Label = T(S("ui.pause.resume"));
	Resume.bPrimary = true;
	Resume.OnClick = [this]()
	{
		bPaused = false;
		SetPause(false);
		SetScreen(SNew(SRCMatchHud).Director(Director.Get()), true);
	};
	FChoice Leave;
	Leave.Label = T(S("ui.pause.leave"));
	Leave.Hint = T(S("ui.pause.leave_hint"));
	Leave.OnClick = [this]()
	{
		SetPause(false);
		EndMatch();
	};
	SetScreen(Dialog(FText::GetEmpty(), T(S("ui.pause.title")), T(S("ui.controls.full")), {Resume, Leave}));
}
