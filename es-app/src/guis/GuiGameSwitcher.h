#pragma once

#include "GuiComponent.h"
#include "Window.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"

class FileData;

class GuiGameSwitcher : public GuiComponent
{
public:
	GuiGameSwitcher(Window* window, bool fromCache = false);
	~GuiGameSwitcher();

	bool input(InputConfig* config, Input input) override;
	void update(int deltaTime) override;
	void render(const Transform4x4f& transform) override;
	std::vector<HelpPrompt> getHelpPrompts() override;
	HelpStyle getHelpStyle() override;

	// Static methods for pending Game Switcher (triggered while game is running)
	static void setPendingGameSwitcher(bool pending);
	static bool hasPendingGameSwitcher();
	static void showPendingGameSwitcher(Window* window);
	static bool isActive();

	// Cached mode (lightweight Game Switcher before full ES loads)
	static bool runCachedMode();

	// Cache management
	static std::string getCachePath();
	static void saveCache(FileData* gameBeingLaunched = nullptr);
	static bool hasCachedData();

	// Pending stats for games launched without full ES (Quick Resume / cached mode)
	static void savePendingStats(const std::string& gamePath, const std::string& systemName, int elapsedSeconds);
	static void applyPendingStats();  // Called when ES fully loads

	// Settings UI (called from GuiMenu)
	static void openSettings(Window* window, bool selectMarqueeEnable = false, bool selectPlayInfoEnable = false);

private:
	static std::string getPendingStatsPath();
	struct GameItem
	{
		FileData* game;           // nullptr in cached mode
		std::string screenshotPath;

		// Cached data (used when game is nullptr)
		std::string gamePath;
		std::string gameName;
		std::string marqueePath;
		std::string launchCommand;
		std::string systemName;
		int playCount;
		int gameTime;
	};

	std::vector<GameItem> mGames;
	int mCurrentIndex;
	bool mCachedMode;

	// Current visual components
	ImageComponent* mScreenshot;
	ImageComponent* mMarquee;
	TextComponent*  mGameName;      // Fallback when no marquee
	TextComponent*  mPlayInfo;      // "Played X times | Play time: Xh Xm"

	// Previous visual components (for animation)
	ImageComponent* mPrevScreenshot;
	ImageComponent* mPrevMarquee;
	TextComponent*  mPrevGameName;
	TextComponent*  mPrevPlayInfo;

	// Animation state
	bool  mAnimating;
	bool  mLaunching;            // Fade-out before launch
	float mAnimationProgress;    // 0.0 to 1.0
	int   mAnimationDirection;   // -1 = left, +1 = right
	int   mAnimationDuration;    // milliseconds

	void loadRecentlyPlayedGames();
	void loadFromCache();
	std::string getScreenshotForGame(FileData* game);
	void updateDisplay();
	void updateDisplayForComponents(ImageComponent* screenshot, ImageComponent* marquee,
	                                 TextComponent* gameName, TextComponent* playInfo,
	                                 int gameIndex);
	void navigateTo(int index);
	void launchCurrentGame();

	static bool sPendingGameSwitcher;
	static GuiGameSwitcher* sActiveInstance;
};
