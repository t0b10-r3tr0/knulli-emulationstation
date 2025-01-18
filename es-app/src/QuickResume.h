#include "SystemConf.h"
#include "utils/FileSystemUtil.h"

namespace QuickResume
{
    bool setQuickResume(std::string quickResumeCommand, std::string quickResumePath);
    bool clearQuickResume();
    bool postLaunchConditionalClear();
    bool shutDownInProgress();
    bool quickResumeEnabled();
    bool postLaunchConditionalClear();

}