# Shared by the command demos (docs/COMMANDS.md): people (the UAL
# mannequin), input that turns into orders (mouse, controller, touch), the
# HUD with the radial wheel, and pack art when it is on this machine.
add_library(kke_command_kit STATIC Humanoid.cpp Scenery.cpp CommandInput.cpp CommandHud.cpp)
target_include_directories(kke_command_kit PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(kke_command_kit PUBLIC kke_engine)

# Everything a command demo needs next to its executable.
function(command_kit_game target)
    engine_copy_runtime_file(${target} ${CMAKE_SOURCE_DIR}/games/command_kit/ui/command_hud.rml ui/command_hud.rml)
    engine_copy_runtime_file(${target} ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
    foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoColorEmoji.ttf)
        engine_copy_runtime_file(${target} ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
    endforeach()
    foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag
                   rml_ui.vert rml_ui.frag rml_gradient.frag)
        engine_add_shader(${target} ${CMAKE_SOURCE_DIR}/shaders/${shader})
    endforeach()
endfunction()
