#include "QuickResume.h"
#include <SystemConf.h>
#include "utils/FileSystemUtil.h"

namespace QuickResume
{
    std::string logFile = "/userdata/system/qr.txt";
    
    bool setQuickResume(std::string quickResumeCommand, std::string quickResumePath)
    {
        bool configSaved = false;

        if (quickResumeEnabled())
        {
            if (!quickResumeCommand.empty() && !quickResumePath.empty())
            {
                SystemConf::getInstance()->set("global.bootgame.path", quickResumePath);
                SystemConf::getInstance()->set("global.bootgame.cmd", quickResumeCommand);
                configSaved = SystemConf::getInstance()->saveSystemConf();
            }
        }

        return configSaved;
    }

    bool clearQuickResume()
    {
        bool configSaved = false;

        SystemConf::getInstance()->set("global.bootgame.path", "");
        SystemConf::getInstance()->set("global.bootgame.cmd", "");
        configSaved = SystemConf::getInstance()->saveSystemConf();

        return configSaved;
    }

    bool postLaunchConditionalClear()
    {
        bool configSaved = false;

        if (quickResumeEnabled() && (shutDownInProgress() == false))
        {
            SystemConf::getInstance()->set("global.bootgame.path", "");
            SystemConf::getInstance()->set("global.bootgame.cmd", "");
            configSaved = SystemConf::getInstance()->saveSystemConf();
        }

        return configSaved;
    }

    bool shutDownInProgress()
    {
        return Utils::FileSystem::exists("/var/run/shutdown.flag");
    }

    bool quickResumeEnabled()
    {
        return SystemConf::getInstance()->getBool("global.quickresume") == true;
    }
}