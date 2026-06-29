#include "pch.h"
#include "D3D11OcclusionQuerry.h"
#include "Engine.h"
#include "D3D11GraphicsEngine.h"
#include "GothicAPI.h"
#include "zCBspTree.h"
#include "Toolbox.h"
#include "zCCamera.h"

// Conservative occlusion settings. The goal is stable rendering first, saved draw calls second.
// A single invisible query result is not enough to hide BSP leaves, because camera motion and
// one-frame GPU latency otherwise cause visible world rebuild/pop-in during fast turns.
const unsigned int VISIBLE_RECHECK_FRAME_DELAY = 2;
const unsigned int INVISIBLE_CONFIRM_FRAME_COUNT = 2;
const unsigned int VISIBLE_GRACE_FRAME_COUNT = 3;
const unsigned int FAST_CAMERA_GRACE_FRAME_COUNT = 6;
const float FAST_CAMERA_MOVE_DISTANCE = 350.0f;
const float FAST_CAMERA_TURN_DOT = 0.80f;

D3D11OcclusionQuerry::D3D11OcclusionQuerry() {
    FrameID = 0;
    PreviousCameraPosition = XMFLOAT3( 0.0f, 0.0f, 0.0f );
    PreviousCameraForward = XMFLOAT3( 0.0f, 0.0f, 1.0f );
    HasPreviousCamera = false;
    CameraRelaxedMode = false;
    ConservativeVisibleFrames = 0;
}

D3D11OcclusionQuerry::~D3D11OcclusionQuerry() {
    for ( size_t i = 0; i < Predicates.size(); i++ ) {
        SAFE_RELEASE( Predicates[i] );
    }
}

/** Creates a new predication-object and returns its ID */
unsigned int D3D11OcclusionQuerry::AddPredicationObject() {
    D3D11GraphicsEngine* g = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);

    HRESULT hr;
    Microsoft::WRL::ComPtr<ID3D11Predicate> p;
    
    // Create new predication-object
    D3D11_QUERY_DESC qd;
    qd.Query = D3D11_QUERY_OCCLUSION_PREDICATE;
    qd.MiscFlags = 0;
    LE( g->GetDevice()->CreatePredicate( &qd, p.GetAddressOf() ) );
    SetDebugName(p.Get(), "OcclusionPredicate");

    // Add to the end of the list and return its ID
    Predicates.push_back( p.Detach() );
    return Predicates.size() - 1;
}

XMFLOAT3 D3D11OcclusionQuerry::GetCameraForward() const {
    XMMATRIX invView = XMMatrixInverse( nullptr, Engine::GAPI->GetViewMatrixXM() );
    XMFLOAT3 forward;
    XMStoreFloat3( &forward, XMVector3Normalize( invView.r[2] ) );
    return forward;
}

/** Checks the BSP-Tree for visibility */
void D3D11OcclusionQuerry::DoOcclusionForBSP( BspInfo* root ) {
    if ( !root || !root->OriginalNode )
        return;

    D3D11GraphicsEngine* g = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);

    // Check if this node has its queryID
    if ( root->OcclusionInfo.QueryID == -1 ) {
        root->OcclusionInfo.QueryID = AddPredicationObject();
        CreateOcclusionNodeMeshFor( root );

        // First-frame safety: render new nodes until occlusion has had enough time to prove
        // invisibility. This is especially important directly after world/load transitions.
        root->OcclusionInfo.VisibleLastFrame = true;
        root->OcclusionInfo.LastVisibleFrameID = FrameID;
        root->OcclusionInfo.VisibleGraceUntilFrameID = FrameID + VISIBLE_GRACE_FRAME_COUNT;
        root->OcclusionInfo.InvisibleCandidateFrames = 0;
    }

    const int fstate = Engine::GAPI->GetCameraBBox3DInFrustum( root->OriginalNode->BBox3D,
        EGothicCullFlags::CullAll );

    // Outside the frustum is a safe CPU decision. Do not turn that into an occlusion miss;
    // frustum re-entry is handled conservatively below.
    if ( fstate == ZTCAM_CLIPTYPE_OUT ) {
        root->OcclusionInfo.LastCameraClipType = fstate;
        return;
    }

    const bool enteredFrustum = root->OcclusionInfo.LastCameraClipType == ZTCAM_CLIPTYPE_OUT;
    root->OcclusionInfo.LastCameraClipType = fstate;

    // During fast camera movement or immediate frustum re-entry, prefer stability over saving
    // draw calls. Marking the subtree visible prevents delayed GPU query answers from causing
    // visible pop-in while the player turns quickly.
    if ( CameraRelaxedMode || enteredFrustum ) {
        MarkTreeVisible( root, true, FAST_CAMERA_GRACE_FRAME_COUNT );
        return;
    }

    // Invisible nodes are rechecked every frame. Visible nodes are only rechecked periodically,
    // with a grace period so one bad query result cannot immediately hide them.
    if ( !root->OcclusionInfo.VisibleLastFrame ||
        (root->OcclusionInfo.LastVisitedFrameID + VISIBLE_RECHECK_FRAME_DELAY <= FrameID && root->OcclusionInfo.VisibleLastFrame) ) {

        if ( Toolbox::PositionInsideBox( Engine::GAPI->GetCameraPosition(), root->OriginalNode->BBox3D.Min, root->OriginalNode->BBox3D.Max ) ||
            (root->IsEmpty() && root->OriginalNode->IsLeaf()) ) {
            DoOcclusionForBSP( root->Front );
            DoOcclusionForBSP( root->Back );

            root->OcclusionInfo.VisibleLastFrame = true;
            root->OcclusionInfo.LastVisibleFrameID = FrameID;
            root->OcclusionInfo.VisibleGraceUntilFrameID = FrameID + VISIBLE_GRACE_FRAME_COUNT;
            root->OcclusionInfo.InvisibleCandidateFrames = 0;
            root->OcclusionInfo.LastVisitedFrameID = FrameID;
            return;
        }

        ID3D11Predicate* p = Predicates[root->OcclusionInfo.QueryID];

        UINT32 data = 0;
        const HRESULT queryResult = g->GetContext()->GetData( p, &data, sizeof( UINT32 ), D3D11_ASYNC_GETDATA_DONOTFLUSH );
        if ( queryResult == S_OK ) {
            if ( data > 0 ) {
                root->OcclusionInfo.VisibleLastFrame = true;
                root->OcclusionInfo.LastVisibleFrameID = FrameID;
                root->OcclusionInfo.VisibleGraceUntilFrameID = FrameID + VISIBLE_GRACE_FRAME_COUNT;
                root->OcclusionInfo.InvisibleCandidateFrames = 0;

                DoOcclusionForBSP( root->Front );
                DoOcclusionForBSP( root->Back );
            } else {
                const bool stillInGrace = root->OcclusionInfo.VisibleGraceUntilFrameID >= FrameID;
                if ( stillInGrace || root->OcclusionInfo.InvisibleCandidateFrames + 1 < INVISIBLE_CONFIRM_FRAME_COUNT ) {
                    root->OcclusionInfo.VisibleLastFrame = true;
                    root->OcclusionInfo.InvisibleCandidateFrames++;

                    DoOcclusionForBSP( root->Front );
                    DoOcclusionForBSP( root->Back );
                } else {
                    root->OcclusionInfo.VisibleLastFrame = false;
                    root->OcclusionInfo.InvisibleCandidateFrames = INVISIBLE_CONFIRM_FRAME_COUNT;
                    MarkTreeVisible( root->Front, false );
                    MarkTreeVisible( root->Back, false );
                }
            }

            root->OcclusionInfo.QueryInProgress = false;
        } else {
            // Pending query: keep prior visibility and continue descending if it was visible.
            // This is intentionally conservative; a pending GPU answer must never hide content.
            if ( root->OcclusionInfo.VisibleLastFrame ) {
                DoOcclusionForBSP( root->Front );
                DoOcclusionForBSP( root->Back );
            }
        }

        if ( !root->OcclusionInfo.QueryInProgress ) {
            MeshInfo* mi = root->OcclusionInfo.NodeMesh;
            if ( mi && !mi->Indices.empty() ) {
                g->GetContext()->Begin( p );
                g->DrawVertexBufferIndexed( mi->MeshVertexBuffer, mi->MeshIndexBuffer, mi->Indices.size() );
                g->GetContext()->End( p );
                root->OcclusionInfo.QueryInProgress = true;
            }
        }
        root->OcclusionInfo.LastVisitedFrameID = FrameID;
    } else if ( root->OcclusionInfo.VisibleLastFrame ) {
        // A visible parent can skip its own expensive query for a few frames, but its
        // children still need to update. Otherwise the hierarchy becomes effectively inert.
        DoOcclusionForBSP( root->Front );
        DoOcclusionForBSP( root->Back );
    }
}
/** Begins the occlusion-checks */
void D3D11OcclusionQuerry::BeginOcclusionPass() {
    D3D11GraphicsEngine* g = reinterpret_cast<D3D11GraphicsEngine*>(Engine::GraphicsEngine);

    // Bind shaders and constant buffers
    g->SetupVS_ExMeshDrawCall();
    g->SetupVS_ExConstantBuffer();

    // Unbind not needed shaders
    g->GetContext()->PSSetShader( nullptr, nullptr, 0 );
    g->GetContext()->HSSetShader( nullptr, nullptr, 0 );
    g->GetContext()->DSSetSamplers( 0, 0, nullptr );
}

/** Ends the occlusion-checks */
void D3D11OcclusionQuerry::EndOcclusionPass() {

}

/** Advances the frame counter of this */
void D3D11OcclusionQuerry::AdvanceFrameCounter() {
    FrameID++;

    const XMFLOAT3 cameraPosition = Engine::GAPI->GetCameraPosition();
    const XMFLOAT3 cameraForward = GetCameraForward();

    if ( HasPreviousCamera ) {
        const XMVECTOR curPos = XMLoadFloat3( &cameraPosition );
        const XMVECTOR prevPos = XMLoadFloat3( &PreviousCameraPosition );
        float moveDistance = 0.0f;
        XMStoreFloat( &moveDistance, XMVector3Length( curPos - prevPos ) );

        const XMVECTOR curForward = XMLoadFloat3( &cameraForward );
        const XMVECTOR prevForward = XMLoadFloat3( &PreviousCameraForward );
        float turnDot = 1.0f;
        XMStoreFloat( &turnDot, XMVector3Dot( curForward, prevForward ) );

        if ( moveDistance > FAST_CAMERA_MOVE_DISTANCE || turnDot < FAST_CAMERA_TURN_DOT ) {
            ConservativeVisibleFrames = FAST_CAMERA_GRACE_FRAME_COUNT;
        } else if ( ConservativeVisibleFrames > 0 ) {
            ConservativeVisibleFrames--;
        }
    } else {
        HasPreviousCamera = true;
        ConservativeVisibleFrames = VISIBLE_GRACE_FRAME_COUNT;
    }

    PreviousCameraPosition = cameraPosition;
    PreviousCameraForward = cameraForward;
    CameraRelaxedMode = ConservativeVisibleFrames > 0;
}

/** Creates the occlusion-node-mesh for the specific bsp-node */
void D3D11OcclusionQuerry::CreateOcclusionNodeMeshFor( BspInfo* node ) {
    MeshInfo* mi = new MeshInfo;
    float3 bbmin = node->OriginalNode->BBox3D.Min;
    float3 bbmax = node->OriginalNode->BBox3D.Max;
    float3 n3 = float3( 0, 0, 0 );
    float2 n2 = float2( 0, 0 );

    ExVertexStruct vx[8] = {
    {bbmin, n3, n2, n2, 0},								// front bot left 0
    {float3( bbmin.x, bbmin.y, bbmax.z ), n3, n2, n2, 0}, // back bot left 1
    {float3( bbmax.x, bbmin.y, bbmax.z ), n3, n2, n2, 0}, // back bot right 2
    {float3( bbmax.x, bbmin.y, bbmin.z ), n3, n2, n2, 0}, // front bot right 3
    {float3( bbmin.x, bbmax.y, bbmin.z ), n3, n2, n2, 0},	// front top left 4
    {float3( bbmin.x, bbmax.y, bbmax.z ), n3, n2, n2, 0}, // back top left 5
    {float3( bbmax.x, bbmax.y, bbmax.z ), n3, n2, n2, 0},	// back top right 6
    {float3( bbmax.x, bbmax.y, bbmin.z ), n3, n2, n2, 0} };// front top right 7

    VERTEX_INDEX idx[] = {
        // bottom
        0, 1, 2,
        0, 2, 3,

        // top
        4, 5, 6,
        4, 6, 7,

        // left
        1, 5, 4,
        1, 4, 0,

        // back
        1, 6, 5,
        1, 2, 6,

        // right
        3, 7, 6,
        3, 6, 2,

        // front
        0, 4, 7,
        0, 7, 3
    };

    // Create the buffers
    mi->Create( vx, sizeof( vx ) / sizeof( vx[0] ), idx, sizeof( idx ) / sizeof( idx[0] ) );

    node->OcclusionInfo.NodeMesh = mi;
}

void D3D11OcclusionQuerry::DebugVisualizeNodeMesh( MeshInfo* m, const XMFLOAT4& color ) {
    for ( unsigned int i = 0; i < m->Indices.size(); i += 3 ) {
        XMFLOAT3 tri[3];

        tri[0] = *m->Vertices[m->Indices[i]].Position.toXMFLOAT3();

        tri[1] = *m->Vertices[m->Indices[i + 1]].Position.toXMFLOAT3();

        tri[2] = *m->Vertices[m->Indices[i + 2]].Position.toXMFLOAT3();

        Engine::GraphicsEngine->GetLineRenderer()->AddLine( LineVertex( tri[0], color ), LineVertex( tri[1], color ) );
        Engine::GraphicsEngine->GetLineRenderer()->AddLine( LineVertex( tri[0], color ), LineVertex( tri[2], color ) );
        Engine::GraphicsEngine->GetLineRenderer()->AddLine( LineVertex( tri[1], color ), LineVertex( tri[2], color ) );

    }
}

/** Marks the entire subtree visible/invisible */
void D3D11OcclusionQuerry::MarkTreeVisible( BspInfo* root, bool visible, unsigned int graceFrames ) {
    if ( !root || !root->OriginalNode )
        return;

    root->OcclusionInfo.LastVisitedFrameID = FrameID;
    root->OcclusionInfo.VisibleLastFrame = visible;
    if ( visible ) {
        root->OcclusionInfo.LastVisibleFrameID = FrameID;
        const unsigned int graceUntilFrameID = FrameID + graceFrames;
        if ( root->OcclusionInfo.VisibleGraceUntilFrameID < graceUntilFrameID )
            root->OcclusionInfo.VisibleGraceUntilFrameID = graceUntilFrameID;
        root->OcclusionInfo.InvisibleCandidateFrames = 0;
    } else {
        root->OcclusionInfo.InvisibleCandidateFrames = INVISIBLE_CONFIRM_FRAME_COUNT;
    }

    MarkTreeVisible( root->Front, visible, graceFrames );
    MarkTreeVisible( root->Back, visible, graceFrames );
}
