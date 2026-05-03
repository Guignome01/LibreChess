#include "config.h"

void configureMenus(BoardMenu& gameMenu, BoardMenu& botDifficultyMenu, BoardMenu& botColorMenu) {
    gameMenu.setItems(gameMenuItems);
    botDifficultyMenu.setItems(botDifficultyItems);
    botDifficultyMenu.setBackButton(4, 4);
    botColorMenu.setItems(botColorItems);
    botColorMenu.setBackButton(4, 4);
}
