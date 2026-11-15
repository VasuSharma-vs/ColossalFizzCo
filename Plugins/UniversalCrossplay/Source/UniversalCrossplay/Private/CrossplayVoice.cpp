#include "CrossplaySubsystem.h"
#include "CrossplayOnlineState.h"
#include "CrossplaySettings.h"
#include "IOnlineSubsystemEOS.h"
#include "VoiceChat.h"

void UCrossplaySubsystem::BindVoice()
{
 if (!Online || !GetDefault<UCrossplaySettings>()->bEnableVoice || !Online->Provider || !Online->Identity) return;
 const auto Id=Online->Identity->GetUniquePlayerId(0);if (!Id) return;
 Online->Voice=static_cast<IOnlineSubsystemEOS*>(Online->Provider)->GetVoiceChatUserInterface(*Id);
 if (!Online->Voice) {OnVoice.Broadcast(false,TEXT("Voice is unavailable; check deployment voice support and microphone access."));return;}
 auto* V=Online->Voice;
 Online->bMicMuted=GetDefault<UCrossplaySettings>()->bMicrophoneMutedByDefault;V->SetAudioInputDeviceMuted(Online->bMicMuted);V->SetAudioOutputVolume(Online->Volume);
 V->TransmitToNoChannels();
 Online->VoiceJoined=V->OnVoiceChatChannelJoined().AddWeakLambda(this,[this](const FString&)
 {if (!Online || !Online->Voice) return;if (!Online->bMicMuted) Online->Voice->TransmitToAllChannels();OnVoice.Broadcast(true,TEXT("Voice connected"));});
 Online->VoiceExited=V->OnVoiceChatChannelExited().AddWeakLambda(this,[this](const FString&,const FVoiceChatResult&)
 {OnVoice.Broadcast(false,TEXT("Voice channel left or interrupted"));});
 Online->VoiceDevices=V->OnVoiceChatAvailableAudioDevicesChanged().AddWeakLambda(this,[this]
 {OnVoice.Broadcast(Online && Online->Voice && !Online->Voice->GetChannels().IsEmpty(),TEXT("Audio devices changed; refresh device selection."));});
}
void UCrossplaySubsystem::ReleaseVoice()
{
 if (!Online || !Online->Voice) return;
 auto* V=Online->Voice;V->OnVoiceChatChannelJoined().Remove(Online->VoiceJoined);V->OnVoiceChatChannelExited().Remove(Online->VoiceExited);V->OnVoiceChatAvailableAudioDevicesChanged().Remove(Online->VoiceDevices);
 V->TransmitToNoChannels();V->SetAudioInputDeviceMuted(true);Online->Voice=nullptr;
 // OSS owns this user and its lobby RTC membership. DestroySession/Logout leaves rooms.
}
bool UCrossplaySubsystem::SetMicrophoneMuted(bool Muted)
{
 if (!Online || !Online->Voice) return false;
 if (!Muted && Online->Voice->GetAvailableInputDeviceInfos().IsEmpty()) {OnVoice.Broadcast(false,TEXT("No microphone is available. Check the OS microphone permission and selected device."));return false;}
 Online->bMicMuted=Muted;Online->Voice->SetAudioInputDeviceMuted(Muted);
 if (Muted) Online->Voice->TransmitToNoChannels();else Online->Voice->TransmitToAllChannels();return true;
}
bool UCrossplaySubsystem::IsMicrophoneMuted() const {return !Online || Online->bMicMuted;}
bool UCrossplaySubsystem::HasPendingInvite() const {return Online && (!Online->InviteId.IsEmpty() || Online->Invite.IsValid());}
bool UCrossplaySubsystem::SetVoiceVolume(float Volume)
{if (!Online || !Online->Voice || !FMath::IsFinite(Volume)) return false;Online->Volume=FMath::Clamp(Volume,0.f,2.f);Online->Voice->SetAudioOutputVolume(Online->Volume);return true;}
bool UCrossplaySubsystem::SetPlayerVoiceMuted(const FString& Id,bool Muted)
{
 if (!Online || !Online->Voice || Id.IsEmpty() || Id.Len()>128) return false;
 bool Present=false;for (const auto& Channel:Online->Voice->GetChannels()) Present|=Online->Voice->GetPlayersInChannel(Channel).Contains(Id);
 if (!Present) return false;Online->Voice->SetPlayerMuted(Id,Muted);return true;
}
TArray<FCrossplayDevice> UCrossplaySubsystem::GetVoiceDevices(bool Input) const
{
 TArray<FCrossplayDevice> Out;if (!Online || !Online->Voice) return Out;
 const auto Devices=Input ? Online->Voice->GetAvailableInputDeviceInfos() : Online->Voice->GetAvailableOutputDeviceInfos();
 for (const auto& D:Devices) {FCrossplayDevice Item;Item.Id=D.Id;Item.Name=D.DisplayName;Out.Add(Item);}return Out;
}
bool UCrossplaySubsystem::SetVoiceDevice(const FString& Id,bool Input)
{
 if (!Online || !Online->Voice || Id.Len()>256) return false;
 const auto Devices=GetVoiceDevices(Input);if (!Id.IsEmpty() && !Devices.ContainsByPredicate([&](const auto& D){return D.Id==Id;})) return false;
 if (Input) Online->Voice->SetInputDeviceId(Id);else Online->Voice->SetOutputDeviceId(Id);return true;
}
