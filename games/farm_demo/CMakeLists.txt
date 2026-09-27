# The wildlife farm (games/farm_demo/README.md): you are a dog, the animals
# run on the AI core (kke::ai, docs/AI.md).
add_executable(farm_demo main.cpp FarmModule.cpp)
target_link_libraries(farm_demo PRIVATE kke_engine)
engine_copy_runtime_file(farm_demo ${CMAKE_CURRENT_SOURCE_DIR}/game.json marketplace/farm_demo/game.json)
engine_copy_runtime_file(farm_demo ${CMAKE_SOURCE_DIR}/scenes/farm.scene.json scenes/farm.scene.json)
foreach(shader model.vert model_instanced.vert shadow_instanced.vert model.frag cube.vert cube.frag glow.frag impostor.vert impostor.frag shadow.vert shadow.frag)
    engine_add_shader(farm_demo ${CMAKE_SOURCE_DIR}/shaders/${shader})
endforeach()
# The farm, the animals and the dog come from POLYGON Farm, POLYGON Dogs
# and Quaternius' Farm Animals, found at runtime (assets/synty/ or
# KKE_ASSETS_DIR); none of them is in the repository.
