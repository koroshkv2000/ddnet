/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef GAME_CLIENT_COMPONENTS_CONTROLS_H
#define GAME_CLIENT_COMPONENTS_CONTROLS_H
#include <SDL_joystick.h>
#include <base/system.h>
#include <base/vmath.h>
#include <game/client/component.h>

#include <game/generated/protocol.h>

class CControls : public CComponent
{
public:
	vec2 m_MousePos[NUM_DUMMIES];
	vec2 m_TargetPos[NUM_DUMMIES];
	float m_OldMouseX;
	float m_OldMouseY;
	SDL_Joystick *m_Joystick;
	bool m_JoystickFirePressed;
	bool m_JoystickRunPressed;
	int64_t m_JoystickTapTime;

	SDL_Joystick *m_Gamepad;
	bool m_UsingGamepad;

	int m_AmmoCount[NUM_WEAPONS];

	CNetObj_PlayerInput m_InputData[NUM_DUMMIES];
	CNetObj_PlayerInput m_LastData[NUM_DUMMIES];
	int m_InputDirectionLeft[NUM_DUMMIES];
	int m_InputDirectionRight[NUM_DUMMIES];
	int m_ShowHookColl[NUM_DUMMIES];
	int m_LastDummy;
	int m_OtherFire;

	CControls();

	virtual void OnReset();
	virtual void OnRelease();
	virtual void OnRender();
	virtual void OnMessage(int MsgType, void *pRawMsg);
	virtual bool OnMouseMove(float x, float y);
	virtual void OnConsoleInit();
	virtual void OnPlayerDeath();

	int SnapInput(int *pData);
	void ClampMousePos();
	void ResetInput(int Dummy);
};
#endif
