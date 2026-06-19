#ifndef __CUSTOM_MENU_H__
#define __CUSTOM_MENU_H__

#include <MenuItemInterface.h>

class CustomMenu : public MenuItemInterface {
public:
    CustomMenu() : MenuItemInterface("Custom SubGHz") {}

    void optionsMenu(void) override;
    void drawIcon(float scale = 1) override;

    bool hasTheme() override { return false; }
    String themePath() override { return ""; }
};

#endif
