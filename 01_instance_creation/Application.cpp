#include "Application.h"

#include <stdexcept>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <Windows.h>

// ComPtr
#include <wrl/client.h>

// ID3D12Device
#include <d3d12.h>

// IDXGIFactory7
// IDXGIAdapter4
#include <dxgi1_6.h>

// IDXGIInfoQueue
// DXGIGetDebugInterface1
#include <dxgidebug.h>

using Microsoft::WRL::ComPtr;

class Application::ApplicationImpl
{
public:
    ApplicationImpl() {}

    void initDX12(HWND);
    void cleanup();
    void update();
    void render();

    ComPtr<IDXGIFactory7> dxgiFactory;
    ComPtr<IDXGIAdapter4> dxgiAdapter;

    ComPtr<ID3D12Device> d3dDevice;

    static constexpr D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_12_0;

    static inline void ThrowIfFailed(HRESULT hr)
    {
        if (FAILED(hr))
        {
#ifdef _DEBUG
            char str[64] = {};
            sprintf_s(str, "**ERROR** Fatal Error with HRESULT of %08X\n", static_cast<unsigned int>(hr));
            OutputDebugStringA(str);
            __debugbreak();
#endif
            static char s_str[64] = {};
            sprintf_s(s_str, "Failure with HRESULT of %08X", static_cast<unsigned int>(hr));
            
            throw std::runtime_error(s_str);
        }
    } 

    ComPtr<IDXGIAdapter4> GetAdapter();
    

};


void Application::ApplicationImpl::initDX12(HWND hwnd)
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



    ThrowIfFailed(CreateDXGIFactory2( dxgiFactoryFlags, IID_PPV_ARGS( dxgiFactory.ReleaseAndGetAddressOf() ) ));

    dxgiAdapter = GetAdapter(); // adapter for physical GPU

    ThrowIfFailed( D3D12CreateDevice( dxgiAdapter.Get(), featureLevel, IID_PPV_ARGS( d3dDevice.ReleaseAndGetAddressOf() ) ) );

    d3dDevice->SetName( L"Direct3D 12 Device" );
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
{}

void Application::ApplicationImpl::update()
{}

void Application::ApplicationImpl::render()
{}


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
