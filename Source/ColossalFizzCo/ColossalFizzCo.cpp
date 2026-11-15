// Fill out your copyright notice in the Description page of Project Settings.

#include "ColossalFizzCo.h"
#include "Modules/ModuleManager.h"
#include "UObject/CoreRedirects.h"

class FColossalFizzCoModule : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		FDefaultGameModuleImpl::StartupModule();
#if !WITH_EDITOR
		// Cooked builds discard editor redirector packages. Old save-game object
		// paths must still resolve before their product definitions deserialize.
		FCoreRedirects::AddRedirectList({
			FCoreRedirect(ECoreRedirectFlags::Type_Package,
				TEXT("/Game/Assest/meshs/Cola1"),TEXT("/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle")),
			FCoreRedirect(ECoreRedirectFlags::Type_Object,
				TEXT("/Game/Assest/meshs/Cola1.Cola1"),TEXT("/Game/Assest/meshs/PlasticBottle/SM_PlasticBottle.SM_PlasticBottle"))
		},TEXT("ColossalFizzCo legacy bottle migration"));
#endif
	}
};

IMPLEMENT_PRIMARY_GAME_MODULE( FColossalFizzCoModule, ColossalFizzCo, "ColossalFizzCo" );
