#include "ManualFillingEditorTools.h"
#include "UI/ShopTerminalWidget.h"
#include "Shop/ManualColaFillingMachine.h"
#include "Shop/ShopProductionStationComponent.h"
#include "Shop/ShopPlayerInteractionComponent.h"
#include "Shop/ShopProcurementComponent.h"
#include "Shop/SharedTeamWalletComponent.h"
#include "Props/InteractablePropManager.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Slate/WidgetRenderer.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "RenderingThread.h"

bool UManualFillingEditorTools::RenderDashboardPreview(const FString& OutputFile)
{
	const FString Destination=FPaths::ConvertRelativePathToFull(OutputFile);
	if (!FPaths::IsUnderDirectory(Destination,FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(),TEXT("Saved")))))
	{ UE_LOG(LogTemp,Error,TEXT("Dashboard preview must be saved inside the project's Saved directory: %s"),*Destination); return false; }
	auto* Instance=NewObject<UGameInstance>(GEngine); Instance->InitializeStandalone();
	auto* World=Instance->GetWorld(); if (!World) return false;
	ON_SCOPE_EXIT { Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Manager=World->SpawnActor<APropManager>(); Manager->bEnableAutosave=false; Manager->InitializePropManager();
	if (!Manager->ShopSupplies->InitializeCatalog()) return false;
	Manager->SharedWallet->Credit(FGuid::NewGuid(),8750,TEXT("Preview"));
	auto* Machine=World->SpawnActor<AManualColaFillingMachine>();
	auto* Pawn=World->SpawnActor<ACharacter>(); auto* Controller=World->SpawnActor<APlayerController>(); Controller->Possess(Pawn);
	auto* Actions=NewObject<UShopPlayerInteractionComponent>(Pawn); Pawn->AddInstanceComponent(Actions); Actions->RegisterComponent();
	auto& View=Machine->Production->View; View.SessionID=FGuid::NewGuid(); View.SeatedBottleISID=10042;
	View.SelectedContentPort=1; View.ConnectedSourceISID=10102; View.CapTrayCount=4; View.SealTrayCount=3; View.bLeverHeld=true; View.bFilling=true;
	FShopProductionItem Bottle; Bottle.ISID=10042; Bottle.Name=FText::FromString(TEXT("Plastic bottle")); Bottle.ContentLevel=.76f; Bottle.ResidualPressure=.46f; View.Bottles.Add(Bottle);
	for (int32 I=0;I<4;++I)
	{
		FShopProductionPort Port; Port.PortIndex=I; Port.SourceISID=I<2 ? 10101+I : 0; Port.bInRange=I<2;
		Port.ContentName=FText::FromString(I==0 ? TEXT("Normal Lemonade") : I==1 ? TEXT("Normal Mint") : TEXT("Empty input")); View.SupplyPorts.Add(Port);
		if (I<2) { FShopProductionItem Supply; Supply.ISID=Port.SourceISID; Supply.Name=Port.ContentName; View.Cylinders.Add(Supply); }
	}
	auto* Widget=CreateWidget<UShopTerminalWidget>(Instance,UShopTerminalWidget::StaticClass());
	Widget->SetOwningPlayer(Controller); Widget->Configure(Actions,Machine);
	FWidgetRenderer Renderer(false); const auto Slate=Widget->TakeWidget();
	auto* Target=Renderer.DrawWidget(Slate,FVector2D(1920,1080));
	if (!Target) return false;
	// Auto-wrapped text needs its allocated width before its final desired height.
	Widget->InvalidateLayoutAndVolatility(); Widget->ForceLayoutPrepass();
	Renderer.DrawWidget(Target,Slate,FVector2D(1920,1080),0.f);
	FlushRenderingCommands(); TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags; ReadFlags.SetLinearToGamma(false);
	if (!Target->GameThread_GetRenderTargetResource()->ReadPixels(Pixels,ReadFlags)) return false;
	TArray64<uint8> PNG; FImageUtils::PNGCompressImageArray(1920,1080,Pixels,PNG);
	return FFileHelper::SaveArrayToFile(PNG,*Destination);
}
