#include "config.h"

BoardMenu gameMenu;
BoardMenu botDifficultyMenu;
BoardMenu botColorMenu;
MenuNavigator navigator;

void initMenus(Board* board) {
    gameMenu.setBoard(board);
    botDifficultyMenu.setBoard(board);
    botColorMenu.setBoard(board);

    gameMenu.setItems(gameMenuItems);
    botDifficultyMenu.setItems(botDifficultyItems);
    botDifficultyMenu.setBackButton(4, 4);
    botColorMenu.setItems(botColorItems);
    botColorMenu.setBackButton(4, 4);
}
