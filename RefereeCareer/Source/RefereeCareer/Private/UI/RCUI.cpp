#include "UI/RCUI.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "Fonts/CompositeFont.h"
#include "Misc/Paths.h"
#include "RCCareerSubsystem.h"
#include "RCMatchDirector.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace RCUI
{
namespace
{
FSlateFontInfo UiFont(const TCHAR* File, float Size, const char* FallbackStyle)
{
	const FString Path = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Fonts"), File);
	if (FPaths::FileExists(Path))
	{
		TSharedPtr<const FCompositeFont> Font = MakeShared<FStandaloneCompositeFont>(NAME_None, Path, EFontHinting::Default, EFontLoadingPolicy::LazyLoad);
		return FSlateFontInfo(Font, Size);
	}
	return FCoreStyle::GetDefaultFontStyle(FName(FallbackStyle), Size);
}

FSlateBrush UiRounded(const FLinearColor& Color, float Radius, const FLinearColor& Outline = FLinearColor::Transparent, float OutlineWidth = 0.f)
{
	return FSlateRoundedBoxBrush(Color, Radius, Outline, OutlineWidth);
}

FStyle MakeStyle()
{
	FStyle S;
	S.Body = UiFont(TEXT("IBMPlexSansArabic-Regular.ttf"), 15.f, "Regular");
	S.BodyBold = UiFont(TEXT("IBMPlexSansArabic-Bold.ttf"), 15.f, "Bold");
	S.Small = UiFont(TEXT("IBMPlexSansArabic-Regular.ttf"), 12.f, "Regular");
	S.Title = UiFont(TEXT("ReemKufi-Bold.ttf"), 24.f, "Bold");
	S.Display = UiFont(TEXT("ReemKufi-Bold.ttf"), 44.f, "Bold");
	S.Huge = UiFont(TEXT("ReemKufi-Bold.ttf"), 72.f, "Bold");
	S.Panel = UiRounded(FLinearColor(0.02f, 0.04f, 0.03f, 0.94f), 6.f, FLinearColor(0.17f, 0.26f, 0.21f), 1.f);
	S.PanelSoft = UiRounded(FLinearColor(0.02f, 0.04f, 0.03f, 0.72f), 6.f);
	S.Line = FSlateColorBrush(FLinearColor(0.17f, 0.26f, 0.21f));
	S.White = FSlateColorBrush(FLinearColor::White);
	S.ChipYellow = UiRounded(S.Yellow, 3.f);
	S.ChipRed = UiRounded(S.Red, 3.f);
	S.BarBack = UiRounded(FLinearColor(0.01f, 0.02f, 0.015f), 3.f, FLinearColor(0.17f, 0.26f, 0.21f), 1.f);
	const FSlateBrush Normal = UiRounded(FLinearColor(0.05f, 0.09f, 0.07f), 5.f, FLinearColor(0.17f, 0.26f, 0.21f), 1.f);
	const FSlateBrush Hover = UiRounded(FLinearColor(0.07f, 0.13f, 0.09f), 5.f, S.Grass, 1.f);
	const FSlateBrush Pressed = UiRounded(FLinearColor(0.04f, 0.07f, 0.05f), 5.f, S.Grass, 1.f);
	const FSlateBrush Disabled = UiRounded(FLinearColor(0.03f, 0.04f, 0.035f, 0.6f), 5.f, FLinearColor(0.1f, 0.13f, 0.11f), 1.f);
	S.Button = FButtonStyle().SetNormal(Normal).SetHovered(Hover).SetPressed(Pressed).SetDisabled(Disabled)
		.SetNormalPadding(FMargin(14.f, 9.f)).SetPressedPadding(FMargin(14.f, 10.f, 14.f, 8.f));
	const FSlateBrush PNormal = UiRounded(S.Chalk, 5.f);
	const FSlateBrush PHover = UiRounded(FLinearColor::White, 5.f);
	S.Primary = FButtonStyle(S.Button).SetNormal(PNormal).SetHovered(PHover).SetPressed(PNormal);
	const FSlateBrush SNormal = UiRounded(FLinearColor(0.06f, 0.2f, 0.1f), 5.f, S.Grass, 2.f);
	S.Selected = FButtonStyle(S.Button).SetNormal(SNormal).SetHovered(SNormal).SetPressed(SNormal);
	return S;
}
}  // namespace

const FStyle& Style()
{
	static FStyle Instance = MakeStyle();
	return Instance;
}

FText T(const FString& S)
{
	return FText::FromString(S);
}

TSharedRef<SWidget> Text(const FText& InText, const FSlateFontInfo& Font, const FLinearColor& Color, bool bWrap)
{
	return SNew(STextBlock).Text(InText).Font(Font).ColorAndOpacity(FSlateColor(Color)).AutoWrapText(bWrap);
}

TSharedRef<SWidget> Button(const FChoice& Choice)
{
	const FStyle& S = Style();
	TFunction<void()> Click = Choice.OnClick;
	const FButtonStyle* BS = Choice.bPrimary ? &S.Primary : Choice.bSelected ? &S.Selected : &S.Button;
	const FLinearColor TextColor = Choice.bPrimary ? FLinearColor(0.02f, 0.05f, 0.03f) : (Choice.bEnabled ? S.Chalk : S.Muted);
	TSharedRef<SVerticalBox> Inner = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Text(Choice.Label, Choice.bPrimary ? S.BodyBold : S.Body, TextColor)];
	if (!Choice.Hint.IsEmpty())
	{
		Inner->AddSlot().AutoHeight().Padding(0.f, 2.f, 0.f, 0.f)[Text(Choice.Hint, S.Small, S.Muted)];
	}
	return SNew(SButton)
		.ButtonStyle(BS)
		.IsEnabled(Choice.bEnabled)
		.OnClicked_Lambda([Click]() {
			if (Click) Click();
			return FReply::Handled();
		})
		[Inner];
}

TSharedRef<SWidget> ChoiceList(const TArray<FChoice>& Choices)
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const FChoice& C : Choices)
	{
		Box->AddSlot().AutoHeight().Padding(0.f, 4.f)[Button(C)];
	}
	return Box;
}

TSharedRef<SWidget> StatBar(const FText& Label, float Value01, const FText& ValueText, const FLinearColor& Color)
{
	const FStyle& S = Style();
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(130.f)[Text(Label, S.Small, S.Chalk, false)]
		]
		+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8.f, 0.f)
		[
			SNew(SBox).HeightOverride(9.f)
			[
				SNew(SProgressBar).Percent(TOptional<float>(FMath::Clamp(Value01, 0.f, 1.f))).FillColorAndOpacity(FSlateColor(Color))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(40.f)[Text(ValueText, S.Small, S.Chalk, false)]
		];
}

TSharedRef<SWidget> RightToLeft(const TSharedRef<SWidget>& Content)
{
	return SNew(SBox).FlowDirectionPreference(EFlowDirectionPreference::RightToLeft)[Content];
}

TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Content, float Width)
{
	const FStyle& S = Style();
	return RightToLeft(
		SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SImage).Image(&S.White).ColorAndOpacity(FLinearColor(0.f, 0.01f, 0.005f, 0.55f))]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center).Padding(16.f)
		[
			SNew(SBox).WidthOverride(Width).MaxDesiredHeight(860.f)
			[
				SNew(SBorder).BorderImage(&S.Panel).Padding(FMargin(28.f, 24.f))
				[
					SNew(SScrollBox) + SScrollBox::Slot()[Content]
				]
			]
		]);
}

TSharedRef<SWidget> Dialog(const FText& Eyebrow, const FText& Title, const FText& Body, const TArray<FChoice>& Choices, float Width)
{
	const FStyle& S = Style();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	if (!Eyebrow.IsEmpty()) Box->AddSlot().AutoHeight()[Text(Eyebrow, S.Small, S.Muted)];
	if (!Title.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0.f, 4.f, 0.f, 10.f)[Text(Title, S.Title, S.Chalk)];
	if (!Body.IsEmpty()) Box->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)[Text(Body, S.Body, S.Chalk)];
	Box->AddSlot().AutoHeight()[ChoiceList(Choices)];
	return Card(Box, Width);
}
}  // namespace RCUI

// ---------------------------------------------------------------- minimap

void SRCMinimap::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
}

int32 SRCMinimap::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const RCUI::FStyle& S = RCUI::Style();
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId, AllottedGeometry.ToPaintGeometry(), &S.White, ESlateDrawEffect::None,
		FLinearColor(0.03f, 0.18f, 0.06f, 0.85f));
	const ARCMatchDirector* D = Director.Get();
	if (!D) return LayerId + 1;
	const FRCHudState& H = D->GetHud();
	auto ToLocal = [&Size](const FVector2D& P) { return FVector2D((P.X * 0.5 + 0.5) * Size.X, (0.5 - P.Y * 0.5) * Size.Y); };
	auto Dot = [&](const FVector2D& P, float Radius, const FLinearColor& C, int32 Layer)
	{
		const FVector2D L = ToLocal(P) - FVector2D(Radius, Radius);
		FSlateDrawElement::MakeBox(OutDrawElements, Layer, AllottedGeometry.ToPaintGeometry(FVector2D(Radius * 2.f, Radius * 2.f), FSlateLayoutTransform(L)),
			&S.White, ESlateDrawEffect::None, C);
	};
	// Halfway line
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, AllottedGeometry.ToPaintGeometry(FVector2D(1.0, Size.Y), FSlateLayoutTransform(FVector2D(Size.X * 0.5, 0.0))),
		&S.White, ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, 0.35f));
	for (const FVector2D& P : H.HomeDots) Dot(P, 3.5f, H.HomeColor, LayerId + 2);
	for (const FVector2D& P : H.AwayDots) Dot(P, 3.5f, H.AwayColor, LayerId + 2);
	if (H.bHasRefDot) Dot(H.RefDot, 4.5f, FLinearColor(0.95f, 0.85f, 0.1f), LayerId + 3);
	Dot(H.BallDot, 2.5f, FLinearColor::White, LayerId + 4);
	return LayerId + 5;
}

// ---------------------------------------------------------------- match HUD

namespace
{
const FRCHudState& HudOf(const TWeakObjectPtr<ARCMatchDirector>& D)
{
	static const FRCHudState Empty;
	return D.IsValid() ? D->GetHud() : Empty;
}

FString HudStr(const char* Key)
{
	if (const UObject* Ctx = GWorld)
	{
		if (URCCareerSubsystem* Sub = URCCareerSubsystem::Get(Ctx)) return Sub->Str(Key);
	}
	return FString(Key);
}
}  // namespace

void SRCMatchHud::Construct(const FArguments& InArgs)
{
	Director = InArgs._Director;
	ChildSlot
	[
		RCUI::RightToLeft(
		SNew(SOverlay).Visibility(EVisibility::SelfHitTestInvisible)
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.f, 14.f)[BuildScoreboard()]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(18.f)[SNew(SRCMinimap).Director(Director)]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(18.f)[BuildMeters()]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)[BuildMessages()]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(16.f, 16.f, 16.f, 40.f)[BuildDecision()]
		+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(16.f, 16.f, 16.f, 40.f)[BuildDissent()]
		+ SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Top).Padding(260.f, 86.f)[BuildReviewBar()]
		+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(18.f)[BuildControlsHint()])
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildScoreboard()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	auto Team = [&S, D](bool bHome)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f)
			[
				SNew(SBox).WidthOverride(12.f).HeightOverride(12.f)
				[
					SNew(SImage).Image(&S.White).ColorAndOpacity_Lambda([D, bHome]() { return FSlateColor(bHome ? HudOf(D).HomeColor : HudOf(D).AwayColor); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Chalk))
				.Text_Lambda([D, bHome]() { return RCUI::T(bHome ? HudOf(D).HomeName : HudOf(D).AwayName); })
			];
	};
	return SNew(SBorder).BorderImage(&S.Panel).Padding(FMargin(16.f, 6.f))
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Team(true)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(14.f, 0.f)
		[
			SNew(STextBlock).Font(S.Title).ColorAndOpacity(FSlateColor(S.Chalk))
			.Text_Lambda([D]() { return RCUI::T(FString::Printf(TEXT("%d - %d"), HudOf(D).HomeScore, HudOf(D).AwayScore)); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Team(false)]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(16.f, 0.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Yellow))
			.Text_Lambda([D]() { return RCUI::T(HudOf(D).Clock); })
		]
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildMeters()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	auto Meter = [&S](const FString& Label, TFunction<float()> Value, FLinearColor Color, TFunction<bool()> Visible)
	{
		return SNew(SVerticalBox).Visibility_Lambda([Visible]() { return Visible() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot().AutoHeight()[RCUI::Text(RCUI::T(Label), S.Small, S.Chalk, false)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 8.f)
			[
				SNew(SBox).WidthOverride(200.f).HeightOverride(8.f)
				[
					SNew(SProgressBar).FillColorAndOpacity(FSlateColor(Color)).Percent_Lambda([Value]() { return TOptional<float>(Value()); })
				]
			];
	};
	return SNew(SBorder).BorderImage(&S.PanelSoft).Padding(12.f)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
		[
			SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Yellow)).Text_Lambda([D]() { return RCUI::T(HudOf(D).RoleLabel); })
		]
		+ SVerticalBox::Slot().AutoHeight()[Meter(HudStr("ui.meter.heat"), [D]() { return HudOf(D).Heat; }, S.Red, []() { return true; })]
		+ SVerticalBox::Slot().AutoHeight()[Meter(HudStr("ui.meter.calm"), [D]() { return HudOf(D).Calm; }, FLinearColor(0.3f, 0.7f, 1.f), []() { return true; })]
		+ SVerticalBox::Slot().AutoHeight()[Meter(HudStr("ui.meter.stamina"), [D]() { return HudOf(D).Stamina; }, S.Grass, [D]() { return HudOf(D).bShowStamina; })]
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(STextBlock).Font(S.Small).ColorAndOpacity(FSlateColor(S.Muted))
			.Text_Lambda([D]() { return RCUI::T(FString::Printf(TEXT("%s: %d"), *HudStr("ui.breaths"), HudOf(D).Breaths)); })
		]
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildDecision()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	auto OptionsText = [D]()
	{
		const FRCHudState& H = HudOf(D);
		FString Out;
		for (int32 i = 0; i < H.Options.Num(); ++i) Out += FString::Printf(TEXT("[%d] %s     "), i + 1, *H.Options[i]);
		return RCUI::T(Out);
	};
	auto CuesText = [D]()
	{
		const FRCHudState& H = HudOf(D);
		FString Out;
		for (const FString& C : H.Cues) Out += TEXT("\u2022 ") + C + TEXT("\n");
		return RCUI::T(Out.TrimEnd());
	};
	return SNew(SBox).WidthOverride(980.f).Visibility_Lambda([D]() { return HudOf(D).bDecision ? EVisibility::Visible : EVisibility::Collapsed; })
	[
		SNew(SBorder).BorderImage(&S.Panel).Padding(FMargin(22.f, 16.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				[
					SNew(STextBlock).Font(S.Title).ColorAndOpacity(FSlateColor(S.Chalk)).Text_Lambda([D]() { return RCUI::T(HudOf(D).DecisionTitle); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(STextBlock).Font(S.Small).ColorAndOpacity(FSlateColor(S.Muted))
					.Text_Lambda([D]() { return RCUI::T(FString::Printf(TEXT("%s %d%%"), *HudStr("ui.clarity"), FMath::RoundToInt(HudOf(D).Clarity * 100.f))); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f)
			[
				SNew(SBox).HeightOverride(6.f)
				[
					SNew(SProgressBar).FillColorAndOpacity(FSlateColor(S.Yellow))
					.Percent_Lambda([D]() { const FRCHudState& H = HudOf(D); return TOptional<float>(H.TimeTotal > 0.f ? H.TimeLeft / H.TimeTotal : 0.f); })
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
			[
				SNew(STextBlock).Font(S.Body).ColorAndOpacity(FSlateColor(S.Muted)).AutoWrapText(true).Text_Lambda([D]() { return RCUI::T(HudOf(D).DecisionNote); })
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 10.f)
			[
				SNew(STextBlock).Font(S.Body).ColorAndOpacity(FSlateColor(S.Chalk)).AutoWrapText(true).Text_Lambda(CuesText)
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Yellow)).AutoWrapText(true).Text_Lambda(OptionsText)
			]
		]
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildDissent()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	return SNew(SBox).WidthOverride(900.f).Visibility_Lambda([D]() { return HudOf(D).bDissent ? EVisibility::Visible : EVisibility::Collapsed; })
	[
		SNew(SBorder).BorderImage(&S.Panel).Padding(FMargin(22.f, 16.f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[RCUI::Text(RCUI::T(HudStr("ui.dissent.title")), S.Title, S.Red)]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 10.f)
			[
				SNew(STextBlock).Font(S.Body).ColorAndOpacity(FSlateColor(S.Chalk)).AutoWrapText(true).Text_Lambda([D]() { return RCUI::T(HudOf(D).DissentText); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Yellow)).AutoWrapText(true)
				.Text_Lambda([D]()
				{
					FString Out;
					const FRCHudState& H = HudOf(D);
					for (int32 i = 0; i < H.DissentOptions.Num(); ++i) Out += FString::Printf(TEXT("[%d] %s     "), i + 1, *H.DissentOptions[i]);
					return RCUI::T(Out);
				})
			]
		]
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildReviewBar()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	return SNew(SBorder).BorderImage(&S.PanelSoft).Padding(10.f).Visibility_Lambda([D]() { return HudOf(D).bReview ? EVisibility::Visible : EVisibility::Collapsed; })
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[RCUI::Text(RCUI::T(HudStr("ui.review.title")), S.BodyBold, S.Yellow, false)]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(12.f, 0.f)
			[
				SNew(STextBlock).Font(S.Small).ColorAndOpacity(FSlateColor(S.Chalk)).Text_Lambda([D]() { return RCUI::T(HudOf(D).ReviewCamera); })
			]
			+ SHorizontalBox::Slot().AutoWidth()[RCUI::Text(RCUI::T(HudStr("ui.review.keys")), S.Small, S.Muted, false)]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
		[
			SNew(SBox).HeightOverride(8.f)
			[
				SNew(SProgressBar).FillColorAndOpacity(FSlateColor(S.Grass))
				.Percent_Lambda([D]() { const FRCHudState& H = HudOf(D); return TOptional<float>(H.ReviewDuration > 0.f ? H.ReviewTime / H.ReviewDuration : 0.f); })
			]
		]
	];
}

TSharedRef<SWidget> SRCMatchHud::BuildMessages()
{
	const RCUI::FStyle& S = RCUI::Style();
	TWeakObjectPtr<ARCMatchDirector> D = Director;
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock).Font(S.Huge).ColorAndOpacity(FSlateColor(S.Chalk)).ShadowOffset(FVector2D(2.0, 2.0))
			.Visibility_Lambda([D]() { return HudOf(D).BigTextTime > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			.Text_Lambda([D]() { return RCUI::T(HudOf(D).BigText); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 8.f)
		[
			SNew(SBox).WidthOverride(90.f).HeightOverride(126.f)
			.Visibility_Lambda([D]() { return HudOf(D).CardTime > 0.f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
			[
				SNew(SImage).Image(&S.White).ColorAndOpacity_Lambda([D, &S]() { return FSlateColor(HudOf(D).CardShown == 2 ? S.Red : S.Yellow); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 140.f, 0.f, 0.f)
		[
			SNew(STextBlock).Font(S.BodyBold).ColorAndOpacity(FSlateColor(S.Chalk)).ShadowOffset(FVector2D(1.0, 1.0)).Justification(ETextJustify::Center)
			.Text_Lambda([D]()
			{
				FString Out;
				for (const TPair<FString, float>& T : HudOf(D).Toasts) Out += T.Key + TEXT("\n");
				return RCUI::T(Out.TrimEnd());
			})
		];
}

TSharedRef<SWidget> SRCMatchHud::BuildControlsHint()
{
	const RCUI::FStyle& S = RCUI::Style();
	return SNew(SBorder).BorderImage(&S.PanelSoft).Padding(8.f)
	[
		RCUI::Text(RCUI::T(HudStr("ui.controls.match")), S.Small, S.Muted, false)
	];
}
