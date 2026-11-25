#include "Shop/ShopClock.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "ShopClockSubsystem.h"

namespace
{
	constexpr int32 PrepStart = 6 * 60;
	constexpr int32 Opening = 9 * 60;
	constexpr int32 RushStart = 12 * 60;
	constexpr int32 RushEnd = 13 * 60;
	constexpr int32 DemoClosing = 19 * 60;
	constexpr int32 FullClosingHour = 4;

	EShopClockPhase GetTradingPhase(int32 Minute)
	{
		if (Minute >= RushStart && Minute < RushEnd) return EShopClockPhase::Rush;
		return EShopClockPhase::Open;
	}
}

FShopClockState ShopClock::EvaluateDemo(double ElapsedSeconds, double DaySeconds, double ClosingSeconds)
{
	DaySeconds = FMath::Max(1.0, DaySeconds);
	ClosingSeconds = FMath::Clamp(ClosingSeconds, 0.1, DaySeconds * 0.5);
	ElapsedSeconds = FMath::Max(0.0, ElapsedSeconds);
	const double ActiveSeconds = DaySeconds - ClosingSeconds;
	const double SecondsIntoDay = FMath::Fmod(ElapsedSeconds, DaySeconds);

	FShopClockState Result;
	Result.DayNumber = FMath::FloorToInt(ElapsedSeconds / DaySeconds) + 1;
	Result.MinuteOfDay = DemoClosing;
	Result.Phase = EShopClockPhase::Closed;
	if (SecondsIntoDay < ActiveSeconds)
	{
		const int32 MinutesSincePrep = FMath::FloorToInt(SecondsIntoDay * (DemoClosing - PrepStart) / ActiveSeconds);
		Result.MinuteOfDay = FMath::Min(PrepStart + MinutesSincePrep, DemoClosing - 1);
		Result.Phase = Result.MinuteOfDay < Opening ? EShopClockPhase::Preparing : GetTradingPhase(Result.MinuteOfDay);
	}
	return Result;
}

FShopClockState ShopClock::EvaluateRealWorld(const FDateTime& LocalTime, const FDateTime& FirstLocalTime)
{
	// Trade after midnight belongs to the same business day until the 4 AM close.
	const FTimespan DayOffset = FTimespan::FromHours(FullClosingHour);
	const FDateTime BusinessDate = (LocalTime - DayOffset).GetDate();
	const FDateTime FirstBusinessDate = (FirstLocalTime - DayOffset).GetDate();
	FShopClockState Result;
	Result.DayNumber = FMath::Max(0, (BusinessDate - FirstBusinessDate).GetDays()) + 1;
	Result.MinuteOfDay = LocalTime.GetHour() * 60 + LocalTime.GetMinute();
	Result.Phase = Result.MinuteOfDay >= FullClosingHour * 60 && Result.MinuteOfDay < Opening
		? EShopClockPhase::Preparing : GetTradingPhase(Result.MinuteOfDay);
	Result.bIsDemo = false;
	return Result;
}

AShopClock::AShopClock()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(4.0f);
}

const AShopClock* AShopClock::Find(const UWorld* World)
{
	const auto* Subsystem = World ? World->GetSubsystem<UShopClockSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetClock() : nullptr;
}

void AShopClock::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// This world clock excludes pauses and is independent of gameplay time dilation.
		DemoStartSeconds = GetWorld()->GetAudioTimeSeconds();
		FirstLocalTime = FDateTime::Now();
		UpdateAuthorityState();
	}
	if (auto* Subsystem = GetWorld()->GetSubsystem<UShopClockSubsystem>())
		Subsystem->RegisterClock(*this);
}

void AShopClock::UpdateAuthorityState()
{
	const FShopClockState Next = ClockMode == EShopClockMode::RealWorld
		? ShopClock::EvaluateRealWorld(FDateTime::Now(), FirstLocalTime)
		: ShopClock::EvaluateDemo(GetWorld()->GetAudioTimeSeconds() - DemoStartSeconds, DemoDaySeconds, DemoClosingSeconds);
	if (Next == State) return;
	State = Next;
	ForceNetUpdate();
}

void AShopClock::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShopClock, State);
}
