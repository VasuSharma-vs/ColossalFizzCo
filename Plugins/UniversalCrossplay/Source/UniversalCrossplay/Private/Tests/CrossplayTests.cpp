#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "CrossplayTypes.h"
#include "CrossplayNetwork.h"
#include "IEOSSDKManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "eos_version.h"
#include <limits>
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossplayChatWireTest,"UniversalCrossplay.Chat.WireAndValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrossplayChatWireTest::RunTest(const FString&)
{
 FString Clean;TestFalse(TEXT("Empty rejected"),FCrossplayChatPolicy::Validate(TEXT("  "),Clean));
 TestFalse(TEXT("Control characters rejected"),FCrossplayChatPolicy::Validate(TEXT("hello\nspoof"),Clean));
 TestFalse(TEXT("Oversize rejected before RPC"),FCrossplayChatPolicy::Validate(FString::ChrN(257,'x'),Clean));
 FCrossplayChatPayload Out;Out.Text=FString::ChrN(256,'z');TArray<uint8> Bytes;FMemoryWriter Writer(Bytes);bool Success=false;
 Out.NetSerialize(Writer,nullptr,Success);TestTrue(TEXT("Bounded payload writes"),Success);TestTrue(TEXT("Payload stays below 1KB"),Bytes.Num()<1024);
 FCrossplayChatPayload In;FMemoryReader Reader(Bytes);In.NetSerialize(Reader,nullptr,Success);TestTrue(TEXT("Payload reads"),Success);TestEqual(TEXT("Text preserved at maximum size"),In.Text,Out.Text);
 Out.Text.AppendChar('x');FMemoryWriter Invalid(Bytes);TestFalse(TEXT("Oversize cannot serialize"),Out.NetSerialize(Invalid,nullptr,Success));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossplayChatRateTest,"UniversalCrossplay.Chat.ReplayAndSpam",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrossplayChatRateTest::RunTest(const FString&)
{
 FCrossplayChatPolicy P;for(uint32 I=1;I<=4;++I) TestTrue(TEXT("Small initial burst accepted"),P.Accept(I,1));
 TestFalse(TEXT("Burst limit enforced"),P.Accept(5,1));TestFalse(TEXT("Replay rejected"),P.Accept(5,2));
 TestTrue(TEXT("Budget refills"),P.Accept(6,2));TestFalse(TEXT("Clock reversal rejected"),P.Accept(7,1));
 TestFalse(TEXT("Invalid timestamp rejected"),P.Accept(7,std::numeric_limits<double>::quiet_NaN()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossplayAuthorityTest,"UniversalCrossplay.Lobby.AuthorityAndDeparture",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrossplayAuthorityTest::RunTest(const FString&)
{
 const auto Init=UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
 auto* W=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);if(!W)return false;
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
 ON_SCOPE_EXIT{W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
 W->InitializeActorsForPlay(FURL());W->GetWorldSettings()->NotifyBeginPlay();
 auto* S=W->SpawnActor<ACrossplayLobbyState>();
 auto MakePlayer=[&](int32 Id){auto* P=W->SpawnActor<APlayerController>();P->PlayerState=W->SpawnActor<APlayerState>();P->PlayerState->SetPlayerId(Id);P->PlayerState->SetPlayerName(FString::FromInt(Id));return P;};
 auto* Host=MakePlayer(1);auto* Client=MakePlayer(2);auto* Third=MakePlayer(3);S->OwnerId=S->Describe(Host).OnlineId;
 S->Register(Host);S->Register(Client);S->Register(Third);TestEqual(TEXT("Prototype capacity enforced"),S->Members.Num(),2);
 TestFalse(TEXT("Cannot start before ready"),S->CanStart(Host));S->SetReady(Host,true);S->SetReady(Client,true);
 TestTrue(TEXT("Owner may start when all ready"),S->CanStart(Host));TestFalse(TEXT("Remote member cannot start"),S->CanStart(Client));TestFalse(TEXT("Unregistered player cannot start"),S->CanStart(Third));
 S->Remove(Client);TestEqual(TEXT("Departure removes member"),S->Members.Num(),1);S->Register(Client);
 TestFalse(TEXT("Rejoining player must ready again"),S->CanStart(Host));S->bGameStarted=true;TestFalse(TEXT("Ready locked after start"),S->SetReady(Client,true));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCrossplaySDKTest,"UniversalCrossplay.SDK.RuntimeVersion",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FCrossplaySDKTest::RunTest(const FString&)
{
 const auto* M=IEOSSDKManager::Get();if (!TestNotNull(TEXT("Engine EOS lifetime owner present"),M)) return false;
 const FString Version=M->GetSDKVersion();AddInfo(TEXT("Engine-managed EOS runtime: ")+Version);
 TestTrue(TEXT("Engine uses supplied SDK1.19.2.1"),Version.StartsWith(TEXT("1.19.2.1")));
 TestTrue(TEXT("Game bridge resolves the same SDK"),FString(UTF8_TO_TCHAR(EOS_GetVersion())).StartsWith(TEXT("1.19.2.1")));return true;
}
#endif
