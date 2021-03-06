#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <thread>

#include "SDL.h"

#include "Game.h"
#include "Options.h"
#include "PlayerInterface.h"
#include "../Assets/CityDataFile.h"
#include "../Interface/Panel.h"
#include "../Media/FontManager.h"
#include "../Media/MusicFile.h"
#include "../Media/MusicName.h"
#include "../Media/TextureManager.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Surface.h"
#include "../Utilities/Debug.h"
#include "../Utilities/File.h"
#include "../Utilities/Platform.h"
#include "../Utilities/String.h"

#include "components/vfs/manager.hpp"

Game::Game()
{
	DebugMention("Initializing (Platform: " + Platform::getPlatform() + ").");

	// Get the current working directory. This is most relevant for platforms
	// like macOS, where the base path might be in the app's own "Resources" folder.
	this->basePath = Platform::getBasePath();

	// Get the path to the options folder. This is platform-dependent and points inside 
	// the "preferences directory" so it's always writable.
	this->optionsPath = Platform::getOptionsPath();

	// Parse options-default.txt and options-changes.txt (if it exists). Always prefer the
	// default file before the "changes" file.
	this->initOptions(this->basePath, this->optionsPath);

	// Verify that GLOBAL.BSA (the most important Arena file) exists.
	const bool arenaPathIsRelative = File::pathIsRelative(
		this->options.getArenaPath());
	const std::string globalBsaPath = [this, arenaPathIsRelative]()
	{
		// Include the base path if the ArenaPath is relative.
		return (arenaPathIsRelative ? this->basePath : "") +
			this->options.getArenaPath() + "/GLOBAL.BSA";
	}();

	DebugAssert(File::exists(globalBsaPath),
		"\"" + this->options.getArenaPath() + "\" not a valid ARENA path.");

	// Initialize virtual file system using the Arena path in the options file.
	VFS::Manager::get().initialize(std::string(
		(arenaPathIsRelative ? this->basePath : "") + this->options.getArenaPath()));

	// Initialize the OpenAL Soft audio manager.
	const bool midiPathIsRelative = File::pathIsRelative(this->options.getMidiConfig());
	const std::string midiPath = (midiPathIsRelative ? this->basePath : "") +
		this->options.getMidiConfig();

	this->audioManager.init(this->options.getMusicVolume(), this->options.getSoundVolume(),
		this->options.getSoundChannels(), this->options.getSoundResampling(), midiPath);

	// Initialize the SDL renderer and window with the given settings.
	this->renderer.init(this->options.getScreenWidth(), this->options.getScreenHeight(),
		this->options.getFullscreen(), this->options.getLetterboxAspect());

	// Initialize the texture manager.
	this->textureManager.init();

	// Load various miscellaneous assets.
	this->miscAssets.init();

	// Load and set window icon.
	const Surface icon = [this]()
	{
		const std::string iconPath = this->basePath + "data/icon.bmp";
		SDL_Surface *surface = Surface::loadBMP(iconPath, Renderer::DEFAULT_PIXELFORMAT);

		// Treat black as transparent.
		const uint32_t black = SDL_MapRGBA(surface->format, 0, 0, 0, 255);
		SDL_SetColorKey(surface, SDL_TRUE, black);

		return Surface(surface);
	}();

	this->renderer.setWindowIcon(icon.get());

	// Initialize panel and music to default.
	this->panel = Panel::defaultPanel(*this);
	this->setMusic(MusicName::PercIntro);

	// Use a texture as the cursor instead.
	SDL_ShowCursor(SDL_FALSE);

	// Leave some members null for now. The game data is initialized when the player 
	// enters the game world, and the "next panel" is a temporary used by the game
	// to avoid corruption between panel events which change the panel.
	this->gameData = nullptr;
	this->nextPanel = nullptr;
	this->nextSubPanel = nullptr;

	// This keeps the programmer from deleting a sub-panel the same frame it's in use.
	// The pop is delayed until the beginning of the next frame.
	this->requestedSubPanelPop = false;
}

AudioManager &Game::getAudioManager()
{
	return this->audioManager;
}

InputManager &Game::getInputManager()
{
	return this->inputManager;
}

FontManager &Game::getFontManager()
{
	return this->fontManager;
}

bool Game::gameDataIsActive() const
{
	return this->gameData.get() != nullptr;
}

GameData &Game::getGameData() const
{
	// The caller should not request the game data when there is no active session.
	assert(this->gameDataIsActive());

	return *this->gameData.get();
}

Options &Game::getOptions()
{
	return this->options;
}

Renderer &Game::getRenderer()
{
	return this->renderer;
}

TextureManager &Game::getTextureManager()
{
	return this->textureManager;
}

MiscAssets &Game::getMiscAssets()
{
	return this->miscAssets;
}

const FPSCounter &Game::getFPSCounter() const
{
	return this->fpsCounter;
}

void Game::setPanel(std::unique_ptr<Panel> nextPanel)
{
	this->nextPanel = std::move(nextPanel);
}

void Game::pushSubPanel(std::unique_ptr<Panel> nextSubPanel)
{
	this->nextSubPanel = std::move(nextSubPanel);
}

void Game::popSubPanel()
{
	// The active sub-panel must not pop more than one sub-panel, because it may 
	// have unintended side effects for other panels below it.
	DebugAssert(!this->requestedSubPanelPop, "Already scheduled to pop this sub-panel.");

	// If there are no sub-panels, then there is only the main panel, and panels 
	// should never have any sub-panels to pop.
	DebugAssert(this->subPanels.size() > 0, "No sub-panels to pop.");

	this->requestedSubPanelPop = true;
}

void Game::setMusic(MusicName name)
{
	const std::string &filename = MusicFile::fromName(name);
	this->audioManager.playMusic(filename);
}

void Game::setGameData(std::unique_ptr<GameData> gameData)
{
	this->gameData = std::move(gameData);
}

void Game::initOptions(const std::string &basePath, const std::string &optionsPath)
{
	// Load the default options first.
	const std::string defaultOptionsPath(basePath + "options/" + Options::DEFAULT_FILENAME);
	this->options.loadDefaults(defaultOptionsPath);

	// Check if the changes options file exists.
	const std::string changesOptionsPath(optionsPath + Options::CHANGES_FILENAME);
	if (!File::exists(changesOptionsPath))
	{
		// Make one. Since the default options object has no changes, the new file will have
		// no key-value pairs.
		DebugMention("Creating options file at \"" + changesOptionsPath + "\".");
		this->options.saveChanges();
	}
	else
	{
		// Read in any key-value pairs in the "changes" options file.
		this->options.loadChanges(changesOptionsPath);
	}
}

void Game::resizeWindow(int width, int height)
{
	// Resize the window, and the 3D renderer if initialized.
	const bool fullGameWindow = this->options.getModernInterface();
	this->renderer.resize(width, height, this->options.getResolutionScale(), fullGameWindow);
}

void Game::saveScreenshot(const Surface &surface)
{
	// Get the path + filename to use for the new screenshot.
	const std::string screenshotPath = []()
	{
		const std::string screenshotFolder = Platform::getScreenshotPath();
		const std::string screenshotPrefix("screenshot");
		int imageIndex = 0;

		auto getNextAvailablePath = [&screenshotFolder, &screenshotPrefix, &imageIndex]()
		{
			std::stringstream ss;
			ss << std::setw(3) << std::setfill('0') << imageIndex;
			imageIndex++;
			return screenshotFolder + screenshotPrefix + ss.str() + ".bmp";
		};

		std::string path = getNextAvailablePath();
		while (File::exists(path))
		{
			path = getNextAvailablePath();
		}

		return path;
	}();

	const int status = SDL_SaveBMP(surface.get(), screenshotPath.c_str());

	if (status == 0)
	{
		DebugMention("Screenshot saved to \"" + screenshotPath + "\".");
	}
	else
	{
		DebugCrash("Failed to save screenshot to \"" + screenshotPath + "\": " +
			std::string(SDL_GetError()));
	}
}

void Game::handlePanelChanges()
{
	// If a sub-panel pop was requested, then pop the top of the sub-panel stack.
	if (this->requestedSubPanelPop)
	{
		this->subPanels.pop_back();
		this->requestedSubPanelPop = false;
	}

	// If a new sub-panel was requested, then add it to the stack.
	if (this->nextSubPanel.get() != nullptr)
	{
		this->subPanels.push_back(std::move(this->nextSubPanel));
	}

	// If a new panel was requested, switch to it. If it will be the active panel 
	// (i.e., there are no sub-panels), then subsequent events will be sent to it.
	if (this->nextPanel.get() != nullptr)
	{
		this->panel = std::move(this->nextPanel);
	}
}

void Game::handleEvents(bool &running)
{
	// Handle events for the current game state.
	SDL_Event e;
	while (SDL_PollEvent(&e) != 0)
	{
		// Application events and window resizes are handled here.
		bool applicationExit = this->inputManager.applicationExit(e);
		bool resized = this->inputManager.windowResized(e);
		bool takeScreenshot = this->inputManager.keyPressed(e, SDLK_PRINTSCREEN);

		if (applicationExit)
		{
			running = false;
		}

		if (resized)
		{
			int width = e.window.data1;
			int height = e.window.data2;
			this->resizeWindow(width, height);

			// Call each panel's resize method. The panels should not be listening for
			// resize events themselves because it's more of an "application event" than
			// a panel event.
			this->panel->resize(width, height);

			for (auto &subPanel : this->subPanels)
			{
				subPanel->resize(width, height);
			}
		}

		if (takeScreenshot)
		{
			// Save a screenshot to the local folder.
			const auto &renderer = this->getRenderer();
			const Surface screenshot = renderer.getScreenshot();
			this->saveScreenshot(screenshot);
		}

		// Panel-specific events are handled by the active panel or sub-panel. If any 
		// sub-panels exist, choose the top one. Otherwise, choose the main panel.
		if (this->subPanels.size() > 0)
		{
			this->subPanels.back()->handleEvent(e);
		}
		else
		{
			this->panel->handleEvent(e);
		}

		// See if the event requested any changes in active panels.
		this->handlePanelChanges();
	}
}

void Game::tick(double dt)
{
	// If any sub-panels are active, tick the top one by delta time. Otherwise, 
	// tick the main panel.
	if (this->subPanels.size() > 0)
	{
		this->subPanels.back()->tick(dt);
	}
	else
	{
		this->panel->tick(dt);
	}

	// See if the panel tick requested any changes in active panels.
	this->handlePanelChanges();
}

void Game::render()
{
	// Draw the panel's main content.
	this->panel->render(this->renderer);

	// Draw any sub-panels back to front.
	for (auto &subPanel : this->subPanels)
	{
		subPanel->render(this->renderer);
	}

	const bool subPanelsExist = this->subPanels.size() > 0;

	// Call the top-most panel's secondary render method. Secondary render items are those
	// that are hidden on panels below the active one.
	if (subPanelsExist)
	{
		this->subPanels.back()->renderSecondary(this->renderer);
	}
	else
	{
		this->panel->renderSecondary(this->renderer);
	}

	// Get the active panel's cursor texture and alignment.
	const std::pair<SDL_Texture*, CursorAlignment> cursor = subPanelsExist ?
		this->subPanels.back()->getCurrentCursor() : this->panel->getCurrentCursor();

	// Draw cursor if not null. Some panels do not define a cursor (like cinematics), 
	// so their cursor is always null.
	if (cursor.first != nullptr)
	{
		// The panel should not be drawing the cursor themselves. It's done here 
		// just to make sure that the cursor is drawn only once and is always drawn last.
		this->renderer.drawCursor(cursor.first, cursor.second,
			this->inputManager.getMousePosition(), this->options.getCursorScale());
	}

	this->renderer.present();
}

void Game::loop()
{
	// Longest allowed frame time in microseconds.
	const std::chrono::duration<int64_t, std::micro> maximumMS(1000000 / Options::MIN_FPS);

	auto thisTime = std::chrono::high_resolution_clock::now();

	// Primary game loop.
	bool running = true;
	while (running)
	{
		const auto lastTime = thisTime;
		thisTime = std::chrono::high_resolution_clock::now();

		// Fastest allowed frame time in microseconds.
		const std::chrono::duration<int64_t, std::micro> minimumMS(
			1000000 / this->options.getTargetFPS());

		// Delay the current frame if the previous one was too fast.
		auto frameTime = std::chrono::duration_cast<std::chrono::microseconds>(thisTime - lastTime);
		if (frameTime < minimumMS)
		{
			const auto sleepTime = minimumMS - frameTime;
			std::this_thread::sleep_for(sleepTime);
			thisTime = std::chrono::high_resolution_clock::now();
			frameTime = std::chrono::duration_cast<std::chrono::microseconds>(thisTime - lastTime);
		}

		// Clamp the delta time to at most the maximum frame time.
		const double dt = std::fmin(frameTime.count(), maximumMS.count()) / 1000000.0;

		// Update the input manager's state.
		this->inputManager.update();

		// Update the audio manager, checking for finished sounds.
		this->audioManager.update();

		// Update FPS counter.
		this->fpsCounter.updateFrameTime(dt);

		// Listen for input events.
		this->handleEvents(running);

		// Animate the current game state by delta time.
		this->tick(dt);

		// Draw to the screen.
		this->render();
	}

	// At this point, the program has received an exit signal, and is now 
	// quitting peacefully.
	this->options.saveChanges();
}
