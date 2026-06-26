#include "pch.h"
#include "WorldObjects.h"
#include "GothicAPI.h"
#include "Engine.h"
#include "BaseGraphicsEngine.h"
#include "zCVob.h"
#include "zCMaterial.h"
#include "zCTexture.h"
#include "D3D11_Helpers.h"

namespace {
    constexpr float VOB_INDOOR_LIGHT_EDGE_TOLERANCE = 20.0f;
    constexpr float VOB_INDOOR_LIGHT_TRACE_HEIGHT = 50.0f;
    constexpr float VOB_INDOOR_LIGHT_RESAMPLE_DISTANCE_SQ = 25.0f;

    XMFLOAT3 GetVobLightSampleCenter( const zCVob* vob ) {
        const zTBBox3D bbox = vob->GetBBox();
        const XMFLOAT3 pos = vob->GetPositionWorld();
        return XMFLOAT3(
            (bbox.Min.x + bbox.Max.x) * 0.5f,
            pos.y,
            (bbox.Min.z + bbox.Max.z) * 0.5f );
    }

    float DistanceSq( const XMFLOAT3& a, const XMFLOAT3& b ) {
        const float dx = a.x - b.x;
        const float dy = a.y - b.y;
        const float dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    bool TraceIndoorWorldAt( const XMFLOAT3& sample, bool& indoor ) {
        indoor = false;
        if ( !Engine::GAPI ) {
            return false;
        }

        XMFLOAT3 hit;
        const XMFLOAT3 origin( sample.x, sample.y + VOB_INDOOR_LIGHT_TRACE_HEIGHT, sample.z );
        return Engine::GAPI->TraceWorldMesh( origin, XMFLOAT3( 0.0f, -1.0f, 0.0f ), hit,
            nullptr, nullptr, nullptr, nullptr, &indoor );
    }

    bool HasIndoorWorldWithinTolerance( const XMFLOAT3& center ) {
        static const XMFLOAT3 offsets[] = {
            XMFLOAT3( VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, 0.0f ),
            XMFLOAT3( -VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, 0.0f ),
            XMFLOAT3( 0.0f, 0.0f, VOB_INDOOR_LIGHT_EDGE_TOLERANCE ),
            XMFLOAT3( 0.0f, 0.0f, -VOB_INDOOR_LIGHT_EDGE_TOLERANCE ),
            XMFLOAT3( VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, VOB_INDOOR_LIGHT_EDGE_TOLERANCE ),
            XMFLOAT3( VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, -VOB_INDOOR_LIGHT_EDGE_TOLERANCE ),
            XMFLOAT3( -VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, VOB_INDOOR_LIGHT_EDGE_TOLERANCE ),
            XMFLOAT3( -VOB_INDOOR_LIGHT_EDGE_TOLERANCE, 0.0f, -VOB_INDOOR_LIGHT_EDGE_TOLERANCE )
        };

        for ( const XMFLOAT3& offset : offsets ) {
            bool indoor = false;
            const XMFLOAT3 sample( center.x + offset.x, center.y, center.z + offset.z );
            if ( TraceIndoorWorldAt( sample, indoor ) && indoor ) {
                return true;
            }
        }

        return false;
    }
}

/** Updates the vobs constantbuffer */
void VobInfo::UpdateVobConstantBuffer(VS_ExConstantBuffer_PerInstance& cb) {
    UpdateState();
    cb.World = WorldMatrix;
    cb.Color = {0.0f, 0.0f, 0.0f, IndoorLightMask ? 0.05f : 1.0f};
}

bool VobInfo::ComputeIndoorLightMask() const {
    if ( !Vob ) {
        return false;
    }

    const bool gothicIndoor = Vob->IsIndoorVob();
    const XMFLOAT3 center = GetVobLightSampleCenter( Vob );

    bool centerIndoor = false;
    if ( TraceIndoorWorldAt( center, centerIndoor ) ) {
        if ( centerIndoor ) {
            return true;
        }

        // If Gothic already reports the vob as indoor, tolerate only a narrow edge overlap.
        return gothicIndoor && HasIndoorWorldWithinTolerance( center );
    }

    return gothicIndoor;
}

void VobInfo::UpdateState() {
    WorldMatrix = *Vob->GetWorldMatrixPtr();
    LastRenderPosition = Vob->GetPositionWorld();

    const bool currentIndoorVob = Vob->IsIndoorVob();
    const XMFLOAT3 lightSampleCenter = GetVobLightSampleCenter( Vob );
    if ( !HasIndoorLightMaskSample
        || currentIndoorVob != IsIndoorVob
        || DistanceSq( lightSampleCenter, LastIndoorLightMaskPosition ) > VOB_INDOOR_LIGHT_RESAMPLE_DISTANCE_SQ ) {
        IsIndoorVob = currentIndoorVob;
        IndoorLightMask = ComputeIndoorLightMask();
        LastIndoorLightMaskPosition = lightSampleCenter;
        HasIndoorLightMaskSample = true;
    } else {
        IsIndoorVob = currentIndoorVob;
    }

    // Colorize the vob according to the underlaying polygon
    if ( IndoorLightMask ) {
        // All lightmapped polys have this color, so just use it
        GroundColor = DEFAULT_LIGHTMAP_POLY_COLOR;
    } else {
        // Get the color of the first found feature of the ground poly
        GroundColor = Vob->GetGroundPoly() ? Vob->GetGroundPoly()->getFeatures()[0]->lightStatic : 0xFFFFFFFF;
    }
}

/** Updates the vobs constantbuffer */
void SkeletalVobInfo::UpdateVobConstantBuffer(VS_ExConstantBuffer_PerInstance& cb) {
    UpdateState();
    cb.World = WorldMatrix;
    cb.Color = {0.0f, 0.0f, 0.0f, IndoorVob ? 0.05f : 1.0f};
}

void SkeletalVobInfo::UpdateState() {
    WorldMatrix = *Vob->GetWorldMatrixPtr();
}

SectionInstanceCache::~SectionInstanceCache() {
    InstanceCache.clear();
}

MeshInfo::~MeshInfo() {
    //Engine::GAPI->GetRendererState().RendererInfo.VOBVerticesDataSize -= Indices.size() * sizeof(VERTEX_INDEX);
    //Engine::GAPI->GetRendererState().RendererInfo.VOBVerticesDataSize -= Vertices.size() * sizeof(ExVertexStruct);

    delete MeshVertexBuffer;
    delete MeshIndexBuffer;
    delete MeshShadowIndexBuffer;
}

SkeletalMeshInfo::~SkeletalMeshInfo() {
    Engine::GAPI->GetRendererState().RendererInfo.SkeletalVerticesDataSize -= Indices.size() * sizeof( VERTEX_INDEX );
    Engine::GAPI->GetRendererState().RendererInfo.SkeletalVerticesDataSize -= Vertices.size() * sizeof( ExSkelVertexStruct );

    delete MeshVertexBuffer;
    delete MeshIndexBuffer;
}

/** Clears the cache for the given progmesh */
void SectionInstanceCache::ClearCacheForStatic( MeshVisualInfo* pm ) {
    if ( InstanceCache.find( pm ) != InstanceCache.end() ) {
        InstanceCache[pm].reset();
        InstanceCacheData[pm].clear();
    }
}

/** Saves this sections mesh to a file */
void WorldMeshSectionInfo::SaveSectionMeshToFile( const std::string& name ) {
    FILE* f;
    fopen_s( &f, name.c_str(), "wb" );

    if ( !f )
        return;
}

/** Creates buffers for this mesh info */
XRESULT MeshInfo::Create( ExVertexStruct* vertices, unsigned int numVertices, VERTEX_INDEX* indices, unsigned int numIndices ) {
    Vertices.resize( numVertices );
    memcpy( &Vertices[0], vertices, numVertices * sizeof( ExVertexStruct ) );

    Indices.resize( numIndices );
    memcpy( &Indices[0], indices, numIndices * sizeof( VERTEX_INDEX ) );

    // Create the buffers
    Engine::GraphicsEngine->CreateVertexBuffer( &MeshVertexBuffer );
    Engine::GraphicsEngine->CreateVertexBuffer( &MeshIndexBuffer );

    // Init and fill it
    MeshVertexBuffer->Init( vertices, numVertices * sizeof( ExVertexStruct ) );
    MeshIndexBuffer->Init( indices, numIndices * sizeof( VERTEX_INDEX ), D3D11VertexBuffer::B_INDEXBUFFER );

    Engine::GAPI->GetRendererState().RendererInfo.VOBVerticesDataSize += numVertices * sizeof( ExVertexStruct );
    Engine::GAPI->GetRendererState().RendererInfo.VOBVerticesDataSize += numIndices * sizeof( VERTEX_INDEX );

    return XR_SUCCESS;
}
