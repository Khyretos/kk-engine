// Every minigame, in the order the menu lists them. A new one: a file in
// minigames/, a make function here, and its id in README.md.

#include "Minigame.h"

namespace party {

std::unique_ptr<Minigame> makeObstacleCourse();
std::unique_ptr<Minigame> makeJumpRope();
std::unique_ptr<Minigame> makeGlassBridge();
std::unique_ptr<Minigame> makeMashTug();
std::unique_ptr<Minigame> makeRedLight();
std::unique_ptr<Minigame> makeFallingTiles();
std::unique_ptr<Minigame> makeSumo();
std::unique_ptr<Minigame> makeHotPotato();

std::vector<std::unique_ptr<Minigame>> makeMinigames() {
    std::vector<std::unique_ptr<Minigame>> all;
    all.push_back(makeObstacleCourse());
    all.push_back(makeJumpRope());
    all.push_back(makeGlassBridge());
    all.push_back(makeMashTug());
    all.push_back(makeRedLight());
    all.push_back(makeFallingTiles());
    all.push_back(makeSumo());
    all.push_back(makeHotPotato());
    return all;
}

} // namespace party
