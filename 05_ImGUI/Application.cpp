#include "Application.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <array>
#include <bit>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

// ComPtr
#include <wrl/client.h>

// d3d12 headers
#include <directx/d3d12.h>
#include <directx/d3dx12.h>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>

// IDXGIFactory7
// IDXGIAdapter4
#include <dxgi1_6.h>

// IDXGIInfoQueue
// DXGIGetDebugInterface1
#include <dxgidebug.h>

//PixEvent
#include <pix3.h>

// stb image
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

// ImGUI
#include "imgui_impl_dx12.h"
#include "imgui.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

class Application::ApplicationImpl
{
public:
    ApplicationImpl();

    void initDX12(HWND);
    void cleanup();
    void update();
    void render();
    void renderImGui();
    void resize();
    void handleEvent( const SDL_Event& event );

    void createDXGIFactory( HWND hwnd );
    void createDevice();
    void createCommandQueueObjects();
    void createSwapChain( HWND hwnd );
    void createRTVHeap();
    void createRTVs();
    void createSyncObjects();
    void createCBVSRVUAVHeap();
    void createPipelineState();
    void uploadResourceData(ComPtr<ID3D12Resource> resource, D3D12_SUBRESOURCE_DATA* subResourceData, UINT firstSubresource = 0, UINT numSubresources = 1, D3D12_RESOURCE_STATES stateAfter = D3D12_RESOURCE_STATE_GENERIC_READ );
    void loadAssets();
    void initImGuiDx12();

    void recordCommandList();
    void waitFrame2Finish();
    void nextFrame();

    CD3DX12_SHADER_BYTECODE createShaderBytecode(std::string_view shaderFileName, std::vector<BYTE>& shaderRaw);

    // number of back buffers
    static constexpr UINT BACKBUFFER_COUNT = 3;

    // DXGI factory and adapter
    ComPtr<IDXGIFactory7> dxgiFactory;
    ComPtr<IDXGIAdapter4> dxgiAdapter;

    // D3D12 device
    ComPtr<ID3D12Device> device;
    
    struct FrameContext
    {
        ComPtr<ID3D12Resource> renderTarget;
        ComPtr<ID3D12CommandAllocator> commandAllocator;
        UINT64 fenceValue = 0;
    } frameContexts[ BACKBUFFER_COUNT ];

    // Command queue, command allocator and command list
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    // Swap chain and rtv related resources
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    UINT rtvDescriptorSize = 0;
    UINT frameIndex = 0;
    CD3DX12_VIEWPORT viewport;
    CD3DX12_RECT scissorRect;

    // pipeline state object
    ComPtr<ID3D12PipelineState> pipelineState;
    ComPtr<ID3D12RootSignature> rootSignature;

    ComPtr<ID3D12Resource> vertexBuffer;
    D3D12_VERTEX_BUFFER_VIEW vertexBufferView;
    ComPtr<ID3D12Resource> indexBuffer;
    D3D12_INDEX_BUFFER_VIEW indexBufferView;

    ComPtr<ID3D12DescriptorHeap> cbvSrvUavDescriptorHeap;
    UINT cbvSrvUavDescriptorSize = 0;

    ComPtr<ID3D12Resource> AspectRatioBufferCbv;

    ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
    ComPtr<ID3D12Resource> texture;
    
    // ImGui
    struct ImGuiContext
    {
        ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap;
        UINT srcDescriptorSize = 0;

        static constexpr UINT MAX_IMGUI_DESCRIPTOR_COUNT = 64;
        std::array<UINT, MAX_IMGUI_DESCRIPTOR_COUNT> freeDescriptorIndices;
        UINT numFreeDescriptors = MAX_IMGUI_DESCRIPTOR_COUNT;

        void init( IDXGISwapChain4* pSwapChain, ID3D12CommandQueue* pCommandQueue )
        {
            ComPtr<ID3D12Device> device;
            pSwapChain->GetDevice( IID_PPV_ARGS( &device ) );

            D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
            srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            srvHeapDesc.NumDescriptors = MAX_IMGUI_DESCRIPTOR_COUNT;
            srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            ThrowIfFailed( device->CreateDescriptorHeap( &srvHeapDesc, IID_PPV_ARGS( &srvDescriptorHeap ) ) );
            srvDescriptorHeap->SetName( L"ImGui SRV Descriptor Heap" );

            srcDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );

            for ( UINT i = 0; i < MAX_IMGUI_DESCRIPTOR_COUNT; ++i )
            {
                freeDescriptorIndices[ MAX_IMGUI_DESCRIPTOR_COUNT - 1 - i ] = i;
            }

            DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
            pSwapChain->GetDesc1( &swapChainDesc );

            ImGui_ImplDX12_InitInfo init_info = {};
            init_info.Device = device.Get();
            init_info.RTVFormat = swapChainDesc.Format;
            init_info.DSVFormat = DXGI_FORMAT_UNKNOWN;
            init_info.NumFramesInFlight = swapChainDesc.BufferCount;
            init_info.CommandQueue = pCommandQueue;
            init_info.UserData = this;

            init_info.SrvDescriptorHeap = srvDescriptorHeap.Get();
            init_info.SrvDescriptorAllocFn = []( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* out_cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE* out_gpu_desc_handle )
            {
                ImGuiContext* context = reinterpret_cast<ImGuiContext*>( info->UserData );
                if ( context->numFreeDescriptors == 0 )
                {
                    throw std::runtime_error( "ImGui_ImplDX12_InitInfo: No free SRV descriptors available" );
                }

                UINT descIdx = context->freeDescriptorIndices[ --context->numFreeDescriptors ];

                CD3DX12_CPU_DESCRIPTOR_HANDLE cpuHandle( context->srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), descIdx, context->srcDescriptorSize );
                *out_cpu_desc_handle = cpuHandle;

                CD3DX12_GPU_DESCRIPTOR_HANDLE gpuHandle( context->srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart(), descIdx, context->srcDescriptorSize );
                *out_gpu_desc_handle = gpuHandle;

            };

            init_info.SrvDescriptorFreeFn = []( ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu_desc_handle, D3D12_GPU_DESCRIPTOR_HANDLE gpu_desc_handle )
            {
                ImGuiContext* context = reinterpret_cast<ImGuiContext*>( info->UserData );
                SIZE_T descIdx = ( cpu_desc_handle.ptr - context->srvDescriptorHeap->GetCPUDescriptorHandleForHeapStart().ptr ) / context->srcDescriptorSize;
                
                if ( ( descIdx >= MAX_IMGUI_DESCRIPTOR_COUNT ) 
                     || ( gpu_desc_handle.ptr - context->srvDescriptorHeap->GetGPUDescriptorHandleForHeapStart().ptr ) / context->srcDescriptorSize != descIdx )
                {
                    throw std::runtime_error( "ImGui_ImplDX12_InitInfo: Invalid SRV descriptor index" );
                }
                context->freeDescriptorIndices[ context->numFreeDescriptors++ ] = static_cast<UINT>(descIdx);
            };

            ImGui_ImplDX12_Init( &init_info );
        }

        void shutdown()
        {
            ImGui_ImplDX12_Shutdown();
            srvDescriptorHeap.Reset();
            numFreeDescriptors = 0;
        }

    } imguiContext;

    struct alignas( D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT ) AspectRatioBuffer
    {
        XMFLOAT2 aspectRatio;
    }* pAspectRatioBufferData = nullptr;
    float zoom = 1.0f;

    static_assert( sizeof( AspectRatioBuffer ) % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT == 0, "AspectRatioBuffer size must be aligned to D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT" );

    // synchronization objects
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;

    static constexpr D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

    static inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
            static char s_str[64] = {};
            sprintf_s(s_str, "**ERROR** Fatal Error with HRESULT of %08X\n", static_cast<unsigned int>(hr));
#ifdef _DEBUG
            OutputDebugStringA(s_str);
            __debugbreak();
#endif
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(hr));
            
            throw std::runtime_error(s_str);
        }
    } 

    static inline HRESULT ReadFileToBlob(
        const std::filesystem::path& path,
        std::vector<BYTE>& blob)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            return HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND);

        const std::streamsize size = file.tellg();
        if (size < 0)
            return E_FAIL;

        file.seekg(0, std::ios::beg);

        blob.resize(static_cast<size_t>(size));
        if (!file.read(reinterpret_cast<char*>(blob.data()), size))
        {
            return E_FAIL;
        }

        return S_OK;
    }

    static inline D3D12_SUBRESOURCE_DATA BufferSubResourceData( const void* data, SIZE_T dataSize )
    {
        D3D12_SUBRESOURCE_DATA subresourceData = {};
        subresourceData.pData = data;
        subresourceData.RowPitch = dataSize;
        subresourceData.SlicePitch = subresourceData.RowPitch;
        return subresourceData;
    }
        

    ComPtr<IDXGIAdapter4> GetAdapter();
    std::filesystem::path runtime_path;
};

Application::ApplicationImpl::ApplicationImpl()
{
    std::wstring buffer(32768, L'\0');

    DWORD len = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size())
    );

    if (len == 0 || len >= buffer.size())
        throw std::runtime_error("GetModuleFileNameW failed");

    buffer.resize(len);
    runtime_path = std::filesystem::path(buffer.c_str()).parent_path();
}

template<typename ResT>
inline static void GetWindowSize( HWND hwnd, ResT& width, ResT& height )
{
    RECT rc;
    GetClientRect( hwnd, &rc );
    width = static_cast<ResT>(rc.right - rc.left);
    height = static_cast<ResT>(rc.bottom - rc.top);
}

void Application::ApplicationImpl::createDXGIFactory( HWND hwnd )
{
    static UINT dxgiFactoryFlags = 0;
#ifdef _DEBUG
    ComPtr<ID3D12Debug1> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.GetAddressOf()))))
    {
        debugController->EnableDebugLayer();
        debugController->SetEnableGPUBasedValidation( TRUE );
    }

    ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
    {
        dxgiFactoryFlags = DXGI_CREATE_FACTORY_DEBUG;
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
        dxgiInfoQueue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);

        DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
        {
            80 /* IDXGISwapChain::GetContainingOutput: The swapchain's adapter does not control the output on which the swapchain's window resides. */,
        };
        DXGI_INFO_QUEUE_FILTER filter = {};
        filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
        filter.DenyList.pIDList = hide;
        dxgiInfoQueue->AddStorageFilterEntries(DXGI_DEBUG_DXGI, &filter);
    }
#endif // _DEBUG

    ThrowIfFailed(CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( &dxgiFactory ) ));
    ThrowIfFailed(dxgiFactory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER));
}

void Application::ApplicationImpl::createDevice()
{
    dxgiAdapter = GetAdapter(); // adapter for physical GPU
    ThrowIfFailed( D3D12CreateDevice( dxgiAdapter.Get(), featureLevel, IID_PPV_ARGS( &device ) ) );
    device->SetName( L"Direct3D 12 Device" );
}

void Application::ApplicationImpl::createCommandQueueObjects()
{
    D3D12_COMMAND_QUEUE_DESC queueDesc = {};

    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

    ThrowIfFailed( device->CreateCommandQueue( &queueDesc, IID_PPV_ARGS( &commandQueue ) ) );
    commandQueue->SetName( L"Direct3D 12 Command Queue" );

    for ( UINT i = 0; i < BACKBUFFER_COUNT; ++i )
    {
        ThrowIfFailed( device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &frameContexts[ i ].commandAllocator ) ) );
        frameContexts[ i ].commandAllocator->SetName( (L"Direct3D 12 Command Allocator " + std::to_wstring( i )).c_str() );
    }

    ThrowIfFailed( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, frameContexts[ 0 ].commandAllocator.Get(), nullptr, IID_PPV_ARGS( &commandList ) ) );
    commandList->SetName( L"Direct3D 12 Command List" );
    ThrowIfFailed( commandList->Close() );
}


void Application::ApplicationImpl::createSwapChain( HWND hwnd )
{
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
    swapChainDesc.BufferCount = BACKBUFFER_COUNT;
    GetWindowSize( hwnd, swapChainDesc.Width, swapChainDesc.Height );
    swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.SampleDesc.Count = 1;

    ComPtr<IDXGISwapChain1> swapChain1Tmp;

    ThrowIfFailed(dxgiFactory->CreateSwapChainForHwnd(
        commandQueue.Get(), 
        hwnd, 
        &swapChainDesc, 
        nullptr, 
        nullptr, 
        swapChain1Tmp.GetAddressOf()));

    swapChain1Tmp.As( &swapChain );

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( swapChainDesc.Width ), static_cast<float>( swapChainDesc.Height ) );
    scissorRect = CD3DX12_RECT( 0, 0, static_cast<LONG>( swapChainDesc.Width ), static_cast<LONG>( swapChainDesc.Height ) );
}

void Application::ApplicationImpl::createRTVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = BACKBUFFER_COUNT;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));
    rtvDescriptorHeap->SetName( L"RTV Descriptor Heap" );
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );
}

void Application::ApplicationImpl::createRTVs()
{
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );
    for ( UINT i = 0; i < BACKBUFFER_COUNT; ++i )
    {
        ThrowIfFailed( swapChain->GetBuffer( i, IID_PPV_ARGS( &frameContexts[ i ].renderTarget )));
        device->CreateRenderTargetView( frameContexts[ i ].renderTarget.Get(), nullptr, rtvHandle);
        rtvHandle.Offset( 1, rtvDescriptorSize );
    }

}

void Application::ApplicationImpl::createSyncObjects()
{
    ThrowIfFailed( device->CreateFence( frameContexts[ frameIndex ].fenceValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fence->SetName( L"Direct3D 12 Fence" );
    frameContexts[ frameIndex ].fenceValue++;

    fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( !fenceEvent )
    {
        ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
    }
}

void Application::ApplicationImpl::createCBVSRVUAVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC cbvSrvUavHeapDesc = {};
    cbvSrvUavHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvSrvUavHeapDesc.NumDescriptors = 2;
    cbvSrvUavHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

    ThrowIfFailed(device->CreateDescriptorHeap(&cbvSrvUavHeapDesc, IID_PPV_ARGS(&cbvSrvUavDescriptorHeap)));
    cbvSrvUavDescriptorHeap->SetName( L"CBV, SRV and UAV Descriptor Heap" );
    cbvSrvUavDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV );
}


void Application::ApplicationImpl::createPipelineState()
{
    // root signature

    CD3DX12_STATIC_SAMPLER_DESC samplerDesc( 0, D3D12_FILTER_MIN_MAG_MIP_LINEAR );

    CD3DX12_DESCRIPTOR_RANGE1 ranges[ 2 ];
    ranges[0].Init( D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE );
    ranges[1].Init( D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_NONE );

    CD3DX12_ROOT_PARAMETER1 rootParameters[ 2 ];
    rootParameters[ 0 ].InitAsDescriptorTable( 1, &ranges[ 0 ], D3D12_SHADER_VISIBILITY_VERTEX );
    rootParameters[ 1 ].InitAsDescriptorTable( 1, &ranges[ 1 ], D3D12_SHADER_VISIBILITY_PIXEL );

    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC  versionedRootSignatureDesc;
    versionedRootSignatureDesc.Init_1_1(2, rootParameters, 1, &samplerDesc, 
                                     D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT // The app is opting in to using the Input Assembler 
                                   | D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS
                                   | D3D12_ROOT_SIGNATURE_FLAG_DENY_AMPLIFICATION_SHADER_ROOT_ACCESS    
                                   | D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS
                                   | D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS
                                   | D3D12_ROOT_SIGNATURE_FLAG_DENY_MESH_SHADER_ROOT_ACCESS
                                   

    );

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeVersionedRootSignature(&versionedRootSignatureDesc, &signature, &error));
    ThrowIfFailed(device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&rootSignature)));

    
    std::vector<BYTE> shaderBlobVS;
    std::vector<BYTE> shaderBlobPS;

    DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    swapChain->GetDesc1( &swapChainDesc );

    std::array<D3D12_INPUT_ELEMENT_DESC, 2> inputElementDescs =
    {
        D3D12_INPUT_ELEMENT_DESC{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        D3D12_INPUT_ELEMENT_DESC{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = createShaderBytecode( "simple_texture_vertex.dxil", shaderBlobVS );
    psoDesc.PS = createShaderBytecode( "simple_texture_pixel.dxil", shaderBlobPS );
    psoDesc.BlendState = CD3DX12_BLEND_DESC( D3D12_DEFAULT );
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC( D3D12_DEFAULT );
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.InputLayout = { inputElementDescs.data(), static_cast<UINT>(inputElementDescs.size()) };
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[ 0 ] = swapChainDesc.Format;
    psoDesc.SampleDesc = swapChainDesc.SampleDesc;
    psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

    ThrowIfFailed( device->CreateGraphicsPipelineState( &psoDesc, IID_PPV_ARGS( &pipelineState ) ) );
}

struct Vertex
{
    XMFLOAT4 position;
    XMFLOAT2 texCoord;
};

void Application::ApplicationImpl::uploadResourceData(ComPtr<ID3D12Resource> resource, D3D12_SUBRESOURCE_DATA* subResourceData, UINT firstSubresource, UINT numSubresources, D3D12_RESOURCE_STATES stateAfter)
{
    ComPtr<ID3D12Resource> uploadBuffer;
    CD3DX12_HEAP_PROPERTIES heapProps( D3D12_HEAP_TYPE_UPLOAD );

    UINT64 resourceDescSize = GetRequiredIntermediateSize( resource.Get(), firstSubresource, numSubresources);

    D3D12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( resourceDescSize );

    ThrowIfFailed( device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS( &uploadBuffer ) ) );

    ThrowIfFailed( frameContexts[ frameIndex ].commandAllocator->Reset());

    ThrowIfFailed( commandList->Reset( frameContexts[ frameIndex ].commandAllocator.Get(), nullptr ) );
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition( resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST );
        commandList->ResourceBarrier( 1, &barrier );
    }

    UpdateSubresources( commandList.Get(), resource.Get(), uploadBuffer.Get(), 0, firstSubresource, numSubresources, subResourceData );
    
    {
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition( resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, stateAfter );
        commandList->ResourceBarrier( 1, &barrier );
    }

    ThrowIfFailed( commandList->Close() );

    commandQueue->ExecuteCommandLists( 1, CommandListCast( commandList.GetAddressOf() ) );

    waitFrame2Finish();

    uploadBuffer.Reset();
}

void Application::ApplicationImpl::loadAssets()
{
    std::array<Vertex, 4> triangleVertices =
    {
        Vertex{ {  1.0f, -1.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        Vertex{ { -1.0f, -1.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },
        Vertex{ { -1.0f,  1.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        Vertex{ {  1.0f,  1.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } }
    };

    std::array<UINT16, 6> triangleIndices =
    {
        0, 1, 2,
        0, 2, 3
    };

    CD3DX12_HEAP_PROPERTIES heapProps( D3D12_HEAP_TYPE_DEFAULT );
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( sizeof( Vertex ) * triangleVertices.size() );

    ThrowIfFailed( device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS( &vertexBuffer ) ) );

    vertexBuffer->SetName( L"Vertex Buffer" );

    UINT64 vertBuffSize = GetRequiredIntermediateSize( vertexBuffer.Get(), 0, 1 );

    auto vertSubresourceData = BufferSubResourceData( triangleVertices.data(), sizeof( Vertex ) * triangleVertices.size() );
    uploadResourceData( vertexBuffer, 
        &vertSubresourceData, 0, 1, D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER );

    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>( sizeof( Vertex ) * triangleVertices.size() );
    vertexBufferView.StrideInBytes = sizeof( Vertex );

    CD3DX12_HEAP_PROPERTIES heapProperties( D3D12_HEAP_TYPE_DEFAULT );
    CD3DX12_RESOURCE_DESC resourceDesc( CD3DX12_RESOURCE_DESC::Buffer( sizeof( UINT16 ) * triangleIndices.size() ) );

    ThrowIfFailed( device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS( &indexBuffer ) ) );

    indexBuffer->SetName( L"Index Buffer" );
    
    auto indSubresourceData = BufferSubResourceData( triangleIndices.data(), sizeof( UINT16 ) * triangleIndices.size() );
    uploadResourceData( indexBuffer, 
        &indSubresourceData, 0, 1, D3D12_RESOURCE_STATE_INDEX_BUFFER );


    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes    = static_cast<UINT>( sizeof( UINT16 ) * triangleIndices.size() );
    indexBufferView.Format         = DXGI_FORMAT_R16_UINT;

    {
        CD3DX12_HEAP_PROPERTIES heapProps( D3D12_HEAP_TYPE_UPLOAD );
        CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( sizeof( AspectRatioBuffer ) );
        
        ThrowIfFailed( device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS( &AspectRatioBufferCbv ) ) );

        AspectRatioBufferCbv->SetName( L"Aspect Ratio Buffer CBV" );

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = AspectRatioBufferCbv->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = sizeof( AspectRatioBuffer );

        device->CreateConstantBufferView( &cbvDesc, cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );

        CD3DX12_RANGE readRange( 0, 0 );
        ThrowIfFailed( AspectRatioBufferCbv->Map( 0, &readRange, reinterpret_cast<void**>( &pAspectRatioBufferData ) ) );
    }
    {
        struct ImageRGBA
        {
            typedef XMUBYTE4 TexelRGBA;

            static_assert( sizeof( TexelRGBA ) == sizeof( std::uint32_t ) );


            std::vector<TexelRGBA> texelData;
            UINT width		   = 0;
            UINT height		   = 0;

            bool Allocate( unsigned int _width, unsigned int _height )
            {
                width = _width;
                height = _height;

                texelData.resize( width * height );

                return !texelData.empty();
            }

            bool Assign( const std::uint32_t* _TexelData, unsigned int _width, unsigned int _height )
            {
                width = _width;
                height = _height;

                const TexelRGBA* _data = reinterpret_cast<const TexelRGBA*>( _TexelData );

                texelData.assign( _data, _data + width * height );

                return !texelData.empty();
            }

            TexelRGBA GetTexel( unsigned int x, unsigned int y ) const
            {
                return texelData[y * width + x];
            }

            void SetTexel( unsigned int x, unsigned int y,const TexelRGBA& texel )
            {
                texelData[y * width + x] = texel;
            }

            const TexelRGBA* data() const
            {
                return texelData.data();
            }

        }; 

        ImageRGBA Image;
        {
            int x, y, n;
            XMUBYTE4* data = reinterpret_cast<XMUBYTE4*>( stbi_load( ( runtime_path / "Assets" / "abstract.png" ).string().c_str(), &x, &y, &n, 4 ) );

            if ( !data )
            {
                throw std::runtime_error( "Failed to load texture image" );
            }

            Image = {
                .texelData = std::vector<XMUBYTE4>( data, data + x * y ),
                .width = static_cast<UINT>( x ),
                .height = static_cast<UINT>( y )
            };
            stbi_image_free(data);
        }

        UINT nMips = std::bit_width<UINT>(const_cast<UINT&>(std::max(Image.width, Image.height)));

        ImageRGBA ImageFiltered;
        ImageRGBA ImageL;

        std::vector<ImageRGBA> mipmaps;

        for ( unsigned int Level = 0; Level < nMips; Level++ )
        {
            static constexpr XMUBYTE4 PIXELFILTER[ 3 ] = { XMUBYTE4(0ui8,1ui8,1ui8,0ui8), XMUBYTE4(1ui8,0ui8,1ui8,0ui8), XMUBYTE4(1ui8,1ui8,0ui8,0ui8) };
            ImageFiltered.Allocate( Image.width, Image.height );
            for ( unsigned int j = 0; j < Image.height; ++j )
                for ( unsigned int i = 0; i < Image.width; ++i )
                {
                    XMUBYTE4 pixel = Image.GetTexel( i, j );
                    
                    {
                        XMUBYTE4 filter = PIXELFILTER[ Level % 3 ];
                        pixel = XMUBYTE4( uint8_t( pixel.x >> filter.x ),
                                          uint8_t( pixel.y >> filter.y ),
                                          uint8_t( pixel.z >> filter.z ),
                                          uint8_t( pixel.w >> filter.w ) );
                    }
                    ImageFiltered.SetTexel( i, j, pixel );
                }

            mipmaps.emplace_back( std::move( ImageFiltered ) );

            ImageL.Allocate( Image.width / 2, Image.height / 2 );
            for ( unsigned int j = 0; j < ImageL.height; ++j )
                for ( unsigned int i = 0; i < ImageL.width; ++i )
                {
                    XMUBYTE4 tmp = Image.GetTexel( 2 * i, 2 * j );
                    XMUINT4 pixel = XMUINT4(tmp.x, tmp.y, tmp.z, tmp.w);
                    pixel.x += Image.GetTexel( 2 * i + 1, 2 * j ).x;
                    pixel.y += Image.GetTexel( 2 * i + 1, 2 * j ).y;
                    pixel.z += Image.GetTexel( 2 * i + 1, 2 * j ).z;
                    pixel.w += Image.GetTexel( 2 * i + 1, 2 * j ).w;

                    pixel.x += Image.GetTexel( 2 * i, 2 * j + 1 ).x;
                    pixel.y += Image.GetTexel( 2 * i, 2 * j + 1 ).y;
                    pixel.z += Image.GetTexel( 2 * i, 2 * j + 1 ).z;
                    pixel.w += Image.GetTexel( 2 * i, 2 * j + 1 ).w;

                    pixel.x += Image.GetTexel( 2 * i + 1, 2 * j + 1 ).x;
                    pixel.y += Image.GetTexel( 2 * i + 1, 2 * j + 1 ).y;
                    pixel.z += Image.GetTexel( 2 * i + 1, 2 * j + 1 ).z;
                    pixel.w += Image.GetTexel( 2 * i + 1, 2 * j + 1 ).w;

                    pixel.x /= 4;
                    pixel.y /= 4;
                    pixel.z /= 4;
                    pixel.w /= 4;

                    ImageL.SetTexel( i, j, XMUBYTE4(uint8_t(pixel.x), uint8_t(pixel.y), uint8_t(pixel.z), uint8_t(pixel.w)) );
                }

            Image = std::move(ImageL);
        }

        CD3DX12_RESOURCE_DESC textureDesc = CD3DX12_RESOURCE_DESC::Tex2D( DXGI_FORMAT_R8G8B8A8_UNORM, mipmaps[0].width, mipmaps[0].height, 1, nMips);

        CD3DX12_HEAP_PROPERTIES texHeapProps( D3D12_HEAP_TYPE_DEFAULT );
        ThrowIfFailed( device->CreateCommittedResource(
            &texHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &textureDesc,
            D3D12_RESOURCE_STATE_COMMON,
            nullptr,
            IID_PPV_ARGS( &texture ) ) );

        texture->SetName( L"Texture" );

        std::vector<D3D12_SUBRESOURCE_DATA> subResourceDataArray;
        for ( const auto& mipmap : mipmaps )
        {
            D3D12_SUBRESOURCE_DATA subResourceData = {};
            subResourceData.pData = mipmap.data();
            subResourceData.RowPitch = mipmap.width * sizeof( XMUBYTE4 );
            subResourceData.SlicePitch = subResourceData.RowPitch * mipmap.height;
            subResourceDataArray.push_back( subResourceData );
        }   

        uploadResourceData( texture, subResourceDataArray.data(), 0, UINT(subResourceDataArray.size()), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE );

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels = nMips;
        srvDesc.Texture2D.PlaneSlice = 0;
        srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

        device->CreateShaderResourceView( texture.Get(), &srvDesc, 
            CD3DX12_CPU_DESCRIPTOR_HANDLE( cbvSrvUavDescriptorHeap->GetCPUDescriptorHandleForHeapStart() ).Offset( 1, cbvSrvUavDescriptorSize ) );


    }

}

void Application::ApplicationImpl::initImGuiDx12()
{
    imguiContext.init( swapChain.Get(), commandQueue.Get() );
}

void Application::ApplicationImpl::initDX12(HWND hwnd)
{
    createDXGIFactory(hwnd);

    createDevice();
    createCommandQueueObjects();
    createSwapChain(hwnd);
    createRTVHeap();
    createRTVs();
    createSyncObjects();
    createCBVSRVUAVHeap();
    loadAssets();
    createPipelineState();
    initImGuiDx12();
}

ComPtr<IDXGIAdapter4> Application::ApplicationImpl::GetAdapter()
{
    ComPtr<IDXGIAdapter4> adapter;
    for (UINT adapterIndex = 0; ; ++adapterIndex)
    {
        if (dxgiFactory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) == DXGI_ERROR_NOT_FOUND)
        {
            break; // No more adapters to enumerate.
        }
        DXGI_ADAPTER_DESC3 desc;
        adapter->GetDesc3(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
        {
            // Don't select the Basic Render Driver adapter.
            continue;
        }
        // Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), featureLevel, _uuidof(ID3D12Device), nullptr)))
        {
            break;
        }
    }
    return adapter;
}

void Application::ApplicationImpl::cleanup()
{
    waitFrame2Finish();
    imguiContext.shutdown();
    CloseHandle( fenceEvent );
}

void Application::ApplicationImpl::waitFrame2Finish()
{
    //PIXScopedEvent( commandQueue.Get(), PIX_COLOR(255, 0, 0), L"Wait for frame to finish");
    const UINT64 fenceToWaitFor = frameContexts[ frameIndex ].fenceValue;
    ThrowIfFailed( commandQueue->Signal( fence.Get(), fenceToWaitFor ) );
    if ( fence->GetCompletedValue() < fenceToWaitFor )
    {
        ThrowIfFailed( fence->SetEventOnCompletion( fenceToWaitFor, fenceEvent ) );
        WaitForSingleObject( fenceEvent, INFINITE );
    }
    frameContexts[ frameIndex ].fenceValue++;
}

void Application::ApplicationImpl::nextFrame()
{
    PIXScopedEvent( commandQueue.Get(), PIX_COLOR(0, 255, 0), L"Next frame");
    const UINT64 fenceToWaitFor = frameContexts[ frameIndex ].fenceValue;
    ThrowIfFailed( commandQueue->Signal( fence.Get(), fenceToWaitFor ) );
    frameIndex = swapChain->GetCurrentBackBufferIndex();
    if ( fence->GetCompletedValue() < frameContexts[ frameIndex ].fenceValue )
    {
        ThrowIfFailed( fence->SetEventOnCompletion( frameContexts[ frameIndex ].fenceValue, fenceEvent ) );
        WaitForSingleObject( fenceEvent, INFINITE );
    }
    frameContexts[ frameIndex ].fenceValue = fenceToWaitFor + 1;
}

CD3DX12_SHADER_BYTECODE Application::ApplicationImpl::createShaderBytecode( std::string_view shaderFileName, std::vector<BYTE>& shaderRaw )
{
    ThrowIfFailed(ReadFileToBlob(runtime_path / "shaders" / shaderFileName, shaderRaw));
    
    return CD3DX12_SHADER_BYTECODE(shaderRaw.data(), shaderRaw.size());
}

void Application::ApplicationImpl::update()
{
    float aspectRatio = viewport.Width / viewport.Height;

    pAspectRatioBufferData->aspectRatio = ( ( aspectRatio >= 1.0f ) ? XMFLOAT2( 1.0f / aspectRatio, 1.0f ) : XMFLOAT2( 1.0f, aspectRatio ) );
    pAspectRatioBufferData->aspectRatio.x *= zoom;
    pAspectRatioBufferData->aspectRatio.y *= zoom;
}

void Application::ApplicationImpl::recordCommandList()
{
    // Reset command allocator and command list
    ThrowIfFailed( frameContexts[ frameIndex ].commandAllocator->Reset());
    ThrowIfFailed( commandList->Reset( frameContexts[ frameIndex ].commandAllocator.Get(), pipelineState.Get()));
    //PIXScopedEvent( commandList.Get(), PIX_COLOR( 255, 255, 0 ), L"Record command list" );
    
    commandList->SetGraphicsRootSignature( rootSignature.Get() );
    commandList->SetDescriptorHeaps( 1, cbvSrvUavDescriptorHeap.GetAddressOf() );
    CD3DX12_GPU_DESCRIPTOR_HANDLE cbvSrvUavDescriptorHandle( cbvSrvUavDescriptorHeap->GetGPUDescriptorHandleForHeapStart() );
    commandList->SetGraphicsRootDescriptorTable( 0, cbvSrvUavDescriptorHandle );
    cbvSrvUavDescriptorHandle.Offset( 1, cbvSrvUavDescriptorSize );
    commandList->SetGraphicsRootDescriptorTable( 1, cbvSrvUavDescriptorHandle );

    commandList->RSSetViewports( 1, &viewport );
    commandList->RSSetScissorRects( 1, &scissorRect );

    CD3DX12_RESOURCE_BARRIER rtTransitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        frameContexts[ frameIndex ].renderTarget.Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    commandList->ResourceBarrier( 1, &rtTransitionBarrier );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize );
    const float clearColor[] = { 0.0f, 0.25f, 0.5f, 1.0f };

    commandList->OMSetRenderTargets( 1, &rtvHandle, FALSE, nullptr );
    commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );

    commandList->IASetPrimitiveTopology( D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    commandList->IASetVertexBuffers( 0, 1, &vertexBufferView );
    commandList->IASetIndexBuffer( &indexBufferView );
    commandList->DrawIndexedInstanced( 6, 1, 0, 0, 0 );

    renderImGui();

    rtTransitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        frameContexts[ frameIndex ].renderTarget.Get(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT
    );

    commandList->ResourceBarrier( 1, &rtTransitionBarrier );

    ThrowIfFailed( commandList->Close() );
}

void Application::ApplicationImpl::render()
{
    nextFrame();
    //PIXScopedEvent( commandQueue.Get(), PIX_COLOR( 0, 0, 255 ), L"Render" );
    recordCommandList();

    commandQueue->ExecuteCommandLists( 1, CommandListCast( commandList.GetAddressOf() ) );

    ThrowIfFailed( swapChain->Present( 1, 0 ) );
}

void Application::ApplicationImpl::renderImGui()
{
    ImGui_ImplDX12_NewFrame();
    ImGui::NewFrame();

    ImGui::ShowDemoWindow();

    ImGui::Render();

    commandList->SetDescriptorHeaps( 1, imguiContext.srvDescriptorHeap.GetAddressOf() );
    ImGui_ImplDX12_RenderDrawData( ImGui::GetDrawData(), commandList.Get() );
}

void Application::ApplicationImpl::resize()
{
    HWND hwnd=nullptr;
    swapChain->GetHwnd( &hwnd );
    UINT width = 0;
    UINT height = 0;
    GetWindowSize( hwnd, width, height );

    waitFrame2Finish();

    //DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
    //swapChain->GetDesc1( &swapChainDesc );

    for ( UINT i = 0; i < BACKBUFFER_COUNT; ++i )
    {
        frameContexts[ i ].renderTarget.Reset();
        frameContexts[ i ].fenceValue = frameContexts[ frameIndex ].fenceValue;
    }

    swapChain->ResizeBuffers(
        BACKBUFFER_COUNT,
        width,
        height,
        DXGI_FORMAT_UNKNOWN,
        0
    );

    frameIndex = swapChain->GetCurrentBackBufferIndex();

    createRTVs();

    viewport = CD3DX12_VIEWPORT( 0.0f, 0.0f, static_cast<float>( width ), static_cast<float>( height ) );
    scissorRect = CD3DX12_RECT( 0, 0, static_cast<LONG>( width ), static_cast<LONG>( height ) );

    waitFrame2Finish();
}

void Application::ApplicationImpl::handleEvent( const SDL_Event& event )
{
    switch ( event.type )
    {
    case SDL_EVENT_MOUSE_WHEEL:
        zoom *= powf( 1.1f, static_cast<float>( event.wheel.y ) );
        break;
    default:
        break;
    }
}

Application::Application()
    : pimpl( std::make_unique<ApplicationImpl>() )
{}

Application::~Application() = default;

void Application::initDX12(void* _hwnd)
{
    pimpl.get()->initDX12(static_cast<HWND>(_hwnd));
}

void Application::cleanup()
{
    pimpl.get()->cleanup();
}

void Application::update()
{
    pimpl.get()->update();
}

void Application::render()
{
    pimpl.get()->render();
}

void Application::resize()
{
    pimpl.get()->resize();
}

void Application::handleEvent( const SDL_Event& event )
{
    pimpl.get()->handleEvent( event );
}