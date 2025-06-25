#include "SystemConf.h"
#include "utils/FileSystemUtil.h"

namespace GameLaunchSplash
{
    bool setGameLaunchSplash(std::string foregroundImage, std::string backgroundImage, std::string initialScale, std::string finalScale, std::string fadeTime, std::string showTime);
    bool clearGameLaunchSplash();
    bool gameLaunchSplashEnabled();
    bool shutDownInProgress();
    bool runGameLaunchSplash(std::string imagePath);
}