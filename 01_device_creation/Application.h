#pragma once
#include <memory>

class Application
{
public:
    Application();
    ~Application();

    void initDX12( void* _hwnd );
    void cleanup();
    void update();
    void render();
private:
    class ApplicationImpl;
    std::unique_ptr<ApplicationImpl> pimpl;
};

