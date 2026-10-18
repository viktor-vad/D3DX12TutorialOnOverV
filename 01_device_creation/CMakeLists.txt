
get_filename_component(TARGET_NAME ${CMAKE_CURRENT_SOURCE_DIR} NAME)

add_executable(${TARGET_NAME} main.cpp WindowMessageHook.cpp Application.h Application.cpp)
set_property(TARGET ${TARGET_NAME} PROPERTY CXX_STANDARD 20)
target_link_libraries(${TARGET_NAME} PRIVATE SDL3::SDL3)
target_link_libraries(${TARGET_NAME} PRIVATE dxguid dxgi Microsoft::DirectX12-Layers Microsoft::DirectX12-Core Microsoft::DirectX-Headers Microsoft::DirectX-Guids Microsoft::DirectX12-Agility)


 if(TARGET Microsoft::DirectX12-Agility)
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DirectX12-Core,IMPORTED_LOCATION_RELEASE> $<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12
        COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_PROPERTY:Microsoft::DirectX12-Layers,IMPORTED_LOCATION_DEBUG> $<TARGET_FILE_DIR:${TARGET_NAME}>/D3D12
           COMMAND_EXPAND_LISTS
        )
 endif() 