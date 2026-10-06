#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SLeafWidget.h"

class ARCMatchDirector;
class SWidget;

/**
 * Slate UI for Referee Career. Everything is built in code (no UMG assets), right-to-left, with the Arabic
 * fonts from Content/Fonts (IBM Plex Sans Arabic for text, Reem Kufi for titles) when present.
 */
namespace RCUI
{
struct FStyle
{
	FSlateFontInfo Body, BodyBold, Small, Title, Display, Huge;
	FSlateBrush Panel, PanelSoft, Line, White, ChipYellow, ChipRed, BarBack;
	FButtonStyle Button, Primary, Selected;
	FLinearColor Chalk = FLinearColor(0.93f, 0.95f, 0.92f);
	FLinearColor Muted = FLinearColor(0.55f, 0.64f, 0.58f);
	FLinearColor Grass = FLinearColor(0.12f, 0.6f, 0.25f);
	FLinearColor Yellow = FLinearColor(0.95f, 0.72f, 0.05f);
	FLinearColor Red = FLinearColor(0.85f, 0.12f, 0.12f);
	FLinearColor Night = FLinearColor(0.012f, 0.025f, 0.018f, 0.92f);
};
const FStyle& Style();

struct FChoice
{
	FText Label;
	FText Hint;
	bool bEnabled = true;
	bool bSelected = false;
	bool bPrimary = false;
	TFunction<void()> OnClick;
};

FText T(const FString& S);
TSharedRef<SWidget> Text(const FText& InText, const FSlateFontInfo& Font, const FLinearColor& Color, bool bWrap = true);
TSharedRef<SWidget> Button(const FChoice& Choice);
TSharedRef<SWidget> ChoiceList(const TArray<FChoice>& Choices);
TSharedRef<SWidget> StatBar(const FText& Label, float Value01, const FText& ValueText, const FLinearColor& Color);
/** Centred card on a dimmed background. */
TSharedRef<SWidget> Card(const TSharedRef<SWidget>& Content, float Width = 760.f);
/** Eyebrow + title + body + choices in a card. */
TSharedRef<SWidget> Dialog(const FText& Eyebrow, const FText& Title, const FText& Body, const TArray<FChoice>& Choices, float Width = 760.f);
/** Wraps a screen so it lays out right-to-left. */
TSharedRef<SWidget> RightToLeft(const TSharedRef<SWidget>& Content);
}  // namespace RCUI

/** Top-down minimap of players, ball and the player's official. */
class SRCMinimap : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SRCMinimap) {}
	SLATE_ARGUMENT(TWeakObjectPtr<ARCMatchDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(220.0, 142.0); }

private:
	TWeakObjectPtr<ARCMatchDirector> Director;
};

/** In-match HUD: scoreboard, meters, minimap, decision / dissent / VAR panels, cards and messages. */
class SRCMatchHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRCMatchHud) {}
	SLATE_ARGUMENT(TWeakObjectPtr<ARCMatchDirector>, Director)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TSharedRef<SWidget> BuildScoreboard();
	TSharedRef<SWidget> BuildMeters();
	TSharedRef<SWidget> BuildDecision();
	TSharedRef<SWidget> BuildDissent();
	TSharedRef<SWidget> BuildReviewBar();
	TSharedRef<SWidget> BuildMessages();
	TSharedRef<SWidget> BuildControlsHint();

	TWeakObjectPtr<ARCMatchDirector> Director;
};
