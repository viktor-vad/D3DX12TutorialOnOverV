#include "Application.h"
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <array>

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

// IDXGIFactory7
// IDXGIAdapter4
#include <dxgi1_6.h>

// IDXGIInfoQueue
// DXGIGetDebugInterface1
#include <dxgidebug.h>

//PixEvent
#include <pix3.h>

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

using Microsoft::WRL::ComPtr;

class Application::ApplicationImpl
{
public:
    ApplicationImpl();

    void initDX12(HWND);
    void cleanup();
    void update();
    void render();
    void resize();

    void createDXGIFactory( HWND hwnd );
    void createDevice();
    void createCommandQueueObjects();
    void createSwapChain( HWND hwnd );
    void createRTVHeap();
    void createRTVs();
    void createSyncObjects();
    void createPipelineState();
    void loadAssets();

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
    
    // Command queue, command allocator and command list
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocators[ BACKBUFFER_COUNT ];
    ComPtr<ID3D12GraphicsCommandList> commandList;

    // Swap chain and rtv related resources
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    UINT rtvDescriptorSize = 0;
    ComPtr<ID3D12Resource> renderTargets[ BACKBUFFER_COUNT ];
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


    // synchronization objects
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValues[ BACKBUFFER_COUNT ] = {};

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
        ThrowIfFailed( device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &commandAllocators[ i ] ) ) );
        commandAllocators[ i ]->SetName( (L"Direct3D 12 Command Allocator " + std::to_wstring( i )).c_str() );
    }

    ThrowIfFailed( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators[ 0 ].Get(), nullptr, IID_PPV_ARGS( &commandList ) ) );
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
        ThrowIfFailed( swapChain->GetBuffer( i, IID_PPV_ARGS( &renderTargets[i])));
        device->CreateRenderTargetView( renderTargets[i].Get(), nullptr, rtvHandle);
        rtvHandle.Offset( 1, rtvDescriptorSize );
    }

}

void Application::ApplicationImpl::createSyncObjects()
{
    ThrowIfFailed( device->CreateFence( fenceValues[frameIndex], D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    fence->SetName( L"Direct3D 12 Fence" );
    fenceValues[frameIndex]++;

    fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( !fenceEvent )
    {
        ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
    }
}

void Application::ApplicationImpl::createPipelineState()
{
    // root signature
    CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC  versionedRootSignatureDesc;
    versionedRootSignatureDesc.Init_1_0(0, nullptr, 0, nullptr, 
                                    D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT // The app is opting in to using the Input Assembler 
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
        D3D12_INPUT_ELEMENT_DESC{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.VS = createShaderBytecode( "buffer_tri_vertex.dxil", shaderBlobVS );
    psoDesc.PS = createShaderBytecode( "buffer_tri_pixel.dxil", shaderBlobPS );
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
    DirectX::XMFLOAT4 position;
    DirectX::XMFLOAT4 color;
};

void Application::ApplicationImpl::loadAssets()
{
    std::array<Vertex, 4> triangleVertices =
    {
        Vertex{ {  0.25f, -0.25f, 0.0f, 1.0f }, { 1.0f, 0.0f, 0.0f, 1.0f } },
        Vertex{ { -0.25f, -0.25f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f, 1.0f } },
        Vertex{ { -0.25f,  0.25f, 0.0f, 1.0f }, { 0.0f, 0.0f, 1.0f, 1.0f } },
        Vertex{ {  0.25f,  0.25f, 0.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 1.0f } }
    };

    std::array<UINT16, 6> triangleIndices =
    {
        0, 1, 2,
        0, 2, 3
    };

    CD3DX12_HEAP_PROPERTIES heapProps( D3D12_HEAP_TYPE_UPLOAD );
    CD3DX12_RESOURCE_DESC bufferDesc = CD3DX12_RESOURCE_DESC::Buffer( sizeof( Vertex ) * triangleVertices.size() );

    ThrowIfFailed( device->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS( &vertexBuffer ) ) );

    vertexBuffer->SetName( L"Vertex Buffer" );

    void* pVertexDataBegin;
    CD3DX12_RANGE readRange( 0, 0 );
    ThrowIfFailed( vertexBuffer->Map( 0, &readRange, &pVertexDataBegin ) );
    memcpy( pVertexDataBegin, triangleVertices.data(), sizeof( Vertex ) * triangleVertices.size() );
    vertexBuffer->Unmap( 0, nullptr );

    vertexBufferView.BufferLocation = vertexBuffer->GetGPUVirtualAddress();
    vertexBufferView.SizeInBytes = static_cast<UINT>( sizeof( Vertex ) * triangleVertices.size() );
    vertexBufferView.StrideInBytes = sizeof( Vertex );

    CD3DX12_HEAP_PROPERTIES heapProperties( D3D12_HEAP_TYPE_UPLOAD );
    CD3DX12_RESOURCE_DESC resourceDesc( CD3DX12_RESOURCE_DESC::Buffer( sizeof( UINT16 ) * triangleIndices.size() ) );

    ThrowIfFailed( device->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS( &indexBuffer ) ) );

    indexBuffer->SetName( L"Index Buffer" );

    void* pIndexDataBegin;
    ThrowIfFailed( indexBuffer->Map( 0, &readRange, &pIndexDataBegin ) );
    memcpy( pIndexDataBegin, triangleIndices.data(), sizeof( UINT16 ) * triangleIndices.size() );
    indexBuffer->Unmap( 0, nullptr );

    indexBufferView.BufferLocation = indexBuffer->GetGPUVirtualAddress();
    indexBufferView.SizeInBytes = static_cast<UINT>( sizeof( UINT16 ) * triangleIndices.size() );
    indexBufferView.Format = DXGI_FORMAT_R16_UINT;
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
    createPipelineState();
    loadAssets();
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
    CloseHandle( fenceEvent );
}

void Application::ApplicationImpl::waitFrame2Finish()
{
    //PIXScopedEvent( commandQueue.Get(), PIX_COLOR(255, 0, 0), L"Wait for frame to finish");
    const UINT64 fenceToWaitFor = fenceValues[frameIndex];
    ThrowIfFailed( commandQueue->Signal( fence.Get(), fenceToWaitFor ) );
    if ( fence->GetCompletedValue() < fenceToWaitFor )
    {
        ThrowIfFailed( fence->SetEventOnCompletion( fenceToWaitFor, fenceEvent ) );
        WaitForSingleObject( fenceEvent, INFINITE );
    }
    fenceValues[ frameIndex ]++;
}

void Application::ApplicationImpl::nextFrame()
{
    PIXScopedEvent( commandQueue.Get(), PIX_COLOR(0, 255, 0), L"Next frame");
    const UINT64 fenceToWaitFor = fenceValues[ frameIndex ];
    ThrowIfFailed( commandQueue->Signal( fence.Get(), fenceToWaitFor ) );
    frameIndex = swapChain->GetCurrentBackBufferIndex();
    if ( fence->GetCompletedValue() < fenceValues[ frameIndex ] )
    {
        ThrowIfFailed( fence->SetEventOnCompletion( fenceValues[ frameIndex ], fenceEvent ) );
        WaitForSingleObject( fenceEvent, INFINITE );
    }
    fenceValues[ frameIndex ]=fenceToWaitFor+1;
}

CD3DX12_SHADER_BYTECODE Application::ApplicationImpl::createShaderBytecode( std::string_view shaderFileName, std::vector<BYTE>& shaderRaw )
{
    ThrowIfFailed(ReadFileToBlob(runtime_path / "shaders" / shaderFileName, shaderRaw));
    
    return CD3DX12_SHADER_BYTECODE(shaderRaw.data(), shaderRaw.size());
}

void Application::ApplicationImpl::update()
{}

void Application::ApplicationImpl::recordCommandList()
{
    // Reset command allocator and command list
    ThrowIfFailed( commandAllocators[frameIndex]->Reset());
    ThrowIfFailed( commandList->Reset( commandAllocators[frameIndex].Get(), pipelineState.Get()));
    //PIXScopedEvent( commandList.Get(), PIX_COLOR( 255, 255, 0 ), L"Record command list" );
    
    commandList->SetGraphicsRootSignature( rootSignature.Get() );
    commandList->RSSetViewports( 1, &viewport );
    commandList->RSSetScissorRects( 1, &scissorRect );

    CD3DX12_RESOURCE_BARRIER rtTransitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets[ frameIndex ].Get(),
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

    rtTransitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets[ frameIndex ].Get(),
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
        renderTargets[ i ].Reset();
        fenceValues[ i ] = fenceValues[ frameIndex ];
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

