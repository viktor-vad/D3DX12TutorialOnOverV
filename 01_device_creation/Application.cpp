#include "Application.h"

#include <stdexcept>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

// ComPtr
#include <wrl/client.h>

// d3d12 headers
#include <directx/d3d12.h>
#include <directx/d3dx12.h>


// IDXGIFactory7
// IDXGIAdapter4
#include <dxgi1_6.h>

// IDXGIInfoQueue
// DXGIGetDebugInterface1
#include <dxgidebug.h>

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;}

extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

using Microsoft::WRL::ComPtr;

class Application::ApplicationImpl
{
public:
    ApplicationImpl() {}

    void initDX12(HWND);
    void cleanup();
    void update();
    void render();

    void createDXGIFactory( HWND hwnd );
    void createDevice();
    void createCommandQueueObjects();
    void createSwapChain( HWND hwnd );
    void createRTVs();
    void createSyncObjects();

    void recordCommandList();
    void waitFrame2Finish();

    // DXGI factory and adapter
    ComPtr<IDXGIFactory7> dxgiFactory;
    ComPtr<IDXGIAdapter4> dxgiAdapter;

    // D3D12 device
    ComPtr<ID3D12Device> device;
    
    // Command queue, command allocator and command list
    ComPtr<ID3D12CommandQueue> commandQueue;
    ComPtr<ID3D12CommandAllocator> commandAllocator;
    ComPtr<ID3D12GraphicsCommandList> commandList;

    // Swap chain and rtv related resources
    static constexpr UINT BACKBUFFER_COUNT = 3;
    ComPtr<IDXGISwapChain4> swapChain;
    ComPtr<ID3D12DescriptorHeap> rtvDescriptorHeap;
    UINT rtvDescriptorSize = 0;
    ComPtr<ID3D12Resource> renderTargets[ BACKBUFFER_COUNT ];
    UINT frameIndex = 0;

    // pipeline state object
    ComPtr<ID3D12PipelineState> pipelineState;

    // synchronization objects
    ComPtr<ID3D12Fence> fence;
    HANDLE fenceEvent = nullptr;
    UINT64 fenceValue = 0;

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

    ComPtr<IDXGIAdapter4> GetAdapter();
    
};


inline static void GetWindowSize( HWND hwnd, UINT& width, UINT& height )
{
    RECT rc;
    GetClientRect( hwnd, &rc );
    width = rc.right - rc.left;
    height = rc.bottom - rc.top;
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

    ThrowIfFailed( device->CreateCommandAllocator( D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS( &commandAllocator ) ) );
    commandAllocator->SetName( L"Direct3D 12 Command Allocator" );

    ThrowIfFailed( device->CreateCommandList( 0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator.Get(), nullptr, IID_PPV_ARGS( &commandList ) ) );
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
}

void Application::ApplicationImpl::createRTVs()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.NumDescriptors = BACKBUFFER_COUNT;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    ThrowIfFailed(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvDescriptorHeap)));
    rtvDescriptorHeap->SetName( L"RTV Descriptor Heap" );
    rtvDescriptorSize = device->GetDescriptorHandleIncrementSize( D3D12_DESCRIPTOR_HEAP_TYPE_RTV );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart() );
    for ( UINT i = 0; i < BACKBUFFER_COUNT; ++i )
    {
        ThrowIfFailed( swapChain->GetBuffer( i, IID_PPV_ARGS( &renderTargets[i])));
        device->CreateRenderTargetView( renderTargets[i].Get(), nullptr, rtvHandle );
        rtvHandle.Offset( 1, rtvDescriptorSize );
    }

}

void Application::ApplicationImpl::createSyncObjects()
{
    ThrowIfFailed( device->CreateFence( 0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS( &fence ) ) );
    fence->SetName( L"Direct3D 12 Fence" );
    fenceValue = 1;

    fenceEvent = CreateEvent( nullptr, FALSE, FALSE, nullptr );
    if ( !fenceEvent )
    {
        ThrowIfFailed( HRESULT_FROM_WIN32( GetLastError() ) );
    }
}

void Application::ApplicationImpl::initDX12(HWND hwnd)
{
    createDXGIFactory(hwnd);

    createDevice();
    createCommandQueueObjects();
    createSwapChain(hwnd);
    createRTVs();
    createSyncObjects();
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
    const UINT64 fenceToWaitFor = fenceValue;
    ThrowIfFailed( commandQueue->Signal( fence.Get(), fenceToWaitFor ) );
    fenceValue++;
    if ( fence->GetCompletedValue() < fenceToWaitFor )
    {
        ThrowIfFailed( fence->SetEventOnCompletion( fenceToWaitFor, fenceEvent ) );
        WaitForSingleObject( fenceEvent, INFINITE );
    }

    frameIndex = swapChain->GetCurrentBackBufferIndex();
}

void Application::ApplicationImpl::update()
{}

void Application::ApplicationImpl::recordCommandList()
{
    // Reset command allocator and command list
    ThrowIfFailed( commandAllocator->Reset() );
    ThrowIfFailed( commandList->Reset( commandAllocator.Get(), pipelineState.Get() ) );
    
    CD3DX12_RESOURCE_BARRIER rtTransitionBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
        renderTargets[ frameIndex ].Get(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET
    );

    commandList->ResourceBarrier( 1, &rtTransitionBarrier );

    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle( rtvDescriptorHeap->GetCPUDescriptorHandleForHeapStart(), frameIndex, rtvDescriptorSize );
    const float clearColor[] = { 0.0f, 0.25f, 0.5f, 1.0f };

    commandList->ClearRenderTargetView( rtvHandle, clearColor, 0, nullptr );

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
    recordCommandList();

    commandQueue->ExecuteCommandLists( 1, CommandListCast( commandList.GetAddressOf() ) );

    ThrowIfFailed( swapChain->Present( 1, 0 ) );
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
