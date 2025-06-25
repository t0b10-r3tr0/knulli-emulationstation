#include "GameLaunchSplash.h"
#include <SystemConf.h>
#include "utils/FileSystemUtil.h"
#include "utils/Platform.h"

namespace GameLaunchSplash
{
    const std::string shutdownFlag = "/var/run/shutdown.flag";
    const std::string logfile = "/userdata/system/gls.txt";

    bool setGameLaunchSplash(std::string foregroundImage, std::string backgroundImage, std::string initialScale, std::string finalScale, std::string fadeTime, std::string showTime)
    {
        bool configSaved = false;

        if (gameLaunchSplashEnabled())
        {
            SystemConf::getInstance()->set("gamelaunchsplash.foreground", foregroundImage);
            SystemConf::getInstance()->set("gamelaunchsplash.background", backgroundImage);
            SystemConf::getInstance()->set("gamelaunchsplash.fadetime", fadeTime);
            SystemConf::getInstance()->set("gamelaunchsplash.showtime", showTime);
            SystemConf::getInstance()->set("gamelaunchsplash.initialscale", initialScale);
            SystemConf::getInstance()->set("gamelaunchsplash.finalscale", finalScale);
            configSaved = SystemConf::getInstance()->saveSystemConf();
        }

        return configSaved;
    }

    bool clearGameLaunchSplash()
    {
        bool configSaved = false;

        if (gameLaunchSplashEnabled())
        {
            SystemConf::getInstance()->set("global.gamelaunchsplash", "");
            SystemConf::getInstance()->set("gamelaunchsplash.foreground", "");
            SystemConf::getInstance()->set("gamelaunchsplash.background", "");
            SystemConf::getInstance()->set("gamelaunchsplash.fadetime", "");
            SystemConf::getInstance()->set("gamelaunchsplash.showtime", "");
            SystemConf::getInstance()->set("gamelaunchsplash.initialscale", "");
            SystemConf::getInstance()->set("gamelaunchsplash.finalscale", "");
            configSaved = SystemConf::getInstance()->saveSystemConf();
        }

        return configSaved;
    }

    bool gameLaunchSplashEnabled()
    {
        return SystemConf::getInstance()->getBool("global.gamelaunchsplash") == true;
    }

    bool runGameLaunchSplash(std::string imagePath)
    {
        bool executed = false;

        if (gameLaunchSplashEnabled())
        {

            std::string foregroundImage = getCustomImagePath(imagePath, SystemConf::getInstance()->get("gamelaunchsplash.foreground"));
            std::string backgroundImage = getCustomImagePath(imagePath, SystemConf::getInstance()->get("gamelaunchsplash.background"));
            auto fadeTime = SystemConf::getInstance()->get("gamelaunchsplash.fadetime");
            auto showTime = SystemConf::getInstance()->get("gamelaunchsplash.showtime");
            auto initialScale = SystemConf::getInstance()->get("gamelaunchsplash.initialscale");
            auto finalScale = SystemConf::getInstance()->get("gamelaunchsplash.finalscale");

            std::string commandToRun;

            // Generate the command, adding background if set
            commandToRun = "game_launch_splash " + foregroundImage + " 0 1 0.75 3";
            commandToRun += !backgroundImage.empty() ? " " + backgroundImage : "";

            Utils::FileSystem::writeAllText(logfile, "Running game launch splash with command: \n" + commandToRun + "\n");

            Utils::Platform::ProcessStartInfo process;
            process.command = commandToRun;

            process.waitForExit = true;
            process.showWindow = true;
            process.stderrFilename = "stderr.ry.txt";
            process.stdoutFilename = "stdout.ry.txt";

            process.run();
            executed = true;
        }

        return executed;
    }

    std::string getCustomImagePath(std::string imagePath, std::string imageType)
    {
        if (!imagePath.empty() && !imageType.empty())
        {
            int imageTypeValue = std::stoi(imageType);
            // logo = 1, box = 2, image = 3, fanart = 4, boxback = 5
            std::string imageTypeName = getImageTypeName(imageTypeValue);

            // create and populate our variables
            std::string fgDir;
            std::string fgExtension;
            std::string fgBaseName;

            parseImagePath(imagePath, fgDir, fgExtension, fgBaseName);

            std::string constructedImagePath = constructImagePath(fgDir, fgBaseName, imageTypeName);

            return constructedImagePath;
        }

        return nullptr;
    }

    void parseImagePath(const std::string &imagePath, std::string &dirPath, std::string &extension, std::string &baseGameName)
    {
        size_t lastSlash = imagePath.find_last_of("/\\");
        dirPath = (lastSlash != std::string::npos) ? imagePath.substr(0, lastSlash + 1) : "";

        size_t lastDot = imagePath.find_last_of('.');
        extension = (lastDot != std::string::npos) ? imagePath.substr(lastDot) : "";

        std::string filename = (lastSlash != std::string::npos) ? imagePath.substr(lastSlash + 1) : imagePath;

        // Remove "-image" suffix before the extension
        std::string suffix = "-image";
        size_t suffixPos = filename.rfind(suffix);
        if (suffixPos != std::string::npos && suffixPos + suffix.length() == filename.length() - extension.length())
        {
            baseGameName = filename.substr(0, suffixPos);
        }
        else
        {
            // fallback: remove extension only
            baseGameName = (lastDot != std::string::npos) ? filename.substr(0, lastDot) : filename;
        }
    }

    std::string constructImagePath(const std::string &dir, const std::string &base, const std::string &type)
    {
        std::string ext = getImageExtensionForType(type);
        std::string path = dir + base + "-" + type + ext;

        // For "image", check for .png first, then .jpg
        if (type == "image" && !Utils::FileSystem::exists(path))
        {
            path = dir + base + "-" + type + ".jpg";
        }

        return path;
    }

    std::string getImageExtensionForType(const std::string &type)
    {
        if (type == "fanart")
            return ".jpg";
        if (type == "image")
            return ".png"; // fallback to .jpg if .png doesn't exist
        // All others are .png
        return ".png";
    }

    std::string getImageTypeName(int value)
    {
        switch (value)
        {
        case 1:
            return "marquee";
        case 2:
            return "thumb";
        case 3:
            return "image";
        case 4:
            return "fanart";
        case 5:
            return "boxback";
        default:
            return "";
        }
    }
}