#include "CustomMenu.h"
#include "../display.h"
#include "../utils.h"

void CustomMenu::optionsMenu() {
    options = {
        {"Back", []() { returnToMenu = true; }}
    };

    addOptionToMainMenu();
    loopOptions(options, MENU_TYPE_SUBMENU, "Custom SubGHz");
}

void CustomMenu::drawIcon(float scale) {
    clearIconArea();
    tft.fillCircle(iconCenterX, iconCenterY, 20 * scale, bruceConfig.priColor);
}
