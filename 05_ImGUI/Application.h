#pragma once
#include <memory>

#include <SDL3/SDL_events.h>

class Application
{
public:
    Application();
    ~Application();

    void initDX12( void* _hwnd );
    void cleanup();
    void update();
    void render();
    void resize();

    void handleEvent( const SDL_Event& event );
private:
    class ApplicationImpl;
    std::unique_ptr<ApplicationImpl> pimpl;
};

