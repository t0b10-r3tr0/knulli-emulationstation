#pragma once
#include "guis/knulli/ExtendedGuiSettings.h"
#include "components/SliderComponent.h"
#include "components/OptionListComponent.h"
#include "components/SwitchComponent.h"
#include <array>
#include <memory>

class GuiGameLaunchSplash : public ExtendedGuiSettings
{
public:
    GuiGameLaunchSplash(Window *window);

private:
    std::shared_ptr<OptionListComponent<std::string>> createModeOptionList();
    std::shared_ptr<OptionListComponent<std::string>> foregroundImage;
    std::shared_ptr<OptionListComponent<std::string>> backgroundImage;
    std::shared_ptr<SliderComponent> sliderInitialScale;
    std::shared_ptr<SliderComponent> sliderFinalScale;
    std::shared_ptr<SliderComponent> sliderFadeTime;
    std::shared_ptr<SliderComponent> sliderShowTime;
};