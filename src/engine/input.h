/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_INPUT_H
#define ENGINE_INPUT_H

#include "kernel.h"

const int g_MaxKeys = 512;
extern const char g_aaKeyStrings[g_MaxKeys][20];

class IInput : public IInterface
{
	MACRO_INTERFACE("input", 0)
public:
	enum
	{
		INPUT_TEXT_SIZE = 128
	};

	class CEvent
	{
	public:
		int m_Flags;
		int m_Key;
		char m_aText[INPUT_TEXT_SIZE];
		int m_InputCount;
	};

protected:
	enum
	{
		INPUT_BUFFER_SIZE = 32
	};

	// quick access to events
	int m_NumEvents;
	IInput::CEvent m_aInputEvents[INPUT_BUFFER_SIZE];

public:
	enum
	{
		FLAG_PRESS = 1,
		FLAG_RELEASE = 2,
		FLAG_REPEAT = 4,
		FLAG_TEXT = 8,
	};

	// events
	int NumEvents() const { return m_NumEvents; }
	virtual bool IsEventValid(CEvent *pEvent) const = 0;
	CEvent GetEvent(int Index) const
	{
		if(Index < 0 || Index >= m_NumEvents)
		{
			IInput::CEvent e = {0, 0};
			return e;
		}
		return m_aInputEvents[Index];
	}

	CEvent *GetEventsRaw() { return m_aInputEvents; }
	int *GetEventCountRaw() { return &m_NumEvents; }

	// keys
	virtual bool KeyIsPressed(int Key) const = 0;
	virtual bool KeyPress(int Key, bool CheckCounter = false) const = 0;
	const char *KeyName(int Key) const { return (Key >= 0 && Key < g_MaxKeys) ? g_aaKeyStrings[Key] : g_aaKeyStrings[0]; }
	virtual void Clear() = 0;

	//
	virtual void NativeMousePos(int *mx, int *my) const = 0;
	virtual bool NativeMousePressed(int index) = 0;
	virtual void MouseModeRelative() = 0;
	virtual void MouseModeAbsolute() = 0;
	virtual int MouseDoubleClick() = 0;
	virtual const char *GetClipboardText() = 0;
	virtual void SetClipboardText(const char *Text) = 0;

	virtual void MouseRelative(float *x, float *y) = 0;

	virtual bool GetIMEState() = 0;
	virtual void SetIMEState(bool Activate) = 0;
	virtual int GetIMEEditingTextLength() const = 0;
	virtual const char *GetIMEEditingText() = 0;
	virtual int GetEditingCursor() = 0;
	virtual void SetEditingPosition(float X, float Y) = 0;
};

class IEngineInput : public IInput
{
	MACRO_INTERFACE("engineinput", 0)
public:
	virtual void Init() = 0;
	virtual int Update() = 0;
	virtual void NextFrame() = 0;
	virtual int VideoRestartNeeded() = 0;
};

extern IEngineInput *CreateEngineInput();

#endif
