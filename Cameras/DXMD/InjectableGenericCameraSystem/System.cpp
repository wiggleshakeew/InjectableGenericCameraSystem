﻿////////////////////////////////////////////////////////////////////////////////////////////////////////
// Part of Injectable Generic Camera System
// Copyright(c) 2017, Frans Bouma
// All rights reserved.
// https://github.com/FransBouma/InjectableGenericCameraSystem
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met :
//
//  * Redistributions of source code must retain the above copyright notice, this
//	  list of conditions and the following disclaimer.
//
//  * Redistributions in binary form must reproduce the above copyright notice,
//    this list of conditions and the following disclaimer in the documentation
//    and / or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED.IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
// FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
// DAMAGES(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
// SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
// CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
// OR TORT(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "stdafx.h"
#include "System.h"
#include "Globals.h"
#include "Defaults.h"
#include "GameConstants.h"
#include "Gamepad.h"
#include "CameraManipulator.h"
#include "InterceptorHelper.h"
#include "InputHooker.h"
#include "input.h"
#include "CameraManipulator.h"
#include "GameImageHooker.h"

namespace IGCS
{
	using namespace IGCS::GameSpecific;

	System::System()
	{
	}


	System::~System()
	{
	}


	void System::start(LPBYTE hostBaseAddress, DWORD hostImageSize)
	{
		Globals::instance().systemActive(true);
		_hostImageAddress = (LPBYTE)hostBaseAddress;
		_hostImageSize = hostImageSize;
		Globals::instance().gamePad().setInvertLStickY(CONTROLLER_Y_INVERT);
		Globals::instance().gamePad().setInvertRStickY(CONTROLLER_Y_INVERT);
		displayHelp();
		initialize();		// will block till camera is found
		mainLoop();
	}


	// Core loop of the system
	void System::mainLoop()
	{
		while (Globals::instance().systemActive())
		{
			Sleep(FRAME_SLEEP);
			updateFrame();
		}
	}


	// updates the data and camera for a frame 
	void System::updateFrame()
	{
		handleUserInput();
		writeNewCameraValuesToCameraStructs();
	}
	

	void System::writeNewCameraValuesToCameraStructs()
	{
		if (!g_cameraEnabled)
		{
			return;
		}

		// calculate new camera values
		XMVECTOR newLookQuaternion = _camera.calculateLookQuaternion();
		XMFLOAT3 currentCoords = GameSpecific::CameraManipulator::getCurrentCameraCoords();
		XMFLOAT3 newCoords = _camera.calculateNewCoords(currentCoords, newLookQuaternion);
		GameSpecific::CameraManipulator::writeNewCameraValuesToGameData(newLookQuaternion, newCoords);
	}


	void System::handleUserInput()
	{
		Globals::instance().gamePad().update();
		bool altPressed = Input::keyDown(VK_LMENU) || Input::keyDown(VK_RMENU);
		bool controlPressed = Input::keyDown(VK_RCONTROL);

		if (Input::keyDown(IGCS_KEY_HELP) && altPressed)
		{
			displayHelp();
		}
		if (Input::keyDown(IGCS_KEY_INVERT_Y_LOOK))
		{
			toggleYLookDirectionState();
			Sleep(350);		// wait for 350ms to avoid fast keyboard hammering
		}
		if (!_cameraStructFound)
		{
			// camera not found yet, can't proceed.
			return;
		}
		if (Input::keyDown(IGCS_KEY_CAMERA_ENABLE))
		{
			if (g_cameraEnabled)
			{
				// it's going to be disabled, make sure things are alright when we give it back to the host
				CameraManipulator::restoreOriginalCameraValues();
				toggleCameraMovementLockState(false);
				InterceptorHelper::toggleFoVWriteState(_aobBlocks, true);	// enable writes
			}
			else
			{
				// it's going to be enabled, so cache the original values before we enable it so we can restore it afterwards
				CameraManipulator::cacheOriginalCameraValues();
				_camera.resetAngles();
				InterceptorHelper::toggleFoVWriteState(_aobBlocks, false);	// disable writes
			}
			g_cameraEnabled = g_cameraEnabled == 0 ? (byte)1 : (byte)0;
			displayCameraState();
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}
		if (Input::keyDown(IGCS_KEY_GAMESPEEDSTOP))
		{
			toggleGamespeedStopState();
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}
		if (Input::keyDown(IGCS_KEY_DECREASE_GAMESPEED))
		{
			modifyGameSpeed(true);
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}
		if (Input::keyDown(IGCS_KEY_INCREASE_GAMESPEED))
		{
			modifyGameSpeed(false);
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}
		if (Input::keyDown(IGCS_KEY_TOGGLE_HUD))
		{
			toggleHUD();
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}

		if (!g_cameraEnabled)
		{
			// camera is disabled. We simply disable all input to the camera movement, by returning now.
			return;
		}
		if (Input::keyDown(IGCS_KEY_FOV_RESET))
		{
			CameraManipulator::resetFoV();
		}
		if (Input::keyDown(IGCS_KEY_FOV_DECREASE))
		{
			CameraManipulator::changeFoV(-DEFAULT_FOV_SPEED);
		}
		if (Input::keyDown(IGCS_KEY_FOV_INCREASE))
		{
			CameraManipulator::changeFoV(DEFAULT_FOV_SPEED);
		}
		if (Input::keyDown(IGCS_KEY_BLOCK_INPUT))
		{
			toggleInputBlockState(!Globals::instance().inputBlocked());
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}

		_camera.resetMovement();
		float multiplier = altPressed ? FASTER_MULTIPLIER : controlPressed ? SLOWER_MULTIPLIER : 1.0f;
		if (Input::keyDown(IGCS_KEY_CAMERA_LOCK))
		{
			toggleCameraMovementLockState(!_cameraMovementLocked);
			Sleep(350);				// wait for 350ms to avoid fast keyboard hammering
		}
		if (_cameraMovementLocked)
		{
			// no movement allowed, simply return
			return;
		}

		handleKeyboardCameraMovement(multiplier);
		handleMouseCameraMovement(multiplier);
		// Game controller support has been disabled as it's of no use to use a controller now, input can't be blocked so it's a pain.
		// Uncomment the line below to enable controller support.
		//handleGamePadMovement(multiplier);
	}


	void System::handleGamePadMovement(float multiplierBase)
	{
		auto gamePad = Globals::instance().gamePad();

		if (gamePad.isConnected())
		{
			float  multiplier = gamePad.isButtonPressed(IGCS_BUTTON_FASTER) ? FASTER_MULTIPLIER : gamePad.isButtonPressed(IGCS_BUTTON_SLOWER) ? SLOWER_MULTIPLIER : multiplierBase;

			vec2 rightStickPosition = gamePad.getRStickPosition();
			_camera.pitch(rightStickPosition.y * multiplier);
			_camera.yaw(rightStickPosition.x * multiplier);

			vec2 leftStickPosition = gamePad.getLStickPosition();
			_camera.moveUp((gamePad.getLTrigger() - gamePad.getRTrigger()) * multiplier);
			_camera.moveForward(leftStickPosition.y * multiplier);
			_camera.moveRight(leftStickPosition.x * multiplier);

			if (gamePad.isButtonPressed(IGCS_BUTTON_TILT_LEFT))
			{
				_camera.roll(multiplier);
			}
			if (gamePad.isButtonPressed(IGCS_BUTTON_TILT_RIGHT))
			{
				_camera.roll(-multiplier);
			}
			if (gamePad.isButtonPressed(IGCS_BUTTON_RESET_FOV))
			{
				CameraManipulator::resetFoV();
			}
			if (gamePad.isButtonPressed(IGCS_BUTTON_FOV_DECREASE))
			{
				CameraManipulator::changeFoV(-DEFAULT_FOV_SPEED);
			}
			if (gamePad.isButtonPressed(IGCS_BUTTON_FOV_INCREASE))
			{
				CameraManipulator::changeFoV(DEFAULT_FOV_SPEED);
			}
			if (gamePad.isButtonPressed(IGCS_BUTTON_BLOCK_INPUT))
			{
				toggleInputBlockState(!Globals::instance().inputBlocked());
				Sleep(350);				// wait for 350ms to avoid fast hammering
			}
		}
	}


	void System::handleMouseCameraMovement(float multiplier)
	{
		long mouseDeltaX = Input::getMouseDeltaX();
		long mouseDeltaY = Input::getMouseDeltaY();
		if (abs(mouseDeltaY) > 1)
		{
			_camera.pitch(-(static_cast<float>(mouseDeltaY) * MOUSE_SPEED_CORRECTION * multiplier));
		}
		if (abs(mouseDeltaX) > 1)
		{
			_camera.yaw(static_cast<float>(mouseDeltaX) * MOUSE_SPEED_CORRECTION * multiplier);
		}
	}


	void System::handleKeyboardCameraMovement(float multiplier)
	{
		if (Input::keyDown(IGCS_KEY_MOVE_FORWARD))
		{
			_camera.moveForward(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_MOVE_BACKWARD))
		{
			_camera.moveForward(-multiplier);
		}
		if (Input::keyDown(IGCS_KEY_MOVE_RIGHT))
		{
			_camera.moveRight(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_MOVE_LEFT))
		{
			_camera.moveRight(-multiplier);
		}
		if (Input::keyDown(IGCS_KEY_MOVE_UP))
		{
			_camera.moveUp(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_MOVE_DOWN))
		{
			_camera.moveUp(-multiplier);
		}
		if (Input::keyDown(IGCS_KEY_ROTATE_DOWN))
		{
			_camera.pitch(-multiplier);
		}
		if (Input::keyDown(IGCS_KEY_ROTATE_UP))
		{
			_camera.pitch(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_ROTATE_RIGHT))
		{
			_camera.yaw(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_ROTATE_LEFT))
		{
			_camera.yaw(-multiplier);
		}
		if (Input::keyDown(IGCS_KEY_TILT_LEFT))
		{
			_camera.roll(multiplier);
		}
		if (Input::keyDown(IGCS_KEY_TILT_RIGHT))
		{
			_camera.roll(-multiplier);
		}
	}


	// Initializes system. Will block till camera struct is found.
	void System::initialize()
	{
		InputHooker::setInputHooks();
		Input::registerRawInput();
		GameSpecific::InterceptorHelper::initializeAOBBlocks(_hostImageAddress, _hostImageSize, _aobBlocks);
		GameSpecific::InterceptorHelper::setCameraStructInterceptorHook(_aobBlocks);
		GameSpecific::InterceptorHelper::setTimestopInterceptorHook(_aobBlocks);
		GameSpecific::CameraManipulator::waitForCameraStructAddresses();		// blocks till camera is found.
		GameSpecific::InterceptorHelper::setCameraWriteInterceptorHook(_aobBlocks);
		GameSpecific::InterceptorHelper::setHudToggleInterceptorHook(_aobBlocks);
		// camera struct found, init our own camera object now and hook into game code which uses camera.
		_cameraStructFound = true;
		_camera.setPitch(INITIAL_PITCH_RADIANS);
		_camera.setRoll(INITIAL_ROLL_RADIANS);
		_camera.setYaw(INITIAL_YAW_RADIANS);
	}
	

	void System::toggleInputBlockState(bool newValue)
	{
		if (Globals::instance().inputBlocked() == newValue)
		{
			// already in this state. Ignore
			return;
		}
		Globals::instance().inputBlocked(newValue);
		Console::WriteLine(newValue ? "Input to game blocked" : "Input to game enabled");
	}


	void System::toggleCameraMovementLockState(bool newValue)
	{
		if (_cameraMovementLocked == newValue)
		{
			// already in this state. Ignore
			return;
		}
		_cameraMovementLocked = newValue;
		Console::WriteLine(_cameraMovementLocked ? "Camera movement is locked" : "Camera movement is unlocked");
	}


	void System::toggleFoVWriteState(bool enabled)
	{
		// if enabled, we'll nop the range where the FoV write is placed, otherwise we'll write back the original statement bytes. 
		if (enabled)
		{
			// enable writes
			byte originalStatementBytes[5] = { 0xF3, 0x0F, 0x11, 0x49, 0x0C };      // DXMD.exe+383F18E - F3 0F11 49 0C - movss [rcx+0C],xmm1
			IGCS::GameImageHooker::writeRange(_aobBlocks[FOV_WRITE_ADDRESS_INTERCEPT_KEY], originalStatementBytes, 5);
		}
		else
		{
			// disable writes, by nopping the range.
			IGCS::GameImageHooker::nopRange(_aobBlocks[FOV_WRITE_ADDRESS_INTERCEPT_KEY], 5);
		}
	}


	void System::toggleGamespeedStopState()
	{
		_gamespeedStopped = _gamespeedStopped == 0 ? (byte)1 : (byte)0;
		Console::WriteLine(_gamespeedStopped ? "Game speed frozen" : "Game speed normal");
		CameraManipulator::setGamespeedFreezeValue(_gamespeedStopped);
	}


	void System::displayCameraState()
	{
		Console::WriteLine(g_cameraEnabled ? "Camera enabled" : "Camera disabled");
	}


	void System::toggleYLookDirectionState()
	{
		_camera.toggleLookDirectionInverter();
		Console::WriteLine(_camera.lookDirectionInverter() < 0 ? "Y look direction is inverted" : "Y look direction is normal");
	}


	void System::toggleHUD()
	{
		_hudVisible = !_hudVisible;
		Console::WriteLine(_hudVisible ? "HUD visible" : "HUD hidden");
		CameraManipulator::toggleHud(_hudVisible);
	}

	void System::modifyGameSpeed(bool decrease)
	{
		if (!_gamespeedStopped)
		{
			return;
		}
		CameraManipulator::modifyGameSpeed(decrease);
	}

	void System::displayHelp()
	{
						  //0         1         2         3         4         5         6         7
						  //01234567890123456789012345678901234567890123456789012345678901234567890123456789
		Console::WriteLine("---[IGCS Help]-----------------------------------------------------------------", CONSOLE_WHITE);
		Console::WriteLine("INS                            : Enable/Disable camera");
		Console::WriteLine("HOME                           : Lock/unlock camera movement");
		Console::WriteLine("ALT + rotate/move              : Faster rotate / move");
		Console::WriteLine("Right-CTRL + rotate/move       : Slower rotate / move");
		Console::WriteLine("Arrow up/down or mouse         : Rotate camera up/down");
		Console::WriteLine("Arrow left/right or mouse      : Rotate camera left/right");
		Console::WriteLine("Numpad 8/Numpad 5              : Move camera forward/backward");
		Console::WriteLine("Numpad 4/Numpad 6              : Move camera left / right");
		Console::WriteLine("Numpad 7/Numpad 9              : Move camera up / down");
		Console::WriteLine("Numpad 1/Numpad 3              : Tilt camera left / right");
		Console::WriteLine("Numpad +/-                     : Increase / decrease FoV (w/ freecam)");
		Console::WriteLine("Numpad *                       : Reset FoV (w/ freecam)");
		Console::WriteLine("Numpad /                       : Toggle Y look direction");
		Console::WriteLine("Numpad .                       : Toggle mouse/keyboard input to game");
		Console::WriteLine("DEL                            : Toggle HUD");
		Console::WriteLine("Numpad 0                       : Toggle game speed freeze");
		Console::WriteLine("[                              : Decrease game speed (during game speed freeze)");
		Console::WriteLine("]                              : Increase game speed (during game speed freeze)");
		Console::WriteLine("ALT+H                          : This help");
		Console::WriteLine("-------------------------------------------------------------------------------", CONSOLE_WHITE);
		Console::WriteLine(" Please read the enclosed readme.txt for the answers to your questions :)");
		Console::WriteLine("-------------------------------------------------------------------------------", CONSOLE_WHITE);
		// wait for 350ms to avoid fast keyboard hammering
		Sleep(350);
	}
}