#pragma once
#include "CoreMinimal.h"
#include "DynamicMeshBuilder.h"
class UCollisionCableComponent;
class FPrimitiveSceneProxy;
class FRHICommandListBase;
namespace CollisionCableRendering
{
    /** Shared by the actual GPU proxy and winding/tangent regression tests. */
    void BuildMeshData(const TArray<FVector>& Points,float Width,int32 Sides,float Tiling,TArray<FDynamicMeshVertex>& Vertices,TArray<uint32>& Indices);
    FPrimitiveSceneProxy* CreateProxy(const UCollisionCableComponent* Component);
    void UpdateProxy(FRHICommandListBase& Commands,FPrimitiveSceneProxy* Proxy,TArray<FVector>&& Points,float Width,float Tiling);
}
