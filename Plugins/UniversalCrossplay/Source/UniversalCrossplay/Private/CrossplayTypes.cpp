#include "CrossplayTypes.h"
bool FCrossplayChatPayload::NetSerialize(FArchive& Ar,UPackageMap*,bool& Success)
{
 uint32 N=Ar.IsSaving() ? uint32(Text.Len()) : 0;
 if (Ar.IsSaving() && N>256) {Success=false; return false;}
 Ar.SerializeInt(N,257);
 if (N>256) {Ar.SetError();Success=false;return false;}
 if (Ar.IsLoading()) Text.Reset(N);
 for (uint32 I=0;I<N;++I) {uint16 C=Ar.IsSaving() ? uint16(Text[I]) : 0; Ar<<C; if (Ar.IsLoading()) Text.AppendChar(TCHAR(C));}
 Success=!Ar.IsError(); return Success;
}
bool FCrossplayChatPolicy::Validate(const FString& Text,FString& Clean)
{
 if (Text.Len()>256) return false;
 Clean=Text.TrimStartAndEnd(); if (Clean.IsEmpty()) return false;
 for (TCHAR C:Clean) if (C<32 || (C>=127 && C<=159)) return false;
 return true;
}
bool FCrossplayChatPolicy::Accept(uint32 Sequence,double Now)
{
 if (!FMath::IsFinite(Now) || Sequence==0 || Sequence<=LastSequence || Now<LastTime) return false;
 Tokens=FMath::Min(4.,Tokens+(Now-LastTime)*1.5);LastTime=Now;LastSequence=Sequence;
 if (Tokens<1) return false;--Tokens;return true;
}
