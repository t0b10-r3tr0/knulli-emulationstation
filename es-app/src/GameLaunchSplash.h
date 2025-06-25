#include "SystemConf.h"
#include "utils/FileSystemUtil.h"

namespace GameLaunchSplash
{
    bool setGameLaunchSplash(std::string foregroundImage, std::string backgroundImage, std::string initialScale, std::string finalScale, std::string fadeTime, std::string showTime);
    bool clearGameLaunchSplash();
    bool gameLaunchSplashEnabled();
    bool shutDownInProgress();
    bool runGameLaunchSplash(std::string imagePath);
    std::string getCustomImagePath(std::string imagePath, std::string imageType);
    std::string getImageTypeName(int value);
    void parseImagePath(const std::string &imagePath, std::string &dirPath, std::string &extension, std::string &baseGameName);
    std::string constructImagePath(const std::string &dir, const std::string &base, const std::string &type);
    std::string getImageExtensionForType(const std::string &type);
}