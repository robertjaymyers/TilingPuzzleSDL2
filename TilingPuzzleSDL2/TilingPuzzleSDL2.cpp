// TilingPuzzleSDL2.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include "pch.h"
#include <iostream>
#include <memory>
#include <vector>
#include <random>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <string>
#include <SDL.h>
#include <SDL_image.h>

// Puzzle images should be 400x400 pixels. Other sizes are not supported.

struct sdlCleanupWindow
{
	void operator()(SDL_Window *window) const
	{
		SDL_DestroyWindow(window);
		SDL_Log("SDL_Window went out of scope and was destroyed");
	}
};

struct sdlCleanupRenderer
{
	void operator()(SDL_Renderer *renderer) const
	{
		SDL_DestroyRenderer(renderer);
		SDL_Log("SDL_Renderer went out of scope and was destroyed");
	}
};

struct sdlCleanupTexture
{
	void operator()(SDL_Texture *texture) const
	{
		SDL_DestroyTexture(texture);
		SDL_Log("SDL_Texture went out of scope and was destroyed");
	}
};

bool mouseClicked = false;

std::unique_ptr<SDL_Texture, sdlCleanupTexture> puzzleCompleteTex;
SDL_Rect puzzleCompleteRect;

const int puzzlePieceSize = 100;
int selectedI = -1;
std::unique_ptr<SDL_Texture, sdlCleanupTexture> selectedOverlayTex;

const int puzzlePiecesTotal = 16;
std::vector<SDL_Rect> dstImgCoords(puzzlePiecesTotal);
std::vector<SDL_Rect> dstImgCoordsOriginal(puzzlePiecesTotal);
std::vector<SDL_Rect> srcImgBase(puzzlePiecesTotal);

std::vector<std::unique_ptr<SDL_Texture, sdlCleanupTexture>> puzzleTextures;
int puzzleCurrent = 0;

SDL_Rect miniRefImgRect;


std::unique_ptr<SDL_Window, sdlCleanupWindow> window;
std::unique_ptr<SDL_Renderer, sdlCleanupRenderer> renderer;

enum class ProgramState { STARTUP, PLAY, TRANSITION, SHUTDOWN };
ProgramState programState = ProgramState::STARTUP;

enum class MoveState { NONE, SELECTED };
MoveState moveState = MoveState::NONE;

enum class MiniRefImg { DISPLAY, HIDE };
MiniRefImg miniRefImg = MiniRefImg::HIDE;

const int fpsCap = 60;
const int fpsDelay = 1000 / fpsCap;
Uint32 fpsTimerStart;
int fpsTimerElapsed;

void programStartup();
void programShutdown();
void eventsCheckPlay();
void eventsCheckTransition();
void renderUpdate();
bool mouseWithinRectBound(const SDL_MouseButtonEvent &btn, const SDL_Rect &rect);
bool puzzleSolved();
void shufflePuzzles();
void shufflePuzzlePieces();

int main(int arg, char *argc[])
{
	while (programState != ProgramState::SHUTDOWN)
	{
		switch (programState)
		{
		case (ProgramState::STARTUP):
			programStartup();
			break;
		case (ProgramState::PLAY):
			fpsTimerStart = SDL_GetTicks();
			eventsCheckPlay();
			renderUpdate();
			fpsTimerElapsed = SDL_GetTicks() - fpsTimerStart;
			if (fpsDelay > fpsTimerElapsed)
			{
				SDL_Delay(fpsDelay - fpsTimerElapsed);
			}
			break;
		case (ProgramState::TRANSITION):
			fpsTimerStart = SDL_GetTicks();
			eventsCheckTransition();
			renderUpdate();
			fpsTimerElapsed = SDL_GetTicks() - fpsTimerStart;
			if (fpsDelay > fpsTimerElapsed)
			{
				SDL_Delay(fpsDelay - fpsTimerElapsed);
			}
			break;
		}
	}

	programShutdown();

	return 0;
}

void programStartup()
{
	SDL_Init(SDL_INIT_EVERYTHING);

	window.reset(SDL_CreateWindow("Tiling Puzzle", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 600, 600, false));
	renderer.reset(SDL_CreateRenderer(window.get(), -1, 0));
	SDL_SetRenderDrawColor(renderer.get(), 255, 255, 255, 255);

	// Get images for puzzles and set them to textures.
	{
		std::string dirPath = "puzzles/";
		auto dirIter = std::experimental::filesystem::directory_iterator(dirPath);
		for (auto& file : dirIter)
		{
			if (file.path().filename().string().find(".png") != std::string::npos)
			{
				SDL_Surface *tempSurface;
				tempSurface = IMG_Load(file.path().string().c_str());
				std::unique_ptr<SDL_Texture, sdlCleanupTexture> puzzleTex;
				puzzleTex.reset(SDL_CreateTextureFromSurface(renderer.get(), tempSurface));
				SDL_FreeSurface(tempSurface);
				puzzleTextures.push_back(std::move(puzzleTex));
			}
		}
	}

	SDL_Surface *tempSurface;
	tempSurface = IMG_Load("textures/selectedOverlay.png");
	selectedOverlayTex.reset(SDL_CreateTextureFromSurface(renderer.get(), tempSurface));
	SDL_FreeSurface(tempSurface);

	tempSurface = IMG_Load("textures/puzzle-complete-txt.png");
	puzzleCompleteTex.reset(SDL_CreateTextureFromSurface(renderer.get(), tempSurface));
	SDL_FreeSurface(tempSurface);

	// Set src coords.
	{
		int xOffset = 0;
		int yOffset = 0;
		int rowCount = 0;
		for (auto& rect : srcImgBase)
		{
			rect.w = puzzlePieceSize;
			rect.h = puzzlePieceSize;
			rect.x = xOffset;
			rect.y = yOffset;

			if (rowCount >= 3)
			{
				rowCount = 0;
				xOffset = 0;
				yOffset += puzzlePieceSize;
			}
			else
			{
				xOffset += puzzlePieceSize;
				rowCount++;
			}
		}
	}

	// Set dst coords.
	{
		int boardOffsetX = 100;
		int boardOffsetY = 20;
		int xOffset = 0;
		int yOffset = 0;
		int rowCount = 1;
		for (auto& rect : dstImgCoords)
		{
			rect.w = puzzlePieceSize;
			rect.h = puzzlePieceSize;
			rect.x = xOffset + boardOffsetX;
			rect.y = yOffset + boardOffsetY;

			if (rowCount >= 4)
			{
				rowCount = 1;
				xOffset = 0;
				yOffset += puzzlePieceSize;
			}
			else
			{
				xOffset += puzzlePieceSize;
				rowCount++;
			}
		}

		dstImgCoordsOriginal = dstImgCoords;

		miniRefImgRect.w = puzzlePieceSize;
		miniRefImgRect.h = puzzlePieceSize;
		miniRefImgRect.x = 100;
		miniRefImgRect.y = 450;

		puzzleCompleteRect.w = 300;
		puzzleCompleteRect.h = 100;
		puzzleCompleteRect.x = 200;
		puzzleCompleteRect.y = 450;
	}

	shufflePuzzles();
	shufflePuzzlePieces();
	programState = ProgramState::PLAY;
}

void programShutdown()
{
	SDL_Quit();
}

void eventsCheckPlay()
{
	SDL_Event sdlEvent;
	SDL_PollEvent(&sdlEvent);

	switch (sdlEvent.type)
	{
	case SDL_QUIT:
		programState = ProgramState::SHUTDOWN;
		break;
	case SDL_MOUSEBUTTONDOWN:
		if (!mouseClicked)
		{
			mouseClicked = true;
			if (sdlEvent.button.button == SDL_BUTTON_LEFT)
			{
				for (int rectI = 0; rectI < puzzlePiecesTotal; rectI++)
				{
					if (mouseWithinRectBound(sdlEvent.button, dstImgCoords[rectI]))
					{
						switch (moveState)
						{
						case (MoveState::NONE):
							selectedI = rectI;
							moveState = MoveState::SELECTED;
							break;
						case (MoveState::SELECTED):
							if (rectI != selectedI)
							{
								std::swap(dstImgCoords[rectI], dstImgCoords[selectedI]);
								selectedI = -1;
								moveState = MoveState::NONE;

								if (puzzleSolved())
								{
									SDL_Log("Puzzle solved!");
									// render puzzle solved text
									mouseClicked = false;
									programState = ProgramState::TRANSITION;
								}
							}
							else
							{
								selectedI = -1;
								moveState = MoveState::NONE;
							}
							break;
						}
						break;
					}
				}
			}
			else if (sdlEvent.button.button == SDL_BUTTON_RIGHT)
			{
				if (moveState == MoveState::SELECTED)
				{
					selectedI = -1;
					moveState = MoveState::NONE;
				}
			}
			else if (sdlEvent.button.button == SDL_BUTTON_MIDDLE)
			{
				if (miniRefImg == MiniRefImg::DISPLAY)
				{
					miniRefImg = MiniRefImg::HIDE;
				}
				else if (miniRefImg == MiniRefImg::HIDE)
				{
					miniRefImg = MiniRefImg::DISPLAY;
				}
			}
		}
		break;
	case SDL_MOUSEBUTTONUP:
		mouseClicked = false;
		break;
	case SDL_KEYUP:
		SDL_Keycode keyReleased = sdlEvent.key.keysym.sym;
		switch (keyReleased)
		{
		case SDLK_s:
			// Skip the current puzzle
			if (puzzleCurrent + 1 == puzzleTextures.size())
			{
				shufflePuzzles();
				puzzleCurrent = 0;
			}
			else
			{
				puzzleCurrent++;
			}
			shufflePuzzlePieces();
			selectedI = -1;
			moveState = MoveState::NONE;
			break;
		}
		break;
	}
}

void eventsCheckTransition()
{
	SDL_Event sdlEvent;
	SDL_PollEvent(&sdlEvent);

	switch (sdlEvent.type)
	{
	case SDL_QUIT:
		programState = ProgramState::SHUTDOWN;
		break;
	case SDL_KEYUP:
		SDL_Keycode keyReleased = sdlEvent.key.keysym.sym;
		switch (keyReleased)
		{
		case SDLK_SPACE:
			// Go through each puzzle sequentially until we run out.
			// If we run out, shuffle the loaded puzzles and start over.
			if (puzzleCurrent + 1 == puzzleTextures.size())
			{
				shufflePuzzles();
				puzzleCurrent = 0;
			}
			else
			{
				puzzleCurrent++;
			}
			shufflePuzzlePieces();
			programState = ProgramState::PLAY;
			break;
		}
		break;
	}
}

void renderUpdate()
{
	SDL_RenderClear(renderer.get());
	for (int i = 0; i < puzzlePiecesTotal; i++)
	{
		SDL_RenderCopy(renderer.get(), puzzleTextures[puzzleCurrent].get(), &srcImgBase[i], &dstImgCoords[i]);
	}

	if (moveState == MoveState::SELECTED)
	{
		SDL_RenderCopy(renderer.get(), selectedOverlayTex.get(), NULL, &dstImgCoords[selectedI]);
	}

	if (miniRefImg == MiniRefImg::DISPLAY)
	{
		SDL_RenderCopy(renderer.get(), puzzleTextures[puzzleCurrent].get(), NULL, &miniRefImgRect);
	}

	if (programState == ProgramState::TRANSITION)
	{
		SDL_RenderCopy(renderer.get(), puzzleCompleteTex.get(), NULL, &puzzleCompleteRect);
	}
	SDL_RenderPresent(renderer.get());
}

bool mouseWithinRectBound(const SDL_MouseButtonEvent &btn, const SDL_Rect &rect)
{
	if (btn.x >= rect.x &&
		btn.x <= rect.x + rect.w &&
		btn.y >= rect.y &&
		btn.y <= rect.y + rect.h)
	{
		return true;
	}
	return false;
}

bool puzzleSolved()
{
	for (int rectI = 0; rectI < puzzlePiecesTotal; rectI++)
	{
		if (dstImgCoordsOriginal[rectI].x != dstImgCoords[rectI].x ||
			dstImgCoordsOriginal[rectI].y != dstImgCoords[rectI].y)
		{
			return false;
		}
	}
	return true;
}

void shufflePuzzles()
{
	int seed = std::chrono::system_clock::now().time_since_epoch().count();
	shuffle(puzzleTextures.begin(), puzzleTextures.end(), std::default_random_engine(seed));
}

void shufflePuzzlePieces()
{
	int seed = std::chrono::system_clock::now().time_since_epoch().count();
	shuffle(dstImgCoords.begin(), dstImgCoords.end(), std::default_random_engine(seed));
}