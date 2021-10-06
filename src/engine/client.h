/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_H
#define ENGINE_CLIENT_H
#include "kernel.h"

#include "graphics.h"
#include "message.h"
#include <base/hash.h>
#include <engine/friends.h>

struct SWarning;

enum
{
	RECORDER_MANUAL = 0,
	RECORDER_AUTO = 1,
	RECORDER_RACE = 2,
	RECORDER_REPLAYS = 3,
	RECORDER_MAX = 4,

	NUM_DUMMIES = 2,
};

typedef bool (*CLIENTFUNC_FILTER)(const void *pData, int DataSize, void *pUser);

class IClient : public IInterface
{
	MACRO_INTERFACE("client", 0)
protected:
	// quick access to state of the client
	int m_State;

	// quick access to time variables
	int m_PrevGameTick[NUM_DUMMIES];
	int m_CurGameTick[NUM_DUMMIES];
	float m_GameIntraTick[NUM_DUMMIES];
	float m_GameTickTime[NUM_DUMMIES];
	float m_GameIntraTickSincePrev[NUM_DUMMIES];

	int m_PredTick[NUM_DUMMIES];
	float m_PredIntraTick[NUM_DUMMIES];

	float m_LocalTime;
	float m_RenderFrameTime;

	int m_GameTickSpeed;

	float m_FrameTimeAvg;

public:
	char m_aNews[3000];
	char m_aMapDownloadUrl[256];
	int m_Points;
	int64_t m_ReconnectTime;

	class CSnapItem
	{
	public:
		int m_Type;
		int m_ID;
		int m_DataSize;
	};

	/* Constants: Client States
		STATE_OFFLINE - The client is offline.
		STATE_CONNECTING - The client is trying to connect to a server.
		STATE_LOADING - The client has connected to a server and is loading resources.
		STATE_ONLINE - The client is connected to a server and running the game.
		STATE_DEMOPLAYBACK - The client is playing a demo
		STATE_QUITTING - The client is quitting.
	*/

	enum
	{
		STATE_OFFLINE = 0,
		STATE_CONNECTING,
		STATE_LOADING,
		STATE_ONLINE,
		STATE_DEMOPLAYBACK,
		STATE_QUITTING,
		STATE_RESTARTING,
	};

	//
	inline int State() const { return m_State; }

	// tick time access
	inline int PrevGameTick(int Dummy) const { return m_PrevGameTick[Dummy]; }
	inline int GameTick(int Dummy) const { return m_CurGameTick[Dummy]; }
	inline int PredGameTick(int Dummy) const { return m_PredTick[Dummy]; }
	inline float IntraGameTick(int Dummy) const { return m_GameIntraTick[Dummy]; }
	inline float PredIntraGameTick(int Dummy) const { return m_PredIntraTick[Dummy]; }
	inline float IntraGameTickSincePrev(int Dummy) const { return m_GameIntraTickSincePrev[Dummy]; }
	inline float GameTickTime(int Dummy) const { return m_GameTickTime[Dummy]; }
	inline int GameTickSpeed() const { return m_GameTickSpeed; }

	// other time access
	inline float RenderFrameTime() const { return m_RenderFrameTime; }
	inline float LocalTime() const { return m_LocalTime; }
	inline float FrameTimeAvg() const { return m_FrameTimeAvg; }

	// actions
	virtual void Connect(const char *pAddress, const char *pPassword = NULL) = 0;
	virtual void Disconnect() = 0;

	// dummy
	virtual void DummyDisconnect(const char *pReason) = 0;
	virtual void DummyConnect() = 0;
	virtual bool DummyConnected() = 0;
	virtual bool DummyConnecting() = 0;

	virtual void Restart() = 0;
	virtual void Quit() = 0;
	virtual const char *DemoPlayer_Play(const char *pFilename, int StorageType) = 0;
#if defined(CONF_VIDEORECORDER)
	virtual const char *DemoPlayer_Render(const char *pFilename, int StorageType, const char *pVideoName, int SpeedIndex) = 0;
#endif
	virtual void DemoRecorder_Start(const char *pFilename, bool WithTimestamp, int Recorder) = 0;
	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual void DemoRecorder_Stop(int Recorder, bool RemoveFile = false) = 0;
	virtual class IDemoRecorder *DemoRecorder(int Recorder) = 0;
	virtual void AutoScreenshot_Start() = 0;
	virtual void AutoStatScreenshot_Start() = 0;
	virtual void AutoCSV_Start() = 0;
	virtual void ServerBrowserUpdate() = 0;

	// gfx
	virtual void SwitchWindowScreen(int Index) = 0;
	virtual void SetWindowParams(int FullscreenMode, bool IsBorderless) = 0;
	virtual void ToggleWindowVSync() = 0;
	virtual void LoadFont() = 0;
	virtual void Notify(const char *pTitle, const char *pMessage) = 0;

	// networking
	virtual void EnterGame() = 0;

	//
	virtual const char *MapDownloadName() const = 0;
	virtual int MapDownloadAmount() const = 0;
	virtual int MapDownloadTotalsize() const = 0;

	// input
	virtual int *GetInput(int Tick, int IsDummy = 0) const = 0;
	virtual int *GetDirectInput(int Tick, int IsDummy = 0) const = 0;

	// remote console
	virtual void RconAuth(const char *pUsername, const char *pPassword) = 0;
	virtual bool RconAuthed() const = 0;
	virtual bool UseTempRconCommands() const = 0;
	virtual void Rcon(const char *pLine) = 0;

	// server info
	virtual void GetServerInfo(class CServerInfo *pServerInfo) const = 0;

	virtual int GetPredictionTime() = 0;

	// snapshot interface

	enum
	{
		SNAP_CURRENT = 0,
		SNAP_PREV = 1
	};

	// TODO: Refactor: should redo this a bit i think, too many virtual calls
	virtual int SnapNumItems(int SnapID) const = 0;
	virtual void *SnapFindItem(int SnapID, int Type, int ID) const = 0;
	virtual void *SnapGetItem(int SnapID, int Index, CSnapItem *pItem) const = 0;
	virtual int SnapItemSize(int SnapID, int Index) const = 0;
	virtual void SnapInvalidateItem(int SnapID, int Index) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags) = 0;
	virtual int SendMsgY(CMsgPacker *pMsg, int Flags, int NetClient = 1) = 0;

	template<class T>
	int SendPackMsg(T *pMsg, int Flags)
	{
		CMsgPacker Packer(pMsg->MsgID(), false);
		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags);
	}

	//
	virtual const char *PlayerName() const = 0;
	virtual const char *DummyName() const = 0;
	virtual const char *ErrorString() const = 0;
	virtual const char *LatestVersion() const = 0;
	virtual bool ConnectionProblems() const = 0;

	virtual bool SoundInitFailed() const = 0;

	virtual IGraphics::CTextureHandle GetDebugFont() const = 0; // TODO: remove this function

	//DDRace

	virtual const char *GetCurrentMap() const = 0;
	virtual const char *GetCurrentMapPath() const = 0;
	virtual SHA256_DIGEST GetCurrentMapSha256() const = 0;
	virtual unsigned GetCurrentMapCrc() const = 0;

	virtual int GetCurrentRaceTime() = 0;

	virtual void RaceRecord_Start(const char *pFilename) = 0;
	virtual void RaceRecord_Stop() = 0;
	virtual bool RaceRecord_IsRecording() = 0;

	virtual void DemoSliceBegin() = 0;
	virtual void DemoSliceEnd() = 0;
	virtual void DemoSlice(const char *pDstPath, CLIENTFUNC_FILTER pfnFilter, void *pUser) = 0;

	virtual void RequestDDNetInfo() = 0;
	virtual bool EditorHasUnsavedData() const = 0;

	virtual void GenerateTimeoutSeed() = 0;

	virtual IFriends *Foes() = 0;

	virtual void GetSmoothTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount) = 0;

	virtual SWarning *GetCurWarning() = 0;
};

class IGameClient : public IInterface
{
	MACRO_INTERFACE("gameclient", 0)
protected:
public:
	virtual void OnConsoleInit() = 0;

	virtual void OnRconType(bool UsernameReq) = 0;
	virtual void OnRconLine(const char *pLine) = 0;
	virtual void OnInit() = 0;
	virtual void InvalidateSnapshot() = 0;
	virtual void OnNewSnapshot() = 0;
	virtual void OnEnterGame() = 0;
	virtual void OnShutdown() = 0;
	virtual void OnRender() = 0;
	virtual void OnUpdate() = 0;
	virtual void OnStateChange(int NewState, int OldState) = 0;
	virtual void OnConnected() = 0;
	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, bool IsDummy = 0) = 0;
	virtual void OnPredict() = 0;
	virtual void OnActivateEditor() = 0;

	virtual int OnSnapInput(int *pData, bool Dummy, bool Force) = 0;
	virtual void OnDummySwap() = 0;
	virtual void SendDummyInfo(bool Start) = 0;
	virtual int GetLastRaceTick() = 0;

	virtual const char *GetItemName(int Type) const = 0;
	virtual const char *Version() const = 0;
	virtual const char *NetVersion() const = 0;
	virtual int DDNetVersion() const = 0;
	virtual const char *DDNetVersionStr() const = 0;

	virtual void OnDummyDisconnect() = 0;
	virtual void DummyResetInput() = 0;
	virtual void Echo(const char *pString) = 0;
	virtual bool CanDisplayWarning() = 0;
	virtual bool IsDisplayingWarning() = 0;
};

void SnapshotRemoveExtraProjectileInfo(unsigned char *pData);

extern IGameClient *CreateGameClient();
#endif
