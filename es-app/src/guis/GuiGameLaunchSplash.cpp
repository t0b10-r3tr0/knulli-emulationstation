#include "guis/GuiGameLaunchSplash.h"
#include "guis/GuiMsgBox.h"
#include "guis/knulli/ExtendedGuiSettings.h"
#include "components/OptionListComponent.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "SystemConf.h"
#include "ApiSystem.h"
#include "Scripting.h"
#include "InputManager.h"
#include "AudioManager.h"
#include <SDL_events.h>
#include <algorithm>
#include <memory>
#include <string>
#include "RgbService.h"
#include "BoardCheck.h"

#include "Log.h"

const std::vector<std::string> RGB_BOARDS_H700 = {"rg40xx-h", "rg40xx-v", "rg-cubexx"};
const std::vector<std::string> RGB_BOARDS_A133 = {"trimui-smart-pro", "trimui-brick"};

constexpr float DEFAULT_FOREGROUND_IMAGE = 1;
constexpr float DEFAULT_BACKGROUND_IMAGE = 3;
constexpr float DEFAULT_INITIAL_SCALE = 0;
constexpr float DEFAULT_FINAL_SCALE = 100;
constexpr float DEFAULT_SHOW_TIME = 2.5;
constexpr float DEFAULT_FADE_TIME = 0.75;

// Constructor creates a new GuiGameLaunchSplash menu.
GuiGameLaunchSplash::GuiGameLaunchSplash(Window *window) : ExtendedGuiSettings(window, "GAME LAUNCH SPLASH SETTINGS")
{
    // IMAGE
    foregroundImage = std::make_shared<OptionListComponent<std::string>>(mWindow, _("FOREGROUND IMAGE"));
    foregroundImage->addRange({{_("LOGO"), _("Use the LOGO SOURCE from scraper settings."), "1"},
                               {_("BOX"), _("Use the BOX SOURCE from scraper settings."), "2"},
                               {_("IMAGE"), _("Use the IMAGE SOURCE from scraper settings"), "3"},
                               {_("FAN ART"), _("Use scraped fan art, if available."), "4"},
                               {_("BOX BACKSIDE"), _("Use the scraped box backside, if available."), "5"}},
                               SystemConf::getInstance()->get("gamelaunchsplash.foreground"));
    // IMAGE
    backgroundImage = std::make_shared<OptionListComponent<std::string>>(mWindow, _("FOREGROUND IMAGE"));
    backgroundImage->addRange({{_("NONE"), _("Do not use a background image."), ""},
                               {_("LOGO"), _("Use the LOGO SOURCE from scraper settings."), "1"},
                               {_("BOX"), _("Use the BOX SOURCE from scraper settings."), "2"},
                               {_("IMAGE"), _("Use the IMAGE SOURCE from scraper settings."), "3"},
                               {_("FAN ART"), _("Use scraped fan art, if available."), "4"},
                               {_("BOX BACKSIDE"), _("Use the scraped box backside, if available."), "5"}},
                              SystemConf::getInstance()->get("gamelaunchsplash.background"));

    // INITIAL SCALE
    sliderInitialScale = createSlider(_("INITIAL SCALE"), 0.f, 100.f, 5.f, "", _("The initial scale of the image."), true);
    setConfigValueForSlider(sliderInitialScale, DEFAULT_FINAL_SCALE, "gamelaunchsplash.initialscale");

    // FINAL SCALE
    sliderFinalScale = createSlider(_("FINAL SCALE"), 0.f, 100.f, 5.f, "", _("The final scale of the image."), true);
    setConfigValueForSlider(sliderFinalScale, DEFAULT_FINAL_SCALE, "gamelaunchsplash.finalscale");

    // Fade Time Slider
    sliderFadeTime = createSlider(_("FADE TIME"), 0.f, 5.f, 0.25f, "", ("The amount of time (seconds) each fade should take."), true);
    setConfigValueForSlider(sliderFadeTime, DEFAULT_FADE_TIME, "gamelaunchsplash.fadetime");

    // Show Time Slider
    sliderShowTime = createSlider(_("SHOW TIME"), 1.f, 10.f, 0.5f, "", _("The amount of time (seconds) to display after initial fade and before fading out."), true);
    setConfigValueForSlider(sliderShowTime, DEFAULT_SHOW_TIME, "gamelaunchsplash.showtime");
}