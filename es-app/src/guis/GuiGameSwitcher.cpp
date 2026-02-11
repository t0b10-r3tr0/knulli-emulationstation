#include "GuiGameSwitcher.h"
#include "GuiSettings.h"

#include "SystemData.h"
#include "FileData.h"
#include "SaveStateRepository.h"
#include "Settings.h"
#include "views/ViewController.h"
#include "views/gamelist/IGameListView.h"
#include "utils/FileSystemUtil.h"
#include "utils/TimeUtil.h"
#include "utils/StringUtil.h"
#include "utils/Platform.h"
#include "LocaleES.h"
#include "ThemeData.h"
#include "Log.h"
#include "PlatformId.h"
#include "Paths.h"
#include "InputManager.h"
#include "QuickResume.h"

#include "InputConfig.h"
#include "components/SliderComponent.h"
#include "components/SwitchComponent.h"

#include <rapidjson/rapidjson.h>
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <algorithm>
#include <fstream>
#include <SDL_events.h>
#include <SDL_timer.h>

// Static members
bool GuiGameSwitcher::sPendingGameSwitcher = false;
GuiGameSwitcher* GuiGameSwitcher::sActiveInstance = nullptr;

void GuiGameSwitcher::setPendingGameSwitcher(bool pending)
{
	sPendingGameSwitcher = pending;
	LOG(LogDebug) << "GuiGameSwitcher: Pending set to " << (pending ? "true" : "false");
}

bool GuiGameSwitcher::hasPendingGameSwitcher()
{
	return sPendingGameSwitcher;
}

bool GuiGameSwitcher::isActive()
{
	return sActiveInstance != nullptr;
}

void GuiGameSwitcher::showPendingGameSwitcher(Window* window)
{
	if (sPendingGameSwitcher && Settings::getInstance()->getBool("GameSwitcherEnabled"))
	{
		sPendingGameSwitcher = false;

		// Don't create another instance if one is already active
		if (sActiveInstance != nullptr)
		{
			LOG(LogDebug) << "GuiGameSwitcher: Already active, skipping";
			return;
		}

		// The game switcher hotkey (Function+Select) doesn't consume the Select button event,
		// so the Quick Access menu (GuiSettings) may have been opened before we got here.
		// Close it if it's on top of the stack.
		GuiComponent* topGui = window->peekGui();
		if (topGui != nullptr && dynamic_cast<GuiSettings*>(topGui) != nullptr)
		{
			LOG(LogDebug) << "GuiGameSwitcher: Closing Quick Access menu that was triggered by hotkey";
			delete topGui;
		}

		GuiGameSwitcher* gameSwitcher = new GuiGameSwitcher(window);
		if (isActive())
		{
			window->pushGui(gameSwitcher);
			LOG(LogDebug) << "GuiGameSwitcher: Showing pending Game Switcher";
		}
		else
		{
			LOG(LogWarning) << "GuiGameSwitcher: No games to show";
			delete gameSwitcher;
		}
	}
}

std::string GuiGameSwitcher::getCachePath()
{
	return Paths::getUserEmulationStationPath() + "/gameswitcher_cache.json";
}

std::string GuiGameSwitcher::getPendingStatsPath()
{
	return Paths::getUserEmulationStationPath() + "/gameswitcher_pending_stats.json";
}

bool GuiGameSwitcher::hasCachedData()
{
	return Utils::FileSystem::exists(getCachePath());
}

void GuiGameSwitcher::savePendingStats(const std::string& gamePath, const std::string& systemName, int elapsedSeconds)
{
	// Load existing pending stats or create new
	rapidjson::Document doc;
	std::string pendingPath = getPendingStatsPath();

	if (Utils::FileSystem::exists(pendingPath))
	{
		std::ifstream file(pendingPath);
		if (file.is_open())
		{
			std::string content((std::istreambuf_iterator<char>(file)),
			                     std::istreambuf_iterator<char>());
			file.close();
			doc.Parse(content.c_str());
			if (doc.HasParseError())
			{
				LOG(LogWarning) << "GuiGameSwitcher: Failed to parse pending stats file, creating new";
				doc.SetArray();
			}
		}
	}

	if (!doc.IsArray())
	{
		doc.SetArray();
	}

	auto& allocator = doc.GetAllocator();

	// Add new entry
	rapidjson::Value entry(rapidjson::kObjectType);
	entry.AddMember("gamePath", rapidjson::Value(gamePath.c_str(), allocator), allocator);
	entry.AddMember("systemName", rapidjson::Value(systemName.c_str(), allocator), allocator);
	entry.AddMember("elapsedSeconds", elapsedSeconds, allocator);
	entry.AddMember("timestamp", (int64_t)time(NULL), allocator);

	doc.PushBack(entry, allocator);

	// Write to file
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);

	std::ofstream outFile(pendingPath);
	if (outFile.is_open())
	{
		outFile << buffer.GetString();
		outFile.close();
		LOG(LogDebug) << "GuiGameSwitcher: Saved pending stats for " << gamePath;
	}

	// Also update the cache file so Game Switcher shows correct order
	std::string cachePath = getCachePath();
	if (Utils::FileSystem::exists(cachePath))
	{
		std::ifstream cacheFile(cachePath);
		if (cacheFile.is_open())
		{
			std::string content((std::istreambuf_iterator<char>(cacheFile)),
			                     std::istreambuf_iterator<char>());
			cacheFile.close();

			rapidjson::Document cacheDoc;
			cacheDoc.Parse(content.c_str());

			if (cacheDoc.HasParseError())
			{
				LOG(LogWarning) << "GuiGameSwitcher: Failed to parse cache file for stats update";
			}
			else if (cacheDoc.IsArray())
			{
				// Find and update the game entry
				for (auto& gameObj : cacheDoc.GetArray())
				{
					if (gameObj.HasMember("gamePath") && gameObj["gamePath"].IsString())
					{
						if (std::string(gameObj["gamePath"].GetString()) == gamePath)
						{
							// Update stats
							if (gameObj.HasMember("playCount"))
								gameObj["playCount"] = gameObj["playCount"].GetInt() + 1;
							if (gameObj.HasMember("gameTime"))
								gameObj["gameTime"] = gameObj["gameTime"].GetInt() + elapsedSeconds;
							break;
						}
					}
				}

				// Re-sort by moving this game to the front (most recently played)
				// Simple approach: find the entry and move it to index 0
				for (rapidjson::SizeType i = 0; i < cacheDoc.Size(); i++)
				{
					if (cacheDoc[i].HasMember("gamePath") && cacheDoc[i]["gamePath"].IsString())
					{
						if (std::string(cacheDoc[i]["gamePath"].GetString()) == gamePath && i > 0)
						{
							// Swap entries to move this game to the front
							rapidjson::Value temp(cacheDoc[i], cacheDoc.GetAllocator());
							for (rapidjson::SizeType j = i; j > 0; j--)
							{
								cacheDoc[j] = cacheDoc[j-1];
							}
							cacheDoc[0] = temp;
							break;
						}
					}
				}

				// Write updated cache
				rapidjson::StringBuffer cacheBuffer;
				rapidjson::PrettyWriter<rapidjson::StringBuffer> cacheWriter(cacheBuffer);
				cacheDoc.Accept(cacheWriter);

				std::ofstream cacheOutFile(cachePath);
				if (cacheOutFile.is_open())
				{
					cacheOutFile << cacheBuffer.GetString();
					cacheOutFile.close();
					LOG(LogDebug) << "GuiGameSwitcher: Updated cache with new stats";
				}
			}
		}
	}
}

void GuiGameSwitcher::applyPendingStats()
{
	std::string pendingPath = getPendingStatsPath();
	if (!Utils::FileSystem::exists(pendingPath))
		return;

	std::ifstream file(pendingPath);
	if (!file.is_open())
		return;

	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	file.close();

	rapidjson::Document doc;
	doc.Parse(content.c_str());

	if (doc.HasParseError())
	{
		LOG(LogWarning) << "GuiGameSwitcher: Failed to parse pending stats file";
		Utils::FileSystem::removeFile(pendingPath);
		return;
	}

	if (!doc.IsArray())
	{
		Utils::FileSystem::removeFile(pendingPath);
		return;
	}

	LOG(LogInfo) << "GuiGameSwitcher: Applying " << doc.Size() << " pending stat updates";

	for (auto& entry : doc.GetArray())
	{
		if (!entry.IsObject())
			continue;

		std::string gamePath;
		std::string systemName;
		int elapsedSeconds = 0;
		int64_t timestamp = 0;

		if (entry.HasMember("gamePath") && entry["gamePath"].IsString())
			gamePath = entry["gamePath"].GetString();
		if (entry.HasMember("systemName") && entry["systemName"].IsString())
			systemName = entry["systemName"].GetString();
		if (entry.HasMember("elapsedSeconds") && entry["elapsedSeconds"].IsInt())
			elapsedSeconds = entry["elapsedSeconds"].GetInt();
		if (entry.HasMember("timestamp") && entry["timestamp"].IsInt64())
			timestamp = entry["timestamp"].GetInt64();

		if (gamePath.empty() || systemName.empty())
			continue;

		// Find the system
		SystemData* system = nullptr;
		for (auto sys : SystemData::sSystemVector)
		{
			if (sys->getName() == systemName)
			{
				system = sys;
				break;
			}
		}

		if (system == nullptr)
		{
			LOG(LogWarning) << "GuiGameSwitcher: System not found: " << systemName;
			continue;
		}

		// Find the game
		FileData* game = nullptr;
		auto games = system->getRootFolder()->getFilesRecursive(GAME, true);
		for (auto g : games)
		{
			if (g->getFullPath() == gamePath)
			{
				game = g;
				break;
			}
		}

		if (game == nullptr)
		{
			LOG(LogWarning) << "GuiGameSwitcher: Game not found: " << gamePath;
			continue;
		}

		// Apply stats
		int playCount = game->getMetadata().getInt(MetaDataId::PlayCount) + 1;
		game->setMetadata(MetaDataId::PlayCount, std::to_string(playCount));

		if (elapsedSeconds >= 10)
		{
			int gameTime = game->getMetadata().getInt(MetaDataId::GameTime) + elapsedSeconds;
			game->setMetadata(MetaDataId::GameTime, std::to_string(gameTime));
		}

		// Set last played time from the timestamp
		game->setMetadata(MetaDataId::LastPlayed, Utils::Time::DateTime(timestamp));

		LOG(LogDebug) << "GuiGameSwitcher: Applied stats for " << game->getName()
		              << " - plays: " << playCount << ", time: +" << elapsedSeconds << "s";
	}

	// Delete the pending stats file
	Utils::FileSystem::removeFile(pendingPath);
}

void GuiGameSwitcher::saveCache(FileData* gameBeingLaunched)
{
	int maxGames = Settings::getInstance()->getInt("GameSwitcherCount");
	if (maxGames <= 0)
		maxGames = 10;

	std::vector<FileData*> allPlayedGames;

	// Iterate all game systems (skip collections, image viewer, etc.)
	for (auto system : SystemData::sSystemVector)
	{
		if (!system->isGameSystem() || system->isCollection())
			continue;

		if (system->hasPlatformId(PlatformIds::IMAGEVIEWER) || system->hasPlatformId(PlatformIds::PLATFORM_IGNORE))
			continue;

		auto games = system->getRootFolder()->getFilesRecursive(GAME, true);
		for (auto game : games)
		{
			int playCount = game->getMetadata().getInt(MetaDataId::PlayCount);
			if (playCount > 0)
				allPlayedGames.push_back(game);
		}
	}

	// Sort by last played date (most recent first)
	std::sort(allPlayedGames.begin(), allPlayedGames.end(), [](FileData* a, FileData* b) {
		return a->getMetadata().get(MetaDataId::LastPlayed) > b->getMetadata().get(MetaDataId::LastPlayed);
	});

	// If a game is being launched, ensure it's at the front of the list
	// (it may not be in the list yet if it's never been played)
	if (gameBeingLaunched != nullptr)
	{
		// Remove from list if already present (to avoid duplicates)
		allPlayedGames.erase(
			std::remove(allPlayedGames.begin(), allPlayedGames.end(), gameBeingLaunched),
			allPlayedGames.end());

		// Insert at front
		allPlayedGames.insert(allPlayedGames.begin(), gameBeingLaunched);
	}

	// Build JSON document
	rapidjson::Document doc;
	doc.SetArray();
	auto& allocator = doc.GetAllocator();

	int count = std::min(maxGames, (int)allPlayedGames.size());
	for (int i = 0; i < count; i++)
	{
		FileData* game = allPlayedGames[i];

		rapidjson::Value gameObj(rapidjson::kObjectType);

		// Game path
		gameObj.AddMember("gamePath",
			rapidjson::Value(game->getFullPath().c_str(), allocator), allocator);

		// Game name
		gameObj.AddMember("gameName",
			rapidjson::Value(game->getName().c_str(), allocator), allocator);

		// System name
		gameObj.AddMember("systemName",
			rapidjson::Value(game->getSystem()->getName().c_str(), allocator), allocator);

		// Screenshot path (using same priority as getScreenshotForGame)
		std::string screenshotPath;
		auto* repo = game->getSourceFileData()->getSystem()->getSaveStateRepository();
		if (repo != nullptr)
		{
			SaveState* autoSave = repo->getGameAutoSave(game);
			if (autoSave != nullptr)
			{
				std::string screenshot = autoSave->getScreenShot();
				if (!screenshot.empty() && Utils::FileSystem::exists(screenshot))
					screenshotPath = screenshot;
			}
			if (screenshotPath.empty())
			{
				auto states = repo->getSaveStates(game);
				for (auto* state : states)
				{
					std::string screenshot = state->getScreenShot();
					if (!screenshot.empty() && Utils::FileSystem::exists(screenshot))
					{
						screenshotPath = screenshot;
						break;
					}
				}
			}
		}
		if (screenshotPath.empty())
		{
			std::string imagePath = game->getImagePath();
			if (!imagePath.empty() && Utils::FileSystem::exists(imagePath))
				screenshotPath = imagePath;
		}
		if (screenshotPath.empty())
		{
			std::string thumbPath = game->getThumbnailPath();
			if (!thumbPath.empty() && Utils::FileSystem::exists(thumbPath))
				screenshotPath = thumbPath;
		}
		gameObj.AddMember("screenshotPath",
			rapidjson::Value(screenshotPath.c_str(), allocator), allocator);

		// Marquee path
		std::string marqueePath = game->getMarqueePath();
		gameObj.AddMember("marqueePath",
			rapidjson::Value(marqueePath.c_str(), allocator), allocator);

		// Launch command
		std::string launchCmd = game->getlaunchCommand(false);
		gameObj.AddMember("launchCommand",
			rapidjson::Value(launchCmd.c_str(), allocator), allocator);

		// Play count
		gameObj.AddMember("playCount", game->getMetadata().getInt(MetaDataId::PlayCount), allocator);

		// Game time
		gameObj.AddMember("gameTime", game->getMetadata().getInt(MetaDataId::GameTime), allocator);

		doc.PushBack(gameObj, allocator);
	}

	// Write to file
	rapidjson::StringBuffer buffer;
	rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
	doc.Accept(writer);

	std::ofstream file(getCachePath());
	if (file.is_open())
	{
		file << buffer.GetString();
		file.close();
		LOG(LogDebug) << "GuiGameSwitcher: Saved cache with " << count << " games";
	}
	else
	{
		LOG(LogError) << "GuiGameSwitcher: Failed to write cache file";
	}
}

void GuiGameSwitcher::loadFromCache()
{
	std::string cachePath = getCachePath();
	if (!Utils::FileSystem::exists(cachePath))
	{
		LOG(LogWarning) << "GuiGameSwitcher: Cache file not found";
		return;
	}

	std::ifstream file(cachePath);
	if (!file.is_open())
	{
		LOG(LogError) << "GuiGameSwitcher: Failed to open cache file";
		return;
	}

	std::string content((std::istreambuf_iterator<char>(file)),
	                     std::istreambuf_iterator<char>());
	file.close();

	rapidjson::Document doc;
	doc.Parse(content.c_str());

	if (doc.HasParseError() || !doc.IsArray())
	{
		LOG(LogError) << "GuiGameSwitcher: Failed to parse cache file";
		return;
	}

	for (auto& gameObj : doc.GetArray())
	{
		if (!gameObj.IsObject())
			continue;

		GameItem item;
		item.game = nullptr;  // Cached mode - no FileData

		if (gameObj.HasMember("gamePath") && gameObj["gamePath"].IsString())
			item.gamePath = gameObj["gamePath"].GetString();

		if (gameObj.HasMember("gameName") && gameObj["gameName"].IsString())
			item.gameName = gameObj["gameName"].GetString();

		if (gameObj.HasMember("systemName") && gameObj["systemName"].IsString())
			item.systemName = gameObj["systemName"].GetString();

		if (gameObj.HasMember("screenshotPath") && gameObj["screenshotPath"].IsString())
			item.screenshotPath = gameObj["screenshotPath"].GetString();

		if (gameObj.HasMember("marqueePath") && gameObj["marqueePath"].IsString())
			item.marqueePath = gameObj["marqueePath"].GetString();

		if (gameObj.HasMember("launchCommand") && gameObj["launchCommand"].IsString())
			item.launchCommand = gameObj["launchCommand"].GetString();

		if (gameObj.HasMember("playCount") && gameObj["playCount"].IsInt())
			item.playCount = gameObj["playCount"].GetInt();

		if (gameObj.HasMember("gameTime") && gameObj["gameTime"].IsInt())
			item.gameTime = gameObj["gameTime"].GetInt();

		mGames.push_back(item);
	}

	LOG(LogDebug) << "GuiGameSwitcher: Loaded " << mGames.size() << " games from cache";
}

GuiGameSwitcher::GuiGameSwitcher(Window* window, bool fromCache) : GuiComponent(window)
{
	sActiveInstance = this;
	mCachedMode = fromCache;

	mCurrentIndex = 0;
	mScreenshot = nullptr;
	mMarquee = nullptr;
	mGameName = nullptr;
	mPlayInfo = nullptr;
	mPrevScreenshot = nullptr;
	mPrevMarquee = nullptr;
	mPrevGameName = nullptr;
	mPrevPlayInfo = nullptr;
	mAnimating = false;
	mLaunching = false;
	mAnimationProgress = 0.0f;
	mAnimationDirection = 0;
	mAnimationDuration = Settings::getInstance()->getInt("GameSwitcherAnimationSpeed");
	if (mAnimationDuration < 50)
		mAnimationDuration = 50;  // Minimum 50ms to prevent division by zero or too-fast animations

	setSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	if (fromCache)
		loadFromCache();
	else
		loadRecentlyPlayedGames();

	if (mGames.empty())
	{
		// No recently played games - mark as inactive so caller knows not to use this
		// Caller must check isActive() and delete if false
		sActiveInstance = nullptr;
		return;
	}

	auto theme = ThemeData::getMenuTheme();
	auto fontPath = theme->Text.font->getPath();
	float fontSize = Renderer::getScreenHeight() / 20.0f;
	float infoFontSize = Renderer::getScreenHeight() / 28.0f;
	auto font = Font::get((int)fontSize, fontPath);
	auto infoFont = Font::get((int)infoFontSize, fontPath);

	// Create current screenshot component (full screen)
	mScreenshot = new ImageComponent(mWindow, true);
	mScreenshot->setOrigin(0.5f, 0.5f);
	mScreenshot->setPosition(Renderer::getScreenWidth() / 2.0f, Renderer::getScreenHeight() / 2.0f);
	mScreenshot->setMaxSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	// Create current marquee component (top center)
	float marqueeWidthPct = Settings::getInstance()->getInt("GameSwitcherMarqueeSize") / 100.0f;
	float marqueeHeightPct = marqueeWidthPct * 0.45f;  // Height proportional to width

	// Calculate Y position - ensure marquee doesn't go off the top of the screen
	float marqueeMaxHeight = Renderer::getScreenHeight() * marqueeHeightPct;
	float marqueeTopMargin = Renderer::getScreenHeight() * 0.02f;  // 2% margin from top
	float marqueeDefaultY = Renderer::getScreenHeight() * 0.14f;
	float marqueeMinY = marqueeTopMargin + (marqueeMaxHeight / 2.0f);  // Minimum Y to keep on screen
	float marqueeY = std::max(marqueeDefaultY, marqueeMinY);

	mMarquee = new ImageComponent(mWindow, true);
	mMarquee->setOrigin(0.5f, 0.5f);
	mMarquee->setPosition(Renderer::getScreenWidth() * 0.50f, marqueeY);
	mMarquee->setMaxSize(Renderer::getScreenWidth() * marqueeWidthPct, marqueeMaxHeight);

	// Create current game name text (fallback when no marquee)
	mGameName = new TextComponent(mWindow);
	mGameName->setPosition(0, Renderer::getScreenHeight() * 0.05f);
	mGameName->setSize((float)Renderer::getScreenWidth(), Renderer::getScreenHeight() * 0.10f);
	mGameName->setHorizontalAlignment(ALIGN_CENTER);
	mGameName->setVerticalAlignment(ALIGN_CENTER);
	mGameName->setColor(0xFFFFFFFF);
	mGameName->setGlowColor(0x00000080);
	mGameName->setGlowSize(4);
	mGameName->setFont(font);

	// Create current play info text (bottom center)
	// Move up when help prompts are visible to avoid overlap with help bar
	float screenH = (float)Renderer::getScreenHeight();
	float playInfoHeight = screenH * 0.08f;
	float playInfoY;

	bool helpEnabled = Settings::getInstance()->getBool("GameSwitcherHelpEnabled");
	if (helpEnabled)
	{
		HelpStyle helpStyle = getHelpStyle();
		float helpBarY = helpStyle.position.y();

		// Account for the help background padding above the text
		float helpContentH = helpStyle.font ? Math::round(helpStyle.font->getLetterHeight() * 1.25f) : screenH * 0.03f;
		float helpBgPadding = helpContentH * 0.4f;
		float gap = screenH * 0.015f;
		playInfoY = helpBarY - helpBgPadding - gap - playInfoHeight;
	}
	else
	{
		playInfoY = screenH * 0.90f;
	}

	mPlayInfo = new TextComponent(mWindow);
	mPlayInfo->setPosition(0, playInfoY);
	mPlayInfo->setSize((float)Renderer::getScreenWidth(), playInfoHeight);
	mPlayInfo->setHorizontalAlignment(ALIGN_CENTER);
	mPlayInfo->setVerticalAlignment(ALIGN_CENTER);
	mPlayInfo->setColor(0xD0D0D0FF);
	mPlayInfo->setGlowColor(0x00000060);
	mPlayInfo->setGlowSize(2);
	mPlayInfo->setFont(infoFont);

	// Create previous screenshot component (for animation)
	mPrevScreenshot = new ImageComponent(mWindow, true);
	mPrevScreenshot->setOrigin(0.5f, 0.5f);
	mPrevScreenshot->setPosition(Renderer::getScreenWidth() / 2.0f, Renderer::getScreenHeight() / 2.0f);
	mPrevScreenshot->setMaxSize((float)Renderer::getScreenWidth(), (float)Renderer::getScreenHeight());

	// Create previous marquee component (for animation)
	mPrevMarquee = new ImageComponent(mWindow, true);
	mPrevMarquee->setOrigin(0.5f, 0.5f);
	mPrevMarquee->setPosition(Renderer::getScreenWidth() * 0.50f, marqueeY);
	mPrevMarquee->setMaxSize(Renderer::getScreenWidth() * marqueeWidthPct, marqueeMaxHeight);

	// Create previous game name text (for animation)
	mPrevGameName = new TextComponent(mWindow);
	mPrevGameName->setPosition(0, Renderer::getScreenHeight() * 0.05f);
	mPrevGameName->setSize((float)Renderer::getScreenWidth(), Renderer::getScreenHeight() * 0.10f);
	mPrevGameName->setHorizontalAlignment(ALIGN_CENTER);
	mPrevGameName->setVerticalAlignment(ALIGN_CENTER);
	mPrevGameName->setColor(0xFFFFFFFF);
	mPrevGameName->setGlowColor(0x00000080);
	mPrevGameName->setGlowSize(4);
	mPrevGameName->setFont(font);

	// Create previous play info text (for animation)
	mPrevPlayInfo = new TextComponent(mWindow);
	mPrevPlayInfo->setPosition(0, playInfoY);
	mPrevPlayInfo->setSize((float)Renderer::getScreenWidth(), playInfoHeight);
	mPrevPlayInfo->setHorizontalAlignment(ALIGN_CENTER);
	mPrevPlayInfo->setVerticalAlignment(ALIGN_CENTER);
	mPrevPlayInfo->setColor(0xD0D0D0FF);
	mPrevPlayInfo->setGlowColor(0x00000060);
	mPrevPlayInfo->setGlowSize(2);
	mPrevPlayInfo->setFont(infoFont);

	// Display the first game
	updateDisplay();
}

GuiGameSwitcher::~GuiGameSwitcher()
{
	sActiveInstance = nullptr;

	if (mScreenshot != nullptr)
		delete mScreenshot;
	if (mMarquee != nullptr)
		delete mMarquee;
	if (mGameName != nullptr)
		delete mGameName;
	if (mPlayInfo != nullptr)
		delete mPlayInfo;
	if (mPrevScreenshot != nullptr)
		delete mPrevScreenshot;
	if (mPrevMarquee != nullptr)
		delete mPrevMarquee;
	if (mPrevGameName != nullptr)
		delete mPrevGameName;
	if (mPrevPlayInfo != nullptr)
		delete mPrevPlayInfo;
}

void GuiGameSwitcher::loadRecentlyPlayedGames()
{
	int maxGames = Settings::getInstance()->getInt("GameSwitcherCount");
	if (maxGames <= 0)
		maxGames = 10;

	std::vector<FileData*> allPlayedGames;

	// Iterate all game systems (skip collections, image viewer, etc.)
	for (auto system : SystemData::sSystemVector)
	{
		if (!system->isGameSystem() || system->isCollection())
			continue;

		if (system->hasPlatformId(PlatformIds::IMAGEVIEWER) || system->hasPlatformId(PlatformIds::PLATFORM_IGNORE))
			continue;

		auto games = system->getRootFolder()->getFilesRecursive(GAME, true);
		for (auto game : games)
		{
			// Only include games that have been played
			int playCount = game->getMetadata().getInt(MetaDataId::PlayCount);
			if (playCount > 0)
				allPlayedGames.push_back(game);
		}
	}

	// Sort by last played date (most recent first)
	std::sort(allPlayedGames.begin(), allPlayedGames.end(), [](FileData* a, FileData* b) {
		return a->getMetadata().get(MetaDataId::LastPlayed) > b->getMetadata().get(MetaDataId::LastPlayed);
	});

	// Take top N games and build the items list
	int count = std::min(maxGames, (int)allPlayedGames.size());
	for (int i = 0; i < count; i++)
	{
		GameItem item;
		item.game = allPlayedGames[i];
		item.screenshotPath = getScreenshotForGame(allPlayedGames[i]);
		item.playCount = 0;
		item.gameTime = 0;
		mGames.push_back(item);
	}

	LOG(LogDebug) << "GuiGameSwitcher: Loaded " << mGames.size() << " recently played games";
}

std::string GuiGameSwitcher::getScreenshotForGame(FileData* game)
{
	// Priority 1: Save state screenshot (auto-save first)
	auto* repo = game->getSourceFileData()->getSystem()->getSaveStateRepository();
	if (repo != nullptr)
	{
		// Try auto-save first
		SaveState* autoSave = repo->getGameAutoSave(game);
		if (autoSave != nullptr)
		{
			std::string screenshot = autoSave->getScreenShot();
			if (!screenshot.empty() && Utils::FileSystem::exists(screenshot))
				return screenshot;
		}

		// Try any save state screenshot
		auto states = repo->getSaveStates(game);
		for (auto* state : states)
		{
			std::string screenshot = state->getScreenShot();
			if (!screenshot.empty() && Utils::FileSystem::exists(screenshot))
				return screenshot;
		}
	}

	// Priority 2: Scraped image
	std::string imagePath = game->getImagePath();
	if (!imagePath.empty() && Utils::FileSystem::exists(imagePath))
		return imagePath;

	// Priority 3: Thumbnail as fallback
	std::string thumbPath = game->getThumbnailPath();
	if (!thumbPath.empty() && Utils::FileSystem::exists(thumbPath))
		return thumbPath;

	// No screenshot available
	return "";
}

void GuiGameSwitcher::updateDisplayForComponents(ImageComponent* screenshot, ImageComponent* marquee,
                                                  TextComponent* gameName, TextComponent* playInfo,
                                                  int gameIndex)
{
	if (mGames.empty() || gameIndex < 0 || gameIndex >= (int)mGames.size())
		return;

	const GameItem& item = mGames[gameIndex];

	// Update screenshot
	if (!item.screenshotPath.empty())
		screenshot->setImage(item.screenshotPath);
	else
		screenshot->setImage("");

	// Get values based on mode (cached vs live)
	std::string marqueePath;
	std::string gameNameStr;
	int playCount;
	int gameTime;

	if (mCachedMode || item.game == nullptr)
	{
		// Use cached data
		marqueePath = item.marqueePath;
		gameNameStr = item.gameName;
		playCount = item.playCount;
		gameTime = item.gameTime;
	}
	else
	{
		// Use live FileData
		marqueePath = item.game->getMarqueePath();
		gameNameStr = item.game->getName();
		playCount = item.game->getMetadata().getInt(MetaDataId::PlayCount);
		gameTime = item.game->getMetadata().getInt(MetaDataId::GameTime);
	}

	// Update marquee or game name
	bool marqueeEnabled = Settings::getInstance()->getBool("GameSwitcherMarqueeEnabled");
	if (marqueeEnabled)
	{
		if (!marqueePath.empty() && Utils::FileSystem::exists(marqueePath))
		{
			marquee->setImage(marqueePath);
			marquee->setVisible(true);
			gameName->setVisible(false);
		}
		else
		{
			marquee->setImage("");
			marquee->setVisible(false);
			bool fallback = Settings::getInstance()->getBool("GameSwitcherMarqueeFallback");
			if (fallback)
			{
				gameName->setText(gameNameStr);
				gameName->setVisible(true);
			}
			else
			{
				gameName->setVisible(false);
			}
		}
	}
	else
	{
		// Marquee disabled - hide both marquee and game name
		marquee->setImage("");
		marquee->setVisible(false);
		gameName->setVisible(false);
	}

	// Update play info
	bool playInfoEnabled = Settings::getInstance()->getBool("GameSwitcherPlayInfoEnabled");
	if (playInfoEnabled)
	{
		std::string playTimeStr = Utils::Time::secondsToString(gameTime);
		std::string playInfoStr;

		if (playCount == 1)
			playInfoStr = _("Played 1 time") + "  |  " + _("Play time") + ": " + playTimeStr;
		else
			playInfoStr = _("Played") + " " + std::to_string(playCount) + " " + _("times") + "  |  " + _("Play time") + ": " + playTimeStr;

		playInfo->setText(playInfoStr);
		playInfo->setVisible(true);
	}
	else
	{
		playInfo->setText("");
		playInfo->setVisible(false);
	}
}

void GuiGameSwitcher::updateDisplay()
{
	updateDisplayForComponents(mScreenshot, mMarquee, mGameName, mPlayInfo, mCurrentIndex);
}

void GuiGameSwitcher::navigateTo(int index)
{
	if (mGames.empty() || mAnimating)
		return;

	int oldIndex = mCurrentIndex;

	// Wrap around navigation
	if (index < 0)
		index = (int)mGames.size() - 1;
	else if (index >= (int)mGames.size())
		index = 0;

	if (index == oldIndex)
		return;

	// Determine animation direction
	// Going right (next): old slides left, new comes from right
	// Going left (prev): old slides right, new comes from left
	if (index > oldIndex || (oldIndex == (int)mGames.size() - 1 && index == 0))
		mAnimationDirection = 1;  // Next game - slide left
	else
		mAnimationDirection = -1; // Prev game - slide right

	// Copy current display to previous components
	updateDisplayForComponents(mPrevScreenshot, mPrevMarquee, mPrevGameName, mPrevPlayInfo, oldIndex);

	// Update current index and display
	mCurrentIndex = index;
	updateDisplay();

	// Start animation
	mAnimating = true;
	mAnimationProgress = 0.0f;
}

void GuiGameSwitcher::launchCurrentGame()
{
	if (mGames.empty() || mCurrentIndex < 0 || mCurrentIndex >= (int)mGames.size())
		return;

	const GameItem& item = mGames[mCurrentIndex];

	// Store window pointer before deleting this
	Window* window = mWindow;

	if (mCachedMode || item.game == nullptr)
	{
		// Cached mode - launch using cached command
		std::string command = item.launchCommand;
		if (command.empty())
		{
			LOG(LogError) << "GuiGameSwitcher: No launch command cached for game";
			return;
		}

		// Copy game info before deleting this
		std::string gamePath = item.gamePath;
		std::string systemName = item.systemName;

		// Close this GUI
		delete this;

		// Update Quick Resume with the new game
		LOG(LogInfo) << "GuiGameSwitcher: Setting Quick Resume - gamePath: " << gamePath;
		LOG(LogDebug) << "GuiGameSwitcher: Setting Quick Resume - command: " << command;
		bool quickResumeSaved = QuickResume::setQuickResume(command, gamePath);
		LOG(LogInfo) << "GuiGameSwitcher: Quick Resume saved: " << (quickResumeSaved ? "true" : "false");

		// Configure controllers and launch
		InputManager::getInstance()->init();
		command = Utils::String::replace(command, "%CONTROLLERSCONFIG%", InputManager::getInstance()->configureEmulators());

		LOG(LogInfo) << "GuiGameSwitcher: Launching cached game: " << command;

		// Track start time
		time_t startTime = time(NULL);

		Utils::Platform::ProcessStartInfo(command).run();

		// Calculate elapsed time and save pending stats
		time_t endTime = time(NULL);
		int elapsedSeconds = (int)difftime(endTime, startTime);

		savePendingStats(gamePath, systemName, elapsedSeconds);
	}
	else
	{
		// Normal mode - use FileData
		FileData* game = item.game;

		// Set cursor in game list view (like screensaver does)
		auto view = ViewController::get()->getGameListView(game->getSystem(), false);
		if (view != nullptr)
			view->setCursor(game);

		// Close this GUI
		delete this;

		// Launch the game
		game->launchGame(window);
	}
}

bool GuiGameSwitcher::input(InputConfig* config, Input input)
{
	if (input.value == 0)
		return false;

	// B button - exit
	if (config->isMappedTo(BUTTON_BACK, input))
	{
		delete this;
		return true;
	}

	// Block all input during launch fade-out
	if (mLaunching)
		return true;

	// A button - start fade-out then launch game (only if not animating)
	if (config->isMappedTo(BUTTON_OK, input) && !mAnimating)
	{
		bool launchAnimEnabled = Settings::getInstance()->getBool("GameSwitcherLaunchAnimationEnabled");
		bool hasMarquee = mMarquee && mMarquee->isVisible() && mMarquee->hasImage();
		bool hasPlayInfo = mPlayInfo && mPlayInfo->isVisible();
		if (launchAnimEnabled && (hasMarquee || hasPlayInfo))
		{
			// Start fade-out animation (uses first half of navigation animation timing)
			mLaunching = true;
			mAnimating = true;
			mAnimationProgress = 0.0f;
		}
		else
		{
			launchCurrentGame();
		}
		return true;
	}

	// Left - previous game
	if (config->isMappedLike("left", input))
	{
		navigateTo(mCurrentIndex - 1);
		return true;
	}

	// Right - next game
	if (config->isMappedLike("right", input))
	{
		navigateTo(mCurrentIndex + 1);
		return true;
	}

	// Consume select button to prevent it from triggering menus on underlying views
	// (the game switcher hotkey uses select, so stale events may be in the queue)
	if (config->isMappedTo("select", input))
	{
		return true;
	}

	return GuiComponent::input(config, input);
}

void GuiGameSwitcher::update(int deltaTime)
{
	GuiComponent::update(deltaTime);

	if (mAnimating)
	{
		// Launch fade-out runs at double speed (same as the fade portion of navigation)
		float speed = mLaunching ? 2.0f : 1.0f;
		mAnimationProgress += speed * (float)deltaTime / (float)mAnimationDuration;

		if (mAnimationProgress >= 1.0f)
		{
			mAnimationProgress = 1.0f;
			mAnimating = false;

			if (mLaunching)
			{
				launchCurrentGame();
				return;  // this is deleted, stop processing
			}
		}
	}

	if (mScreenshot)
		mScreenshot->update(deltaTime);
	if (mMarquee)
		mMarquee->update(deltaTime);
	if (mPrevScreenshot)
		mPrevScreenshot->update(deltaTime);
	if (mPrevMarquee)
		mPrevMarquee->update(deltaTime);
}

void GuiGameSwitcher::render(const Transform4x4f& transform)
{
	float screenWidth = (float)Renderer::getScreenWidth();
	float screenHeight = (float)Renderer::getScreenHeight();

	// Calculate info background color from opacity setting
	int bgOpacityPct = Settings::getInstance()->getInt("GameSwitcherInfoBackgroundOpacity");
	unsigned char bgAlpha = (unsigned char)((bgOpacityPct / 100.0f) * 255.0f);
	unsigned int infoBgColor = 0x00000000 | bgAlpha;

	// Draw black background
	Renderer::setMatrix(Transform4x4f::Identity());
	Renderer::drawRect(0.0f, 0.0f, screenWidth, screenHeight, 0x000000FF);

	// Calculate animation offset and opacity using smootherstep for acceleration and deceleration
	float animOffset = 0.0f;
	unsigned char currOpacity = 255;
	unsigned char prevOpacity = 255;
	float currOpacityFactor = 1.0f;
	float prevOpacityFactor = 1.0f;

	if (mAnimating)
	{
		float t = mAnimationProgress;

		if (mLaunching)
		{
			// Launch fade-out: fade current marquee/play info from 255 → 0
			// Progress already runs at 2x speed so this takes half the animation duration
			float eased = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
			currOpacity = (unsigned char)((1.0f - eased) * 255.0f);
			currOpacityFactor = 1.0f - eased;
		}
		else
		{
			// Smootherstep: 6t^5 - 15t^4 + 10t^3 (starts slow, speeds up, ends slow)
			float eased = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
			animOffset = eased * screenWidth * mAnimationDirection;

			// Calculate opacity for fade effect on marquee and play info
			// Fade takes half the time of the slide animation:
			// - Outgoing: fades out during first half (0.0 - 0.5 progress)
			// - Incoming: fades in during second half (0.5 - 1.0 progress)

			// Previous (outgoing) fades out in first half: 255 → 0 during 0.0-0.5
			float prevFadeProgress = std::min(t * 2.0f, 1.0f);  // 0→1 over first half
			float prevEased = prevFadeProgress * prevFadeProgress * prevFadeProgress *
			                  (prevFadeProgress * (prevFadeProgress * 6.0f - 15.0f) + 10.0f);
			prevOpacity = (unsigned char)((1.0f - prevEased) * 255.0f);
			prevOpacityFactor = 1.0f - prevEased;

			// Current (incoming) fades in during second half: 0 → 255 during 0.5-1.0
			float currFadeProgress = std::max((t - 0.5f) * 2.0f, 0.0f);  // 0→1 over second half
			float currEased = currFadeProgress * currFadeProgress * currFadeProgress *
			                  (currFadeProgress * (currFadeProgress * 6.0f - 15.0f) + 10.0f);
			currOpacity = (unsigned char)(currEased * 255.0f);
			currOpacityFactor = currEased;
		}
	}

	// Render previous components (sliding out) if animating (not during launch fade-out)
	if (mAnimating && !mLaunching)
	{
		float prevOffset = -animOffset;  // Previous slides opposite direction

		Transform4x4f prevTransform = transform;
		prevTransform.translate(Vector3f(prevOffset, 0, 0));

		if (mPrevScreenshot && mPrevScreenshot->hasImage())
			mPrevScreenshot->render(prevTransform);

		// Apply fade-out opacity to previous marquee/game name
		if (mPrevMarquee && mPrevMarquee->isVisible() && mPrevMarquee->hasImage())
		{
			mPrevMarquee->setOpacity(prevOpacity);
			mPrevMarquee->render(prevTransform);
		}
		else if (mPrevGameName && mPrevGameName->isVisible())
		{
			mPrevGameName->setOpacity(prevOpacity);
			mPrevGameName->render(prevTransform);
		}

		// Draw previous play info with background (both fading out)
		if (mPrevPlayInfo && mPrevPlayInfo->isVisible())
		{
			float padding = screenHeight * 0.015f;
			auto font = mPrevPlayInfo->getFont();
			float textWidth = font->sizeText(mPrevPlayInfo->getText()).x();
			float textHeight = font->getHeight();

			float bgWidth = textWidth + (padding * 2);
			float bgHeight = textHeight + (padding * 2);
			float bgX = (screenWidth - bgWidth) / 2.0f + prevOffset;
			float bgY = mPrevPlayInfo->getPosition().y() + (mPrevPlayInfo->getSize().y() - bgHeight) / 2.0f;

			// Fade out the background as well
			unsigned char fadedBgAlpha = (unsigned char)(bgAlpha * prevOpacityFactor);
			unsigned int fadedBgColor = 0x00000000 | fadedBgAlpha;

			Renderer::setMatrix(Transform4x4f::Identity());
			Renderer::drawRect(bgX, bgY, bgWidth, bgHeight, fadedBgColor, fadedBgColor);

			mPrevPlayInfo->setOpacity(prevOpacity);
			mPrevPlayInfo->render(prevTransform);
		}
	}

	// Render current components (sliding in or static)
	// During launch fade-out, current components stay in place (no horizontal offset)
	float currOffset = (mAnimating && !mLaunching) ? (screenWidth * mAnimationDirection - animOffset) : 0.0f;

	Transform4x4f currTransform = transform;
	currTransform.translate(Vector3f(currOffset, 0, 0));

	if (mScreenshot && mScreenshot->hasImage())
		mScreenshot->render(currTransform);

	// Apply fade-in opacity to current marquee/game name (full opacity when not animating)
	if (mMarquee && mMarquee->isVisible() && mMarquee->hasImage())
	{
		mMarquee->setOpacity(currOpacity);
		mMarquee->render(currTransform);
	}
	else if (mGameName && mGameName->isVisible())
	{
		mGameName->setOpacity(currOpacity);
		mGameName->render(currTransform);
	}

	// Draw current play info with background (both fading in when animating)
	if (mPlayInfo && mPlayInfo->isVisible())
	{
		float padding = screenHeight * 0.015f;
		auto font = mPlayInfo->getFont();
		float textWidth = font->sizeText(mPlayInfo->getText()).x();
		float textHeight = font->getHeight();

		float bgWidth = textWidth + (padding * 2);
		float bgHeight = textHeight + (padding * 2);
		float bgX = (screenWidth - bgWidth) / 2.0f + currOffset;
		float bgY = mPlayInfo->getPosition().y() + (mPlayInfo->getSize().y() - bgHeight) / 2.0f;

		// Fade in the background as well (full opacity when not animating)
		unsigned char fadedCurrBgAlpha = (unsigned char)(bgAlpha * currOpacityFactor);
		unsigned int fadedCurrBgColor = 0x00000000 | fadedCurrBgAlpha;

		Renderer::setMatrix(Transform4x4f::Identity());
		Renderer::drawRect(bgX, bgY, bgWidth, bgHeight, fadedCurrBgColor, fadedCurrBgColor);

		mPlayInfo->setOpacity(currOpacity);
		mPlayInfo->render(currTransform);
	}

	// Render help prompts early to bypass Window's fullScreenMenus suppression
	if (Settings::getInstance()->getBool("GameSwitcherHelpEnabled"))
	{
		// Draw a semi-transparent background strip behind the help prompts
		HelpStyle style = getHelpStyle();
		if (style.font)
		{
			float helpHeight = Math::round(style.font->getLetterHeight() * 1.25f);
			float padding = helpHeight * 0.4f;
			float bgY = style.position.y() - padding;
			float bgHeight = screenHeight - bgY;

			Renderer::setMatrix(Transform4x4f::Identity());
			Renderer::drawRect(0.0f, bgY, screenWidth, bgHeight, infoBgColor, infoBgColor);
		}

		mWindow->renderHelpPromptsEarly(transform);
	}
}

std::vector<HelpPrompt> GuiGameSwitcher::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;

	// Hide help prompts if disabled in settings
	if (!Settings::getInstance()->getBool("GameSwitcherHelpEnabled"))
		return prompts;

	prompts.push_back(HelpPrompt("left/right", _("NAVIGATE")));
	prompts.push_back(HelpPrompt(BUTTON_OK, _("LAUNCH")));
	prompts.push_back(HelpPrompt(BUTTON_BACK, _("BACK")));
	return prompts;
}

HelpStyle GuiGameSwitcher::getHelpStyle()
{
	HelpStyle style = GuiComponent::getHelpStyle();

	// Always center help prompts horizontally in Game Switcher
	style.position = Vector2f(Renderer::getScreenWidth() / 2.0f, style.position.y());
	style.origin = Vector2f(0.5f, 0.0f);

	return style;
}

bool GuiGameSwitcher::runCachedMode()
{
	if (!hasCachedData())
	{
		LOG(LogWarning) << "No cached Game Switcher data available";
		return false;
	}

	LOG(LogInfo) << "Starting cached Game Switcher mode";

	// Initialize minimal components
	Window window;
	if (!window.init(true, false))
	{
		LOG(LogError) << "Window failed to initialize for cached Game Switcher!";
		return false;
	}

	// Initialize input
	InputConfig::AssignActionButtons();
	InputManager::getInstance()->init();
	SDL_StopTextInput();

	// Create and show cached Game Switcher
	GuiGameSwitcher* gameSwitcher = new GuiGameSwitcher(&window, true);

	// Check if Game Switcher was created successfully (has games)
	if (!isActive())
	{
		LOG(LogWarning) << "Cached Game Switcher has no games";
		delete gameSwitcher;
		window.deinit(true);
		return false;
	}

	window.pushGui(gameSwitcher);

	// Suppress clock/battery/controller overlays during cached mode
	bool origDrawClock = Settings::DrawClock();
	bool origShowController = Settings::ShowControllerActivity();
	Settings::setDrawClock(false);
	Settings::setShowControllerActivity(false);

	// Run minimal event loop
	bool running = true;
	int lastTime = SDL_GetTicks();

	while (running && isActive())
	{
		SDL_Event event;
		if (SDL_PollEvent(&event))
		{
			do
			{
				InputManager::getInstance()->parseEvent(event, &window);

				if (event.type == SDL_QUIT)
					running = false;
			}
			while (SDL_PollEvent(&event));
		}

		int curTime = SDL_GetTicks();
		int deltaTime = curTime - lastTime;
		lastTime = curTime;

		window.update(deltaTime);
		window.render();
		Renderer::swapBuffers();
	}

	// Restore overlay settings
	Settings::setDrawClock(origDrawClock);
	Settings::setShowControllerActivity(origShowController);

	window.deinit(true);

	return false;
}

void GuiGameSwitcher::openSettings(Window* window, bool selectMarqueeEnable, bool selectPlayInfoEnable)
{
	bool baseMarqueeEnabled = Settings::getInstance()->getBool("GameSwitcherMarqueeEnabled");
	bool basePlayInfoEnabled = Settings::getInstance()->getBool("GameSwitcherPlayInfoEnabled");

	auto s = new GuiSettings(window, _("GAME SWITCHER SETTINGS").c_str());

	// Game Switcher count setting
	auto gameSwitcherCount = std::make_shared<SliderComponent>(window, 5.f, 25.f, 1.f, "");
	gameSwitcherCount->setValue((float)Settings::getInstance()->getInt("GameSwitcherCount"));
	s->addWithDescription(_("AVAILABLE SAVE COUNT"), _("Maximum number of recently played games to show."), gameSwitcherCount);
	s->addSaveFunc([gameSwitcherCount] {
		Settings::getInstance()->setInt("GameSwitcherCount", (int)Math::round(gameSwitcherCount->getValue()));
	});

	// Game Switcher animation speed setting
	auto gameSwitcherSpeed = std::make_shared<SliderComponent>(window, 100.f, 1000.f, 50.f, "ms");
	gameSwitcherSpeed->setValue((float)Settings::getInstance()->getInt("GameSwitcherAnimationSpeed"));
	s->addWithDescription(_("ANIMATION SPEED"), _("Duration of the transition animation in milliseconds."), gameSwitcherSpeed);
	s->addSaveFunc([gameSwitcherSpeed] {
		Settings::getInstance()->setInt("GameSwitcherAnimationSpeed", (int)Math::round(gameSwitcherSpeed->getValue()));
	});

	// Enable Marquee toggle
	auto marqueeEnable = std::make_shared<SwitchComponent>(window);
	marqueeEnable->setState(baseMarqueeEnabled);
	s->addWithDescription(_("ENABLE MARQUEE"), _("Show the game's marquee image above the screenshot."), marqueeEnable, selectMarqueeEnable);
	s->addSaveFunc([marqueeEnable] {
		Settings::getInstance()->setBool("GameSwitcherMarqueeEnabled", marqueeEnable->getState());
	});

	// Game Switcher marquee size setting - only shown when marquee is enabled
	if (baseMarqueeEnabled)
	{
		auto gameSwitcherMarquee = std::make_shared<SliderComponent>(window, 20.f, 80.f, 5.f, "%");
		gameSwitcherMarquee->setValue((float)Settings::getInstance()->getInt("GameSwitcherMarqueeSize"));
		s->addWithDescription(_("MARQUEE SIZE"), _("Size of the marquee image as a percentage of screen width."), gameSwitcherMarquee);
		s->addSaveFunc([gameSwitcherMarquee] {
			Settings::getInstance()->setInt("GameSwitcherMarqueeSize", (int)Math::round(gameSwitcherMarquee->getValue()));
		});

		auto marqueeFallback = std::make_shared<SwitchComponent>(window);
		marqueeFallback->setState(Settings::getInstance()->getBool("GameSwitcherMarqueeFallback"));
		s->addWithDescription(_("SHOW GAME NAME"), _("Display the game name as text when no marquee image is available."), marqueeFallback);
		s->addSaveFunc([marqueeFallback] {
			Settings::getInstance()->setBool("GameSwitcherMarqueeFallback", marqueeFallback->getState());
		});
	}

	// Dynamic menu recreation when marquee toggle changes
	marqueeEnable->setOnChangedCallback([window, s, baseMarqueeEnabled, marqueeEnable]()
	{
		bool enabled = marqueeEnable->getState();
		if (baseMarqueeEnabled != enabled)
		{
			Settings::getInstance()->setBool("GameSwitcherMarqueeEnabled", enabled);
			delete s;
			openSettings(window, true, false);
		}
	});

	// Enable Play Information toggle
	auto playInfoEnable = std::make_shared<SwitchComponent>(window);
	playInfoEnable->setState(basePlayInfoEnabled);
	s->addWithDescription(_("ENABLE PLAY INFORMATION"), _("Show play count and total play time below the screenshot."), playInfoEnable, selectPlayInfoEnable);
	s->addSaveFunc([playInfoEnable] {
		Settings::getInstance()->setBool("GameSwitcherPlayInfoEnabled", playInfoEnable->getState());
	});

	// Game Switcher info background opacity setting - only shown when play info is enabled
	if (basePlayInfoEnabled)
	{
		auto gameSwitcherBgOpacity = std::make_shared<SliderComponent>(window, 0.f, 100.f, 5.f, "%");
		gameSwitcherBgOpacity->setValue((float)Settings::getInstance()->getInt("GameSwitcherInfoBackgroundOpacity"));
		s->addWithDescription(_("BACKGROUND OPACITY"), _("Opacity of the dark background behind play information text."), gameSwitcherBgOpacity);
		s->addSaveFunc([gameSwitcherBgOpacity] {
			Settings::getInstance()->setInt("GameSwitcherInfoBackgroundOpacity", (int)Math::round(gameSwitcherBgOpacity->getValue()));
		});
	}

	// Enable Launch Animation toggle
	auto launchAnimEnable = std::make_shared<SwitchComponent>(window);
	launchAnimEnable->setState(Settings::getInstance()->getBool("GameSwitcherLaunchAnimationEnabled"));
	s->addWithDescription(_("ENABLE LAUNCH ANIMATION"), _("Fade out marquee and play information before launching a game."), launchAnimEnable);
	s->addSaveFunc([launchAnimEnable] {
		Settings::getInstance()->setBool("GameSwitcherLaunchAnimationEnabled", launchAnimEnable->getState());
	});

	// Show Help Prompts toggle
	auto helpEnable = std::make_shared<SwitchComponent>(window);
	helpEnable->setState(Settings::getInstance()->getBool("GameSwitcherHelpEnabled"));
	s->addWithDescription(_("SHOW NAVIGATION HELP"), _("Show button shortcuts at the bottom of the screen."), helpEnable);
	s->addSaveFunc([helpEnable] {
		Settings::getInstance()->setBool("GameSwitcherHelpEnabled", helpEnable->getState());
	});

	// Boot to Game Switcher toggle
	auto bootEnable = std::make_shared<SwitchComponent>(window);
	bootEnable->setState(Settings::getInstance()->getBool("GameSwitcherBootEnabled"));
	s->addWithDescription(_("BOOT TO GAME SWITCHER"), _("Show Game Switcher on startup when no Quick Resume game is in progress."), bootEnable);
	s->addSaveFunc([bootEnable] {
		Settings::getInstance()->setBool("GameSwitcherBootEnabled", bootEnable->getState());
	});

	// Dynamic menu recreation when play info toggle changes
	playInfoEnable->setOnChangedCallback([window, s, basePlayInfoEnabled, playInfoEnable]()
	{
		bool enabled = playInfoEnable->getState();
		if (basePlayInfoEnabled != enabled)
		{
			Settings::getInstance()->setBool("GameSwitcherPlayInfoEnabled", enabled);
			delete s;
			openSettings(window, false, true);
		}
	});

	window->pushGui(s);
}
