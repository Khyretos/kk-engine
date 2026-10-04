# Sea (games/sea_demo/README.md): Synty pirate ships, cannon battles, a
# wooden fort and sailing online.
kke_add_game(sea_demo main.cpp SeaDemoModule.cpp Ships.cpp Battle.cpp Effects.cpp World.cpp SeaNet.cpp)
target_link_libraries(sea_demo PRIVATE kke_engine)
engine_copy_runtime_file(sea_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/sea_demo/game.json)
engine_copy_runtime_file(sea_demo ${CMAKE_SOURCE_DIR}/games/rmlui_demo/ui/theme.rcss ui/theme.rcss)
foreach(shader cube.vert cube.frag glow.frag impostor.vert impostor.frag shadow.vert shadow.frag ocean.vert ocean.frag sky.vert sky.frag
               model.vert model_instanced.vert shadow_instanced.vert model.frag effect.vert effect_smoke.frag effect_spark.frag)
    engine_add_shader(sea_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# kke_use_ui: the RmlUi shaders and the Noto fonts (lobby, shell, panel).
kke_use_ui(sea_demo)
# The ships are Synty's POLYGON Pirates pack, found at runtime in
# KKE_ASSETS_DIR (never committed); without it the ships are built from boxes.
