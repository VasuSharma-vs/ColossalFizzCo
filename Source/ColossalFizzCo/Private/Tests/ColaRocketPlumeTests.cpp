#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "CoreGlobals.h"
#include "Materials/MaterialInterface.h"
#include "NiagaraComponent.h"
#include "NiagaraDataInterfaceArrayFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraDataSet.h"
#include "NiagaraDataSetAccessor.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "Props/ColaRocketPlumeSubsystem.h"
#include "Props/ColaPropActor.h"
#include "Props/InteractablePropDataAsset.h"
#include "Props/InteractablePropManager.h"
#include "Shop/ShopProcurementComponent.h"

enum class EColaPlumeCheck
{
	FullEffect,
	ContentColors
};

class FColaPlumeTestBase : public FAutomationTestBase
{
public:
	using FAutomationTestBase::FAutomationTestBase;
protected:
	bool RunPlumeCheck(EColaPlumeCheck Check);
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FColaRocketPlumeTest, FColaPlumeTestBase, "ColossalFizzCo.Props.ColaRocketPlume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FColaRocketContentColorsTest, FColaPlumeTestBase, "ColossalFizzCo.Props.ColaRocketContentColors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FColaRocketPlumeTest::RunTest(const FString& Parameters)
{
	return RunPlumeCheck(EColaPlumeCheck::FullEffect);
}

bool FColaRocketContentColorsTest::RunTest(const FString& Parameters)
{
	return RunPlumeCheck(EColaPlumeCheck::ContentColors);
}

bool FColaPlumeTestBase::RunPlumeCheck(EColaPlumeCheck Check)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("Isolated bottle world"), World)) return false;
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	TSharedPtr<UWorld> WorldLifetime = MakeShareable(World, [](UWorld* World)
	{
		World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	});
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	APropManager* Manager = World->SpawnActor<APropManager>();
	Manager->bEnableAutosave = false;
	Manager->InitializePropManager();
	if (!TestTrue(TEXT("Real bottle catalog"), Manager->ShopSupplies->InitializeCatalog())) return false;
	auto* Lemonade = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/NormalColaPrototype/DA_NormalLemonade.DA_NormalLemonade"));
	auto* Sweet = LoadObject<UInteractablePropDataAsset>(nullptr, TEXT("/Game/NormalColaPrototype/DA_NormalSweet.DA_NormalSweet"));
	if (!TestNotNull(TEXT("Lemonade bottle product"), Lemonade) || !TestNotNull(TEXT("Sweet bottle product"), Sweet)) return false;
	auto Launch = [&](UInteractablePropDataAsset* Product, const FVector& Position, const FRotator& Rotation, float Scale) -> AColaPropActor*
	{
		const int32 ProductID = Manager->RegisterProductFromDataAsset(Product);
		const int32 ID = Manager->GenerateProductInstance(ProductID, FTransform(Rotation, Position, FVector(Scale)));
		if (!Manager->SetInstanceToUnsettled(ID)) return nullptr;
		auto* Bottle = Cast<AColaPropActor>(Manager->GetActiveActorFromISID(ID));
		if (!Bottle || !Bottle->RocketComp) return nullptr;
		Manager->SetBottleAgitationPressure(ID, Bottle->GetBottlePressureTolerance() + 1.f);
		Bottle->AddAgitationPressure(.01f);
		return Bottle->RocketComp->IsPoweredFlight() ? Bottle : nullptr;
	};
	UNiagaraSystem* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/Niagara/ColaRocket/NS_ColaRocketThrustShared.NS_ColaRocketThrustShared"));
	if (!TestNotNull(TEXT("Shared Niagara system is saved"), System)) return false;
	System->WaitForCompilationComplete(true, false);
	auto* Shared = World->GetSubsystem<UColaRocketPlumeSubsystem>();
	if (!TestNotNull(TEXT("World owns the shared plume"), Shared)) return false;
	constexpr float Dt = 1.f / 60.f;
	auto* First = Launch(Lemonade, FVector(0, 1000, 2000), FRotator(0, 0, 0), 1.f);
	auto* Second = Launch(Sweet, FVector(0, -1000, 2000), FRotator(-90, 0, 0), .5f);
	if (!TestNotNull(TEXT("First bottle launches"), First) || !TestNotNull(TEXT("Second bottle launches"), Second)) return false;
	World->Tick(LEVELTICK_All, Dt);
	World->Tick(LEVELTICK_All, Dt); // Allow the newly registered post-physics tick to run.
	auto* Plume = Shared->GetSharedPlume();
	if (!TestNotNull(TEXT("Two flying bottles create one shared plume"), Plume)) return false;
	TestEqual(TEXT("Both emitting nozzles are registered"), Shared->GetNozzleCount(), 2);
	TestTrue(TEXT("Uses the batched graph"), Plume->GetAsset() == System);
	auto Nozzles = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(Plume, TEXT("User.NozzlePositions"));
	auto Directions = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayVector(Plume, TEXT("User.NozzleDirections"));
	auto Scales = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayFloat(Plume, TEXT("User.NozzleScales"));
	auto Colors = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayColor(Plume, TEXT("User.NozzleColors"));
	if (!TestEqual(TEXT("Both nozzle positions uploaded"), Nozzles.Num(), 2)
		|| !TestEqual(TEXT("Both nozzle colors uploaded"), Colors.Num(), 2)) return false;
	if (Check == EColaPlumeCheck::FullEffect && (!TestEqual(TEXT("Both nozzle scales uploaded"), Scales.Num(), 2)
		|| !TestEqual(TEXT("Both nozzle directions uploaded"), Directions.Num(), 2))) return false;

	// The two mouths are far enough apart to detect particles from one being
	// emitted at the other, even when the bottle registrations change order.
	for (const AColaPropActor* Bottle : {First, Second})
	{
		const FBox Shell = Bottle->PropMesh->GetStaticMesh()->GetBoundingBox();
		const FVector LocalLip(Shell.GetCenter().X, Shell.GetCenter().Y, Shell.Max.Z);
		const FVector Lip = Bottle->PropMesh->GetComponentTransform().TransformPosition(LocalLip);
		const int32 Near = FVector::DistSquared(Lip, Nozzles[0]) < FVector::DistSquared(Lip, Nozzles[1]) ? 0 : 1;
		const auto* Content = Manager->FindContentDefinition(Bottle->GetContentID());
		FLinearColor Expected;
		if (!TestTrue(TEXT("Stock flavor has an authored liquid color"), Content && Content->LiquidMaterial &&
			Content->LiquidMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("Liquid Color")), Expected))) return false;
		Expected.A = 1.f;
		TestTrue(TEXT("Each nozzle receives its own flavor's material color"), Colors[Near].Equals(Expected));
		if (Check == EColaPlumeCheck::FullEffect)
		{
			TestTrue(TEXT("Nozzle is at the moving shell lip"), FVector::Dist(Lip, Nozzles[Near]) < 12.);
			TestTrue(TEXT("Jet points outward through the lip"), FVector::DotProduct(Directions[Near], Bottle->PropMesh->GetUpVector()) > .95);
			TestTrue(TEXT("Jet scale matches the bottle mouth"), FMath::IsNearlyEqual(Scales[Near], (float)Bottle->PropMesh->GetComponentScale().X, .02f));
		}
	}
	TestFalse(TEXT("Lemonade and Sweet have distinct spray colors"), Colors[0].Equals(Colors[1]));
	auto Positions = [Plume](FName Emitter, TArray<float>* Widths = nullptr, TArray<FLinearColor>* Tints = nullptr)
	{
		TArray<FVector> Out;
		const auto Controller = Plume->GetSystemInstanceController();
		if (!Controller || !Controller->IsValid()) return Out;
		Controller->WaitForConcurrentTickAndFinalize();
		const auto* Instance = Controller->IsSolo() ? Controller->GetSoloSystemInstance() : nullptr;
		if (!Instance) return Out;
		for (const auto& E : Instance->GetEmitters())
		{
			if (E->GetEmitterHandle().GetName() != Emitter) continue;
			const auto& Data = E->GetParticleData();
			const auto Reader = FNiagaraDataSetAccessor<FNiagaraPosition>::CreateReader(Data, TEXT("Position"));
			const auto Size = FNiagaraDataSetAccessor<FVector2f>::CreateReader(Data, TEXT("SpriteSize"));
			const auto Color = FNiagaraDataSetAccessor<FLinearColor>::CreateReader(Data, TEXT("Color"));
			if (Reader.IsValid())
			{
				const auto Coordinates = Instance->GetLWCConverter();
				for (int32 I = 0; I < E->GetNumParticles(); ++I)
				{
					Out.Add(Coordinates.ConvertSimulationPositionToWorld(Reader[I]));
					if (Widths && Size.IsValid()) Widths->Add(Size[I].X);
					if (Tints && Color.IsValid()) Tints->Add(Color[I]);
				}
			}
		}
		return Out;
	};
	Plume->SetForceSolo(true);
	Plume->AdvanceSimulation(18, Dt);
	for (const FName Layer : { FName(TEXT("ColaJet")), FName(TEXT("ColaFan")) })
	{
		TArray<FLinearColor> Tints;
		const auto Points = Positions(Layer, nullptr, &Tints);
		if (!TestEqual(Layer.ToString() + TEXT(" particles carry a color"), Tints.Num(), Points.Num())) return false;
		int32 Births[2] = {};
		bool bCorrectColors = true;
		for (int32 I = 0; I < Points.Num(); ++I)
		{
			const int32 Mouth = FVector::DistSquared(Points[I], Nozzles[0]) < FVector::DistSquared(Points[I], Nozzles[1]) ? 0 : 1;
			++Births[Mouth];
			bCorrectColors &= Tints[I].Equals(Colors[Mouth], .002f);
		}
		TestTrue(Layer.ToString() + TEXT(" keeps the two flavors separate"), bCorrectColors && Births[0] && Births[1]);
	}
	if (Check == EColaPlumeCheck::FullEffect)
	{
		const TArray<FVector> Jets = Positions(TEXT("ColaJet"));
		for (int32 Index = 0; Index < Nozzles.Num(); ++Index)
		{
			TestTrue(TEXT("Liquid travels outward at each source"), Jets.ContainsByPredicate([&](const FVector& P)
			{
				const FVector D = P - Nozzles[Index];
				return D.Size() < 400 && FVector::DotProduct(D, Directions[Index]) > 20;
			}));
		}
	}
	const FLinearColor Override(.1f, .7f, .3f, 1.f);
	auto UpdatedSweet = *Manager->FindContentDefinition(Second->GetContentID());
	UpdatedSweet.bOverrideSprayColor = true;
	UpdatedSweet.SprayColor = Override;
	if (!TestTrue(TEXT("Designer may override a stock flavor's spray"), Manager->RegisterContentDefinition(UpdatedSweet))) return false;
	First->RocketComp->FinishPressureThrust();
	// Tick each cutoff on a fresh engine frame. Tick functions can run only
	// once per GFrameCounter, even in a manually stepped automation world.
	ADD_LATENT_AUTOMATION_COMMAND(FFunctionLatentCommand(([this, WorldLifetime, Shared, Plume, First, Second, Positions,
		Colors, Override, Check, LastFrame = GFrameCounter, bFirstCutoff = true]() mutable
	{
		if (LastFrame == GFrameCounter) return false;
		LastFrame = GFrameCounter;
		UWorld* World = WorldLifetime.Get();
		constexpr float Dt = 1.f / 60.f;
		if (bFirstCutoff)
		{
			World->Tick(LEVELTICK_All, Dt);
			TestEqual(TEXT("One bottle can stop independently"), Shared->GetNozzleCount(), 1);
			TestTrue(TEXT("Second bottle keeps the same simulation"), Shared->GetSharedPlume() == Plume);
			TestEqual(TEXT("One nozzle uploaded after cutoff"), UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(Plume, TEXT("User.NozzlePositions")).Num(), 1);
			const auto RemainingColors = UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayColor(Plume, TEXT("User.NozzleColors"));
			TestTrue(TEXT("The still-flying bottle picks up its content override"), RemainingColors.Num() == 1 && RemainingColors[0].Equals(Override));
			Plume->AdvanceSimulation(3, Dt);
			TArray<FLinearColor> ChangingColors;
			Positions(TEXT("ColaJet"), nullptr, &ChangingColors);
			for (const FLinearColor& Expected : {Colors[0], Colors[1], Override})
				TestTrue(TEXT("Live drops keep their birth colors as the next drops change"),
					ChangingColors.ContainsByPredicate([&](const FLinearColor& Tint) { return Tint.Equals(Expected, .002f); }));
			if (Check == EColaPlumeCheck::ContentColors) return true;
			Plume->AdvanceSimulation(42, Dt);
			const TArray<FVector> RemainingJets = Positions(TEXT("ColaJet"));
			TestTrue(TEXT("Remaining bottle still produces liquid"), RemainingJets.Num() > 0);
			TestFalse(TEXT("Stopped bottle no longer produces liquid"), RemainingJets.ContainsByPredicate([&](const FVector& P)
			{
				return FVector::Dist(P, First->GetActorLocation()) < 500;
			}));
			Second->RocketComp->FinishPressureThrust();
			bFirstCutoff = false;
			return false;
		}
		World->Tick(LEVELTICK_All, Dt);
		TestEqual(TEXT("All nozzles stop"), Shared->GetNozzleCount(), 0);
		TestEqual(TEXT("Empty nozzle list uploaded"), UNiagaraDataInterfaceArrayFunctionLibrary::GetNiagaraArrayPosition(Plume, TEXT("User.NozzlePositions")).Num(), 0);
		Plume->AdvanceSimulation(120, Dt);
		TestTrue(TEXT("Released bubbles survive two seconds without thrust"), Positions(TEXT("LingeringBubbles")).Num() > 0);
		TestTrue(TEXT("Jet shuts off before the bubbles"), Positions(TEXT("ColaJet")).IsEmpty());
		Plume->AdvanceSimulation(380, Dt); // Finish the previous launches before sampling a fresh cohort.

		// Drive the same graph with 24 isolated mouths, half of them at half scale.
		TArray<FVector> CrowdedPositions;
		TArray<FVector> CrowdedDirections;
		TArray<float> CrowdedScales;
		TArray<FLinearColor> CrowdedColors;
		constexpr int32 BottleCount = 24;
		for (int32 Index = 0; Index < BottleCount; ++Index)
		{
			CrowdedPositions.Add(FVector(0, Index * 1000., 2000));
			CrowdedDirections.Add(FVector::UpVector);
			CrowdedScales.Add(Index % 2 ? .5f : 1.f);
			CrowdedColors.Add(Colors[Index % 2]);
		}
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayPosition(Plume, TEXT("User.NozzlePositions"), CrowdedPositions);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Plume, TEXT("User.NozzleDirections"), CrowdedDirections);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayFloat(Plume, TEXT("User.NozzleScales"), CrowdedScales);
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayColor(Plume, TEXT("User.NozzleColors"), CrowdedColors);
		Plume->SetVariableInt(TEXT("User.NozzleCount"), BottleCount);
		Plume->AdvanceSimulation(48, Dt);
		TArray<float> Widths;
		const TArray<FVector> CrowdedJets = Positions(TEXT("ColaJet"), &Widths);
		TSet<int32> EmittingMouths;
		for (const FVector& P : CrowdedJets)
		{
			EmittingMouths.Add(FMath::RoundToInt(P.Y / 1000.));
		}
		TestEqual(TEXT("All 24 mouths receive particles above the budget threshold"), EmittingMouths.Num(), BottleCount);
		const int32 MaxLiveJetParticles = FMath::CeilToInt(12.f * 480.f * (.34f + Dt));
		TestTrue(TEXT("Live liquid is bounded by the 12-bottle spawn budget"), CrowdedJets.Num() <= MaxLiveJetParticles);
		if (TestEqual(TEXT("Each core droplet has a sprite width"), Widths.Num(), CrowdedJets.Num()))
		{
			TestFalse(TEXT("Individual drops are narrower than the smallest 10.3 cm opening"),
				Widths.ContainsByPredicate([](float Width) { return Width <= 0.f || Width > 5.5f; }));
		}
		AddInfo(FString::Printf(TEXT("%d active mouths shared %d live liquid particles"), EmittingMouths.Num(), CrowdedJets.Num()));

		// All 0.8 seconds of births are younger than the shortest lifetime.
		// A steady-state count would overrepresent the longer-lived bubbles.
		const auto ShortBirths = Positions(TEXT("FizzBubbles"));
		const int32 ShortCount = ShortBirths.Num();
		const int32 LongCount = Positions(TEXT("LingeringBubbles")).Num();
		const double ShortShare = double(ShortCount) / FMath::Max(1, ShortCount + LongCount);
		TestTrue(TEXT("Dense bubble emission at the shared rate limit"), ShortCount + LongCount > 800);
		TestTrue(TEXT("Nine of ten new bubbles use the 2-4 second lifetime"), FMath::IsNearlyEqual(ShortShare, .90, .015));
		TestTrue(TEXT("Liquid also sprays sideways from vertical nozzles"), Positions(TEXT("ColaFan")).ContainsByPredicate(
			[](const FVector& P) { return FMath::Abs(P.X) > 12.; }));

		auto MeanZ = [](const TArray<FVector>& Points)
		{
			double Sum = 0.;
			for (const FVector& P : Points) Sum += P.Z;
			return Sum / FMath::Max(1, Points.Num());
		};
		Plume->SetVariableInt(TEXT("User.NozzleCount"), 0);
		Plume->AdvanceSimulation(6, Dt);
		const double EarlySpeed = (MeanZ(Positions(TEXT("FizzBubbles"))) - MeanZ(ShortBirths)) / .1;
		Plume->AdvanceSimulation(54, Dt); // One second since cutoff.
		const double LateZ = MeanZ(Positions(TEXT("FizzBubbles")));
		Plume->AdvanceSimulation(6, Dt);
		const auto BeforeFirstPops = Positions(TEXT("FizzBubbles"));
		const double LateSpeed = (MeanZ(BeforeFirstPops) - LateZ) / .1;
		TestEqual(TEXT("No short bubbles pop before two seconds of age"), BeforeFirstPops.Num(), ShortCount);
		TestTrue(TEXT("Launched bubbles brake to a drift"), EarlySpeed > 70. && FMath::Abs(LateSpeed) < EarlySpeed * .5);
		Plume->AdvanceSimulation(180, Dt); // 4.1 s since cutoff.
		TestTrue(TEXT("All short bubbles pop by four seconds of age"), Positions(TEXT("FizzBubbles")).IsEmpty());
		Plume->AdvanceSimulation(60, Dt); // Oldest bubble is now at most 5.9 s old.
		TestEqual(TEXT("Long bubbles survive to six seconds of age"), Positions(TEXT("LingeringBubbles")).Num(), LongCount);
		Plume->AdvanceSimulation(102, Dt); // 6.8 s since cutoff.
		const int32 Floating = Positions(TEXT("LingeringBubbles")).Num();
		TestTrue(TEXT("The last bubbles pop gradually between six and eight seconds"), Floating > 0 && Floating < LongCount);
		Plume->AdvanceSimulation(84, Dt);
		TestTrue(TEXT("Final bubbles have popped by eight seconds"), Positions(TEXT("LingeringBubbles")).IsEmpty());
		AddInfo(FString::Printf(TEXT("Bubble cohort: %d short, %d long; upward speed %.0f to %.0f cm/s"), ShortCount, LongCount, EarlySpeed, LateSpeed));

		// Sample actual births. During the first 120 ms no liquid particles
		// expire, so each new entry is at most one millisecond old.
		for (int32 I = 0; I < BottleCount; ++I)
			CrowdedDirections[I] = I % 4 < 2 ? FVector::UpVector : FVector::ForwardVector;
		UNiagaraDataInterfaceArrayFunctionLibrary::SetNiagaraArrayVector(Plume, TEXT("User.NozzleDirections"), CrowdedDirections);
		Plume->SetVariableInt(TEXT("User.NozzleCount"), BottleCount);
		struct FDiskSample
		{
			int32 Observed = 0;
			int32 Interior = 0;
			int32 OuterHalf = 0;
			double SumSquaredFraction = 0;
			bool bInsideDisk = true;
		};
		const FName Emitters[] = {TEXT("ColaJet"), TEXT("ColaFan")};
		FDiskSample Samples[2];
		for (int32 Step = 0; Step < 120; ++Step)
		{
			Plume->AdvanceSimulation(1, .001f);
			for (int32 Layer = 0; Layer < 2; ++Layer)
			{
				const TArray<FVector> Points = Positions(Emitters[Layer]);
				auto& Sample = Samples[Layer];
				for (int32 J = Sample.Observed; J < Points.Num(); ++J)
				{
					const int32 Mouth = FMath::RoundToInt(Points[J].Y / 1000.);
					if (!CrowdedPositions.IsValidIndex(Mouth)) { Sample.bInsideDisk = false; continue; }
					const FVector Offset = Points[J] - CrowdedPositions[Mouth];
					const double Depth = FVector::DotProduct(Offset, CrowdedDirections[Mouth]);
					const double Radius = (Offset - Depth * CrowdedDirections[Mouth]).Size();
					const double BoreRadius = 10.3 * CrowdedScales[Mouth];
					const double Fraction = Radius / BoreRadius;
					Sample.Interior += Fraction < .5;
					Sample.OuterHalf += Fraction > .7071;
					Sample.SumSquaredFraction += Fraction * Fraction;
					// Include motion between spawn and observation (<= 1.02 cm).
					Sample.bInsideDisk &= FMath::Abs(Depth) < 1.1 && Radius <= BoreRadius + .8;
				}
				Sample.Observed = Points.Num();
			}
		}
		for (int32 Layer = 0; Layer < 2; ++Layer)
		{
			const auto& S = Samples[Layer];
			const FString Name = Emitters[Layer].ToString();
			TestTrue(Name + TEXT(" births fit the scaled nozzle plane"), S.bInsideDisk && S.Observed > 30);
			TestTrue(Name + TEXT(" births cover the interior and outer half of the disk"),
				S.Interior > 0 && S.OuterHalf > 0 && FMath::IsNearlyEqual(S.SumSquaredFraction / FMath::Max(1, S.Observed), .5, .16));
			AddInfo(FString::Printf(TEXT("%s: %d nozzle-face births, %d in outer half by area"), *Name, S.Observed, S.OuterHalf));
		}
		return true;
	})));
	return true;
}
#endif
