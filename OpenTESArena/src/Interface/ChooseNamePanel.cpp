#include <cassert>
#include <unordered_map>

#include "SDL.h"

#include "ChooseClassPanel.h"
#include "ChooseGenderPanel.h"
#include "ChooseNamePanel.h"
#include "CursorAlignment.h"
#include "RichTextString.h"
#include "TextAlignment.h"
#include "TextBox.h"
#include "../Assets/ExeData.h"
#include "../Assets/MiscAssets.h"
#include "../Game/Game.h"
#include "../Game/Options.h"
#include "../Math/Vector2.h"
#include "../Media/Color.h"
#include "../Media/FontManager.h"
#include "../Media/FontName.h"
#include "../Media/PaletteFile.h"
#include "../Media/PaletteName.h"
#include "../Media/TextureFile.h"
#include "../Media/TextureManager.h"
#include "../Media/TextureName.h"
#include "../Rendering/Renderer.h"
#include "../Rendering/Surface.h"
#include "../Utilities/String.h"

const int ChooseNamePanel::MAX_NAME_LENGTH = 25;

ChooseNamePanel::ChooseNamePanel(Game &game, const CharacterClass &charClass)
	: Panel(game), charClass(charClass)
{
	this->parchment = Texture(Texture::generate(
		Texture::PatternType::Parchment, 300, 60, game.getTextureManager(),
		game.getRenderer()));

	this->titleTextBox = [&game, &charClass]()
	{
		const int x = 26;
		const int y = 82;

		const auto &exeData = game.getMiscAssets().getExeData();
		std::string text = exeData.charCreation.chooseName;
		text = String::replace(text, "%s", charClass.getName());

		const RichTextString richText(
			text,
			FontName::A,
			Color(48, 12, 12),
			TextAlignment::Left,
			game.getFontManager());

		return std::make_unique<TextBox>(x, y, richText, game.getRenderer());
	}();

	this->nameTextBox = [&game]()
	{
		const int x = 61;
		const int y = 101;

		const RichTextString richText(
			std::string(),
			FontName::A,
			Color(48, 12, 12),
			TextAlignment::Left,
			game.getFontManager());

		return std::make_unique<TextBox>(x, y, richText, game.getRenderer());
	}();

	this->backToClassButton = []()
	{
		auto function = [](Game &game)
		{
			SDL_StopTextInput();
			game.setPanel<ChooseClassPanel>(game);
		};
		return Button<Game&>(function);
	}();

	this->acceptButton = []()
	{
		auto function = [](Game &game, const CharacterClass &charClass,
			const std::string &name)
		{
			SDL_StopTextInput();
			game.setPanel<ChooseGenderPanel>(game, charClass, name);
		};
		return Button<Game&, const CharacterClass&, const std::string&>(function);
	}();

	// Activate SDL text input (handled in handleEvent()).
	SDL_StartTextInput();
}

std::pair<SDL_Texture*, CursorAlignment> ChooseNamePanel::getCurrentCursor() const
{
	auto &game = this->getGame();
	auto &renderer = game.getRenderer();
	auto &textureManager = game.getTextureManager();
	const auto &texture = textureManager.getTexture(
		TextureFile::fromName(TextureName::SwordCursor),
		PaletteFile::fromName(PaletteName::Default), renderer);
	return std::make_pair(texture.get(), CursorAlignment::TopLeft);
}

void ChooseNamePanel::handleEvent(const SDL_Event &e)
{
	const auto &inputManager = this->getGame().getInputManager();
	const bool escapePressed = inputManager.keyPressed(e, SDLK_ESCAPE);
	const bool enterPressed = inputManager.keyPressed(e, SDLK_RETURN) ||
		inputManager.keyPressed(e, SDLK_KP_ENTER);
	const bool backspacePressed = inputManager.keyPressed(e, SDLK_BACKSPACE) ||
		inputManager.keyPressed(e, SDLK_KP_BACKSPACE);

	if (escapePressed)
	{
		this->backToClassButton.click(this->getGame());
	}
	else if (enterPressed && (this->name.size() > 0))
	{
		// Accept the given name.
		this->acceptButton.click(this->getGame(), this->charClass, this->name);
	}
	else
	{
		// Listen for SDL text input and changes in text.
		const bool textChanged = [this, &e, backspacePressed]()
		{
			if (backspacePressed)
			{
				// Erase one letter if able.
				if (this->name.size() > 0)
				{
					this->name.pop_back();
					return true;
				}
			}

			const bool letterReceived = e.type == SDL_TEXTINPUT;

			// Only process the input if a letter was received and the player's name has
			// space remaining.
			if (letterReceived && (this->name.size() < ChooseNamePanel::MAX_NAME_LENGTH))
			{
				const char letter = e.text.text[0];

				// Only letters and spaces are allowed.
				auto charIsAllowed = [](char c)
				{
					return (c == ' ') || ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z'));
				};

				if (charIsAllowed(letter))
				{
					// Append to the name string.
					this->name.push_back(letter);
					return true;
				}
			}
			
			// No change in the displayed text.
			return false;
		}();

		if (textChanged)
		{
			// Update the displayed name.
			this->nameTextBox = [this]()
			{
				const int x = 61;
				const int y = 101;

				auto &game = this->getGame();
				const RichTextString &oldRichText = this->nameTextBox->getRichText();

				const RichTextString richText(
					this->name,
					oldRichText.getFontName(),
					oldRichText.getColor(),
					oldRichText.getAlignment(),
					game.getFontManager());

				return std::make_unique<TextBox>(x, y, richText, game.getRenderer());
			}();
		}
	}
}

void ChooseNamePanel::render(Renderer &renderer)
{
	// Clear full screen.
	renderer.clear();

	// Set palette.
	auto &textureManager = this->getGame().getTextureManager();
	textureManager.setPalette(PaletteFile::fromName(PaletteName::Default));

	// Draw background.
	const auto &background = textureManager.getTexture(
		TextureFile::fromName(TextureName::CharacterCreation),
		PaletteFile::fromName(PaletteName::BuiltIn), renderer);
	renderer.drawOriginal(background.get());

	// Draw parchment: title.
	renderer.drawOriginal(this->parchment.get(),
		(Renderer::ORIGINAL_WIDTH / 2) - (this->parchment.getWidth() / 2),
		(Renderer::ORIGINAL_HEIGHT / 2) - (this->parchment.getHeight() / 2));

	// Draw text: title, name.
	renderer.drawOriginal(this->titleTextBox->getTexture(),
		this->titleTextBox->getX(), this->titleTextBox->getY());
	renderer.drawOriginal(this->nameTextBox->getTexture(),
		this->nameTextBox->getX(), this->nameTextBox->getY());
}
