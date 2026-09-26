# Makes a new game from games/template (docs/tutorials/getting-started.md).
#
#   cmake -DNAME=my_game -P tools/new_game.cmake
#   (or: tools/new_game my_game)
#
# Copies games/template to games/<NAME>, names the executable, window and
# manifest after it, and adds it to the build (games/my_games.cmake). Build
# again and run build/bin/<NAME>.
cmake_minimum_required(VERSION 3.25)

get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
if(NOT DEFINED NAME OR NAME STREQUAL "")
    message(FATAL_ERROR "Give the game a name: cmake -DNAME=my_game -P tools/new_game.cmake")
endif()
if(NOT NAME MATCHES "^[a-z][a-z0-9_]*$")
    message(FATAL_ERROR "'${NAME}': use lower-case letters, digits and _ (starting with a letter), e.g. my_game")
endif()
set(DEST "${ROOT}/games/${NAME}")
if(EXISTS "${DEST}")
    message(FATAL_ERROR "games/${NAME} already exists")
endif()
foreach(taken template kke_demo kke_basics starter_game sandbox physics_demo melt_demo sea_demo jiggle_demo synty_demo imgui_demo rmlui_demo)
    if(NAME STREQUAL taken)
        message(FATAL_ERROR "'${NAME}' is already used by the engine; pick another name")
    endif()
endforeach()

file(COPY "${ROOT}/games/template/" DESTINATION "${DEST}")

# "my_game" -> "My Game" for the window and the manifest.
string(REPLACE "_" ";" words "${NAME}")
set(TITLE "")
foreach(w ${words})
    string(SUBSTRING "${w}" 0 1 first)
    string(SUBSTRING "${w}" 1 -1 rest)
    string(TOUPPER "${first}" first)
    string(APPEND TITLE " ${first}${rest}")
endforeach()
string(STRIP "${TITLE}" TITLE)

function(replace_in file from to)
    file(READ "${file}" text)
    string(REPLACE "${from}" "${to}" text "${text}")
    file(WRITE "${file}" "${text}")
endfunction()
replace_in("${DEST}/CMakeLists.txt" "set(GAME_NAME starter_game)" "set(GAME_NAME ${NAME})")
replace_in("${DEST}/main.cpp" "app(\"Starter Game\"" "app(\"${TITLE}\"")
replace_in("${DEST}/game.json" "\"local.starter_game\"" "\"local.${NAME}\"")
replace_in("${DEST}/game.json" "\"Starter Game\"" "\"${TITLE}\"")

set(LIST "${ROOT}/games/my_games.cmake")
file(APPEND "${LIST}" "add_subdirectory(\${CMAKE_SOURCE_DIR}/games/${NAME})\n")

message(STATUS "Made games/${NAME} (\"${TITLE}\").")
message(STATUS "Build:  cmake --build build")
message(STATUS "Run:    cd build/bin && ./${NAME}")
message(STATUS "Edit:   games/${NAME}/scripts/game.lua (reloads while the game runs)")
