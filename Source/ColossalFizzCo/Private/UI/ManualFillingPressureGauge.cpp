#include "UI/ManualFillingPressureGauge.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"

class SManualFillingPressureGauge : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SManualFillingPressureGauge) {} SLATE_ARGUMENT(TWeakObjectPtr<UManualFillingPressureGauge>, Reading) SLATE_END_ARGS()
	void Construct(const FArguments& Args) { Reading=Args._Reading; }
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(240,190); }
	virtual int32 OnPaint(const FPaintArgs&,const FGeometry& Geometry,const FSlateRect&,FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle&,bool) const override
	{
		const float Pressure=Reading.IsValid() ? FMath::Max(0.f,Reading->Pressure) : 0;
		const float Safe=Reading.IsValid() ? Reading->SafePressure : .1f;
		const FVector2D Size=Geometry.GetLocalSize(), Center(Size.X*.5,Size.Y*.53);
		const float Radius=FMath::Min(Size.X*.43,Size.Y*.48);
		const FLinearColor Mint(.23f,.85f,.65f), Amber(.95f,.67f,.3f), Red(.95f,.32f,.3f), Muted(.28f,.39f,.43f);
		auto Point=[&](float Fraction,float R){const float A=FMath::DegreesToRadians(135.f+270.f*Fraction); return Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*R;};
		auto Arc=[&](float From,float To,FLinearColor Color,float Width)
		{
			TArray<FVector2D> Points; for (int32 I=0;I<=64;++I) Points.Add(Point(FMath::Lerp(From,To,I/64.f),Radius));
			FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),Points,ESlateDrawEffect::None,Color,true,Width);
		};
		Arc(0,1,Muted,7); Arc(0,FMath::Clamp(Safe/.8f,0.f,1.f),Mint,7); Arc(.5f,.78f,Amber,7); Arc(.79f,1,Red,7);
		for (int32 I=0;I<=24;++I)
		{
			const float F=I/24.f;
			TArray<FVector2D> Tick={Point(F,Radius-12),Point(F,Radius-(I%4==0 ? 23 : 17))};
			FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Tick,ESlateDrawEffect::None,Muted,true,1.4f);
		}
		TArray<FVector2D> Needle={Center,Point(FMath::Clamp(Pressure/.8f,0.f,1.f),Radius-28)};
		FSlateDrawElement::MakeLines(Elements,Layer+2,Geometry.ToPaintGeometry(),Needle,ESlateDrawEffect::None,Pressure<=Safe ? Mint : Amber,true,3.f);
		const auto Small=FCoreStyle::GetDefaultFontStyle("Regular",10);
		FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(80,15),FSlateLayoutTransform(FVector2D(Center.X-31,Center.Y+12))),TEXT("PRESSURE"),Small,ESlateDrawEffect::None,FLinearColor(.62f,.72f,.74f));
		FSlateDrawElement::MakeText(Elements,Layer+2,Geometry.ToPaintGeometry(FVector2D(90,32),FSlateLayoutTransform(FVector2D(Center.X-30,Center.Y+28))),FString::Printf(TEXT("%.2f"),Pressure),FCoreStyle::GetDefaultFontStyle("Bold",24),ESlateDrawEffect::None,Pressure<=Safe ? Mint : Amber);
		return Layer+3;
	}
private:
	TWeakObjectPtr<UManualFillingPressureGauge> Reading;
};

void UManualFillingPressureGauge::SetReading(float InPressure, float InSafePressure)
{
	if (Pressure==InPressure && SafePressure==InSafePressure) return;
	Pressure=InPressure;
	SafePressure=InSafePressure;
	if (const auto Widget=GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

TSharedRef<SWidget> UManualFillingPressureGauge::RebuildWidget() { return SNew(SManualFillingPressureGauge).Reading(this); }
