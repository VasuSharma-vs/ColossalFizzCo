#include "Misc/AutomationTest.h"
#include "Shop/ShopClock.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopDemoClockTest, "ColossalFizzCo.Shop.Clock.Demo", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopDemoClockTest::RunTest(const FString& Parameters)
{
	constexpr double DaySeconds = 3.0 * 60.0 * 60.0;
	constexpr double ClosingSeconds = 10.0;
	constexpr double SecondsPerGameMinute = (DaySeconds - ClosingSeconds) / (13.0 * 60.0);
	auto Check = [this](const TCHAR* Label, double Seconds, int32 Day, int32 Minute, EShopClockPhase Phase)
	{
		const FShopClockState State = ShopClock::EvaluateDemo(Seconds);
		TestTrue(FString::Printf(TEXT("%s uses demo time"), Label), State.bIsDemo);
		TestEqual(FString::Printf(TEXT("%s day"), Label), State.DayNumber, Day);
		TestEqual(FString::Printf(TEXT("%s minute"), Label), State.MinuteOfDay, Minute);
		TestEqual(FString::Printf(TEXT("%s phase"), Label), State.Phase, Phase);
	};

	Check(TEXT("First morning"), 0.0, 1, 6 * 60, EShopClockPhase::Preparing);
	Check(TEXT("Immediately before opening"), 180.0 * SecondsPerGameMinute - 0.001, 1, 9 * 60 - 1, EShopClockPhase::Preparing);
	Check(TEXT("Opening"), 180.0 * SecondsPerGameMinute, 1, 9 * 60, EShopClockPhase::Open);
	Check(TEXT("Immediately before noon"), 360.0 * SecondsPerGameMinute - 0.001, 1, 12 * 60 - 1, EShopClockPhase::Open);
	Check(TEXT("Noon rush"), 360.0 * SecondsPerGameMinute, 1, 12 * 60, EShopClockPhase::Rush);
	Check(TEXT("Last minute of rush"), 420.0 * SecondsPerGameMinute - 0.001, 1, 13 * 60 - 1, EShopClockPhase::Rush);
	Check(TEXT("Regular afternoon trading"), 420.0 * SecondsPerGameMinute, 1, 13 * 60, EShopClockPhase::Open);
	Check(TEXT("Immediately before closing"), DaySeconds - ClosingSeconds - 0.001, 1, 19 * 60 - 1, EShopClockPhase::Open);
	Check(TEXT("Closing"), DaySeconds - ClosingSeconds, 1, 19 * 60, EShopClockPhase::Closed);
	Check(TEXT("End of the closing transition"), DaySeconds - 0.001, 1, 19 * 60, EShopClockPhase::Closed);
	Check(TEXT("Next morning at exactly three hours"), DaySeconds, 2, 6 * 60, EShopClockPhase::Preparing);
	Check(TEXT("Rush after several complete cycles"), 3.0 * DaySeconds + 360.0 * SecondsPerGameMinute, 4, 12 * 60, EShopClockPhase::Rush);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShopRealWorldClockTest, "ColossalFizzCo.Shop.Clock.RealWorld", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FShopRealWorldClockTest::RunTest(const FString& Parameters)
{
	const FDateTime FirstLocalTime(2026, 12, 31, 5, 0);
	auto Check = [this, FirstLocalTime](const TCHAR* Label, const FDateTime& LocalTime, int32 Day, EShopClockPhase Phase)
	{
		const FShopClockState State = ShopClock::EvaluateRealWorld(LocalTime, FirstLocalTime);
		TestFalse(FString::Printf(TEXT("%s follows wall time"), Label), State.bIsDemo);
		TestEqual(FString::Printf(TEXT("%s day"), Label), State.DayNumber, Day);
		TestEqual(FString::Printf(TEXT("%s minute"), Label), State.MinuteOfDay, LocalTime.GetHour() * 60 + LocalTime.GetMinute());
		TestEqual(FString::Printf(TEXT("%s phase"), Label), State.Phase, Phase);
	};

	Check(TEXT("Before opening"), FDateTime(2026, 12, 31, 8, 59, 59), 1, EShopClockPhase::Preparing);
	Check(TEXT("Opening"), FDateTime(2026, 12, 31, 9, 0), 1, EShopClockPhase::Open);
	Check(TEXT("Before noon"), FDateTime(2026, 12, 31, 11, 59, 59), 1, EShopClockPhase::Open);
	Check(TEXT("Noon rush"), FDateTime(2026, 12, 31, 12, 0), 1, EShopClockPhase::Rush);
	Check(TEXT("During rush"), FDateTime(2026, 12, 31, 12, 42), 1, EShopClockPhase::Rush);
	Check(TEXT("Afternoon trading"), FDateTime(2026, 12, 31, 13, 0), 1, EShopClockPhase::Open);
	Check(TEXT("Evening trading"), FDateTime(2026, 12, 31, 19, 0), 1, EShopClockPhase::Open);
	Check(TEXT("Before midnight"), FDateTime(2026, 12, 31, 23, 59, 59), 1, EShopClockPhase::Open);
	Check(TEXT("Midnight within the same business day"), FDateTime(2027, 1, 1, 0, 0), 1, EShopClockPhase::Open);
	Check(TEXT("Before overnight closing"), FDateTime(2027, 1, 1, 3, 59, 59), 1, EShopClockPhase::Open);
	Check(TEXT("Closing and next business day"), FDateTime(2027, 1, 1, 4, 0), 2, EShopClockPhase::Preparing);
	Check(TEXT("Morning after skipped days"), FDateTime(2027, 1, 3, 6, 37), 4, EShopClockPhase::Preparing);
	TestEqual(TEXT("A session started after midnight starts on day one"),
		ShopClock::EvaluateRealWorld(FDateTime(2027, 1, 1, 3, 30), FDateTime(2027, 1, 1, 1, 0)).DayNumber, 1);
	return true;
}
#endif
