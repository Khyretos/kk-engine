# Party (games/party/README.md): a show of minigames on jelly beans the
# engine makes itself, for up to four on one screen and friends online.
file(GLOB PARTY_MINIGAMES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/minigames/*.cpp)
kke_add_game(party main.cpp PartyModule.cpp Beans.cpp Lobby.cpp Hud.cpp Net.cpp NetParty.cpp Bean.cpp Show.cpp Shatter.cpp Minigames.cpp
             ${PARTY_MINIGAMES})
target_link_libraries(party PRIVATE kke_engine)
target_include_directories(party PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
engine_copy_runtime_file(party ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/party/game.json)
engine_copy_runtime_file(party ${CMAKE_CURRENT_SOURCE_DIR}/ui/party_hud.rml ui/party_hud.rml)
engine_copy_runtime_file(party ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(font NotoSans-Regular.ttf NotoSans-Bold.ttf NotoSans-Italic.ttf NotoColorEmoji.ttf)
    engine_copy_runtime_file(party ${CMAKE_SOURCE_DIR}/assets/fonts/${font} assets/fonts/${font})
endforeach()
foreach(shader cube.vert cube.frag glow.frag shadow.vert shadow.frag model.vert model_instanced.vert shadow_instanced.vert model.frag translucent_absorb.frag translucent_light.frag
               rml_ui.vert rml_ui.frag rml_gradient.frag)
    engine_add_shader(party ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
