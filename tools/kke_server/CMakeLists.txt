# kke_server: the headless game server (docs/SERVER_HOSTING.md). Needs the
# net layer; the physics role also needs Jolt.
add_executable(kke_server main.cpp)
target_link_libraries(kke_server PRIVATE kke_engine)
