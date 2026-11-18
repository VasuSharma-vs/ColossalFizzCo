#include "CollisionCableRendering.h"
#include "Components/CollisionCableComponent.h"
#include "DynamicMeshBuilder.h"
#include "LocalVertexFactory.h"
#include "Materials/Material.h"
#include "Materials/MaterialRenderProxy.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"
#include "StaticMeshResources.h"

namespace CollisionCableRendering
{
    void BuildMeshData(const TArray<FVector>& Points,float Width,int32 Sides,float Tiling,TArray<FDynamicMeshVertex>& Vertices,TArray<uint32>& Indices)
    {
        Vertices.Reset();Indices.Reset();if(Points.Num()<2)return;
        Sides=FMath::Clamp(Sides,3,16);Width=FMath::Max(Width,.1f);
        Vertices.Reserve(Points.Num()*(Sides+1));
        TArray<double> Arc;Arc.Init(0.,Points.Num());
        for(int32 I=1;I<Points.Num();++I)Arc[I]=Arc[I-1]+FVector::Distance(Points[I-1],Points[I]);
        const double Total=FMath::Max(Arc.Last(),UE_SMALL_NUMBER);
        FVector PreviousRight=FVector::RightVector;
        for(int32 I=0;I<Points.Num();++I)
        {
            const FVector Tangent=(Points[FMath::Min(I+1,Points.Num()-1)]-Points[FMath::Max(I-1,0)]).GetSafeNormal(UE_SMALL_NUMBER,FVector::ForwardVector);
            FVector Right=(PreviousRight-Tangent*FVector::DotProduct(PreviousRight,Tangent)).GetSafeNormal();
            if(Right.IsNearlyZero()){FVector Unused;Tangent.FindBestAxisVectors(Right,Unused);}
            const FVector Up=FVector::CrossProduct(Tangent,Right).GetSafeNormal();PreviousRight=Right;
            for(int32 Side=0;Side<=Sides;++Side)
            {
                const float Angle=2.f*PI*Side/Sides;const FVector Normal=Right*FMath::Cos(Angle)+Up*FMath::Sin(Angle);
                FDynamicMeshVertex Vertex;Vertex.Position=FVector3f(Points[I]+Normal*Width*.5f);
                // V increases around the ring in Tangent x Normal direction.
                // Preserve the correct handedness for one-sided normal maps.
                Vertex.SetTangents(FVector3f(Tangent),FVector3f(FVector::CrossProduct(Tangent,Normal)),FVector3f(Normal));
                Vertex.TextureCoordinate[0]=FVector2f(float(Arc[I]/Total)*Tiling,float(Side)/Sides);Vertex.Color=FColor::White;Vertices.Add(Vertex);
            }
        }
        for(int32 I=0;I+1<Points.Num();++I)for(int32 Side=0;Side<Sides;++Side)
        {
            const uint32 A=I*(Sides+1)+Side,B=A+Sides+1;
            // Unreal's clockwise front faces must face the outside of the tube.
            Indices.Append({A,B,A+1,A+1,B,B+1});
        }
    }

    /** Geometry/GPU buffers are rebuilt on a published change, never per view. */
    class FSceneProxy final:public FPrimitiveSceneProxy
    {
    public:
        explicit FSceneProxy(const UCollisionCableComponent* Component)
            :FPrimitiveSceneProxy(Component),VertexFactory(GetScene().GetFeatureLevel(),"CollisionCableVF"),
             Width(Component->CableWidth),Sides(FMath::Clamp(Component->NumSides,3,16)),Tiling(Component->TileMaterial),
             Material(Component->GetMaterial(0)?Component->GetMaterial(0):UMaterial::GetDefaultMaterial(MD_Surface)),
             Relevance(Material->GetRelevance_Concurrent(GetScene().GetShaderPlatform()))
        {
            Component->GetCableRenderLocations(Points);
            for(auto& P:Points)P-=Component->GetComponentLocation();
            BuildBuffers();
        }
        virtual ~FSceneProxy()override{ReleaseBuffers();}
        virtual void CreateRenderThreadResources(FRHICommandListBase& Commands)override{InitBuffers(Commands);}
        virtual SIZE_T GetTypeHash()const override{static size_t Unique;return reinterpret_cast<SIZE_T>(&Unique);}
        virtual uint32 GetMemoryFootprint()const override{return uint32(sizeof(*this)+Points.GetAllocatedSize()+Indices.Indices.GetAllocatedSize());}
        virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View)const override
        {
            FPrimitiveViewRelevance Result;Result.bDrawRelevance=IsShown(View);Result.bDynamicRelevance=true;
            Result.bShadowRelevance=IsShadowCast(View);Result.bRenderInMainPass=ShouldRenderInMainPass();Result.bRenderCustomDepth=ShouldRenderCustomDepth();
            Relevance.SetPrimitiveViewRelevance(Result);return Result;
        }
        virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,const FSceneViewFamily& Family,uint32 VisibilityMap,FMeshElementCollector& Collector)const override
        {
            if(!bReady||Indices.Indices.Num()<3)return;
            for(int32 I=0;I<Views.Num();++I)if(VisibilityMap&(1u<<I))
            {
                FMeshBatch& Mesh=Collector.AllocateMesh();Mesh.VertexFactory=&VertexFactory;Mesh.MaterialRenderProxy=Material->GetRenderProxy();
                Mesh.Type=PT_TriangleList;Mesh.DepthPriorityGroup=SDPG_World;Mesh.ReverseCulling=IsLocalToWorldDeterminantNegative();
                FMeshBatchElement& Element=Mesh.Elements[0];Element.IndexBuffer=&Indices;Element.FirstIndex=0;
                Element.NumPrimitives=Indices.Indices.Num()/3;Element.MinVertexIndex=0;Element.MaxVertexIndex=Buffers.PositionVertexBuffer.GetNumVertices()-1;
                Element.PrimitiveUniformBuffer=GetUniformBuffer();Collector.AddMesh(I,Mesh);
            }
        }
        void Update(FRHICommandListBase& Commands,TArray<FVector>&& NewPoints,float NewWidth,float NewTiling)
        {
            check(IsInRenderingThread());
            ReleaseBuffers();Points=MoveTemp(NewPoints);Width=NewWidth;Tiling=NewTiling;BuildBuffers();InitBuffers(Commands);
        }
    private:
        TArray<FVector> Points;
        FStaticMeshVertexBuffers Buffers;
        FDynamicMeshIndexBuffer32 Indices;
        FLocalVertexFactory VertexFactory;
        float Width;int32 Sides;float Tiling;
        UMaterialInterface* Material;
        FMaterialRelevance Relevance;
        bool bReady=false;
        void ReleaseBuffers()
        {
            VertexFactory.ReleaseResource();Buffers.PositionVertexBuffer.ReleaseResource();
            Buffers.StaticMeshVertexBuffer.ReleaseResource();Buffers.ColorVertexBuffer.ReleaseResource();Indices.ReleaseResource();bReady=false;
        }
        void BuildBuffers()
        {
            TArray<FDynamicMeshVertex> Vertices;Indices.Indices.Reset();
            BuildMeshData(Points,Width,Sides,Tiling,Vertices,Indices.Indices);
            if(Vertices.IsEmpty())return;
            Buffers.PositionVertexBuffer.Init(Vertices.Num());Buffers.StaticMeshVertexBuffer.Init(Vertices.Num(),1);Buffers.ColorVertexBuffer.Init(Vertices.Num());
            for(int32 I=0;I<Vertices.Num();++I)
            {
                const auto& V=Vertices[I];Buffers.PositionVertexBuffer.VertexPosition(I)=V.Position;
                Buffers.StaticMeshVertexBuffer.SetVertexTangents(I,V.TangentX.ToFVector3f(),V.GetTangentY(),V.TangentZ.ToFVector3f());
                Buffers.StaticMeshVertexBuffer.SetVertexUV(I,0,V.TextureCoordinate[0]);Buffers.ColorVertexBuffer.VertexColor(I)=V.Color;
            }
        }
        void InitBuffers(FRHICommandListBase& Commands)
        {
            if(Indices.Indices.IsEmpty())return;
            Buffers.PositionVertexBuffer.InitResource(Commands);Buffers.StaticMeshVertexBuffer.InitResource(Commands);
            Buffers.ColorVertexBuffer.InitResource(Commands);Indices.InitResource(Commands);
            FLocalVertexFactory::FDataType Data;
            Buffers.PositionVertexBuffer.BindPositionVertexBuffer(&VertexFactory,Data);
            Buffers.StaticMeshVertexBuffer.BindTangentVertexBuffer(&VertexFactory,Data);
            Buffers.StaticMeshVertexBuffer.BindPackedTexCoordVertexBuffer(&VertexFactory,Data);
            Buffers.StaticMeshVertexBuffer.BindLightMapVertexBuffer(&VertexFactory,Data,0);
            Buffers.ColorVertexBuffer.BindColorVertexBuffer(&VertexFactory,Data);
            VertexFactory.SetData(Commands,Data);VertexFactory.InitResource(Commands);bReady=true;
        }
    };

    FPrimitiveSceneProxy* CreateProxy(const UCollisionCableComponent* Component){return new FSceneProxy(Component);}
    void UpdateProxy(FRHICommandListBase& Commands,FPrimitiveSceneProxy* Proxy,TArray<FVector>&& Points,float Width,float Tiling)
    {static_cast<FSceneProxy*>(Proxy)->Update(Commands,MoveTemp(Points),Width,Tiling);}
}
