#include "UI/FlowControllerIcon.h"

#include "Rendering/DrawElements.h"
#include "Widgets/SLeafWidget.h"

#include <initializer_list>

namespace
{
	struct FFlowIconPainter
	{
		const FGeometry& Geometry;
		FSlateWindowElementList& Elements;
		FLinearColor Color;
		ESlateDrawEffect Effects;
		int32 Layer;
		double Scale;
		FVector2D Offset;

		void Path(std::initializer_list<FVector2D> Points) const
		{
			TArray<FVector2f> Stroke;
			Stroke.Reserve(static_cast<int32>(Points.size()));
			for (const FVector2D& Point:Points) Stroke.Add(FVector2f(Offset+Point*Scale));
			Draw(MoveTemp(Stroke));
		}

		void Arc(double X,double Y,double RadiusX,double RadiusY,double From=0,double To=360) const
		{
			TArray<FVector2f> Stroke;
			constexpr int32 Segments=24;
			Stroke.Reserve(Segments+1);
			for (int32 Index=0;Index<=Segments;++Index)
			{
				const double Angle=FMath::DegreesToRadians(FMath::Lerp(From,To,double(Index)/Segments));
				Stroke.Add(FVector2f(Offset+Scale*FVector2D(X+RadiusX*FMath::Cos(Angle),Y+RadiusY*FMath::Sin(Angle))));
			}
			Draw(MoveTemp(Stroke));
		}

		void Draw(TArray<FVector2f>&& Stroke) const
		{
			FSlateDrawElement::MakeLines(Elements,Layer,Geometry.ToPaintGeometry(),MoveTemp(Stroke),Effects,Color,true,float(1.65*Scale));
		}

		void Icon(EFlowControllerIcon Type) const
		{
			switch (Type)
			{
			case EFlowControllerIcon::Controller:
				Path({{4,6},{20,6},{20,18},{4,18},{4,6}});
				Path({{1.5,10},{4,10}}); Path({{1.5,14},{4,14}});
				Path({{20,9},{22.5,9}}); Path({{20,12},{22.5,12}}); Path({{20,15},{22.5,15}});
				Arc(12,12,2.6,2.6);
				break;
			case EFlowControllerIcon::Inlet:
				Path({{15.5,5},{21,5},{21,19},{15.5,19}});
				Path({{3,12},{14,12}}); Path({{10,8},{14,12},{10,16}});
				break;
			case EFlowControllerIcon::Outlet:
				Path({{8.5,5},{3,5},{3,19},{8.5,19}});
				Path({{10,12},{21,12}}); Path({{17,8},{21,12},{17,16}});
				break;
			case EFlowControllerIcon::Flow:
				Path({{3,7},{14,7}}); Path({{2,12},{21,12}}); Path({{17,8},{21,12},{17,16}}); Path({{5,17},{14,17}});
				break;
			case EFlowControllerIcon::Pressure:
				Arc(12,14,8.3,8.3,153,387);
				Path({{12,14},{16.4,9.6}}); Arc(12,14,1.25,1.25); Path({{8.5,19},{15.5,19}});
				break;
			case EFlowControllerIcon::Suction:
				Arc(12,12,8.3,8.3,225,505); Arc(12,12,5,5,36,315); Arc(12,12,1.1,1.1);
				Path({{18,3},{16,6},{13,5}});
				break;
			case EFlowControllerIcon::Lever:
				Path({{4.5,19},{19.5,19},{19.5,21},{4.5,21},{4.5,19}});
				Path({{10,19},{15.7,8.5}}); Arc(17,6,3,3);
				break;
			case EFlowControllerIcon::Hold:
				Path({{12,3},{12,13}}); Path({{8.5,9.5},{12,13},{15.5,9.5}});
				Path({{5,17},{19,17},{19,21},{5,21},{5,17}});
				break;
			case EFlowControllerIcon::Toggle:
				Arc(7.5,12,4.5,4.5,90,270); Arc(16.5,12,4.5,4.5,-90,90);
				Path({{7.5,7.5},{16.5,7.5}}); Path({{7.5,16.5},{16.5,16.5}}); Arc(16.4,12,2,2);
				break;
			case EFlowControllerIcon::AutoStop:
				Path({{8.5,3},{15.5,3},{21,8.5},{21,15.5},{15.5,21},{8.5,21},{3,15.5},{3,8.5},{8.5,3}});
				Path({{9,9},{15,9},{15,15},{9,15},{9,9}});
				break;
			case EFlowControllerIcon::Automatic:
				Arc(12,12,8.3,8.3,210,345); Arc(12,12,8.3,8.3,30,165);
				Path({{20.5,5.5},{20,10},{16,9.5}}); Path({{3.5,18.5},{4,14},{8,14.5}});
				Path({{12,8},{9.5,12.5},{12.8,12.5},{11.8,16}});
				break;
			case EFlowControllerIcon::Bottle:
				Path({{10,3},{14,3},{14,7},{17,10},{17,20.5},{7,20.5},{7,10},{10,7},{10,3}});
				Path({{7,14},{17,14}});
				break;
			case EFlowControllerIcon::Coin:
				Arc(12,12,8.5,8.5); Arc(12,12,5.5,5.5);
				Path({{12,9},{12,15}});
				break;
			case EFlowControllerIcon::Upgrade:
				Path({{5,11},{12,4},{19,11}}); Path({{5,18},{12,11},{19,18}});
				break;
			case EFlowControllerIcon::Move:
				Path({{3,12},{21,12}}); Path({{12,3},{12,21}});
				Path({{6,9},{3,12},{6,15}}); Path({{18,9},{21,12},{18,15}});
				Path({{9,6},{12,3},{15,6}}); Path({{9,18},{12,21},{15,18}});
				break;
			case EFlowControllerIcon::Pause:
				Path({{8.5,5},{8.5,19}}); Path({{15.5,5},{15.5,19}});
				break;
			case EFlowControllerIcon::Play:
				Path({{7,4},{20,12},{7,20},{7,4}});
				break;
			case EFlowControllerIcon::Lock:
				Path({{6,10},{18,10},{18,21},{6,21},{6,10}}); Arc(12,8,4,5,180,360);
				Path({{8,8},{8,10}}); Path({{16,8},{16,10}}); Path({{12,14},{12,17}});
				break;
			case EFlowControllerIcon::Check:
				Path({{4,12},{9.5,17.5},{20,7}});
				break;
			case EFlowControllerIcon::Close:
				Path({{6,6},{18,18}}); Path({{18,6},{6,18}});
				break;
			case EFlowControllerIcon::ArrowRight:
				Path({{4,12},{20,12}}); Path({{14,6},{20,12},{14,18}});
				break;
			case EFlowControllerIcon::Warning:
				Path({{12,3.5},{21,20},{3,20},{12,3.5}}); Path({{12,9},{12,13.5}}); Arc(12,17,.55,.55);
				break;
			case EFlowControllerIcon::Link:
				Path({{10,17},{8.5,18.5},{5,18.5},{3,16.5},{3,13},{7.5,8.5},{11,8.5}});
				Path({{14,7},{15.5,5.5},{19,5.5},{21,7.5},{21,11},{16.5,15.5},{13,15.5}});
				Path({{8.5,15.5},{15.5,8.5}});
				break;
			case EFlowControllerIcon::Clock:
				Arc(12,12,8.5,8.5); Path({{12,6.5},{12,12},{15.5,14}});
				break;
			}
		}
	};

	class SFlowControllerIcon : public SLeafWidget
	{
	public:
		SLATE_BEGIN_ARGS(SFlowControllerIcon) {}
			SLATE_ARGUMENT(TWeakObjectPtr<UFlowControllerIcon>, Icon)
		SLATE_END_ARGS()

		void Construct(const FArguments& Args) { Icon=Args._Icon; }

		FVector2D ComputeDesiredSize(float) const override
		{
			const auto* Source=Icon.Get();
			return Source ? Source->GetIconSize() : FVector2D::ZeroVector;
		}

		int32 OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&, FSlateWindowElementList& Elements,
			int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
		{
			const auto* Source=Icon.Get();
			if (!Source) return Layer;
			const FVector2D Size=Geometry.GetLocalSize();
			const double Side=FMath::Min(Size.X,Size.Y);
			if (Side<=0) return Layer;
			const FFlowIconPainter Painter{Geometry,Elements,Source->GetTint()*Style.GetColorAndOpacityTint(),
				ShouldBeEnabled(bParentEnabled) ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect,
				Layer,Side/24.,(Size-FVector2D(Side,Side))*.5};
			Painter.Icon(Source->GetIcon());
			return Layer;
		}

	private:
		TWeakObjectPtr<UFlowControllerIcon> Icon;
	};
}

UFlowControllerIcon::UFlowControllerIcon()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UFlowControllerIcon::Configure(EFlowControllerIcon InIcon, const FLinearColor& InTint, FVector2D InSize)
{
	SetIcon(InIcon);
	SetTint(InTint);
	SetIconSize(InSize);
}

void UFlowControllerIcon::SetIcon(EFlowControllerIcon InIcon)
{
	if (Icon==InIcon) return;
	Icon=InIcon;
	if (const auto Widget=GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

void UFlowControllerIcon::SetTint(const FLinearColor& InTint)
{
	if (Tint==InTint) return;
	Tint=InTint;
	if (const auto Widget=GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Paint);
}

void UFlowControllerIcon::SetIconSize(FVector2D InSize)
{
	if (Size==InSize) return;
	Size=InSize;
	if (const auto Widget=GetCachedWidget()) Widget->Invalidate(EInvalidateWidgetReason::Layout);
}

TSharedRef<SWidget> UFlowControllerIcon::RebuildWidget()
{
	return SNew(SFlowControllerIcon).Icon(this);
}
