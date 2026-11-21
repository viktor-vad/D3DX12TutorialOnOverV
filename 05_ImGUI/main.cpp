#include <SDL3/SDL.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL_main.h>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#include "Application.h"

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv);
SDL_AppResult SDL_AppIterate(void *appstate);
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event);
void SDL_AppQuit(void *appstate, SDL_AppResult result);

SDL_Window* window = nullptr;

static constexpr uint32_t WIDTH = 1280;
static constexpr uint32_t HEIGHT = 1120;

extern bool SDLCALL WindowsMessageHook( void* userdata, MSG* msg );

Application app;

SDL_AppResult SDL_AppInit( void** appstate, int argc, char** argv )
{
    if ( !SDL_Init( SDL_INIT_VIDEO ) )
    {
        SDL_LogCritical( SDL_LOG_CATEGORY_ERROR, "SDL_Init failed: %s", SDL_GetError() );
        return SDL_APP_FAILURE;
    }

    float displayScale = SDL_GetDisplayContentScale( SDL_GetPrimaryDisplay() );

    SDL_PropertiesID windowProps = SDL_CreateProperties();
    if(windowProps == 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_ERROR,"Unable to create properties: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    SDL_SetStringProperty (windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, "D3DX12");
    SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_RESIZABLE_BOOLEAN, true);
    SDL_SetNumberProperty (windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER,  static_cast<Sint64>( WIDTH  * displayScale) );
    SDL_SetNumberProperty (windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, static_cast<Sint64>( HEIGHT * displayScale) );
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.Fonts->AddFontFromFileTTF( "C:\\Windows\\Fonts\\arial.ttf" );

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes( displayScale * 1.25f );
    style.FontScaleDpi = displayScale * 1.25f;

    ImGui_ImplSDL3_InitForD3D(window);

    app.initDX12(HWND);

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate( void* appstate )
{
    ImGui_ImplSDL3_NewFrame();
    app.update();
    app.render();

    return SDL_APP_CONTINUE;
}

inline bool isKeyboardEvent( const SDL_Event& event )
{
    switch ( event.type )
    {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_TEXT_EDITING:
        case SDL_EVENT_TEXT_INPUT:
        case SDL_EVENT_KEYMAP_CHANGED:
        case SDL_EVENT_KEYBOARD_ADDED:      
        case SDL_EVENT_KEYBOARD_REMOVED:
        case SDL_EVENT_TEXT_EDITING_CANDIDATES:
        case SDL_EVENT_SCREEN_KEYBOARD_SHOWN:
        case SDL_EVENT_SCREEN_KEYBOARD_HIDDEN:
            return true;
     
        default: return false;
    };
}

inline bool isMouseEvent( const SDL_Event& event )
{
    switch ( event.type )
    {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_MOUSE_ADDED:
        case SDL_EVENT_MOUSE_REMOVED:
            return true;

        default: return false;
    };
}

SDL_AppResult SDL_AppEvent( void* appstate, SDL_Event* event )
{
    ImGui_ImplSDL3_ProcessEvent( event );
    bool is_mouse_captured    = ImGui::GetIO().WantCaptureMouse;    
    bool is_keyboard_captured = ImGui::GetIO().WantCaptureKeyboard;

    if ( ( isKeyboardEvent( *event ) && is_keyboard_captured ) 
       || ( isMouseEvent( *event ) && is_mouse_captured ) )
    {
        return SDL_APP_CONTINUE;
    }

    switch ( event->type )
    {
        case SDL_EVENT_QUIT: return SDL_APP_SUCCESS; break;
        case SDL_EVENT_WINDOW_RESIZED: app.resize(); break;
        default: app.handleEvent( *event ); break;
    };

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit( void* appstate, SDL_AppResult result )
{
    app.cleanup();

    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    SDL_DestroyWindow( window );
    SDL_Quit();
}