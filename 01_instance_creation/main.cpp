#include <SDL3/SDL.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "Application.h"

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv);
SDL_AppResult SDL_AppIterate(void *appstate);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
void SDL_AppQuit(void *appstate, SDL_AppResult result);

SDL_Window* window = nullptr;

static constexpr uint32_t WIDTH = 640;
static constexpr uint32_t HEIGHT = 480;

extern bool SDLCALL WindowsMessageHook( void* userdata, MSG* msg );

Application app;

SDL_AppResult SDL_AppInit( void** appstate, int argc, char** argv )
{
    if ( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        SDL_LogCritical( SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s", SDL_GetError() );
        return SDL_APP_FAILURE;
    }


    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if(windowProps == 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR,"Unable to create properties: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetStringProperty (windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "D3DX12");
    SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetNumberProperty (windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, WIDTH);
    SDL_SetNumberProperty (windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, HEIGHT);
    SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);

    window = SDL_CreateWindowWithProperties( windowProps );
    if ( !window )
    {
        SDL_LogCritical( SDL_LOG_CATEGORY_ERROR, "SDL_CreateWindowWithProperties failed: %s", SDL_GetError() );
        return SDL_APP_FAILURE;
    }

    SDL_DestroyProperties( windowProps );

    SDL_SetWindowsMessageHook(WindowsMessageHook, nullptr);

    windowProps = SDL_GetWindowProperties( window );

    void* HWND = SDL_GetPointerProperty( windowProps, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr );
    void* HDC = SDL_GetPointerProperty( windowProps, SDL_PROP_WINDOW_WIN32_HDC_POINTER, nullptr );
    void* HINSTANCE = SDL_GetPointerProperty( windowProps, SDL_PROP_WINDOW_WIN32_INSTANCE_POINTER, nullptr );

    app.initDX12(HWND);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate( void* appstate )
{
    app.update();
    app.render();

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent( void* appstate, SDL_Event* event )
{
    if ( event->type == SDL_EVENT_QUIT )
    {
        return SDL_APP_SUCCESS;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit( void* appstate, SDL_AppResult result )
{
    app.cleanup();
    SDL_DestroyWindow( window );
    SDL_Quit();
}