/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#define _WIN32_WINNT 0x0501

#include <new>

#include <stdarg.h>
#include <tuple>

#include <base/hash_ctxt.h>
#include <base/math.h>
#include <base/system.h>
#include <base/vmath.h>

#include <game/client/components/menus.h>
#include <game/client/gameclient.h>
#include <game/editor/editor.h>

#include <engine/client.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/editor.h>
#include <engine/engine.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/map.h>
#include <engine/serverbrowser.h>
#include <engine/sound.h>
#include <engine/steam.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <engine/client/http.h>
#include <engine/client/notifications.h>
#include <engine/shared/compression.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/shared/demo.h>
#include <engine/shared/fifo.h>
#include <engine/shared/filecollection.h>
#include <engine/shared/json.h>
#include <engine/shared/network.h>
#include <engine/shared/packer.h>
#include <engine/shared/protocol.h>
#include <engine/shared/protocol_ex.h>
#include <engine/shared/ringbuffer.h>
#include <engine/shared/snapshot.h>
#include <engine/shared/uuid_manager.h>

#include <game/version.h>

#include <mastersrv/mastersrv.h>

#include <engine/client/demoedit.h>
#include <engine/client/serverbrowser.h>

#if defined(CONF_FAMILY_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "client.h"
#include "friends.h"
#include "serverbrowser.h"

#if defined(CONF_VIDEORECORDER)
#include "video.h"
#endif

#include <zlib.h>

#include "SDL.h"
#ifdef main
#undef main
#endif

// for android
#include "SDL_rwops.h"
#include "base/hash.h"

static const ColorRGBA ClientNetworkPrintColor{0.7f, 1, 0.7f, 1.0f};
static const ColorRGBA ClientNetworkErrPrintColor{1.0f, 0.25f, 0.25f, 1.0f};

void CGraph::Init(float Min, float Max)
{
	m_MinRange = m_Min = Min;
	m_MaxRange = m_Max = Max;
	m_Index = 0;
}

void CGraph::ScaleMax()
{
	int i = 0;
	m_Max = m_MaxRange;
	for(i = 0; i < MAX_VALUES; i++)
	{
		if(m_aValues[i] > m_Max)
			m_Max = m_aValues[i];
	}
}

void CGraph::ScaleMin()
{
	int i = 0;
	m_Min = m_MinRange;
	for(i = 0; i < MAX_VALUES; i++)
	{
		if(m_aValues[i] < m_Min)
			m_Min = m_aValues[i];
	}
}

void CGraph::Add(float v, float r, float g, float b)
{
	m_Index = (m_Index + 1) & (MAX_VALUES - 1);
	m_aValues[m_Index] = v;
	m_aColors[m_Index][0] = r;
	m_aColors[m_Index][1] = g;
	m_aColors[m_Index][2] = b;
}

void CGraph::Render(IGraphics *pGraphics, IGraphics::CTextureHandle FontTexture, float x, float y, float w, float h, const char *pDescription)
{
	//m_pGraphics->BlendNormal();

	pGraphics->TextureClear();

	pGraphics->QuadsBegin();
	pGraphics->SetColor(0, 0, 0, 0.75f);
	IGraphics::CQuadItem QuadItem(x, y, w, h);
	pGraphics->QuadsDrawTL(&QuadItem, 1);
	pGraphics->QuadsEnd();

	pGraphics->LinesBegin();
	pGraphics->SetColor(0.95f, 0.95f, 0.95f, 1.00f);
	IGraphics::CLineItem LineItem(x, y + h / 2, x + w, y + h / 2);
	pGraphics->LinesDraw(&LineItem, 1);
	pGraphics->SetColor(0.5f, 0.5f, 0.5f, 0.75f);
	IGraphics::CLineItem Array[2] = {
		IGraphics::CLineItem(x, y + (h * 3) / 4, x + w, y + (h * 3) / 4),
		IGraphics::CLineItem(x, y + h / 4, x + w, y + h / 4)};
	pGraphics->LinesDraw(Array, 2);
	for(int i = 1; i < MAX_VALUES; i++)
	{
		float a0 = (i - 1) / (float)MAX_VALUES;
		float a1 = i / (float)MAX_VALUES;
		int i0 = (m_Index + i - 1) & (MAX_VALUES - 1);
		int i1 = (m_Index + i) & (MAX_VALUES - 1);

		float v0 = (m_aValues[i0] - m_Min) / (m_Max - m_Min);
		float v1 = (m_aValues[i1] - m_Min) / (m_Max - m_Min);

		IGraphics::CColorVertex Array[2] = {
			IGraphics::CColorVertex(0, m_aColors[i0][0], m_aColors[i0][1], m_aColors[i0][2], 0.75f),
			IGraphics::CColorVertex(1, m_aColors[i1][0], m_aColors[i1][1], m_aColors[i1][2], 0.75f)};
		pGraphics->SetColorVertex(Array, 2);
		IGraphics::CLineItem LineItem(x + a0 * w, y + h - v0 * h, x + a1 * w, y + h - v1 * h);
		pGraphics->LinesDraw(&LineItem, 1);
	}
	pGraphics->LinesEnd();

	pGraphics->TextureSet(FontTexture);
	pGraphics->QuadsBegin();
	pGraphics->QuadsText(x + 2, y + h - 16, 16, pDescription);

	char aBuf[32];
	str_format(aBuf, sizeof(aBuf), "%.2f", m_Max);
	pGraphics->QuadsText(x + w - 8 * str_length(aBuf) - 8, y + 2, 16, aBuf);

	str_format(aBuf, sizeof(aBuf), "%.2f", m_Min);
	pGraphics->QuadsText(x + w - 8 * str_length(aBuf) - 8, y + h - 16, 16, aBuf);
	pGraphics->QuadsEnd();
}

void CSmoothTime::Init(int64_t Target)
{
	m_Snap = time_get();
	m_Current = Target;
	m_Target = Target;
	m_aAdjustSpeed[0] = 0.3f;
	m_aAdjustSpeed[1] = 0.3f;
	m_Graph.Init(0.0f, 0.5f);
}

void CSmoothTime::SetAdjustSpeed(int Direction, float Value)
{
	m_aAdjustSpeed[Direction] = Value;
}

int64_t CSmoothTime::Get(int64_t Now)
{
	int64_t c = m_Current + (Now - m_Snap);
	int64_t t = m_Target + (Now - m_Snap);

	// it's faster to adjust upward instead of downward
	// we might need to adjust these abit

	float AdjustSpeed = m_aAdjustSpeed[0];
	if(t > c)
		AdjustSpeed = m_aAdjustSpeed[1];

	float a = ((Now - m_Snap) / (float)time_freq()) * AdjustSpeed;
	if(a > 1.0f)
		a = 1.0f;

	int64_t r = c + (int64_t)((t - c) * a);

	m_Graph.Add(a + 0.5f, 1, 1, 1);

	return r;
}

void CSmoothTime::UpdateInt(int64_t Target)
{
	int64_t Now = time_get();
	m_Current = Get(Now);
	m_Snap = Now;
	m_Target = Target;
}

void CSmoothTime::Update(CGraph *pGraph, int64_t Target, int TimeLeft, int AdjustDirection)
{
	int UpdateTimer = 1;

	if(TimeLeft < 0)
	{
		int IsSpike = 0;
		if(TimeLeft < -50)
		{
			IsSpike = 1;

			m_SpikeCounter += 5;
			if(m_SpikeCounter > 50)
				m_SpikeCounter = 50;
		}

		if(IsSpike && m_SpikeCounter < 15)
		{
			// ignore this ping spike
			UpdateTimer = 0;
			pGraph->Add(TimeLeft, 1, 1, 0);
		}
		else
		{
			pGraph->Add(TimeLeft, 1, 0, 0);
			if(m_aAdjustSpeed[AdjustDirection] < 30.0f)
				m_aAdjustSpeed[AdjustDirection] *= 2.0f;
		}
	}
	else
	{
		if(m_SpikeCounter)
			m_SpikeCounter--;

		pGraph->Add(TimeLeft, 0, 1, 0);

		m_aAdjustSpeed[AdjustDirection] *= 0.95f;
		if(m_aAdjustSpeed[AdjustDirection] < 2.0f)
			m_aAdjustSpeed[AdjustDirection] = 2.0f;
	}

	if(UpdateTimer)
		UpdateInt(Target);
}

CClient::CClient() :
	m_DemoPlayer(&m_SnapshotDelta, [&]() { UpdateDemoIntraTimers(); })
{
	for(auto &DemoRecorder : m_DemoRecorder)
		DemoRecorder = CDemoRecorder(&m_SnapshotDelta);

	m_pEditor = 0;
	m_pInput = 0;
	m_pGraphics = 0;
	m_pSound = 0;
	m_pGameClient = 0;
	m_pMap = 0;
	m_pConfigManager = 0;
	m_pConfig = 0;
	m_pConsole = 0;

	m_RenderFrameTime = 0.0001f;
	m_RenderFrameTimeLow = 1.0f;
	m_RenderFrameTimeHigh = 0.0f;
	m_RenderFrames = 0;
	m_LastRenderTime = time_get();

	m_GameTickSpeed = SERVER_TICK_SPEED;

	m_SnapCrcErrors = 0;
	m_AutoScreenshotRecycle = false;
	m_AutoStatScreenshotRecycle = false;
	m_AutoCSVRecycle = false;
	m_EditorActive = false;

	m_AckGameTick[0] = -1;
	m_AckGameTick[1] = -1;
	m_CurrentRecvTick[0] = 0;
	m_CurrentRecvTick[1] = 0;
	m_RconAuthed[0] = 0;
	m_RconAuthed[1] = 0;
	m_RconPassword[0] = '\0';
	m_Password[0] = '\0';

	// version-checking
	m_aVersionStr[0] = '0';
	m_aVersionStr[1] = '\0';

	// pinging
	m_PingStartTime = 0;

	m_aCurrentMap[0] = 0;

	m_aCmdConnect[0] = 0;

	// map download
	m_aMapdownloadFilename[0] = 0;
	m_aMapdownloadFilenameTemp[0] = 0;
	m_aMapdownloadName[0] = 0;
	m_pMapdownloadTask = NULL;
	m_MapdownloadFileTemp = 0;
	m_MapdownloadChunk = 0;
	m_MapdownloadSha256Present = false;
	m_MapdownloadSha256 = SHA256_ZEROED;
	m_MapdownloadCrc = 0;
	m_MapdownloadAmount = -1;
	m_MapdownloadTotalsize = -1;

	m_MapDetailsPresent = false;
	m_aMapDetailsName[0] = 0;
	m_MapDetailsSha256 = SHA256_ZEROED;
	m_MapDetailsCrc = 0;

	str_format(m_aDDNetInfoTmp, sizeof(m_aDDNetInfoTmp), DDNET_INFO ".%d.tmp", pid());
	m_pDDNetInfoTask = NULL;
	m_aNews[0] = '\0';
	m_aMapDownloadUrl[0] = '\0';
	m_Points = -1;

	m_CurrentServerInfoRequestTime = -1;
	m_CurrentServerPingInfoType = -1;
	m_CurrentServerPingBasicToken = -1;
	m_CurrentServerPingToken = -1;
	mem_zero(&m_CurrentServerPingUuid, sizeof(m_CurrentServerPingUuid));
	m_CurrentServerCurrentPingTime = -1;
	m_CurrentServerNextPingTime = -1;

	m_CurrentInput[0] = 0;
	m_CurrentInput[1] = 0;
	m_LastDummy = 0;

	mem_zero(&m_aInputs, sizeof(m_aInputs));

	m_State = IClient::STATE_OFFLINE;
	m_aServerAddressStr[0] = 0;

	mem_zero(m_aSnapshots, sizeof(m_aSnapshots));
	m_SnapshotStorage[0].Init();
	m_SnapshotStorage[1].Init();
	m_ReceivedSnapshots[0] = 0;
	m_ReceivedSnapshots[1] = 0;
	m_SnapshotParts[0] = 0;
	m_SnapshotParts[1] = 0;

	m_VersionInfo.m_State = CVersionInfo::STATE_INIT;

	if(g_Config.m_ClDummy == 0)
		m_LastDummyConnectTime = 0;

	m_ReconnectTime = 0;

	m_GenerateTimeoutSeed = true;

	m_FrameTimeAvg = 0.0001f;
	m_BenchmarkFile = 0;
	m_BenchmarkStopTime = 0;
}

// ----- send functions -----
static inline bool RepackMsg(const CMsgPacker *pMsg, CPacker &Packer)
{
	Packer.Reset();
	if(pMsg->m_MsgID < OFFSET_UUID)
	{
		Packer.AddInt((pMsg->m_MsgID << 1) | (pMsg->m_System ? 1 : 0));
	}
	else
	{
		Packer.AddInt((0 << 1) | (pMsg->m_System ? 1 : 0)); // NETMSG_EX, NETMSGTYPE_EX
		g_UuidManager.PackUuid(pMsg->m_MsgID, &Packer);
	}
	Packer.AddRaw(pMsg->Data(), pMsg->Size());

	return false;
}

int CClient::SendMsg(CMsgPacker *pMsg, int Flags)
{
	CNetChunk Packet;

	if(State() == IClient::STATE_OFFLINE)
		return 0;

	// repack message (inefficient)
	CPacker Pack;
	if(RepackMsg(pMsg, Pack))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = 0;
	Packet.m_pData = Pack.Data();
	Packet.m_DataSize = Pack.Size();

	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	if(Flags & MSGFLAG_RECORD)
	{
		for(auto &i : m_DemoRecorder)
			if(i.IsRecording())
				i.RecordMessage(Packet.m_pData, Packet.m_DataSize);
	}

	if(!(Flags & MSGFLAG_NOSEND))
	{
		m_NetClient[g_Config.m_ClDummy].Send(&Packet);
	}

	return 0;
}

void CClient::SendInfo()
{
	CMsgPacker MsgVer(NETMSG_CLIENTVER, true);
	MsgVer.AddRaw(&m_ConnectionID, sizeof(m_ConnectionID));
	MsgVer.AddInt(GameClient()->DDNetVersion());
	MsgVer.AddString(GameClient()->DDNetVersionStr(), 0);
	SendMsg(&MsgVer, MSGFLAG_VITAL);

	CMsgPacker Msg(NETMSG_INFO, true);
	Msg.AddString(GameClient()->NetVersion(), 128);
	Msg.AddString(m_Password, 128);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendEnterGame()
{
	CMsgPacker Msg(NETMSG_ENTERGAME, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendReady()
{
	CMsgPacker Msg(NETMSG_READY, true);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::SendMapRequest()
{
	if(m_MapdownloadFileTemp)
	{
		io_close(m_MapdownloadFileTemp);
		Storage()->RemoveFile(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
	}
	m_MapdownloadFileTemp = Storage()->OpenFile(m_aMapdownloadFilenameTemp, IOFLAG_WRITE, IStorage::TYPE_SAVE);
	CMsgPacker Msg(NETMSG_REQUEST_MAP_DATA, true);
	Msg.AddInt(m_MapdownloadChunk);
	SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);
}

void CClient::RconAuth(const char *pName, const char *pPassword)
{
	if(RconAuthed())
		return;

	if(pPassword != m_RconPassword)
		str_copy(m_RconPassword, pPassword, sizeof(m_RconPassword));

	CMsgPacker Msg(NETMSG_RCON_AUTH, true);
	Msg.AddString(pName, 32);
	Msg.AddString(pPassword, 32);
	Msg.AddInt(1);
	SendMsg(&Msg, MSGFLAG_VITAL);
}

void CClient::Rcon(const char *pCmd)
{
	CMsgPacker Msg(NETMSG_RCON_CMD, true);
	Msg.AddString(pCmd, 256);
	SendMsg(&Msg, MSGFLAG_VITAL);
}

bool CClient::ConnectionProblems() const
{
	return m_NetClient[g_Config.m_ClDummy].GotProblems() != 0;
}

void CClient::DirectInput(int *pInput, int Size)
{
	CMsgPacker Msg(NETMSG_INPUT, true);
	Msg.AddInt(m_AckGameTick[g_Config.m_ClDummy]);
	Msg.AddInt(m_PredTick[g_Config.m_ClDummy]);
	Msg.AddInt(Size);

	for(int i = 0; i < Size / 4; i++)
		Msg.AddInt(pInput[i]);

	SendMsg(&Msg, 0);
}

void CClient::SendInput()
{
	int64_t Now = time_get();

	if(m_PredTick[g_Config.m_ClDummy] <= 0)
		return;

	bool Force = false;
	// fetch input
	for(int Dummy = 0; Dummy < NUM_DUMMIES; Dummy++)
	{
		if(!m_DummyConnected && Dummy != 0)
		{
			break;
		}
		int i = g_Config.m_ClDummy ^ Dummy;
		int Size = GameClient()->OnSnapInput(m_aInputs[i][m_CurrentInput[i]].m_aData, Dummy, Force);

		if(Size)
		{
			// pack input
			CMsgPacker Msg(NETMSG_INPUT, true);
			Msg.AddInt(m_AckGameTick[i]);
			Msg.AddInt(m_PredTick[i]);
			Msg.AddInt(Size);

			m_aInputs[i][m_CurrentInput[i]].m_Tick = m_PredTick[g_Config.m_ClDummy];
			m_aInputs[i][m_CurrentInput[i]].m_PredictedTime = m_PredictedTime.Get(Now);
			m_aInputs[i][m_CurrentInput[i]].m_Time = Now;

			// pack it
			for(int k = 0; k < Size / 4; k++)
				Msg.AddInt(m_aInputs[i][m_CurrentInput[i]].m_aData[k]);

			m_CurrentInput[i]++;
			m_CurrentInput[i] %= 200;

			SendMsgY(&Msg, MSGFLAG_FLUSH, i);
			// ugly workaround for dummy. we need to send input with dummy to prevent
			// prediction time resets. but if we do it too often, then it's
			// impossible to use grenade with frozen dummy that gets hammered...
			if(g_Config.m_ClDummyCopyMoves || m_CurrentInput[i] % 2)
				Force = true;
		}
	}
}

const char *CClient::LatestVersion() const
{
	return m_aVersionStr;
}

// TODO: OPT: do this a lot smarter!
int *CClient::GetInput(int Tick, int IsDummy) const
{
	int Best = -1;
	const int d = IsDummy ^ g_Config.m_ClDummy;
	for(int i = 0; i < 200; i++)
	{
		if(m_aInputs[d][i].m_Tick <= Tick && (Best == -1 || m_aInputs[d][Best].m_Tick < m_aInputs[d][i].m_Tick))
			Best = i;
	}

	if(Best != -1)
		return (int *)m_aInputs[g_Config.m_ClDummy][Best].m_aData;
	return 0;
}

int *CClient::GetDirectInput(int Tick, int IsDummy) const
{
	const int d = IsDummy ^ g_Config.m_ClDummy;
	for(int i = 0; i < 200; i++)
		if(m_aInputs[d][i].m_Tick == Tick)
			return (int *)m_aInputs[d][i].m_aData;
	return 0;
}

// ------ state handling -----
void CClient::SetState(int s)
{
	if(m_State == IClient::STATE_QUITTING || m_State == IClient::STATE_RESTARTING)
		return;

	int Old = m_State;
	if(g_Config.m_Debug)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "state change. last=%d current=%d", m_State, s);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
	}
	m_State = s;
	if(Old != s)
	{
		GameClient()->OnStateChange(m_State, Old);

		if(s == IClient::STATE_OFFLINE && m_ReconnectTime == 0)
		{
			if(g_Config.m_ClReconnectFull > 0 && (str_find_nocase(ErrorString(), "full") || str_find_nocase(ErrorString(), "reserved")))
				m_ReconnectTime = time_get() + time_freq() * g_Config.m_ClReconnectFull;
			else if(g_Config.m_ClReconnectTimeout > 0 && (str_find_nocase(ErrorString(), "Timeout") || str_find_nocase(ErrorString(), "Too weak connection")))
				m_ReconnectTime = time_get() + time_freq() * g_Config.m_ClReconnectTimeout;
		}

		if(s == IClient::STATE_ONLINE)
		{
			Discord()->SetGameInfo(m_ServerAddress, m_aCurrentMap);
			Steam()->SetGameInfo(m_ServerAddress, m_aCurrentMap);
		}
		else if(Old == IClient::STATE_ONLINE)
		{
			Discord()->ClearGameInfo();
			Steam()->ClearGameInfo();
		}
	}
}

// called when the map is loaded and we should init for a new round
void CClient::OnEnterGame()
{
	// reset input
	int i;
	for(i = 0; i < 200; i++)
	{
		m_aInputs[0][i].m_Tick = -1;
		m_aInputs[1][i].m_Tick = -1;
	}
	m_CurrentInput[0] = 0;
	m_CurrentInput[1] = 0;

	// reset snapshots
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = 0;
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = 0;
	m_SnapshotStorage[g_Config.m_ClDummy].PurgeAll();
	m_ReceivedSnapshots[g_Config.m_ClDummy] = 0;
	m_SnapshotParts[g_Config.m_ClDummy] = 0;
	m_PredTick[g_Config.m_ClDummy] = 0;
	m_CurrentRecvTick[g_Config.m_ClDummy] = 0;
	m_CurGameTick[g_Config.m_ClDummy] = 0;
	m_PrevGameTick[g_Config.m_ClDummy] = 0;

	if(g_Config.m_ClDummy == 0)
		m_LastDummyConnectTime = 0;

	GameClient()->OnEnterGame();
}

void CClient::EnterGame()
{
	if(State() == IClient::STATE_DEMOPLAYBACK)
		return;

	// now we will wait for two snapshots
	// to finish the connection
	SendEnterGame();
	OnEnterGame();

	ServerInfoRequest(); // fresh one for timeout protection
	m_CurrentServerNextPingTime = time_get() + time_freq() / 2;
	m_aTimeoutCodeSent[0] = false;
	m_aTimeoutCodeSent[1] = false;
}

void GenerateTimeoutCode(char *pBuffer, unsigned Size, char *pSeed, const NETADDR &Addr, bool Dummy)
{
	MD5_CTX Md5;
	md5_init(&Md5);
	const char *pDummy = Dummy ? "dummy" : "normal";
	md5_update(&Md5, (unsigned char *)pDummy, str_length(pDummy) + 1);
	md5_update(&Md5, (unsigned char *)pSeed, str_length(pSeed) + 1);
	md5_update(&Md5, (unsigned char *)&Addr, sizeof(Addr));
	MD5_DIGEST Digest = md5_finish(&Md5);

	unsigned short Random[8];
	mem_copy(Random, Digest.data, sizeof(Random));
	generate_password(pBuffer, Size, Random, 8);
}

void CClient::GenerateTimeoutSeed()
{
	secure_random_password(g_Config.m_ClTimeoutSeed, sizeof(g_Config.m_ClTimeoutSeed), 16);
}

void CClient::GenerateTimeoutCodes()
{
	if(g_Config.m_ClTimeoutSeed[0])
	{
		for(int i = 0; i < 2; i++)
		{
			GenerateTimeoutCode(m_aTimeoutCodes[i], sizeof(m_aTimeoutCodes[i]), g_Config.m_ClTimeoutSeed, m_ServerAddress, i);

			char aBuf[64];
			str_format(aBuf, sizeof(aBuf), "timeout code '%s' (%s)", m_aTimeoutCodes[i], i == 0 ? "normal" : "dummy");
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
		}
	}
	else
	{
		str_copy(m_aTimeoutCodes[0], g_Config.m_ClTimeoutCode, sizeof(m_aTimeoutCodes[0]));
		str_copy(m_aTimeoutCodes[1], g_Config.m_ClDummyTimeoutCode, sizeof(m_aTimeoutCodes[1]));
	}
}

void CClient::Connect(const char *pAddress, const char *pPassword)
{
	char aBuf[512];
	int Port = 8303;

	Disconnect();

	m_ConnectionID = RandomUuid();
	if(pAddress != m_aServerAddressStr)
		str_copy(m_aServerAddressStr, pAddress, sizeof(m_aServerAddressStr));

	str_format(aBuf, sizeof(aBuf), "connecting to '%s'", m_aServerAddressStr);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ClientNetworkPrintColor);
	bool is_websocket = false;
	if(strncmp(m_aServerAddressStr, "ws://", 5) == 0)
	{
		is_websocket = true;
		str_copy(m_aServerAddressStr, pAddress + 5, sizeof(m_aServerAddressStr));
	}

	ServerInfoRequest();
	if(net_host_lookup(m_aServerAddressStr, &m_ServerAddress, m_NetClient[CLIENT_MAIN].NetType()) != 0)
	{
		char aBufMsg[256];
		str_format(aBufMsg, sizeof(aBufMsg), "could not find the address of %s, connecting to localhost", aBuf);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBufMsg);
		net_host_lookup("localhost", &m_ServerAddress, m_NetClient[CLIENT_MAIN].NetType());
	}

	if(m_SendPassword)
	{
		str_copy(m_Password, g_Config.m_Password, sizeof(m_Password));
		m_SendPassword = false;
	}
	else if(!pPassword)
		m_Password[0] = 0;
	else
		str_copy(m_Password, pPassword, sizeof(m_Password));

	m_CanReceiveServerCapabilities = true;
	// Deregister Rcon commands from last connected server, might not have called
	// DisconnectWithReason if the server was shut down
	m_RconAuthed[0] = 0;
	m_UseTempRconCommands = 0;
	m_pConsole->DeregisterTempAll();
	if(m_ServerAddress.port == 0)
		m_ServerAddress.port = Port;
	if(is_websocket)
	{
		m_ServerAddress.type = NETTYPE_WEBSOCKET_IPV4;
	}
	m_NetClient[CLIENT_MAIN].Connect(&m_ServerAddress);
	SetState(IClient::STATE_CONNECTING);

	for(int i = 0; i < RECORDER_MAX; i++)
		if(m_DemoRecorder[i].IsRecording())
			DemoRecorder_Stop(i);

	m_InputtimeMarginGraph.Init(-150.0f, 150.0f);
	m_GametimeMarginGraph.Init(-150.0f, 150.0f);

	GenerateTimeoutCodes();
}

void CClient::DisconnectWithReason(const char *pReason)
{
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "disconnecting. reason='%s'", pReason ? pReason : "unknown");
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ClientNetworkPrintColor);

	// stop demo playback and recorder
	m_DemoPlayer.Stop();
	for(int i = 0; i < RECORDER_MAX; i++)
		DemoRecorder_Stop(i);

	//
	m_RconAuthed[0] = 0;
	m_ServerSentCapabilities = false;
	m_UseTempRconCommands = 0;
	m_pConsole->DeregisterTempAll();
	m_NetClient[CLIENT_MAIN].Disconnect(pReason);
	SetState(IClient::STATE_OFFLINE);
	m_pMap->Unload();
	m_CurrentServerPingInfoType = -1;
	m_CurrentServerPingBasicToken = -1;
	m_CurrentServerPingToken = -1;
	mem_zero(&m_CurrentServerPingUuid, sizeof(m_CurrentServerPingUuid));
	m_CurrentServerCurrentPingTime = -1;
	m_CurrentServerNextPingTime = -1;

	// disable all downloads
	m_MapdownloadChunk = 0;
	if(m_pMapdownloadTask)
		m_pMapdownloadTask->Abort();
	if(m_MapdownloadFileTemp)
	{
		io_close(m_MapdownloadFileTemp);
		Storage()->RemoveFile(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
	}
	m_MapdownloadFileTemp = 0;
	m_MapdownloadSha256Present = false;
	m_MapdownloadSha256 = SHA256_ZEROED;
	m_MapdownloadCrc = 0;
	m_MapdownloadTotalsize = -1;
	m_MapdownloadAmount = 0;
	m_MapDetailsPresent = false;

	// clear the current server info
	mem_zero(&m_CurrentServerInfo, sizeof(m_CurrentServerInfo));
	mem_zero(&m_ServerAddress, sizeof(m_ServerAddress));

	// clear snapshots
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = 0;
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = 0;
	m_ReceivedSnapshots[g_Config.m_ClDummy] = 0;
}

void CClient::Disconnect()
{
	m_ButtonRender = false;
	if(m_DummyConnected)
		DummyDisconnect(0);
	if(m_State != IClient::STATE_OFFLINE)
		DisconnectWithReason(0);

	// make sure to remove replay tmp demo
	if(g_Config.m_ClReplays)
	{
		Storage()->RemoveFile((&m_DemoRecorder[RECORDER_REPLAYS])->GetCurrentFilename(), IStorage::TYPE_SAVE);
	}
}

bool CClient::DummyConnected()
{
	return m_DummyConnected;
}

bool CClient::DummyConnecting()
{
	return !m_DummyConnected && m_LastDummyConnectTime > 0 && m_LastDummyConnectTime + GameTickSpeed() * 5 > GameTick(g_Config.m_ClDummy);
}

void CClient::DummyConnect()
{
	if(m_LastDummyConnectTime > 0 && m_LastDummyConnectTime + GameTickSpeed() * 5 > GameTick(g_Config.m_ClDummy))
		return;

	if(m_NetClient[CLIENT_MAIN].State() != NET_CONNSTATE_ONLINE && m_NetClient[CLIENT_MAIN].State() != NET_CONNSTATE_PENDING)
		return;

	if(m_DummyConnected)
		return;

	m_LastDummyConnectTime = GameTick(g_Config.m_ClDummy);

	m_RconAuthed[1] = 0;

	m_DummySendConnInfo = true;

	g_Config.m_ClDummyCopyMoves = 0;
	g_Config.m_ClDummyHammer = 0;

	//connecting to the server
	m_NetClient[CLIENT_DUMMY].Connect(&m_ServerAddress);
}

void CClient::DummyDisconnect(const char *pReason)
{
	if(!m_DummyConnected)
		return;

	m_NetClient[CLIENT_DUMMY].Disconnect(pReason);
	g_Config.m_ClDummy = 0;
	m_RconAuthed[1] = 0;
	m_aSnapshots[1][SNAP_CURRENT] = 0;
	m_aSnapshots[1][SNAP_PREV] = 0;
	m_ReceivedSnapshots[1] = 0;
	m_DummyConnected = false;
	GameClient()->OnDummyDisconnect();
}

int CClient::GetCurrentRaceTime()
{
	if(GameClient()->GetLastRaceTick() < 0)
		return 0;
	return (GameTick(g_Config.m_ClDummy) - GameClient()->GetLastRaceTick()) / 50;
}

int CClient::SendMsgY(CMsgPacker *pMsg, int Flags, int NetClient)
{
	CNetChunk Packet;

	// repack message (inefficient)
	CPacker Pack;
	if(RepackMsg(pMsg, Pack))
		return 0;

	mem_zero(&Packet, sizeof(CNetChunk));
	Packet.m_ClientID = 0;
	Packet.m_pData = Pack.Data();
	Packet.m_DataSize = Pack.Size();

	if(Flags & MSGFLAG_VITAL)
		Packet.m_Flags |= NETSENDFLAG_VITAL;
	if(Flags & MSGFLAG_FLUSH)
		Packet.m_Flags |= NETSENDFLAG_FLUSH;

	m_NetClient[NetClient].Send(&Packet);
	return 0;
}

void CClient::GetServerInfo(CServerInfo *pServerInfo) const
{
	mem_copy(pServerInfo, &m_CurrentServerInfo, sizeof(m_CurrentServerInfo));

	if(m_DemoPlayer.IsPlaying() && g_Config.m_ClDemoAssumeRace)
		str_copy(pServerInfo->m_aGameType, "DDraceNetwork", 14);
}

void CClient::ServerInfoRequest()
{
	mem_zero(&m_CurrentServerInfo, sizeof(m_CurrentServerInfo));
	m_CurrentServerInfoRequestTime = 0;
}

int CClient::LoadData()
{
	m_DebugFont = Graphics()->LoadTexture("debug_font.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	return 1;
}

// ---

void *CClient::SnapGetItem(int SnapID, int Index, CSnapItem *pItem) const
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItem(Index);
	pItem->m_DataSize = m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItemSize(Index);
	pItem->m_Type = m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItemType(Index);
	pItem->m_ID = i->ID();
	return (void *)i->Data();
}

int CClient::SnapItemSize(int SnapID, int Index) const
{
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	return m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItemSize(Index);
}

void CClient::SnapInvalidateItem(int SnapID, int Index)
{
	CSnapshotItem *i;
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	i = m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItem(Index);
	if(i)
	{
		if((char *)i < (char *)m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap || (char *)i > (char *)m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap + m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_SnapSize)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", "snap invalidate problem");
		if((char *)i >= (char *)m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pSnap && (char *)i < (char *)m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pSnap + m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_SnapSize)
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", "snap invalidate problem");
		i->m_TypeAndID = -1;
	}
}

void *CClient::SnapFindItem(int SnapID, int Type, int ID) const
{
	// TODO: linear search. should be fixed.
	int i;

	if(!m_aSnapshots[g_Config.m_ClDummy][SnapID])
		return 0x0;

	for(i = 0; i < m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pSnap->NumItems(); i++)
	{
		CSnapshotItem *pItem = m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItem(i);
		if(m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pAltSnap->GetItemType(i) == Type && pItem->ID() == ID)
			return (void *)pItem->Data();
	}
	return 0x0;
}

int CClient::SnapNumItems(int SnapID) const
{
	dbg_assert(SnapID >= 0 && SnapID < NUM_SNAPSHOT_TYPES, "invalid SnapID");
	if(!m_aSnapshots[g_Config.m_ClDummy][SnapID])
		return 0;
	return m_aSnapshots[g_Config.m_ClDummy][SnapID]->m_pSnap->NumItems();
}

void CClient::SnapSetStaticsize(int ItemType, int Size)
{
	m_SnapshotDelta.SetStaticsize(ItemType, Size);
}

void CClient::DebugRender()
{
	static NETSTATS Prev, Current;
	static int64_t LastSnap = 0;
	static float FrameTimeAvg = 0;
	char aBuffer[512];

	if(!g_Config.m_Debug)
		return;

	//m_pGraphics->BlendNormal();
	Graphics()->TextureSet(m_DebugFont);
	Graphics()->MapScreen(0, 0, Graphics()->ScreenWidth(), Graphics()->ScreenHeight());
	Graphics()->QuadsBegin();

	if(time_get() - LastSnap > time_freq())
	{
		LastSnap = time_get();
		Prev = Current;
		net_stats(&Current);
	}

	/*
		eth = 14
		ip = 20
		udp = 8
		total = 42
	*/
	FrameTimeAvg = FrameTimeAvg * 0.9f + m_RenderFrameTime * 0.1f;
	str_format(aBuffer, sizeof(aBuffer), "ticks: %8d %8d gfxmem: %dk fps: %3d",
		m_CurGameTick[g_Config.m_ClDummy], m_PredTick[g_Config.m_ClDummy],
		Graphics()->MemoryUsage() / 1024,
		(int)(1.0f / FrameTimeAvg + 0.5f));
	Graphics()->QuadsText(2, 2, 16, aBuffer);

	{
		int SendPackets = (Current.sent_packets - Prev.sent_packets);
		int SendBytes = (Current.sent_bytes - Prev.sent_bytes);
		int SendTotal = SendBytes + SendPackets * 42;
		int RecvPackets = (Current.recv_packets - Prev.recv_packets);
		int RecvBytes = (Current.recv_bytes - Prev.recv_bytes);
		int RecvTotal = RecvBytes + RecvPackets * 42;

		if(!SendPackets)
			SendPackets++;
		if(!RecvPackets)
			RecvPackets++;
		str_format(aBuffer, sizeof(aBuffer), "send: %3d %5d+%4d=%5d (%3d kbps) avg: %5d\nrecv: %3d %5d+%4d=%5d (%3d kbps) avg: %5d",
			SendPackets, SendBytes, SendPackets * 42, SendTotal, (SendTotal * 8) / 1024, SendBytes / SendPackets,
			RecvPackets, RecvBytes, RecvPackets * 42, RecvTotal, (RecvTotal * 8) / 1024, RecvBytes / RecvPackets);
		Graphics()->QuadsText(2, 14, 16, aBuffer);
	}

	// render rates
	{
		int y = 0;
		int i;
		for(i = 0; i < 256; i++)
		{
			if(m_SnapshotDelta.GetDataRate(i))
			{
				str_format(aBuffer, sizeof(aBuffer), "%4d %20s: %8d %8d %8d", i, GameClient()->GetItemName(i), m_SnapshotDelta.GetDataRate(i) / 8, m_SnapshotDelta.GetDataUpdates(i),
					(m_SnapshotDelta.GetDataRate(i) / m_SnapshotDelta.GetDataUpdates(i)) / 8);
				Graphics()->QuadsText(2, 100 + y * 12, 16, aBuffer);
				y++;
			}
		}
	}

	str_format(aBuffer, sizeof(aBuffer), "pred: %d ms", GetPredictionTime());
	Graphics()->QuadsText(2, 70, 16, aBuffer);
	Graphics()->QuadsEnd();

	// render graphs
	if(g_Config.m_DbgGraphs)
	{
		//Graphics()->MapScreen(0,0,400.0f,300.0f);
		float w = Graphics()->ScreenWidth() / 4.0f;
		float h = Graphics()->ScreenHeight() / 6.0f;
		float sp = Graphics()->ScreenWidth() / 100.0f;
		float x = Graphics()->ScreenWidth() - w - sp;

		m_FpsGraph.ScaleMax();
		m_FpsGraph.ScaleMin();
		m_FpsGraph.Render(Graphics(), m_DebugFont, x, sp * 5, w, h, "FPS");
		m_InputtimeMarginGraph.ScaleMin();
		m_InputtimeMarginGraph.ScaleMax();
		m_InputtimeMarginGraph.Render(Graphics(), m_DebugFont, x, sp * 5 + h + sp, w, h, "Prediction Margin");
		m_GametimeMarginGraph.ScaleMin();
		m_GametimeMarginGraph.ScaleMax();
		m_GametimeMarginGraph.Render(Graphics(), m_DebugFont, x, sp * 5 + h + sp + h + sp, w, h, "Gametime Margin");
	}
}

void CClient::Restart()
{
	SetState(IClient::STATE_RESTARTING);
}

void CClient::Quit()
{
	SetState(IClient::STATE_QUITTING);
}

const char *CClient::PlayerName() const
{
	if(g_Config.m_PlayerName[0])
	{
		return g_Config.m_PlayerName;
	}
	if(g_Config.m_SteamName[0])
	{
		return g_Config.m_SteamName;
	}
	return "nameless tee";
}

const char *CClient::DummyName() const
{
	if(g_Config.m_ClDummyName[0])
	{
		return g_Config.m_ClDummyName;
	}
	const char *pBase = 0;
	if(g_Config.m_PlayerName[0])
	{
		pBase = g_Config.m_PlayerName;
	}
	else if(g_Config.m_SteamName[0])
	{
		pBase = g_Config.m_SteamName;
	}
	if(pBase)
	{
		static char aDummyNameBuf[16];
		str_format(aDummyNameBuf, sizeof(aDummyNameBuf), "[D] %s", pBase);
		return aDummyNameBuf;
	}
	return "brainless tee";
}

const char *CClient::ErrorString() const
{
	return m_NetClient[CLIENT_MAIN].ErrorString();
}

void CClient::Render()
{
	if(g_Config.m_ClOverlayEntities)
	{
		ColorRGBA bg = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClBackgroundEntitiesColor));
		Graphics()->Clear(bg.r, bg.g, bg.b);
	}
	else
	{
		ColorRGBA bg = color_cast<ColorRGBA>(ColorHSLA(g_Config.m_ClBackgroundColor));
		Graphics()->Clear(bg.r, bg.g, bg.b);
	}

	GameClient()->OnRender();
	DebugRender();

	if(State() == IClient::STATE_ONLINE && g_Config.m_ClAntiPingLimit)
	{
		int64_t Now = time_get();
		g_Config.m_ClAntiPing = (m_PredictedTime.Get(Now) - m_GameTime[g_Config.m_ClDummy].Get(Now)) * 1000 / (float)time_freq() > g_Config.m_ClAntiPingLimit;
	}
}

const char *CClient::LoadMap(const char *pName, const char *pFilename, SHA256_DIGEST *pWantedSha256, unsigned WantedCrc)
{
	static char s_aErrorMsg[128];

	SetState(IClient::STATE_LOADING);

	if(!m_pMap->Load(pFilename))
	{
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map '%s' not found", pFilename);
		return s_aErrorMsg;
	}

	if(pWantedSha256 && m_pMap->Sha256() != *pWantedSha256)
	{
		char aWanted[SHA256_MAXSTRSIZE];
		char aGot[SHA256_MAXSTRSIZE];
		sha256_str(*pWantedSha256, aWanted, sizeof(aWanted));
		sha256_str(m_pMap->Sha256(), aGot, sizeof(aWanted));
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map differs from the server. %s != %s", aGot, aWanted);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", s_aErrorMsg);
		m_pMap->Unload();
		return s_aErrorMsg;
	}

	// Only check CRC if we don't have the secure SHA256.
	if(!pWantedSha256 && m_pMap->Crc() != WantedCrc)
	{
		str_format(s_aErrorMsg, sizeof(s_aErrorMsg), "map differs from the server. %08x != %08x", m_pMap->Crc(), WantedCrc);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", s_aErrorMsg);
		m_pMap->Unload();
		return s_aErrorMsg;
	}

	// stop demo recording if we loaded a new map
	for(int i = 0; i < RECORDER_MAX; i++)
		DemoRecorder_Stop(i, i == RECORDER_REPLAYS);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "loaded map '%s'", pFilename);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
	m_ReceivedSnapshots[g_Config.m_ClDummy] = 0;

	str_copy(m_aCurrentMap, pName, sizeof(m_aCurrentMap));
	str_copy(m_aCurrentMapPath, pFilename, sizeof(m_aCurrentMapPath));

	return 0;
}

static void FormatMapDownloadFilename(const char *pName, const SHA256_DIGEST *pSha256, int Crc, bool Temp, char *pBuffer, int BufferSize)
{
	char aSuffix[32];
	if(Temp)
	{
		str_format(aSuffix, sizeof(aSuffix), ".%d.tmp", pid());
	}
	else
	{
		str_copy(aSuffix, ".map", sizeof(aSuffix));
	}

	if(pSha256)
	{
		char aSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pSha256, aSha256, sizeof(aSha256));
		str_format(pBuffer, BufferSize, "downloadedmaps/%s_%s%s", pName, aSha256, aSuffix);
	}
	else
	{
		str_format(pBuffer, BufferSize, "downloadedmaps/%s_%08x%s", pName, Crc, aSuffix);
	}
}

const char *CClient::LoadMapSearch(const char *pMapName, SHA256_DIGEST *pWantedSha256, int WantedCrc)
{
	const char *pError = 0;
	char aBuf[512];
	char aWanted[SHA256_MAXSTRSIZE + 16];
	aWanted[0] = 0;
	if(pWantedSha256)
	{
		char aWantedSha256[SHA256_MAXSTRSIZE];
		sha256_str(*pWantedSha256, aWantedSha256, sizeof(aWantedSha256));
		str_format(aWanted, sizeof(aWanted), "sha256=%s ", aWantedSha256);
	}
	str_format(aBuf, sizeof(aBuf), "loading map, map=%s wanted %scrc=%08x", pMapName, aWanted, WantedCrc);
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);
	SetState(IClient::STATE_LOADING);

	// try the normal maps folder
	str_format(aBuf, sizeof(aBuf), "maps/%s.map", pMapName);
	pError = LoadMap(pMapName, aBuf, pWantedSha256, WantedCrc);
	if(!pError)
		return pError;

	// try the downloaded maps
	FormatMapDownloadFilename(pMapName, pWantedSha256, WantedCrc, false, aBuf, sizeof(aBuf));
	pError = LoadMap(pMapName, aBuf, pWantedSha256, WantedCrc);
	if(!pError)
		return pError;

	// backward compatibility with old names
	if(pWantedSha256)
	{
		FormatMapDownloadFilename(pMapName, 0, WantedCrc, false, aBuf, sizeof(aBuf));
		pError = LoadMap(pMapName, aBuf, pWantedSha256, WantedCrc);
		if(!pError)
			return pError;
	}

	// search for the map within subfolders
	char aFilename[IO_MAX_PATH_LENGTH];
	str_format(aFilename, sizeof(aFilename), "%s.map", pMapName);
	if(Storage()->FindFile(aFilename, "maps", IStorage::TYPE_ALL, aBuf, sizeof(aBuf)))
		pError = LoadMap(pMapName, aBuf, pWantedSha256, WantedCrc);

	return pError;
}

void CClient::ProcessConnlessPacket(CNetChunk *pPacket)
{
	// server info
	if(pPacket->m_DataSize >= (int)sizeof(SERVERBROWSE_INFO))
	{
		int Type = -1;
		if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO, sizeof(SERVERBROWSE_INFO)) == 0)
			Type = SERVERINFO_VANILLA;
		else if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO_64_LEGACY, sizeof(SERVERBROWSE_INFO_64_LEGACY)) == 0)
			Type = SERVERINFO_64_LEGACY;
		else if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO_EXTENDED, sizeof(SERVERBROWSE_INFO_EXTENDED)) == 0)
			Type = SERVERINFO_EXTENDED;
		else if(mem_comp(pPacket->m_pData, SERVERBROWSE_INFO_EXTENDED_MORE, sizeof(SERVERBROWSE_INFO_EXTENDED_MORE)) == 0)
			Type = SERVERINFO_EXTENDED_MORE;

		if(Type != -1)
		{
			void *pData = (unsigned char *)pPacket->m_pData + sizeof(SERVERBROWSE_INFO);
			int DataSize = pPacket->m_DataSize - sizeof(SERVERBROWSE_INFO);
			ProcessServerInfo(Type, &pPacket->m_Address, pData, DataSize);
		}
	}
}

static int SavedServerInfoType(int Type)
{
	if(Type == SERVERINFO_EXTENDED_MORE)
		return SERVERINFO_EXTENDED;

	return Type;
}

void CClient::ProcessServerInfo(int RawType, NETADDR *pFrom, const void *pData, int DataSize)
{
	CServerBrowser::CServerEntry *pEntry = m_ServerBrowser.Find(*pFrom);

	CServerInfo Info = {0};
	int SavedType = SavedServerInfoType(RawType);
	if((SavedType == SERVERINFO_64_LEGACY || SavedType == SERVERINFO_EXTENDED) &&
		pEntry && pEntry->m_GotInfo && SavedType == pEntry->m_Info.m_Type)
	{
		Info = pEntry->m_Info;
	}

	Info.m_Type = SavedType;

	net_addr_str(pFrom, Info.m_aAddress, sizeof(Info.m_aAddress), true);

	CUnpacker Up;
	Up.Reset(pData, DataSize);

#define GET_STRING(array) str_copy(array, Up.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES), sizeof(array))
#define GET_INT(integer) (integer) = str_toint(Up.GetString())

	int Offset = 0; // Only used for SavedType == SERVERINFO_64_LEGACY
	int Token;
	int PacketNo = 0; // Only used if SavedType == SERVERINFO_EXTENDED

	GET_INT(Token);
	if(RawType != SERVERINFO_EXTENDED_MORE)
	{
		GET_STRING(Info.m_aVersion);
		GET_STRING(Info.m_aName);
		GET_STRING(Info.m_aMap);

		if(SavedType == SERVERINFO_EXTENDED)
		{
			GET_INT(Info.m_MapCrc);
			GET_INT(Info.m_MapSize);
		}

		GET_STRING(Info.m_aGameType);
		GET_INT(Info.m_Flags);
		GET_INT(Info.m_NumPlayers);
		GET_INT(Info.m_MaxPlayers);
		GET_INT(Info.m_NumClients);
		GET_INT(Info.m_MaxClients);
		if(Info.m_aMap[0])
			Info.m_HasRank = m_ServerBrowser.HasRank(Info.m_aMap);

		// don't add invalid info to the server browser list
		if(Info.m_NumClients < 0 || Info.m_MaxClients < 0 ||
			Info.m_NumPlayers < 0 || Info.m_MaxPlayers < 0 ||
			Info.m_NumPlayers > Info.m_NumClients || Info.m_MaxPlayers > Info.m_MaxClients)
		{
			return;
		}

		switch(SavedType)
		{
		case SERVERINFO_VANILLA:
			if(Info.m_MaxPlayers > VANILLA_MAX_CLIENTS ||
				Info.m_MaxClients > VANILLA_MAX_CLIENTS)
			{
				return;
			}
			break;
		case SERVERINFO_64_LEGACY:
			if(Info.m_MaxPlayers > MAX_CLIENTS ||
				Info.m_MaxClients > MAX_CLIENTS)
			{
				return;
			}
			break;
		case SERVERINFO_EXTENDED:
			if(Info.m_NumPlayers > Info.m_NumClients)
				return;
			break;
		default:
			dbg_assert(false, "unknown serverinfo type");
		}

		if(SavedType == SERVERINFO_64_LEGACY)
			Offset = Up.GetInt();

		// Check for valid offset.
		if(Offset < 0)
			return;

		if(SavedType == SERVERINFO_EXTENDED)
			PacketNo = 0;
	}
	else
	{
		GET_INT(PacketNo);
		// 0 needs to be excluded because that's reserved for the main packet.
		if(PacketNo <= 0 || PacketNo >= 64)
			return;
	}

	bool DuplicatedPacket = false;
	if(SavedType == SERVERINFO_EXTENDED)
	{
		Up.GetString(); // extra info, reserved

		uint64_t Flag = (uint64_t)1 << PacketNo;
		DuplicatedPacket = Info.m_ReceivedPackets & Flag;
		Info.m_ReceivedPackets |= Flag;
	}

	bool IgnoreError = false;
	for(int i = Offset; i < MAX_CLIENTS && Info.m_NumReceivedClients < MAX_CLIENTS && !Up.Error(); i++)
	{
		CServerInfo::CClient *pClient = &Info.m_aClients[Info.m_NumReceivedClients];
		GET_STRING(pClient->m_aName);
		if(Up.Error())
		{
			// Packet end, no problem unless it happens during one
			// player info, so ignore the error.
			IgnoreError = true;
			break;
		}
		GET_STRING(pClient->m_aClan);
		GET_INT(pClient->m_Country);
		GET_INT(pClient->m_Score);
		GET_INT(pClient->m_Player);
		if(SavedType == SERVERINFO_EXTENDED)
		{
			Up.GetString(); // extra info, reserved
		}
		if(!Up.Error())
		{
			if(SavedType == SERVERINFO_64_LEGACY)
			{
				uint64_t Flag = (uint64_t)1 << i;
				if(!(Info.m_ReceivedPackets & Flag))
				{
					Info.m_ReceivedPackets |= Flag;
					Info.m_NumReceivedClients++;
				}
			}
			else
			{
				Info.m_NumReceivedClients++;
			}
		}
	}

	str_clean_whitespaces(Info.m_aName);

	if(!Up.Error() || IgnoreError)
	{
		if(!DuplicatedPacket && (!pEntry || !pEntry->m_GotInfo || SavedType >= pEntry->m_Info.m_Type))
		{
			m_ServerBrowser.Set(*pFrom, IServerBrowser::SET_TOKEN, Token, &Info);
		}

		// Player info is irrelevant for the client (while connected),
		// it gets its info from elsewhere.
		//
		// SERVERINFO_EXTENDED_MORE doesn't carry any server
		// information, so just skip it.
		if(net_addr_comp(&m_ServerAddress, pFrom) == 0 && RawType != SERVERINFO_EXTENDED_MORE)
		{
			// Only accept server info that has a type that is
			// newer or equal to something the server already sent
			// us.
			if(SavedType >= m_CurrentServerInfo.m_Type)
			{
				mem_copy(&m_CurrentServerInfo, &Info, sizeof(m_CurrentServerInfo));
				m_CurrentServerInfo.m_NetAddr = m_ServerAddress;
				m_CurrentServerInfoRequestTime = -1;
			}

			bool ValidPong = false;
			if(!m_ServerCapabilities.m_PingEx && m_CurrentServerCurrentPingTime >= 0 && SavedType >= m_CurrentServerPingInfoType)
			{
				if(RawType == SERVERINFO_VANILLA)
				{
					ValidPong = Token == m_CurrentServerPingBasicToken;
				}
				else if(RawType == SERVERINFO_EXTENDED)
				{
					ValidPong = Token == m_CurrentServerPingToken;
				}
			}
			if(ValidPong)
			{
				int LatencyMs = (time_get() - m_CurrentServerCurrentPingTime) * 1000 / time_freq();
				m_ServerBrowser.SetCurrentServerPing(m_ServerAddress, LatencyMs);
				m_CurrentServerPingInfoType = SavedType;
				m_CurrentServerCurrentPingTime = -1;

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "got pong from current server, latency=%dms", LatencyMs);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
			}
		}
	}

#undef GET_STRING
#undef GET_INT
}

bool CClient::ShouldSendChatTimeoutCodeHeuristic()
{
	if(m_ServerSentCapabilities)
	{
		return false;
	}
	return IsDDNet(&m_CurrentServerInfo);
}

static CServerCapabilities GetServerCapabilities(int Version, int Flags)
{
	CServerCapabilities Result;
	bool DDNet = false;
	if(Version >= 1)
	{
		DDNet = Flags & SERVERCAPFLAG_DDNET;
	}
	Result.m_ChatTimeoutCode = DDNet;
	Result.m_AnyPlayerFlag = DDNet;
	Result.m_PingEx = false;
	if(Version >= 1)
	{
		Result.m_ChatTimeoutCode = Flags & SERVERCAPFLAG_CHATTIMEOUTCODE;
	}
	if(Version >= 2)
	{
		Result.m_AnyPlayerFlag = Flags & SERVERCAPFLAG_ANYPLAYERFLAG;
	}
	if(Version >= 3)
	{
		Result.m_PingEx = Flags & SERVERCAPFLAG_PINGEX;
	}
	return Result;
}

void CClient::ProcessServerPacket(CNetChunk *pPacket)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}
	else if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsg(&Packer, MSGFLAG_VITAL);
	}

	if(Sys)
	{
		// system message
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_DETAILS)
		{
			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
			SHA256_DIGEST *pMapSha256 = (SHA256_DIGEST *)Unpacker.GetRaw(sizeof(*pMapSha256));
			int MapCrc = Unpacker.GetInt();

			if(Unpacker.Error())
			{
				return;
			}

			m_MapDetailsPresent = true;
			str_copy(m_aMapDetailsName, pMap, sizeof(m_aMapDetailsName));
			m_MapDetailsSha256 = *pMapSha256;
			m_MapDetailsCrc = MapCrc;
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CAPABILITIES)
		{
			if(!m_CanReceiveServerCapabilities)
			{
				return;
			}
			int Version = Unpacker.GetInt();
			int Flags = Unpacker.GetInt();
			if(Version <= 0)
			{
				return;
			}
			m_ServerCapabilities = GetServerCapabilities(Version, Flags);
			m_CanReceiveServerCapabilities = false;
			m_ServerSentCapabilities = true;
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_MAP_CHANGE)
		{
			if(m_CanReceiveServerCapabilities)
			{
				m_ServerCapabilities = GetServerCapabilities(0, 0);
				m_CanReceiveServerCapabilities = false;
			}
			bool MapDetailsWerePresent = m_MapDetailsPresent;
			m_MapDetailsPresent = false;

			const char *pMap = Unpacker.GetString(CUnpacker::SANITIZE_CC | CUnpacker::SKIP_START_WHITESPACES);
			int MapCrc = Unpacker.GetInt();
			int MapSize = Unpacker.GetInt();
			const char *pError = 0;

			if(Unpacker.Error())
				return;

			if(m_DummyConnected)
				DummyDisconnect(0);

			for(int i = 0; pMap[i]; i++) // protect the player from nasty map names
			{
				if(pMap[i] == '/' || pMap[i] == '\\')
					pError = "strange character in map name";
			}

			if(MapSize < 0)
				pError = "invalid map size";

			if(pError)
				DisconnectWithReason(pError);
			else
			{
				SHA256_DIGEST *pMapSha256 = 0;
				if(MapDetailsWerePresent && str_comp(m_aMapDetailsName, pMap) == 0 && m_MapDetailsCrc == MapCrc)
				{
					pMapSha256 = &m_MapDetailsSha256;
				}
				pError = LoadMapSearch(pMap, pMapSha256, MapCrc);

				if(!pError)
				{
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "loading done");
					SendReady();
				}
				else
				{
					if(m_MapdownloadFileTemp)
					{
						io_close(m_MapdownloadFileTemp);
						Storage()->RemoveFile(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
					}

					// start map download
					FormatMapDownloadFilename(pMap, pMapSha256, MapCrc, false, m_aMapdownloadFilename, sizeof(m_aMapdownloadFilename));
					FormatMapDownloadFilename(pMap, pMapSha256, MapCrc, true, m_aMapdownloadFilenameTemp, sizeof(m_aMapdownloadFilenameTemp));

					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "starting to download map to '%s'", m_aMapdownloadFilenameTemp);
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", aBuf);

					m_MapdownloadChunk = 0;
					str_copy(m_aMapdownloadName, pMap, sizeof(m_aMapdownloadName));

					m_MapdownloadSha256Present = (bool)pMapSha256;
					m_MapdownloadSha256 = pMapSha256 ? *pMapSha256 : SHA256_ZEROED;
					m_MapdownloadCrc = MapCrc;
					m_MapdownloadTotalsize = MapSize;
					m_MapdownloadAmount = 0;

					ResetMapDownload();

					if(pMapSha256 && g_Config.m_ClHttpMapDownload)
					{
						char aUrl[256];
						char aEscaped[256];
						EscapeUrl(aEscaped, sizeof(aEscaped), m_aMapdownloadFilename + 15); // cut off downloadedmaps/
						bool UseConfigUrl = str_comp(g_Config.m_ClMapDownloadUrl, "https://maps2.ddnet.tw") != 0 || m_aMapDownloadUrl[0] == '\0';
						str_format(aUrl, sizeof(aUrl), "%s/%s", UseConfigUrl ? g_Config.m_ClMapDownloadUrl : m_aMapDownloadUrl, aEscaped);

						m_pMapdownloadTask = std::make_shared<CGetFile>(Storage(), aUrl, m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE, CTimeout{g_Config.m_ClMapDownloadConnectTimeoutMs, g_Config.m_ClMapDownloadLowSpeedLimit, g_Config.m_ClMapDownloadLowSpeedTime});
						Engine()->AddJob(m_pMapdownloadTask);
					}
					else
						SendMapRequest();
				}
			}
		}
		else if(Msg == NETMSG_MAP_DATA)
		{
			int Last = Unpacker.GetInt();
			int MapCRC = Unpacker.GetInt();
			int Chunk = Unpacker.GetInt();
			int Size = Unpacker.GetInt();
			const unsigned char *pData = Unpacker.GetRaw(Size);

			// check for errors
			if(Unpacker.Error() || Size <= 0 || MapCRC != m_MapdownloadCrc || Chunk != m_MapdownloadChunk || !m_MapdownloadFileTemp)
				return;

			io_write(m_MapdownloadFileTemp, pData, Size);

			m_MapdownloadAmount += Size;

			if(Last)
			{
				if(m_MapdownloadFileTemp)
				{
					io_close(m_MapdownloadFileTemp);
					m_MapdownloadFileTemp = 0;
				}
				FinishMapDownload();
			}
			else
			{
				// request new chunk
				m_MapdownloadChunk++;

				CMsgPacker Msg(NETMSG_REQUEST_MAP_DATA, true);
				Msg.AddInt(m_MapdownloadChunk);
				SendMsg(&Msg, MSGFLAG_VITAL | MSGFLAG_FLUSH);

				if(g_Config.m_Debug)
				{
					char aBuf[256];
					str_format(aBuf, sizeof(aBuf), "requested chunk %d", m_MapdownloadChunk);
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client/network", aBuf);
				}
			}
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_CON_READY)
		{
			GameClient()->OnConnected();
		}
		else if(Msg == NETMSG_PING)
		{
			CMsgPacker Msg(NETMSG_PING_REPLY, true);
			SendMsg(&Msg, 0);
		}
		else if(Msg == NETMSG_PINGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if(Unpacker.Error())
			{
				return;
			}
			CMsgPacker Msg(NETMSG_PONGEX, true);
			Msg.AddRaw(pID, sizeof(*pID));
			SendMsg(&Msg, MSGFLAG_FLUSH);
		}
		else if(Msg == NETMSG_PONGEX)
		{
			CUuid *pID = (CUuid *)Unpacker.GetRaw(sizeof(*pID));
			if(Unpacker.Error())
			{
				return;
			}
			if(m_ServerCapabilities.m_PingEx && m_CurrentServerCurrentPingTime >= 0 && *pID == m_CurrentServerPingUuid)
			{
				int LatencyMs = (time_get() - m_CurrentServerCurrentPingTime) * 1000 / time_freq();
				m_ServerBrowser.SetCurrentServerPing(m_ServerAddress, LatencyMs);
				m_CurrentServerCurrentPingTime = -1;

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "got pong from current server, latency=%dms", LatencyMs);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
			}
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_ADD)
		{
			if(!g_Config.m_ClDummy)
			{
				const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				const char *pHelp = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				const char *pParams = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() == 0)
					m_pConsole->RegisterTemp(pName, pParams, CFGFLAG_SERVER, pHelp);
			}
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_CMD_REM)
		{
			if(!g_Config.m_ClDummy)
			{
				const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() == 0)
					m_pConsole->DeregisterTemp(pName);
			}
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_AUTH_STATUS)
		{
			int Result = Unpacker.GetInt();
			if(Unpacker.Error() == 0)
				m_RconAuthed[g_Config.m_ClDummy] = Result;
			int Old = m_UseTempRconCommands;
			m_UseTempRconCommands = Unpacker.GetInt();
			if(Unpacker.Error() != 0)
				m_UseTempRconCommands = 0;
			if(Old != 0 && m_UseTempRconCommands == 0)
				m_pConsole->DeregisterTempAll();
		}
		else if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 && Msg == NETMSG_RCON_LINE)
		{
			const char *pLine = Unpacker.GetString();
			if(Unpacker.Error() == 0)
				GameClient()->OnRconLine(pLine);
		}
		else if(Msg == NETMSG_PING_REPLY)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "latency %.2f", (time_get() - m_PingStartTime) * 1000 / (float)time_freq());
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client/network", aBuf);
		}
		else if(Msg == NETMSG_INPUTTIMING)
		{
			int InputPredTick = Unpacker.GetInt();
			int TimeLeft = Unpacker.GetInt();
			int64_t Now = time_get();

			// adjust our prediction time
			int64_t Target = 0;
			for(int k = 0; k < 200; k++)
			{
				if(m_aInputs[g_Config.m_ClDummy][k].m_Tick == InputPredTick)
				{
					Target = m_aInputs[g_Config.m_ClDummy][k].m_PredictedTime + (Now - m_aInputs[g_Config.m_ClDummy][k].m_Time);
					Target = Target - (int64_t)(((TimeLeft - PREDICTION_MARGIN) / 1000.0f) * time_freq());
					break;
				}
			}

			if(Target)
				m_PredictedTime.Update(&m_InputtimeMarginGraph, Target, TimeLeft, 1);
		}
		else if(Msg == NETMSG_SNAP || Msg == NETMSG_SNAPSINGLE || Msg == NETMSG_SNAPEMPTY)
		{
			int NumParts = 1;
			int Part = 0;
			int GameTick = Unpacker.GetInt();
			int DeltaTick = GameTick - Unpacker.GetInt();
			int PartSize = 0;
			unsigned int Crc = 0;
			int CompleteSize = 0;
			const char *pData = 0;

			// only allow packets from the server we actually want
			if(net_addr_comp(&pPacket->m_Address, &m_ServerAddress))
				return;

			// we are not allowed to process snapshot yet
			if(State() < IClient::STATE_LOADING)
				return;

			if(Msg == NETMSG_SNAP)
			{
				NumParts = Unpacker.GetInt();
				Part = Unpacker.GetInt();
			}

			if(Msg != NETMSG_SNAPEMPTY)
			{
				Crc = Unpacker.GetInt();
				PartSize = Unpacker.GetInt();
			}

			pData = (const char *)Unpacker.GetRaw(PartSize);

			if(Unpacker.Error() || NumParts < 1 || NumParts > CSnapshot::MAX_PARTS || Part < 0 || Part >= NumParts || PartSize < 0 || PartSize > MAX_SNAPSHOT_PACKSIZE)
				return;

			if(GameTick >= m_CurrentRecvTick[g_Config.m_ClDummy])
			{
				if(GameTick != m_CurrentRecvTick[g_Config.m_ClDummy])
				{
					m_SnapshotParts[g_Config.m_ClDummy] = 0;
					m_CurrentRecvTick[g_Config.m_ClDummy] = GameTick;
				}

				mem_copy((char *)m_aSnapshotIncomingData + Part * MAX_SNAPSHOT_PACKSIZE, pData, clamp(PartSize, 0, (int)sizeof(m_aSnapshotIncomingData) - Part * MAX_SNAPSHOT_PACKSIZE));
				m_SnapshotParts[g_Config.m_ClDummy] |= 1 << Part;

				if(m_SnapshotParts[g_Config.m_ClDummy] == (unsigned)((1 << NumParts) - 1))
				{
					static CSnapshot Emptysnap;
					CSnapshot *pDeltaShot = &Emptysnap;
					int PurgeTick;
					void *pDeltaData;
					int DeltaSize;
					unsigned char aTmpBuffer2[CSnapshot::MAX_SIZE];
					unsigned char aTmpBuffer3[CSnapshot::MAX_SIZE];
					CSnapshot *pTmpBuffer3 = (CSnapshot *)aTmpBuffer3; // Fix compiler warning for strict-aliasing
					int SnapSize;

					CompleteSize = (NumParts - 1) * MAX_SNAPSHOT_PACKSIZE + PartSize;

					// reset snapshoting
					m_SnapshotParts[g_Config.m_ClDummy] = 0;

					// find snapshot that we should use as delta
					Emptysnap.Clear();

					// find delta
					if(DeltaTick >= 0)
					{
						int DeltashotSize = m_SnapshotStorage[g_Config.m_ClDummy].Get(DeltaTick, 0, &pDeltaShot, 0);

						if(DeltashotSize < 0)
						{
							// couldn't find the delta snapshots that the server used
							// to compress this snapshot. force the server to resync
							if(g_Config.m_Debug)
							{
								char aBuf[256];
								str_format(aBuf, sizeof(aBuf), "error, couldn't find the delta snapshot");
								m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
							}

							// ack snapshot
							// TODO: combine this with the input message
							m_AckGameTick[g_Config.m_ClDummy] = -1;
							return;
						}
					}

					// decompress snapshot
					pDeltaData = m_SnapshotDelta.EmptyDelta();
					DeltaSize = sizeof(int) * 3;

					if(CompleteSize)
					{
						int IntSize = CVariableInt::Decompress(m_aSnapshotIncomingData, CompleteSize, aTmpBuffer2, sizeof(aTmpBuffer2));

						if(IntSize < 0) // failure during decompression, bail
							return;

						pDeltaData = aTmpBuffer2;
						DeltaSize = IntSize;
					}

					// unpack delta
					SnapSize = m_SnapshotDelta.UnpackDelta(pDeltaShot, pTmpBuffer3, pDeltaData, DeltaSize);
					if(SnapSize < 0)
					{
						dbg_msg("client", "delta unpack failed!=%d", SnapSize);
						return;
					}

					if(Msg != NETMSG_SNAPEMPTY && pTmpBuffer3->Crc() != Crc)
					{
						if(g_Config.m_Debug)
						{
							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "snapshot crc error #%d - tick=%d wantedcrc=%d gotcrc=%d compressed_size=%d delta_tick=%d",
								m_SnapCrcErrors, GameTick, Crc, pTmpBuffer3->Crc(), CompleteSize, DeltaTick);
							m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
						}

						m_SnapCrcErrors++;
						if(m_SnapCrcErrors > 10)
						{
							// to many errors, send reset
							m_AckGameTick[g_Config.m_ClDummy] = -1;
							SendInput();
							m_SnapCrcErrors = 0;
						}
						return;
					}
					else
					{
						if(m_SnapCrcErrors)
							m_SnapCrcErrors--;
					}

					// purge old snapshots
					PurgeTick = DeltaTick;
					if(m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick;
					if(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
					m_SnapshotStorage[g_Config.m_ClDummy].PurgeUntil(PurgeTick);

					// add new
					m_SnapshotStorage[g_Config.m_ClDummy].Add(GameTick, time_get(), SnapSize, pTmpBuffer3, 1);

					// for antiping: if the projectile netobjects from the server contains extra data, this is removed and the original content restored before recording demo
					unsigned char aExtraInfoRemoved[CSnapshot::MAX_SIZE];
					mem_copy(aExtraInfoRemoved, pTmpBuffer3, SnapSize);
					SnapshotRemoveExtraProjectileInfo(aExtraInfoRemoved);

					// add snapshot to demo
					for(auto &DemoRecorder : m_DemoRecorder)
					{
						if(DemoRecorder.IsRecording())
						{
							// write snapshot
							DemoRecorder.RecordSnapshot(GameTick, aExtraInfoRemoved, SnapSize);
						}
					}

					// apply snapshot, cycle pointers
					m_ReceivedSnapshots[g_Config.m_ClDummy]++;

					m_CurrentRecvTick[g_Config.m_ClDummy] = GameTick;

					// we got two snapshots until we see us self as connected
					if(m_ReceivedSnapshots[g_Config.m_ClDummy] == 2)
					{
						// start at 200ms and work from there
						m_PredictedTime.Init(GameTick * time_freq() / 50);
						m_PredictedTime.SetAdjustSpeed(1, 1000.0f);
						m_GameTime[g_Config.m_ClDummy].Init((GameTick - 1) * time_freq() / 50);
						m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = m_SnapshotStorage[g_Config.m_ClDummy].m_pFirst;
						m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = m_SnapshotStorage[g_Config.m_ClDummy].m_pLast;
						m_LocalStartTime = time_get();
#if defined(CONF_VIDEORECORDER)
						IVideo::SetLocalStartTime(m_LocalStartTime);
#endif
						GameClient()->OnNewSnapshot();
						SetState(IClient::STATE_ONLINE);
						DemoRecorder_HandleAutoStart();
					}

					// adjust game time
					if(m_ReceivedSnapshots[g_Config.m_ClDummy] > 2)
					{
						int64_t Now = m_GameTime[g_Config.m_ClDummy].Get(time_get());
						int64_t TickStart = GameTick * time_freq() / 50;
						int64_t TimeLeft = (TickStart - Now) * 1000 / time_freq();
						m_GameTime[g_Config.m_ClDummy].Update(&m_GametimeMarginGraph, (GameTick - 1) * time_freq() / 50, TimeLeft, 0);
					}

					if(m_ReceivedSnapshots[g_Config.m_ClDummy] > 50 && !m_aTimeoutCodeSent[g_Config.m_ClDummy])
					{
						if(m_ServerCapabilities.m_ChatTimeoutCode || ShouldSendChatTimeoutCodeHeuristic())
						{
							m_aTimeoutCodeSent[g_Config.m_ClDummy] = true;
							CNetMsg_Cl_Say Msg;
							Msg.m_Team = 0;
							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "/timeout %s", m_aTimeoutCodes[g_Config.m_ClDummy]);
							Msg.m_pMessage = aBuf;
							CMsgPacker Packer(Msg.MsgID(), false);
							Msg.Pack(&Packer);
							SendMsgY(&Packer, MSGFLAG_VITAL, g_Config.m_ClDummy);
						}
					}

					// ack snapshot
					m_AckGameTick[g_Config.m_ClDummy] = GameTick;
				}
			}
		}
		else if(Msg == NETMSG_RCONTYPE)
		{
			bool UsernameReq = Unpacker.GetInt() & 1;
			GameClient()->OnRconType(UsernameReq);
		}
	}
	else
	{
		if((pPacket->m_Flags & NET_CHUNKFLAG_VITAL) != 0 || Msg == NETMSGTYPE_SV_EXTRAPROJECTILE)
		{
			// game message
			for(auto &DemoRecorder : m_DemoRecorder)
				if(DemoRecorder.IsRecording())
					DemoRecorder.RecordMessage(pPacket->m_pData, pPacket->m_DataSize);

			GameClient()->OnMessage(Msg, &Unpacker);
		}
	}
}

void CClient::ProcessServerPacketDummy(CNetChunk *pPacket)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pPacket->m_pData, pPacket->m_DataSize);
	CMsgPacker Packer(NETMSG_EX, true);

	// unpack msgid and system flag
	int Msg;
	bool Sys;
	CUuid Uuid;

	int Result = UnpackMessageID(&Msg, &Sys, &Uuid, &Unpacker, &Packer);
	if(Result == UNPACKMESSAGE_ERROR)
	{
		return;
	}
	else if(Result == UNPACKMESSAGE_ANSWER)
	{
		SendMsgY(&Packer, MSGFLAG_VITAL, !g_Config.m_ClDummy);
	}

	if(Sys)
	{
		if(Msg == NETMSG_CON_READY)
		{
			m_DummyConnected = true;
			g_Config.m_ClDummy = 1;
			Rcon("crashmeplx");
			if(m_RconAuthed[0])
				RconAuth("", m_RconPassword);
		}
		else if(Msg == NETMSG_RCON_CMD_ADD)
		{
			if(g_Config.m_ClDummy)
			{
				const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				const char *pHelp = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				const char *pParams = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() == 0)
					m_pConsole->RegisterTemp(pName, pParams, CFGFLAG_SERVER, pHelp);
			}
		}
		else if(Msg == NETMSG_RCON_CMD_REM)
		{
			if(g_Config.m_ClDummy)
			{
				const char *pName = Unpacker.GetString(CUnpacker::SANITIZE_CC);
				if(Unpacker.Error() == 0)
					m_pConsole->DeregisterTemp(pName);
			}
		}
		else if(Msg == NETMSG_SNAP || Msg == NETMSG_SNAPSINGLE || Msg == NETMSG_SNAPEMPTY)
		{
			int NumParts = 1;
			int Part = 0;
			int GameTick = Unpacker.GetInt();
			int DeltaTick = GameTick - Unpacker.GetInt();
			int PartSize = 0;
			unsigned int Crc = 0;
			int CompleteSize = 0;
			const char *pData = 0;

			// only allow packets from the server we actually want
			if(net_addr_comp(&pPacket->m_Address, &m_ServerAddress))
				return;

			// we are not allowed to process snapshot yet
			if(State() < IClient::STATE_LOADING)
				return;

			if(Msg == NETMSG_SNAP)
			{
				NumParts = Unpacker.GetInt();
				Part = Unpacker.GetInt();
			}

			if(Msg != NETMSG_SNAPEMPTY)
			{
				Crc = Unpacker.GetInt();
				PartSize = Unpacker.GetInt();
			}

			pData = (const char *)Unpacker.GetRaw(PartSize);

			if(Unpacker.Error() || NumParts < 1 || NumParts > CSnapshot::MAX_PARTS || Part < 0 || Part >= NumParts || PartSize < 0 || PartSize > MAX_SNAPSHOT_PACKSIZE)
				return;

			if(GameTick >= m_CurrentRecvTick[!g_Config.m_ClDummy])
			{
				if(GameTick != m_CurrentRecvTick[!g_Config.m_ClDummy])
				{
					m_SnapshotParts[!g_Config.m_ClDummy] = 0;
					m_CurrentRecvTick[!g_Config.m_ClDummy] = GameTick;
				}

				mem_copy((char *)m_aSnapshotIncomingData + Part * MAX_SNAPSHOT_PACKSIZE, pData, clamp(PartSize, 0, (int)sizeof(m_aSnapshotIncomingData) - Part * MAX_SNAPSHOT_PACKSIZE));
				m_SnapshotParts[!g_Config.m_ClDummy] |= 1 << Part;

				if(m_SnapshotParts[!g_Config.m_ClDummy] == (unsigned)((1 << NumParts) - 1))
				{
					static CSnapshot Emptysnap;
					CSnapshot *pDeltaShot = &Emptysnap;
					int PurgeTick;
					void *pDeltaData;
					int DeltaSize;
					unsigned char aTmpBuffer2[CSnapshot::MAX_SIZE];
					unsigned char aTmpBuffer3[CSnapshot::MAX_SIZE];
					CSnapshot *pTmpBuffer3 = (CSnapshot *)aTmpBuffer3; // Fix compiler warning for strict-aliasing
					int SnapSize;

					CompleteSize = (NumParts - 1) * MAX_SNAPSHOT_PACKSIZE + PartSize;

					// reset snapshoting
					m_SnapshotParts[!g_Config.m_ClDummy] = 0;

					// find snapshot that we should use as delta
					Emptysnap.Clear();

					// find delta
					if(DeltaTick >= 0)
					{
						int DeltashotSize = m_SnapshotStorage[!g_Config.m_ClDummy].Get(DeltaTick, 0, &pDeltaShot, 0);

						if(DeltashotSize < 0)
						{
							// couldn't find the delta snapshots that the server used
							// to compress this snapshot. force the server to resync
							if(g_Config.m_Debug)
							{
								char aBuf[256];
								str_format(aBuf, sizeof(aBuf), "error, couldn't find the delta snapshot");
								m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
							}

							// ack snapshot
							// TODO: combine this with the input message
							m_AckGameTick[!g_Config.m_ClDummy] = -1;
							return;
						}
					}

					// decompress snapshot
					pDeltaData = m_SnapshotDelta.EmptyDelta();
					DeltaSize = sizeof(int) * 3;

					if(CompleteSize)
					{
						int IntSize = CVariableInt::Decompress(m_aSnapshotIncomingData, CompleteSize, aTmpBuffer2, sizeof(aTmpBuffer2));

						if(IntSize < 0) // failure during decompression, bail
							return;

						pDeltaData = aTmpBuffer2;
						DeltaSize = IntSize;
					}

					// unpack delta
					SnapSize = m_SnapshotDelta.UnpackDelta(pDeltaShot, pTmpBuffer3, pDeltaData, DeltaSize);
					if(SnapSize < 0)
					{
						m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", "delta unpack failed!");
						return;
					}

					if(Msg != NETMSG_SNAPEMPTY && pTmpBuffer3->Crc() != Crc)
					{
						if(g_Config.m_Debug)
						{
							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "snapshot crc error #%d - tick=%d wantedcrc=%d gotcrc=%d compressed_size=%d delta_tick=%d",
								m_SnapCrcErrors, GameTick, Crc, pTmpBuffer3->Crc(), CompleteSize, DeltaTick);
							m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "client", aBuf);
						}

						m_SnapCrcErrors++;
						if(m_SnapCrcErrors > 10)
						{
							// to many errors, send reset
							m_AckGameTick[!g_Config.m_ClDummy] = -1;
							SendInput();
							m_SnapCrcErrors = 0;
						}
						return;
					}
					else
					{
						if(m_SnapCrcErrors)
							m_SnapCrcErrors--;
					}

					// purge old snapshots
					PurgeTick = DeltaTick;
					if(m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV] && m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV]->m_Tick;
					if(m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick < PurgeTick)
						PurgeTick = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
					m_SnapshotStorage[!g_Config.m_ClDummy].PurgeUntil(PurgeTick);

					// add new
					m_SnapshotStorage[!g_Config.m_ClDummy].Add(GameTick, time_get(), SnapSize, pTmpBuffer3, 1);

					// apply snapshot, cycle pointers
					m_ReceivedSnapshots[!g_Config.m_ClDummy]++;

					m_CurrentRecvTick[!g_Config.m_ClDummy] = GameTick;

					// we got two snapshots until we see us self as connected
					if(m_ReceivedSnapshots[!g_Config.m_ClDummy] == 2)
					{
						// start at 200ms and work from there
						//m_PredictedTime[!g_Config.m_ClDummy].Init(GameTick*time_freq()/50);
						//m_PredictedTime[!g_Config.m_ClDummy].SetAdjustSpeed(1, 1000.0f);
						m_GameTime[!g_Config.m_ClDummy].Init((GameTick - 1) * time_freq() / 50);
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV] = m_SnapshotStorage[!g_Config.m_ClDummy].m_pFirst;
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT] = m_SnapshotStorage[!g_Config.m_ClDummy].m_pLast;
						m_LocalStartTime = time_get();
#if defined(CONF_VIDEORECORDER)
						IVideo::SetLocalStartTime(m_LocalStartTime);
#endif
						SetState(IClient::STATE_ONLINE);
					}

					// adjust game time
					if(m_ReceivedSnapshots[!g_Config.m_ClDummy] > 2)
					{
						int64_t Now = m_GameTime[!g_Config.m_ClDummy].Get(time_get());
						int64_t TickStart = GameTick * time_freq() / 50;
						int64_t TimeLeft = (TickStart - Now) * 1000 / time_freq();
						m_GameTime[!g_Config.m_ClDummy].Update(&m_GametimeMarginGraph, (GameTick - 1) * time_freq() / 50, TimeLeft, 0);
					}

					// ack snapshot
					m_AckGameTick[!g_Config.m_ClDummy] = GameTick;
				}
			}
		}
	}
	else
	{
		GameClient()->OnMessage(Msg, &Unpacker, 1);
	}
}

void CClient::ResetMapDownload()
{
	if(m_pMapdownloadTask)
	{
		m_pMapdownloadTask->Abort();
		m_pMapdownloadTask = NULL;
	}
	m_MapdownloadFileTemp = 0;
	m_MapdownloadAmount = 0;
}

void CClient::FinishMapDownload()
{
	const char *pError;
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "download complete, loading map");

	int Prev = m_MapdownloadTotalsize;
	m_MapdownloadTotalsize = -1;
	SHA256_DIGEST *pSha256 = m_MapdownloadSha256Present ? &m_MapdownloadSha256 : 0;

	Storage()->RemoveFile(m_aMapdownloadFilename, IStorage::TYPE_SAVE);
	Storage()->RenameFile(m_aMapdownloadFilenameTemp, m_aMapdownloadFilename, IStorage::TYPE_SAVE);

	// load map
	pError = LoadMap(m_aMapdownloadName, m_aMapdownloadFilename, pSha256, m_MapdownloadCrc);
	if(!pError)
	{
		ResetMapDownload();
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client/network", "loading done");
		SendReady();
	}
	else if(m_pMapdownloadTask) // fallback
	{
		ResetMapDownload();
		m_MapdownloadTotalsize = Prev;
		SendMapRequest();
	}
	else
	{
		if(m_MapdownloadFileTemp)
		{
			io_close(m_MapdownloadFileTemp);
			m_MapdownloadFileTemp = 0;
			Storage()->RemoveFile(m_aMapdownloadFilenameTemp, IStorage::TYPE_SAVE);
		}
		ResetMapDownload();
		DisconnectWithReason(pError);
	}
}

void CClient::ResetDDNetInfo()
{
	if(m_pDDNetInfoTask)
	{
		m_pDDNetInfoTask->Abort();
		m_pDDNetInfoTask = NULL;
	}
}

bool CClient::IsDDNetInfoChanged()
{
	IOHANDLE OldFile = m_pStorage->OpenFile(DDNET_INFO, IOFLAG_READ, IStorage::TYPE_SAVE);

	if(!OldFile)
		return true;

	IOHANDLE NewFile = m_pStorage->OpenFile(m_aDDNetInfoTmp, IOFLAG_READ, IStorage::TYPE_SAVE);

	if(NewFile)
	{
		char aOldData[4096];
		char aNewData[4096];
		unsigned OldBytes;
		unsigned NewBytes;

		do
		{
			OldBytes = io_read(OldFile, aOldData, sizeof(aOldData));
			NewBytes = io_read(NewFile, aNewData, sizeof(aNewData));

			if(OldBytes != NewBytes || mem_comp(aOldData, aNewData, OldBytes) != 0)
			{
				io_close(NewFile);
				io_close(OldFile);
				return true;
			}
		} while(OldBytes > 0);

		io_close(NewFile);
	}

	io_close(OldFile);
	return false;
}

void CClient::FinishDDNetInfo()
{
	ResetDDNetInfo();
	if(IsDDNetInfoChanged())
	{
		m_pStorage->RenameFile(m_aDDNetInfoTmp, DDNET_INFO, IStorage::TYPE_SAVE);
		LoadDDNetInfo();

		if(g_Config.m_UiPage == CMenus::PAGE_DDNET)
			m_ServerBrowser.Refresh(IServerBrowser::TYPE_DDNET);
		else if(g_Config.m_UiPage == CMenus::PAGE_KOG)
			m_ServerBrowser.Refresh(IServerBrowser::TYPE_KOG);
	}
	else
	{
		m_pStorage->RemoveFile(m_aDDNetInfoTmp, IStorage::TYPE_SAVE);
	}
}

typedef std::tuple<int, int, int> Version;
static const Version InvalidVersion = std::make_tuple(-1, -1, -1);

Version ToVersion(char *pStr)
{
	int version[3] = {0, 0, 0};
	const char *p = strtok(pStr, ".");

	for(int i = 0; i < 3 && p; ++i)
	{
		if(!str_isallnum(p))
			return InvalidVersion;

		version[i] = str_toint(p);
		p = strtok(NULL, ".");
	}

	if(p)
		return InvalidVersion;

	return std::make_tuple(version[0], version[1], version[2]);
}

void CClient::LoadDDNetInfo()
{
	const json_value *pDDNetInfo = m_ServerBrowser.LoadDDNetInfo();

	if(!pDDNetInfo)
		return;

	const json_value *pVersion = json_object_get(pDDNetInfo, "version");
	if(pVersion->type == json_string)
	{
		char aNewVersionStr[64];
		str_copy(aNewVersionStr, json_string_get(pVersion), sizeof(aNewVersionStr));
		char aCurVersionStr[64];
		str_copy(aCurVersionStr, GAME_RELEASE_VERSION, sizeof(aCurVersionStr));
		if(ToVersion(aNewVersionStr) > ToVersion(aCurVersionStr))
		{
			str_copy(m_aVersionStr, json_string_get(pVersion), sizeof(m_aVersionStr));
		}
		else
		{
			m_aVersionStr[0] = '0';
			m_aVersionStr[1] = '\0';
		}
	}

	const json_value *pNews = json_object_get(pDDNetInfo, "news");
	if(pNews->type == json_string)
	{
		const char *pNewsString = json_string_get(pNews);

		// Only mark news button if something new was added to the news
		if(m_aNews[0] && str_find(m_aNews, pNewsString) == nullptr)
			g_Config.m_UiUnreadNews = true;

		str_copy(m_aNews, pNewsString, sizeof(m_aNews));
	}

	const json_value *pMapDownloadUrl = json_object_get(pDDNetInfo, "map-download-url");
	if(pMapDownloadUrl->type == json_string)
	{
		const char *pMapDownloadUrlString = json_string_get(pMapDownloadUrl);
		str_copy(m_aMapDownloadUrl, pMapDownloadUrlString, sizeof(m_aMapDownloadUrl));
	}

	const json_value *pPoints = json_object_get(pDDNetInfo, "points");
	if(pPoints->type == json_integer)
		m_Points = pPoints->u.integer;
}

void CClient::PumpNetwork()
{
	for(auto &NetClient : m_NetClient)
	{
		NetClient.Update();
	}

	if(State() != IClient::STATE_DEMOPLAYBACK)
	{
		// check for errors
		if(State() != IClient::STATE_OFFLINE && State() < IClient::STATE_QUITTING && m_NetClient[CLIENT_MAIN].State() == NETSTATE_OFFLINE)
		{
			SetState(IClient::STATE_OFFLINE);
			Disconnect();
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "offline error='%s'", m_NetClient[CLIENT_MAIN].ErrorString());
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ClientNetworkErrPrintColor);
		}

		if(State() != IClient::STATE_OFFLINE && State() < IClient::STATE_QUITTING && m_DummyConnected &&
			m_NetClient[CLIENT_DUMMY].State() == NETSTATE_OFFLINE)
		{
			DummyDisconnect(0);
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "offline dummy error='%s'", m_NetClient[CLIENT_DUMMY].ErrorString());
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ClientNetworkErrPrintColor);
		}

		//
		if(State() == IClient::STATE_CONNECTING && m_NetClient[CLIENT_MAIN].State() == NETSTATE_ONLINE)
		{
			// we switched to online
			m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", "connected, sending info", ClientNetworkPrintColor);
			SetState(IClient::STATE_LOADING);
			SendInfo();
		}
	}

	// process packets
	CNetChunk Packet;
	for(int i = 0; i < NUM_CLIENTS; i++)
	{
		while(m_NetClient[i].Recv(&Packet))
		{
			if(Packet.m_ClientID == -1 || i > 1)
			{
				ProcessConnlessPacket(&Packet);
			}
			else if(i > 0 && i < 2)
			{
				if(g_Config.m_ClDummy)
					ProcessServerPacket(&Packet); //self
				else
					ProcessServerPacketDummy(&Packet); //multiclient
			}
			else
			{
				if(g_Config.m_ClDummy)
					ProcessServerPacketDummy(&Packet); //multiclient
				else
					ProcessServerPacket(&Packet); //self
			}
		}
	}
}

void CClient::OnDemoPlayerSnapshot(void *pData, int Size)
{
	// update ticks, they could have changed
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	CSnapshotStorage::CHolder *pTemp;
	m_CurGameTick[g_Config.m_ClDummy] = pInfo->m_Info.m_CurrentTick;
	m_PrevGameTick[g_Config.m_ClDummy] = pInfo->m_PreviousTick;

	// handle snapshots
	pTemp = m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = pTemp;

	mem_copy(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pSnap, pData, Size);
	mem_copy(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pAltSnap, pData, Size);

	GameClient()->OnNewSnapshot();
}

void CClient::OnDemoPlayerMessage(void *pData, int Size)
{
	CUnpacker Unpacker;
	Unpacker.Reset(pData, Size);

	// unpack msgid and system flag
	int Msg = Unpacker.GetInt();
	int Sys = Msg & 1;
	Msg >>= 1;

	if(Unpacker.Error())
		return;

	if(!Sys)
		GameClient()->OnMessage(Msg, &Unpacker);
}
/*
const IDemoPlayer::CInfo *client_demoplayer_getinfo()
{
	static DEMOPLAYBACK_INFO ret;
	const DEMOREC_PLAYBACKINFO *info = m_DemoPlayer.Info();
	ret.first_tick = info->first_tick;
	ret.last_tick = info->last_tick;
	ret.current_tick = info->current_tick;
	ret.paused = info->paused;
	ret.speed = info->speed;
	return &ret;
}*/

/*
void DemoPlayer()->SetPos(float percent)
{
	demorec_playback_set(percent);
}

void DemoPlayer()->SetSpeed(float speed)
{
	demorec_playback_setspeed(speed);
}

void DemoPlayer()->SetPause(int paused)
{
	if(paused)
		demorec_playback_pause();
	else
		demorec_playback_unpause();
}*/

void CClient::UpdateDemoIntraTimers()
{
	// update timers
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	m_CurGameTick[g_Config.m_ClDummy] = pInfo->m_Info.m_CurrentTick;
	m_PrevGameTick[g_Config.m_ClDummy] = pInfo->m_PreviousTick;
	m_GameIntraTick[g_Config.m_ClDummy] = pInfo->m_IntraTick;
	m_GameTickTime[g_Config.m_ClDummy] = pInfo->m_TickTime;
	m_GameIntraTickSincePrev[g_Config.m_ClDummy] = pInfo->m_IntraTickSincePrev;
};

void CClient::Update()
{
	if(State() == IClient::STATE_DEMOPLAYBACK)
	{
#if defined(CONF_VIDEORECORDER)
		if(m_DemoPlayer.IsPlaying() && IVideo::Current())
		{
			if(IVideo::Current()->FrameRendered())
				IVideo::Current()->NextVideoFrame();
			if(IVideo::Current()->AudioFrameRendered())
				IVideo::Current()->NextAudioFrameTimeline();
		}
		else if(m_ButtonRender)
			Disconnect();
#endif

		m_DemoPlayer.Update();

		if(m_DemoPlayer.IsPlaying())
		{
			// update timers
			const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
			m_CurGameTick[g_Config.m_ClDummy] = pInfo->m_Info.m_CurrentTick;
			m_PrevGameTick[g_Config.m_ClDummy] = pInfo->m_PreviousTick;
			m_GameIntraTick[g_Config.m_ClDummy] = pInfo->m_IntraTick;
			m_GameTickTime[g_Config.m_ClDummy] = pInfo->m_TickTime;
		}
		else
		{
			// disconnect on error
			Disconnect();
		}
	}
	else if(State() == IClient::STATE_ONLINE)
	{
		if(m_LastDummy != (bool)g_Config.m_ClDummy)
		{
			// Invalidate references to !m_ClDummy snapshots
			GameClient()->InvalidateSnapshot();
			GameClient()->OnDummySwap();
		}

		if(m_ReceivedSnapshots[!g_Config.m_ClDummy] >= 3)
		{
			// switch dummy snapshot
			int64_t Now = m_GameTime[!g_Config.m_ClDummy].Get(time_get());
			while(1)
			{
				CSnapshotStorage::CHolder *pCur = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT];
				int64_t TickStart = (pCur->m_Tick) * time_freq() / 50;

				if(TickStart < Now)
				{
					CSnapshotStorage::CHolder *pNext = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;
					if(pNext)
					{
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT];
						m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT] = pNext;

						// set ticks
						m_CurGameTick[!g_Config.m_ClDummy] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
						m_PrevGameTick[!g_Config.m_ClDummy] = m_aSnapshots[!g_Config.m_ClDummy][SNAP_PREV]->m_Tick;
					}
					else
						break;
				}
				else
					break;
			}
		}

		if(m_ReceivedSnapshots[g_Config.m_ClDummy] >= 3)
		{
			// switch snapshot
			int Repredict = 0;
			int64_t Freq = time_freq();
			int64_t Now = m_GameTime[g_Config.m_ClDummy].Get(time_get());
			int64_t PredNow = m_PredictedTime.Get(time_get());

			if(m_LastDummy != (bool)g_Config.m_ClDummy && m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV])
			{
				// Load snapshot for m_ClDummy
				GameClient()->OnNewSnapshot();
				Repredict = 1;
			}

			while(1)
			{
				CSnapshotStorage::CHolder *pCur = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
				int64_t TickStart = (pCur->m_Tick) * time_freq() / 50;

				if(TickStart < Now)
				{
					CSnapshotStorage::CHolder *pNext = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pNext;
					if(pNext)
					{
						m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT];
						m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = pNext;

						// set ticks
						m_CurGameTick[g_Config.m_ClDummy] = m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick;
						m_PrevGameTick[g_Config.m_ClDummy] = m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick;

						if(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV])
						{
							GameClient()->OnNewSnapshot();
							Repredict = 1;
						}
					}
					else
						break;
				}
				else
					break;
			}

			if(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] && m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV])
			{
				int64_t CurtickStart = (m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick) * time_freq() / 50;
				int64_t PrevtickStart = (m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick) * time_freq() / 50;
				int PrevPredTick = (int)(PredNow * 50 / time_freq());
				int NewPredTick = PrevPredTick + 1;

				m_GameIntraTick[g_Config.m_ClDummy] = (Now - PrevtickStart) / (float)(CurtickStart - PrevtickStart);
				m_GameTickTime[g_Config.m_ClDummy] = (Now - PrevtickStart) / (float)Freq; //(float)SERVER_TICK_SPEED);
				m_GameIntraTickSincePrev[g_Config.m_ClDummy] = (Now - PrevtickStart) / (float)(Freq / SERVER_TICK_SPEED);

				CurtickStart = NewPredTick * time_freq() / 50;
				PrevtickStart = PrevPredTick * time_freq() / 50;
				m_PredIntraTick[g_Config.m_ClDummy] = (PredNow - PrevtickStart) / (float)(CurtickStart - PrevtickStart);

				if(NewPredTick < m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick - SERVER_TICK_SPEED || NewPredTick > m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick + SERVER_TICK_SPEED)
				{
					m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", "prediction time reset!");
					m_PredictedTime.Init(m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick * time_freq() / 50);
				}

				if(NewPredTick > m_PredTick[g_Config.m_ClDummy])
				{
					m_PredTick[g_Config.m_ClDummy] = NewPredTick;
					Repredict = 1;

					// send input
					SendInput();
				}
			}

			// only do sane predictions
			if(Repredict)
			{
				if(m_PredTick[g_Config.m_ClDummy] > m_CurGameTick[g_Config.m_ClDummy] && m_PredTick[g_Config.m_ClDummy] < m_CurGameTick[g_Config.m_ClDummy] + 50)
					GameClient()->OnPredict();
			}

			// fetch server info if we don't have it
			if(State() >= IClient::STATE_LOADING &&
				m_CurrentServerInfoRequestTime >= 0 &&
				time_get() > m_CurrentServerInfoRequestTime)
			{
				m_ServerBrowser.RequestCurrentServer(m_ServerAddress);
				m_CurrentServerInfoRequestTime = time_get() + time_freq() * 2;
			}

			// periodically ping server
			if(State() == IClient::STATE_ONLINE &&
				m_CurrentServerNextPingTime >= 0 &&
				time_get() > m_CurrentServerNextPingTime)
			{
				int64_t Now = time_get();
				int64_t Freq = time_freq();

				char aBuf[64];
				str_format(aBuf, sizeof(aBuf), "pinging current server%s", !m_ServerCapabilities.m_PingEx ? ", using fallback via server info" : "");
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_ADDINFO, "client", aBuf);

				m_CurrentServerPingUuid = RandomUuid();
				if(!m_ServerCapabilities.m_PingEx)
				{
					m_ServerBrowser.RequestCurrentServerWithRandomToken(m_ServerAddress, &m_CurrentServerPingBasicToken, &m_CurrentServerPingToken);
				}
				else
				{
					CMsgPacker Msg(NETMSG_PINGEX, true);
					Msg.AddRaw(&m_CurrentServerPingUuid, sizeof(m_CurrentServerPingUuid));
					SendMsg(&Msg, MSGFLAG_FLUSH);
				}
				m_CurrentServerCurrentPingTime = Now;
				m_CurrentServerNextPingTime = Now + 600 * Freq; // ping every 10 minutes
			}
		}

		m_LastDummy = (bool)g_Config.m_ClDummy;
	}

	// STRESS TEST: join the server again
#ifdef CONF_DEBUG
	if(g_Config.m_DbgStress)
	{
		static int64_t ActionTaken = 0;
		int64_t Now = time_get();
		if(State() == IClient::STATE_OFFLINE)
		{
			if(Now > ActionTaken + time_freq() * 2)
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "reconnecting!");
				Connect(g_Config.m_DbgStressServer);
				ActionTaken = Now;
			}
		}
		else
		{
			if(Now > ActionTaken + time_freq() * (10 + g_Config.m_DbgStress))
			{
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "stress", "disconnecting!");
				Disconnect();
				ActionTaken = Now;
			}
		}
	}
#endif

	// pump the network
	PumpNetwork();

	if(m_pMapdownloadTask)
	{
		if(m_pMapdownloadTask->State() == HTTP_DONE)
			FinishMapDownload();
		else if(m_pMapdownloadTask->State() == HTTP_ERROR)
		{
			dbg_msg("webdl", "http failed, falling back to gameserver");
			ResetMapDownload();
			SendMapRequest();
		}
		else if(m_pMapdownloadTask->State() == HTTP_ABORTED)
		{
			m_pMapdownloadTask = NULL;
		}
	}

	if(m_pDDNetInfoTask)
	{
		if(m_pDDNetInfoTask->State() == HTTP_DONE)
			FinishDDNetInfo();
		else if(m_pDDNetInfoTask->State() == HTTP_ERROR)
		{
			Storage()->RemoveFile(m_aDDNetInfoTmp, IStorage::TYPE_SAVE);
			ResetDDNetInfo();
		}
		else if(m_pDDNetInfoTask->State() == HTTP_ABORTED)
		{
			Storage()->RemoveFile(m_aDDNetInfoTmp, IStorage::TYPE_SAVE);
			m_pDDNetInfoTask = NULL;
		}
	}

	if(State() == IClient::STATE_ONLINE)
	{
		if(m_EditJobs.size() > 0)
		{
			std::shared_ptr<CDemoEdit> e = m_EditJobs.front();
			if(e->Status() == IJob::STATE_DONE)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "Successfully saved the replay to %s!", e->Destination());
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", aBuf);

				GameClient()->Echo(Localize("Successfully saved the replay!"));

				m_EditJobs.pop_front();
			}
		}
	}

	// update the server browser
	m_ServerBrowser.Update(m_ResortServerBrowser);
	m_ResortServerBrowser = false;

	// update gameclient
	if(!m_EditorActive)
		GameClient()->OnUpdate();

	Discord()->Update();
	Steam()->Update();
	if(Steam()->GetConnectAddress())
	{
		char aAddress[NETADDR_MAXSTRSIZE];
		net_addr_str(Steam()->GetConnectAddress(), aAddress, sizeof(aAddress), true);
		Connect(aAddress);
		Steam()->ClearConnectAddress();
	}

	if(m_ReconnectTime > 0 && time_get() > m_ReconnectTime)
	{
		if(State() != STATE_ONLINE)
			Connect(m_aServerAddressStr);
		m_ReconnectTime = 0;
	}
}

void CClient::RegisterInterfaces()
{
	Kernel()->RegisterInterface(static_cast<IDemoRecorder *>(&m_DemoRecorder[RECORDER_MANUAL]), false);
	Kernel()->RegisterInterface(static_cast<IDemoPlayer *>(&m_DemoPlayer), false);
	Kernel()->RegisterInterface(static_cast<IGhostRecorder *>(&m_GhostRecorder), false);
	Kernel()->RegisterInterface(static_cast<IGhostLoader *>(&m_GhostLoader), false);
	Kernel()->RegisterInterface(static_cast<IServerBrowser *>(&m_ServerBrowser), false);
#if defined(CONF_AUTOUPDATE)
	Kernel()->RegisterInterface(static_cast<IUpdater *>(&m_Updater), false);
#endif
	Kernel()->RegisterInterface(static_cast<IFriends *>(&m_Friends), false);
	Kernel()->ReregisterInterface(static_cast<IFriends *>(&m_Foes));
}

void CClient::InitInterfaces()
{
	// fetch interfaces
	m_pEngine = Kernel()->RequestInterface<IEngine>();
	m_pEditor = Kernel()->RequestInterface<IEditor>();
	//m_pGraphics = Kernel()->RequestInterface<IEngineGraphics>();
	m_pSound = Kernel()->RequestInterface<IEngineSound>();
	m_pGameClient = Kernel()->RequestInterface<IGameClient>();
	m_pInput = Kernel()->RequestInterface<IEngineInput>();
	m_pMap = Kernel()->RequestInterface<IEngineMap>();
	m_pConfigManager = Kernel()->RequestInterface<IConfigManager>();
	m_pConfig = m_pConfigManager->Values();
#if defined(CONF_AUTOUPDATE)
	m_pUpdater = Kernel()->RequestInterface<IUpdater>();
#endif
	m_pDiscord = Kernel()->RequestInterface<IDiscord>();
	m_pSteam = Kernel()->RequestInterface<ISteam>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();

	m_DemoEditor.Init(m_pGameClient->NetVersion(), &m_SnapshotDelta, m_pConsole, m_pStorage);

	m_ServerBrowser.SetBaseInfo(&m_NetClient[CLIENT_CONTACT], m_pGameClient->NetVersion());

	HttpInit(m_pStorage);

#if defined(CONF_AUTOUPDATE)
	m_Updater.Init();
#endif

	m_Friends.Init();
	m_Foes.Init(true);

	m_GhostRecorder.Init();
	m_GhostLoader.Init();
}

void CClient::Run()
{
	m_LocalStartTime = time_get();
#if defined(CONF_VIDEORECORDER)
	IVideo::SetLocalStartTime(m_LocalStartTime);
#endif
	m_SnapshotParts[0] = 0;
	m_SnapshotParts[1] = 0;

	if(m_GenerateTimeoutSeed)
	{
		GenerateTimeoutSeed();
	}

	unsigned int Seed;
	secure_random_fill(&Seed, sizeof(Seed));
	srand(Seed);

	if(g_Config.m_Debug)
	{
		g_UuidManager.DebugDump();
	}

	// init SDL
	{
		if(SDL_Init(0) < 0)
		{
			dbg_msg("client", "unable to init SDL base: %s", SDL_GetError());
			return;
		}

#ifndef CONF_PLATFORM_ANDROID
		atexit(SDL_Quit); // ignore_convention
#endif
	}

	// init graphics
	{
		m_pGraphics = CreateEngineGraphicsThreaded();

		bool RegisterFail = false;
		RegisterFail = RegisterFail || !Kernel()->RegisterInterface(m_pGraphics); // IEngineGraphics
		RegisterFail = RegisterFail || !Kernel()->RegisterInterface(static_cast<IGraphics *>(m_pGraphics), false);

		if(RegisterFail || m_pGraphics->Init() != 0)
		{
			dbg_msg("client", "couldn't init graphics");
			return;
		}
	}

	// make sure the first frame just clears everything to prevent undesired colors when waiting for io
	Graphics()->Clear(0, 0, 0);
	Graphics()->Swap();

	// init sound, allowed to fail
	m_SoundInitFailed = Sound()->Init() != 0;

#if defined(CONF_VIDEORECORDER)
	// init video recorder aka ffmpeg
	CVideo::Init();
#endif

	// open socket
	{
		NETADDR BindAddr;
		if(g_Config.m_Bindaddr[0] && net_host_lookup(g_Config.m_Bindaddr, &BindAddr, NETTYPE_ALL) == 0)
		{
			// got bindaddr
			BindAddr.type = NETTYPE_ALL;
		}
		else
		{
			mem_zero(&BindAddr, sizeof(BindAddr));
			BindAddr.type = NETTYPE_ALL;
		}
		for(auto &NetClient : m_NetClient)
		{
			do
			{
				BindAddr.port = (secure_rand() % 64511) + 1024;
			} while(!NetClient.Open(BindAddr, 0));
		}
	}

	// init font rendering
	Kernel()->RequestInterface<IEngineTextRender>()->Init();

	// init the input
	Input()->Init();

	// init the editor
	m_pEditor->Init();

	// load and save a map to fix it
	/*if(m_pEditor->Load(arg, IStorage::TYPE_ALL))
		m_pEditor->Save(arg);
	return;*/

	// load data
	if(!LoadData())
		return;

	if(Steam()->GetPlayerName())
	{
		str_copy(g_Config.m_SteamName, Steam()->GetPlayerName(), sizeof(g_Config.m_SteamName));
	}

	GameClient()->OnInit();
	m_ServerBrowser.OnInit();

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "version %s", GameClient()->NetVersion());
	m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf, ColorRGBA(0.7f, 0.7f, 1, 1.0f));

	// connect to the server if wanted
	/*
	if(config.cl_connect[0] != 0)
		Connect(config.cl_connect);
	config.cl_connect[0] = 0;
	*/

	//
	m_FpsGraph.Init(0.0f, 120.0f);

	// never start with the editor
	g_Config.m_ClEditor = 0;

	// process pending commands
	m_pConsole->StoreCommands(false);

#if defined(CONF_FAMILY_UNIX)
	m_Fifo.Init(m_pConsole, g_Config.m_ClInputFifo, CFGFLAG_CLIENT);
#endif

	// loads the existing ddnet info file if it exists
	LoadDDNetInfo();
	// but still request the new one from server
	if(g_Config.m_ClShowWelcome)
		g_Config.m_ClShowWelcome = 0;
	else
		RequestDDNetInfo();

	bool LastD = false;
	bool LastE = false;
	bool LastG = false;

	int64_t LastTime = time_get_microseconds();
	int64_t LastRenderTime = time_get();

	while(1)
	{
		set_new_tick();

		// handle pending connects
		if(m_aCmdConnect[0])
		{
			str_copy(g_Config.m_UiServerAddress, m_aCmdConnect, sizeof(g_Config.m_UiServerAddress));
			Connect(m_aCmdConnect);
			m_aCmdConnect[0] = 0;
		}

		// handle pending demo play
		if(m_aCmdPlayDemo[0])
		{
			const char *pError = DemoPlayer_Play(m_aCmdPlayDemo, IStorage::TYPE_ABSOLUTE);
			if(pError)
				dbg_msg("demo_player", "playing passed demo file '%s' failed: %s", m_aCmdPlayDemo, pError);
			m_aCmdPlayDemo[0] = 0;
		}

		// handle pending map edits
		if(m_aCmdEditMap[0])
		{
			int Result = m_pEditor->Load(m_aCmdEditMap, IStorage::TYPE_ABSOLUTE);
			if(Result)
				g_Config.m_ClEditor = true;
			else
				dbg_msg("demo_player", "editing passed map file '%s' failed", m_aCmdEditMap);
			m_aCmdEditMap[0] = 0;
		}

		// progress on dummy connect if security token handshake skipped/passed
		if(m_DummySendConnInfo && !m_NetClient[CLIENT_DUMMY].SecurityTokenUnknown())
		{
			m_DummySendConnInfo = false;

			// send client info
			CMsgPacker MsgVer(NETMSG_CLIENTVER, true);
			MsgVer.AddRaw(&m_ConnectionID, sizeof(m_ConnectionID));
			MsgVer.AddInt(GameClient()->DDNetVersion());
			MsgVer.AddString(GameClient()->DDNetVersionStr(), 0);
			SendMsgY(&MsgVer, MSGFLAG_VITAL, 1);

			CMsgPacker MsgInfo(NETMSG_INFO, true);
			MsgInfo.AddString(GameClient()->NetVersion(), 128);
			MsgInfo.AddString(m_Password, 128);
			SendMsgY(&MsgInfo, MSGFLAG_VITAL | MSGFLAG_FLUSH, 1);

			// update netclient
			m_NetClient[CLIENT_DUMMY].Update();

			// send ready
			CMsgPacker MsgReady(NETMSG_READY, true);
			SendMsgY(&MsgReady, MSGFLAG_VITAL | MSGFLAG_FLUSH, 1);

			// startinfo
			GameClient()->SendDummyInfo(true);

			// send enter game an finish the connection
			CMsgPacker MsgEnter(NETMSG_ENTERGAME, true);
			SendMsgY(&MsgEnter, MSGFLAG_VITAL | MSGFLAG_FLUSH, 1);
		}

		// update input
		if(Input()->Update())
		{
			if(State() == IClient::STATE_QUITTING)
				break;
			else
				SetState(IClient::STATE_QUITTING); // SDL_QUIT
		}
#if defined(CONF_AUTOUPDATE)
		Updater()->Update();
#endif

		// update sound
		Sound()->Update();

		if(CtrlShiftKey(KEY_D, LastD))
			g_Config.m_Debug ^= 1;

		if(CtrlShiftKey(KEY_G, LastG))
			g_Config.m_DbgGraphs ^= 1;

		if(CtrlShiftKey(KEY_E, LastE))
		{
			g_Config.m_ClEditor = g_Config.m_ClEditor ^ 1;
			Input()->MouseModeRelative();
			Input()->SetIMEState(true);
		}

		// render
		{
			if(g_Config.m_ClEditor)
			{
				if(!m_EditorActive)
				{
					Input()->MouseModeRelative();
					GameClient()->OnActivateEditor();
					m_pEditor->ResetMentions();
					m_EditorActive = true;
				}
			}
			else if(m_EditorActive)
				m_EditorActive = false;

			Update();
			int64_t Now = time_get();

			bool IsRenderActive = (g_Config.m_GfxBackgroundRender || m_pGraphics->WindowOpen());

			if(IsRenderActive &&
				(!g_Config.m_GfxAsyncRenderOld || m_pGraphics->IsIdle()) &&
				(!g_Config.m_GfxRefreshRate || (time_freq() / (int64_t)g_Config.m_GfxRefreshRate) <= Now - LastRenderTime))
			{
				m_RenderFrames++;

				// update frametime
				m_RenderFrameTime = (Now - m_LastRenderTime) / (float)time_freq();
				if(m_RenderFrameTime < m_RenderFrameTimeLow)
					m_RenderFrameTimeLow = m_RenderFrameTime;
				if(m_RenderFrameTime > m_RenderFrameTimeHigh)
					m_RenderFrameTimeHigh = m_RenderFrameTime;
				m_FpsGraph.Add(1.0f / m_RenderFrameTime, 1, 1, 1);

				if(m_BenchmarkFile)
				{
					char aBuf[64];
					str_format(aBuf, sizeof(aBuf), "Frametime %d us\n", (int)(m_RenderFrameTime * 1000000));
					io_write(m_BenchmarkFile, aBuf, str_length(aBuf));
					if(time_get() > m_BenchmarkStopTime)
					{
						io_close(m_BenchmarkFile);
						m_BenchmarkFile = 0;
						Quit();
					}
				}

				m_FrameTimeAvg = m_FrameTimeAvg * 0.9f + m_RenderFrameTime * 0.1f;

				// keep the overflow time - it's used to make sure the gfx refreshrate is reached
				int64_t AdditionalTime = g_Config.m_GfxRefreshRate ? ((Now - LastRenderTime) - (time_freq() / (int64_t)g_Config.m_GfxRefreshRate)) : 0;
				// if the value is over a second time loose, reset the additional time (drop the frames, that are lost already)
				if(AdditionalTime > time_freq())
					AdditionalTime = time_freq();
				LastRenderTime = Now - AdditionalTime;
				m_LastRenderTime = Now;

#ifdef CONF_DEBUG
				if(g_Config.m_DbgStress)
				{
					if((m_RenderFrames % 10) == 0)
					{
						if(!m_EditorActive)
							Render();
						else
						{
							m_pEditor->UpdateAndRender();
							DebugRender();
						}
						m_pGraphics->Swap();
					}
				}
				else
#endif
				{
					if(!m_EditorActive)
						Render();
					else
					{
						m_pEditor->UpdateAndRender();
						DebugRender();
					}
					m_pGraphics->Swap();
				}

				Input()->NextFrame();
			}
			else if(!IsRenderActive)
			{
				// if the client does not render, it should reset its render time to a time where it would render the first frame, when it wakes up again
				LastRenderTime = g_Config.m_GfxRefreshRate ? (Now - (time_freq() / (int64_t)g_Config.m_GfxRefreshRate)) : Now;
			}

			if(Input()->VideoRestartNeeded())
			{
				m_pGraphics->Init();
				LoadData();
				GameClient()->OnInit();
			}
		}

		AutoScreenshot_Cleanup();
		AutoStatScreenshot_Cleanup();
		AutoCSV_Cleanup();

		// check conditions
		if(State() == IClient::STATE_QUITTING || State() == IClient::STATE_RESTARTING)
		{
			static bool s_SavedConfig = false;
			if(!s_SavedConfig)
			{
				// write down the config and quit
				if(!m_pConfigManager->Save())
					m_Warnings.emplace_back(SWarning(Localize("Saving ddnet-settings.cfg failed")));
				s_SavedConfig = true;
			}

			IOHANDLE File = m_pStorage->OpenFile(m_aDDNetInfoTmp, IOFLAG_READ, IStorage::TYPE_SAVE);
			if(File)
			{
				io_close(File);
				m_pStorage->RemoveFile(m_aDDNetInfoTmp, IStorage::TYPE_SAVE);
			}

			if(m_Warnings.empty() && !GameClient()->IsDisplayingWarning())
				break;
		}

#if defined(CONF_FAMILY_UNIX)
		m_Fifo.Update();
#endif

		// beNice
		int64_t Now = time_get_microseconds();
		int64_t SleepTimeInMicroSeconds = 0;
		bool Slept = false;
		if(
#ifdef CONF_DEBUG
			g_Config.m_DbgStress ||
#endif
			(g_Config.m_ClRefreshRateInactive && !m_pGraphics->WindowActive()))
		{
			SleepTimeInMicroSeconds = ((int64_t)1000000 / (int64_t)g_Config.m_ClRefreshRateInactive) - (Now - LastTime);
			if(SleepTimeInMicroSeconds / (int64_t)1000 > (int64_t)0)
				thread_sleep(SleepTimeInMicroSeconds);
			Slept = true;
		}
		else if(g_Config.m_ClRefreshRate)
		{
			SleepTimeInMicroSeconds = ((int64_t)1000000 / (int64_t)g_Config.m_ClRefreshRate) - (Now - LastTime);
			if(SleepTimeInMicroSeconds > (int64_t)0)
				net_socket_read_wait(m_NetClient[CLIENT_MAIN].m_Socket, SleepTimeInMicroSeconds);
			Slept = true;
		}
		if(Slept)
		{
			// if the diff gets too small it shouldn't get even smaller (drop the updates, that could not be handled)
			if(SleepTimeInMicroSeconds < (int64_t)-1000000)
				SleepTimeInMicroSeconds = (int64_t)-1000000;
			// don't go higher than the game ticks speed, because the network is waking up the client with the server's snapshots anyway
			else if(SleepTimeInMicroSeconds > (int64_t)1000000 / m_GameTickSpeed)
				SleepTimeInMicroSeconds = (int64_t)1000000 / m_GameTickSpeed;
			// the time diff between the time that was used actually used and the time the thread should sleep/wait
			// will be calculated in the sleep time of the next update tick by faking the time it should have slept/wait.
			// so two cases (and the case it slept exactly the time it should):
			//	- the thread slept/waited too long, then it adjust the time to sleep/wait less in the next update tick
			//	- the thread slept/waited too less, then it adjust the time to sleep/wait more in the next update tick
			LastTime = Now + SleepTimeInMicroSeconds;
		}
		else
			LastTime = Now;

		if(g_Config.m_DbgHitch)
		{
			thread_sleep(g_Config.m_DbgHitch * 1000);
			g_Config.m_DbgHitch = 0;
		}

		// update local time
		m_LocalTime = (time_get() - m_LocalStartTime) / (float)time_freq();
	}

#if defined(CONF_FAMILY_UNIX)
	m_Fifo.Shutdown();
#endif

	GameClient()->OnShutdown();
	Disconnect();

	delete m_pEditor;
	m_pGraphics->Shutdown();

	// shutdown SDL
	SDL_Quit();
}

bool CClient::CtrlShiftKey(int Key, bool &Last)
{
	if(Input()->KeyIsPressed(KEY_LCTRL) && Input()->KeyIsPressed(KEY_LSHIFT) && !Last && Input()->KeyIsPressed(Key))
	{
		Last = true;
		return true;
	}
	else if(Last && !Input()->KeyIsPressed(Key))
		Last = false;

	return false;
}

void CClient::Con_Connect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	str_copy(pSelf->m_aCmdConnect, pResult->GetString(0), sizeof(pSelf->m_aCmdConnect));
}

void CClient::Con_Disconnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Disconnect();
}

void CClient::Con_DummyConnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DummyConnect();
}

void CClient::Con_DummyDisconnect(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DummyDisconnect(0);
}

void CClient::Con_DummyResetInput(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->GameClient()->DummyResetInput();
}

void CClient::Con_Quit(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Quit();
}

void CClient::Con_Minimize(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->Minimize();
}

void CClient::Con_Ping(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;

	CMsgPacker Msg(NETMSG_PING, true);
	pSelf->SendMsg(&Msg, 0);
	pSelf->m_PingStartTime = time_get();
}

void CClient::AutoScreenshot_Start()
{
	if(g_Config.m_ClAutoScreenshot)
	{
		Graphics()->TakeScreenshot("auto/autoscreen");
		m_AutoScreenshotRecycle = true;
	}
}

void CClient::AutoStatScreenshot_Start()
{
	if(g_Config.m_ClAutoStatboardScreenshot)
	{
		Graphics()->TakeScreenshot("auto/stats/autoscreen");
		m_AutoStatScreenshotRecycle = true;
	}
}

void CClient::AutoScreenshot_Cleanup()
{
	if(m_AutoScreenshotRecycle)
	{
		if(g_Config.m_ClAutoScreenshotMax)
		{
			// clean up auto taken screens
			CFileCollection AutoScreens;
			AutoScreens.Init(Storage(), "screenshots/auto", "autoscreen", ".png", g_Config.m_ClAutoScreenshotMax);
		}
		m_AutoScreenshotRecycle = false;
	}
}

void CClient::AutoStatScreenshot_Cleanup()
{
	if(m_AutoStatScreenshotRecycle)
	{
		if(g_Config.m_ClAutoStatboardScreenshotMax)
		{
			// clean up auto taken screens
			CFileCollection AutoScreens;
			AutoScreens.Init(Storage(), "screenshots/auto/stats", "autoscreen", ".png", g_Config.m_ClAutoStatboardScreenshotMax);
		}
		m_AutoStatScreenshotRecycle = false;
	}
}

void CClient::AutoCSV_Start()
{
	if(g_Config.m_ClAutoCSV)
		m_AutoCSVRecycle = true;
}

void CClient::AutoCSV_Cleanup()
{
	if(m_AutoCSVRecycle)
	{
		if(g_Config.m_ClAutoCSVMax)
		{
			// clean up auto csvs
			CFileCollection AutoRecord;
			AutoRecord.Init(Storage(), "record/csv", "autorecord", ".csv", g_Config.m_ClAutoCSVMax);
		}
		m_AutoCSVRecycle = false;
	}
}

void CClient::Con_Screenshot(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Graphics()->TakeScreenshot(0);
}

#if defined(CONF_VIDEORECORDER)

void CClient::Con_StartVideo(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;

	if(pSelf->State() != IClient::STATE_DEMOPLAYBACK)
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "videorecorder", "Can not start videorecorder outside of demoplayer.");

	if(!IVideo::Current())
	{
		new CVideo((CGraphics_Threaded *)pSelf->m_pGraphics, pSelf->Storage(), pSelf->m_pConsole, pSelf->Graphics()->ScreenWidth(), pSelf->Graphics()->ScreenHeight(), "");
		IVideo::Current()->Start();
		bool paused = pSelf->m_DemoPlayer.Info()->m_Info.m_Paused;
		if(paused)
			IVideo::Current()->Pause(true);
	}
	else
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "videorecorder", "Videorecorder already running.");
}

void CClient::StartVideo(IConsole::IResult *pResult, void *pUserData, const char *pVideoName)
{
	CClient *pSelf = (CClient *)pUserData;

	if(pSelf->State() != IClient::STATE_DEMOPLAYBACK)
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "videorecorder", "Can not start videorecorder outside of demoplayer.");

	pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "demo_render", pVideoName);
	if(!IVideo::Current())
	{
		new CVideo((CGraphics_Threaded *)pSelf->m_pGraphics, pSelf->Storage(), pSelf->m_pConsole, pSelf->Graphics()->ScreenWidth(), pSelf->Graphics()->ScreenHeight(), pVideoName);
		IVideo::Current()->Start();
	}
	else
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "videorecorder", "Videorecorder already running.");
}

void CClient::Con_StopVideo(IConsole::IResult *pResult, void *pUserData)
{
	if(IVideo::Current())
		IVideo::Current()->Stop();
}

#endif

void CClient::Con_Rcon(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->Rcon(pResult->GetString(0));
}

void CClient::Con_RconAuth(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->RconAuth("", pResult->GetString(0));
}

void CClient::Con_RconLogin(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->RconAuth(pResult->GetString(0), pResult->GetString(1));
}

void CClient::Con_AddFavorite(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) != 0)
	{
		char aBuf[128];
		str_format(aBuf, sizeof(aBuf), "invalid address '%s'", pResult->GetString(0));
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "client", aBuf);
		return;
	}
	pSelf->m_ServerBrowser.AddFavorite(Addr);
	if(pResult->NumArguments() > 1 && str_find(pResult->GetString(1), "allow_ping"))
	{
		pSelf->m_ServerBrowser.FavoriteAllowPing(Addr, true);
	}
}

void CClient::Con_RemoveFavorite(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	NETADDR Addr;
	if(net_addr_from_str(&Addr, pResult->GetString(0)) == 0)
		pSelf->m_ServerBrowser.RemoveFavorite(Addr);
}

void CClient::DemoSliceBegin()
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	g_Config.m_ClDemoSliceBegin = pInfo->m_Info.m_CurrentTick;
}

void CClient::DemoSliceEnd()
{
	const CDemoPlayer::CPlaybackInfo *pInfo = m_DemoPlayer.Info();
	g_Config.m_ClDemoSliceEnd = pInfo->m_Info.m_CurrentTick;
}

void CClient::Con_DemoSliceBegin(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoSliceBegin();
}

void CClient::Con_DemoSliceEnd(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoSliceEnd();
}

void CClient::Con_SaveReplay(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pResult->NumArguments())
	{
		int Length = pResult->GetInteger(0);
		if(Length <= 0)
			pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: length must be greater than 0 second.");
		else
			pSelf->SaveReplay(Length);
	}
	else
		pSelf->SaveReplay(g_Config.m_ClReplayLength);
}

void CClient::SaveReplay(const int Length)
{
	if(!g_Config.m_ClReplays)
	{
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "Feature is disabled. Please enable it via configuration.");
		GameClient()->Echo(Localize("Replay feature is disabled!"));
		return;
	}

	if(!DemoRecorder(RECORDER_REPLAYS)->IsRecording())
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: demorecorder isn't recording. Try to rejoin to fix that.");
	else if(DemoRecorder(RECORDER_REPLAYS)->Length() < 1)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "ERROR: demorecorder isn't recording for at least 1 second.");
	else
	{
		// First we stop the recorder to slice correctly the demo after
		DemoRecorder_Stop(RECORDER_REPLAYS);
		char aFilename[IO_MAX_PATH_LENGTH];

		char aDate[64];
		str_timestamp(aDate, sizeof(aDate));

		str_format(aFilename, sizeof(aFilename), "demos/replays/%s_%s (replay).demo", m_aCurrentMap, aDate);
		char *pSrc = (&m_DemoRecorder[RECORDER_REPLAYS])->GetCurrentFilename();

		// Slice the demo to get only the last cl_replay_length seconds
		const int EndTick = GameTick(g_Config.m_ClDummy);
		const int StartTick = EndTick - Length * GameTickSpeed();

		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "replay", "Saving replay...");

		// Create a job to do this slicing in background because it can be a bit long depending on the file size
		std::shared_ptr<CDemoEdit> pDemoEditTask = std::make_shared<CDemoEdit>(GameClient()->NetVersion(), &m_SnapshotDelta, m_pStorage, pSrc, aFilename, StartTick, EndTick);
		Engine()->AddJob(pDemoEditTask);
		m_EditJobs.push_back(pDemoEditTask);

		// And we restart the recorder
		DemoRecorder_StartReplayRecorder();
	}
}

void CClient::DemoSlice(const char *pDstPath, CLIENTFUNC_FILTER pfnFilter, void *pUser)
{
	if(m_DemoPlayer.IsPlaying())
	{
		const char *pDemoFileName = m_DemoPlayer.GetDemoFileName();
		m_DemoEditor.Slice(pDemoFileName, pDstPath, g_Config.m_ClDemoSliceBegin, g_Config.m_ClDemoSliceEnd, pfnFilter, pUser);
	}
}

const char *CClient::DemoPlayer_Play(const char *pFilename, int StorageType)
{
	int Crc;
	const char *pError;

	IOHANDLE File = Storage()->OpenFile(pFilename, IOFLAG_READ, StorageType);
	if(!File)
		return "error opening demo file";

	io_close(File);

	Disconnect();
	m_NetClient[CLIENT_MAIN].ResetErrorString();

	// try to start playback
	m_DemoPlayer.SetListener(this);

	if(m_DemoPlayer.Load(Storage(), m_pConsole, pFilename, StorageType))
		return "error loading demo";

	// load map
	Crc = m_DemoPlayer.GetMapInfo()->m_Crc;
	SHA256_DIGEST Sha = m_DemoPlayer.GetMapInfo()->m_Sha256;
	pError = LoadMapSearch(m_DemoPlayer.Info()->m_Header.m_aMapName, Sha != SHA256_ZEROED ? &Sha : nullptr, Crc);
	if(pError)
	{
		if(!m_DemoPlayer.ExtractMap(Storage()))
			return pError;

		Sha = m_DemoPlayer.GetMapInfo()->m_Sha256;
		pError = LoadMapSearch(m_DemoPlayer.Info()->m_Header.m_aMapName, &Sha, Crc);
		if(pError)
		{
			DisconnectWithReason(pError);
			return pError;
		}
	}

	GameClient()->OnConnected();

	// setup buffers
	mem_zero(m_aDemorecSnapshotData, sizeof(m_aDemorecSnapshotData));

	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT] = &m_aDemorecSnapshotHolders[SNAP_CURRENT];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV] = &m_aDemorecSnapshotHolders[SNAP_PREV];

	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pSnap = (CSnapshot *)m_aDemorecSnapshotData[SNAP_CURRENT][0];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_pAltSnap = (CSnapshot *)m_aDemorecSnapshotData[SNAP_CURRENT][1];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_SnapSize = 0;
	m_aSnapshots[g_Config.m_ClDummy][SNAP_CURRENT]->m_Tick = -1;

	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_pSnap = (CSnapshot *)m_aDemorecSnapshotData[SNAP_PREV][0];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_pAltSnap = (CSnapshot *)m_aDemorecSnapshotData[SNAP_PREV][1];
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_SnapSize = 0;
	m_aSnapshots[g_Config.m_ClDummy][SNAP_PREV]->m_Tick = -1;

	// enter demo playback state
	SetState(IClient::STATE_DEMOPLAYBACK);

	m_DemoPlayer.Play();
	GameClient()->OnEnterGame();

	return 0;
}

#if defined(CONF_VIDEORECORDER)
const char *CClient::DemoPlayer_Render(const char *pFilename, int StorageType, const char *pVideoName, int SpeedIndex)
{
	const char *pError;
	pError = DemoPlayer_Play(pFilename, StorageType);
	if(pError)
		return pError;
	m_ButtonRender = true;

	this->CClient::StartVideo(NULL, this, pVideoName);
	m_DemoPlayer.Play();
	m_DemoPlayer.SetSpeed(g_aSpeeds[SpeedIndex]);
	//m_pConsole->Print(IConsole::OUTPUT_LEVEL_DEBUG, "demo_recorder", "demo eof");
	return 0;
}
#endif

void CClient::Con_Play(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	const char *pError = pSelf->DemoPlayer_Play(pResult->GetString(0), IStorage::TYPE_ALL);
	if(pError)
		pSelf->m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demo_player", pError);
}

void CClient::Con_DemoPlay(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->m_DemoPlayer.IsPlaying())
	{
		if(pSelf->m_DemoPlayer.BaseInfo()->m_Paused)
		{
			pSelf->m_DemoPlayer.Unpause();
		}
		else
		{
			pSelf->m_DemoPlayer.Pause();
		}
	}
}

void CClient::Con_DemoSpeed(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->m_DemoPlayer.SetSpeed(pResult->GetFloat(0));
}

void CClient::DemoRecorder_Start(const char *pFilename, bool WithTimestamp, int Recorder)
{
	if(State() != IClient::STATE_ONLINE)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demorec/record", "client is not online");
	else
	{
		char aFilename[IO_MAX_PATH_LENGTH];
		if(WithTimestamp)
		{
			char aDate[20];
			str_timestamp(aDate, sizeof(aDate));
			str_format(aFilename, sizeof(aFilename), "demos/%s_%s.demo", pFilename, aDate);
		}
		else
			str_format(aFilename, sizeof(aFilename), "demos/%s.demo", pFilename);

		SHA256_DIGEST Sha256 = m_pMap->Sha256();
		m_DemoRecorder[Recorder].Start(Storage(), m_pConsole, aFilename, GameClient()->NetVersion(), m_aCurrentMap, &Sha256, m_pMap->Crc(), "client", m_pMap->MapSize(), 0, m_pMap->File());
	}
}

void CClient::DemoRecorder_HandleAutoStart()
{
	if(g_Config.m_ClAutoDemoRecord)
	{
		DemoRecorder_Stop(RECORDER_AUTO);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "auto/%s", m_aCurrentMap);
		DemoRecorder_Start(aBuf, true, RECORDER_AUTO);
		if(g_Config.m_ClAutoDemoMax)
		{
			// clean up auto recorded demos
			CFileCollection AutoDemos;
			AutoDemos.Init(Storage(), "demos/auto", "" /* empty for wild card */, ".demo", g_Config.m_ClAutoDemoMax);
		}
	}
	if(!DemoRecorder(RECORDER_REPLAYS)->IsRecording())
	{
		DemoRecorder_StartReplayRecorder();
	}
}

void CClient::DemoRecorder_StartReplayRecorder()
{
	if(g_Config.m_ClReplays)
	{
		DemoRecorder_Stop(RECORDER_REPLAYS);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "replays/replay_tmp-%s", m_aCurrentMap);
		DemoRecorder_Start(aBuf, true, RECORDER_REPLAYS);
	}
}

void CClient::DemoRecorder_Stop(int Recorder, bool RemoveFile)
{
	m_DemoRecorder[Recorder].Stop();
	if(RemoveFile)
	{
		const char *pFilename = (&m_DemoRecorder[Recorder])->GetCurrentFilename();
		if(pFilename[0] != '\0')
			Storage()->RemoveFile(pFilename, IStorage::TYPE_SAVE);
	}
}

void CClient::DemoRecorder_AddDemoMarker(int Recorder)
{
	m_DemoRecorder[Recorder].AddDemoMarker();
}

class IDemoRecorder *CClient::DemoRecorder(int Recorder)
{
	return &m_DemoRecorder[Recorder];
}

void CClient::Con_Record(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pResult->NumArguments())
		pSelf->DemoRecorder_Start(pResult->GetString(0), false, RECORDER_MANUAL);
	else
		pSelf->DemoRecorder_Start(pSelf->m_aCurrentMap, true, RECORDER_MANUAL);
}

void CClient::Con_StopRecord(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoRecorder_Stop(RECORDER_MANUAL);
}

void CClient::Con_AddDemoMarker(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pSelf->DemoRecorder_AddDemoMarker(RECORDER_MANUAL);
	pSelf->DemoRecorder_AddDemoMarker(RECORDER_RACE);
	pSelf->DemoRecorder_AddDemoMarker(RECORDER_AUTO);
	pSelf->DemoRecorder_AddDemoMarker(RECORDER_REPLAYS);
}

void CClient::Con_BenchmarkQuit(IConsole::IResult *pResult, void *pUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	int Seconds = pResult->GetInteger(0);
	const char *pFilename = pResult->GetString(1);
	pSelf->BenchmarkQuit(Seconds, pFilename);
}

void CClient::BenchmarkQuit(int Seconds, const char *pFilename)
{
	char aBuf[IO_MAX_PATH_LENGTH];
	m_BenchmarkFile = Storage()->OpenFile(pFilename, IOFLAG_WRITE, IStorage::TYPE_ABSOLUTE, aBuf, sizeof(aBuf));
	m_BenchmarkStopTime = time_get() + time_freq() * Seconds;
}

void CClient::ServerBrowserUpdate()
{
	m_ResortServerBrowser = true;
}

void CClient::ConchainServerBrowserUpdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		((CClient *)pUserData)->ServerBrowserUpdate();
}

void CClient::SwitchWindowScreen(int Index)
{
	// Todo SDL: remove this when fixed (changing screen when in fullscreen is bugged)
	if(g_Config.m_GfxFullscreen)
	{
		SetWindowParams(0, g_Config.m_GfxBorderless);
		if(Graphics()->SetWindowScreen(Index))
			g_Config.m_GfxScreen = Index;
		SetWindowParams(g_Config.m_GfxFullscreen, g_Config.m_GfxBorderless);
	}
	else
	{
		if(Graphics()->SetWindowScreen(Index))
			g_Config.m_GfxScreen = Index;
	}
}

void CClient::ConchainWindowScreen(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxScreen != pResult->GetInteger(0))
			pSelf->SwitchWindowScreen(pResult->GetInteger(0));
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CClient::SetWindowParams(int FullscreenMode, bool IsBorderless)
{
	g_Config.m_GfxFullscreen = clamp(FullscreenMode, 0, 2);
	g_Config.m_GfxBorderless = (int)IsBorderless;
	Graphics()->SetWindowParams(FullscreenMode, IsBorderless);
}

void CClient::ConchainFullscreen(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxFullscreen != pResult->GetInteger(0))
			pSelf->SetWindowParams(pResult->GetInteger(0), g_Config.m_GfxBorderless);
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CClient::ConchainWindowBordered(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(!g_Config.m_GfxFullscreen && (g_Config.m_GfxBorderless != pResult->GetInteger(0)))
			pSelf->SetWindowParams(g_Config.m_GfxFullscreen, !g_Config.m_GfxBorderless);
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CClient::ToggleWindowVSync()
{
	if(Graphics()->SetVSync(g_Config.m_GfxVsync ^ 1))
		g_Config.m_GfxVsync ^= 1;
}

void CClient::LoadFont()
{
	static CFont *pDefaultFont = 0;
	char aFilename[IO_MAX_PATH_LENGTH];
	char aBuff[1024];
	const char *pFontFile = "fonts/DejaVuSans.ttf";
	const char *apFallbackFontFiles[] =
		{
			"fonts/GlowSansJCompressed-Book.otf",
			"fonts/SourceHanSansSC-Regular.otf",
		};
	IOHANDLE File = Storage()->OpenFile(pFontFile, IOFLAG_READ, IStorage::TYPE_ALL, aFilename, sizeof(aFilename));
	if(File)
	{
		size_t Size = io_length(File);
		unsigned char *pBuf = (unsigned char *)malloc(Size);
		io_read(File, pBuf, Size);
		io_close(File);
		IEngineTextRender *pTextRender = Kernel()->RequestInterface<IEngineTextRender>();
		pDefaultFont = pTextRender->GetFont(aFilename);
		if(pDefaultFont == NULL)
			pDefaultFont = pTextRender->LoadFont(aFilename, pBuf, Size);

		for(auto &pFallbackFontFile : apFallbackFontFiles)
		{
			bool FontLoaded = false;
			File = Storage()->OpenFile(pFallbackFontFile, IOFLAG_READ, IStorage::TYPE_ALL, aFilename, sizeof(aFilename));
			if(File)
			{
				size_t Size = io_length(File);
				unsigned char *pBuf = (unsigned char *)malloc(Size);
				io_read(File, pBuf, Size);
				io_close(File);
				IEngineTextRender *pTextRender = Kernel()->RequestInterface<IEngineTextRender>();
				FontLoaded = pTextRender->LoadFallbackFont(pDefaultFont, aFilename, pBuf, Size);
			}

			if(!FontLoaded)
			{
				str_format(aBuff, sizeof(aBuff) / sizeof(aBuff[0]), "failed to load the fallback font. filename='%s'", pFallbackFontFile);
				m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "gameclient", aBuff);
			}
		}

		Kernel()->RequestInterface<IEngineTextRender>()->SetDefaultFont(pDefaultFont);
	}

	if(!pDefaultFont)
	{
		str_format(aBuff, sizeof(aBuff) / sizeof(aBuff[0]), "failed to load font. filename='%s'", pFontFile);
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "gameclient", aBuff);
	}
}

void CClient::Notify(const char *pTitle, const char *pMessage)
{
	if(m_pGraphics->WindowActive() || !g_Config.m_ClShowNotifications)
		return;

	NotificationsNotify(pTitle, pMessage);
	Graphics()->NotifyWindow();
}

void CClient::ConchainWindowVSync(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	if(pSelf->Graphics() && pResult->NumArguments())
	{
		if(g_Config.m_GfxVsync != pResult->GetInteger(0))
			pSelf->ToggleWindowVSync();
	}
	else
		pfnCallback(pResult, pCallbackUserData);
}

void CClient::ConchainTimeoutSeed(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
		pSelf->m_GenerateTimeoutSeed = false;
}

void CClient::ConchainPassword(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments() && pSelf->m_LocalStartTime) //won't set m_SendPassword before game has started
		pSelf->m_SendPassword = true;
}

void CClient::ConchainReplays(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	CClient *pSelf = (CClient *)pUserData;
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		int Status = pResult->GetInteger(0);
		if(Status == 0)
		{
			// stop recording and remove the tmp demo file
			pSelf->DemoRecorder_Stop(RECORDER_REPLAYS, true);
		}
		else
		{
			// start recording
			pSelf->DemoRecorder_HandleAutoStart();
		}
	}
}

void CClient::RegisterCommands()
{
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	// register server dummy commands for tab completion
	m_pConsole->Register("kick", "i[id] ?r[reason]", CFGFLAG_SERVER, 0, 0, "Kick player with specified id for any reason");
	m_pConsole->Register("ban", "s[ip|id] ?i[minutes] r[reason]", CFGFLAG_SERVER, 0, 0, "Ban player with ip/id for x minutes for any reason");
	m_pConsole->Register("unban", "r[ip]", CFGFLAG_SERVER, 0, 0, "Unban ip");
	m_pConsole->Register("bans", "", CFGFLAG_SERVER, 0, 0, "Show banlist");
	m_pConsole->Register("status", "?r[name]", CFGFLAG_SERVER, 0, 0, "List players containing name or all players");
	m_pConsole->Register("shutdown", "", CFGFLAG_SERVER, 0, 0, "Shut down");
	m_pConsole->Register("record", "r[file]", CFGFLAG_SERVER, 0, 0, "Record to a file");
	m_pConsole->Register("stoprecord", "", CFGFLAG_SERVER, 0, 0, "Stop recording");
	m_pConsole->Register("reload", "", CFGFLAG_SERVER, 0, 0, "Reload the map");

	m_pConsole->Register("dummy_connect", "", CFGFLAG_CLIENT, Con_DummyConnect, this, "Connect dummy");
	m_pConsole->Register("dummy_disconnect", "", CFGFLAG_CLIENT, Con_DummyDisconnect, this, "Disconnect dummy");
	m_pConsole->Register("dummy_reset", "", CFGFLAG_CLIENT, Con_DummyResetInput, this, "Reset dummy");

	m_pConsole->Register("quit", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Quit, this, "Quit Teeworlds");
	m_pConsole->Register("exit", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Quit, this, "Quit Teeworlds");
	m_pConsole->Register("minimize", "", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Minimize, this, "Minimize Teeworlds");
	m_pConsole->Register("connect", "r[host|ip]", CFGFLAG_CLIENT, Con_Connect, this, "Connect to the specified host/ip");
	m_pConsole->Register("disconnect", "", CFGFLAG_CLIENT, Con_Disconnect, this, "Disconnect from the server");
	m_pConsole->Register("ping", "", CFGFLAG_CLIENT, Con_Ping, this, "Ping the current server");
	m_pConsole->Register("screenshot", "", CFGFLAG_CLIENT, Con_Screenshot, this, "Take a screenshot");

#if defined(CONF_VIDEORECORDER)
	m_pConsole->Register("start_video", "", CFGFLAG_CLIENT, Con_StartVideo, this, "Start recording a video");
	m_pConsole->Register("stop_video", "", CFGFLAG_CLIENT, Con_StopVideo, this, "Stop recording a video");
#endif

	m_pConsole->Register("rcon", "r[rcon-command]", CFGFLAG_CLIENT, Con_Rcon, this, "Send specified command to rcon");
	m_pConsole->Register("rcon_auth", "r[password]", CFGFLAG_CLIENT, Con_RconAuth, this, "Authenticate to rcon");
	m_pConsole->Register("rcon_login", "s[username] r[password]", CFGFLAG_CLIENT, Con_RconLogin, this, "Authenticate to rcon with a username");
	m_pConsole->Register("play", "r[file]", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_Play, this, "Play the file specified");
	m_pConsole->Register("record", "?r[file]", CFGFLAG_CLIENT, Con_Record, this, "Record to the file");
	m_pConsole->Register("stoprecord", "", CFGFLAG_CLIENT, Con_StopRecord, this, "Stop recording");
	m_pConsole->Register("add_demomarker", "", CFGFLAG_CLIENT, Con_AddDemoMarker, this, "Add demo timeline marker");
	m_pConsole->Register("add_favorite", "s[host|ip] ?s['allow_ping']", CFGFLAG_CLIENT, Con_AddFavorite, this, "Add a server as a favorite");
	m_pConsole->Register("remove_favorite", "r[host|ip]", CFGFLAG_CLIENT, Con_RemoveFavorite, this, "Remove a server from favorites");
	m_pConsole->Register("demo_slice_start", "", CFGFLAG_CLIENT, Con_DemoSliceBegin, this, "");
	m_pConsole->Register("demo_slice_end", "", CFGFLAG_CLIENT, Con_DemoSliceEnd, this, "");
	m_pConsole->Register("demo_play", "", CFGFLAG_CLIENT, Con_DemoPlay, this, "Play demo");
	m_pConsole->Register("demo_speed", "i[speed]", CFGFLAG_CLIENT, Con_DemoSpeed, this, "Set demo speed");

	m_pConsole->Register("save_replay", "?i[length]", CFGFLAG_CLIENT, Con_SaveReplay, this, "Save a replay of the last defined amount of seconds");
	m_pConsole->Register("benchmark_quit", "i[seconds] r[file]", CFGFLAG_CLIENT | CFGFLAG_STORE, Con_BenchmarkQuit, this, "Benchmark frame times for number of seconds to file, then quit");

	m_pConsole->Chain("cl_timeout_seed", ConchainTimeoutSeed, this);
	m_pConsole->Chain("cl_replays", ConchainReplays, this);

	m_pConsole->Chain("password", ConchainPassword, this);

	// used for server browser update
	m_pConsole->Chain("br_filter_string", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_gametype", ConchainServerBrowserUpdate, this);
	m_pConsole->Chain("br_filter_serveraddress", ConchainServerBrowserUpdate, this);

	m_pConsole->Chain("gfx_screen", ConchainWindowScreen, this);
	m_pConsole->Chain("gfx_fullscreen", ConchainFullscreen, this);
	m_pConsole->Chain("gfx_borderless", ConchainWindowBordered, this);
	m_pConsole->Chain("gfx_vsync", ConchainWindowVSync, this);

	// DDRace

#define CONSOLE_COMMAND(name, params, flags, callback, userdata, help) m_pConsole->Register(name, params, flags, 0, 0, help);
#include <game/ddracecommands.h>
}

static CClient *CreateClient()
{
	CClient *pClient = static_cast<CClient *>(malloc(sizeof(*pClient)));
	mem_zero(pClient, sizeof(CClient));
	return new(pClient) CClient;
}

void CClient::HandleConnectAddress(const NETADDR *pAddr)
{
	net_addr_str(pAddr, m_aCmdConnect, sizeof(m_aCmdConnect), true);
}

void CClient::HandleConnectLink(const char *pLink)
{
	str_copy(m_aCmdConnect, pLink + sizeof(CONNECTLINK) - 1, sizeof(m_aCmdConnect));
}

void CClient::HandleDemoPath(const char *pPath)
{
	str_copy(m_aCmdPlayDemo, pPath, sizeof(m_aCmdPlayDemo));
}

void CClient::HandleMapPath(const char *pPath)
{
	str_copy(m_aCmdEditMap, pPath, sizeof(m_aCmdEditMap));
}

/*
	Server Time
	Client Mirror Time
	Client Predicted Time

	Snapshot Latency
		Downstream latency

	Prediction Latency
		Upstream latency
*/

#if defined(CONF_PLATFORM_MACOS)
extern "C" int TWMain(int argc, const char **argv) // ignore_convention
#elif defined(CONF_PLATFORM_ANDROID)
extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char *argv[]);
extern "C" void InitAndroid();

int SDL_main(int argc, char *argv[])
#else
int main(int argc, const char **argv) // ignore_convention
#endif
{
	bool Silent = false;
	bool RandInitFailed = false;

	for(int i = 1; i < argc; i++) // ignore_convention
	{
		if(str_comp("-s", argv[i]) == 0 || str_comp("--silent", argv[i]) == 0) // ignore_convention
		{
			Silent = true;
		}
		else if(str_comp("-c", argv[i]) == 0 || str_comp("--console", argv[i]) == 0) // ignore_convention
		{
#if defined(CONF_FAMILY_WINDOWS)
			AllocConsole();
#endif
		}
	}

#if defined(CONF_PLATFORM_ANDROID)
	InitAndroid();
#endif

	if(secure_random_init() != 0)
	{
		RandInitFailed = true;
	}

	NotificationsInit();

	CClient *pClient = CreateClient();
	IKernel *pKernel = IKernel::Create();
	pKernel->RegisterInterface(pClient, false);
	pClient->RegisterInterfaces();

	// create the components
	IEngine *pEngine = CreateEngine("DDNet", Silent, 2);
	IConsole *pConsole = CreateConsole(CFGFLAG_CLIENT);
	IStorage *pStorage = CreateStorage("Teeworlds", IStorage::STORAGETYPE_CLIENT, argc, (const char **)argv); // ignore_convention
	IConfigManager *pConfigManager = CreateConfigManager();
	IEngineSound *pEngineSound = CreateEngineSound();
	IEngineInput *pEngineInput = CreateEngineInput();
	IEngineTextRender *pEngineTextRender = CreateEngineTextRender();
	IEngineMap *pEngineMap = CreateEngineMap();
	IDiscord *pDiscord = CreateDiscord();
	ISteam *pSteam = CreateSteam();

	if(RandInitFailed)
	{
		dbg_msg("secure", "could not initialize secure RNG");
		return -1;
	}

	{
		bool RegisterFail = false;

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngine);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConsole);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pConfigManager);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineSound); // IEngineSound
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<ISound *>(pEngineSound), false);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineInput); // IEngineInput
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IInput *>(pEngineInput), false);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineTextRender); // IEngineTextRender
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<ITextRender *>(pEngineTextRender), false);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pEngineMap); // IEngineMap
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(static_cast<IMap *>(pEngineMap), false);

		RegisterFail = RegisterFail || !pKernel->RegisterInterface(CreateEditor(), false);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(CreateGameClient());
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pStorage);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pDiscord);
		RegisterFail = RegisterFail || !pKernel->RegisterInterface(pSteam);

		if(RegisterFail)
		{
			delete pKernel;
			pClient->~CClient();
			free(pClient);
			return -1;
		}
	}

	pEngine->Init();
	pConfigManager->Init();
	pConsole->Init();

	// register all console commands
	pClient->RegisterCommands();

	pKernel->RequestInterface<IGameClient>()->OnConsoleInit();

	// init client's interfaces
	pClient->InitInterfaces();

	// execute config file
	IOHANDLE File = pStorage->OpenFile(CONFIG_FILE, IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		io_close(File);
		pConsole->ExecuteFile(CONFIG_FILE);
	}

#if defined(CONF_FAMILY_WINDOWS)
	if(g_Config.m_ClShowConsole)
	{
		AllocConsole();
		HANDLE hInput;
		DWORD prev_mode;
		hInput = GetStdHandle(STD_INPUT_HANDLE);
		GetConsoleMode(hInput, &prev_mode);
		SetConsoleMode(hInput, prev_mode & ENABLE_EXTENDED_FLAGS);
	}
#endif

	// execute autoexec file
	File = pStorage->OpenFile(AUTOEXEC_CLIENT_FILE, IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		io_close(File);
		pConsole->ExecuteFile(AUTOEXEC_CLIENT_FILE);
	}
	else // fallback
	{
		pConsole->ExecuteFile(AUTOEXEC_FILE);
	}

	if(g_Config.m_ClConfigVersion < 1)
	{
		if(g_Config.m_ClAntiPing == 0)
		{
			g_Config.m_ClAntiPingPlayers = 1;
			g_Config.m_ClAntiPingGrenade = 1;
			g_Config.m_ClAntiPingWeapons = 1;
		}
	}
	g_Config.m_ClConfigVersion = 1;

	// parse the command line arguments
	if(argc == 2 && str_startswith(argv[1], CONNECTLINK))
		pClient->HandleConnectLink(argv[1]);
	else if(argc == 2 && str_endswith(argv[1], ".demo"))
		pClient->HandleDemoPath(argv[1]);
	else if(argc == 2 && str_endswith(argv[1], ".map"))
		pClient->HandleMapPath(argv[1]);
	else if(argc > 1) // ignore_convention
		pConsole->ParseArguments(argc - 1, (const char **)&argv[1]); // ignore_convention

	if(pSteam->GetConnectAddress())
	{
		pClient->HandleConnectAddress(pSteam->GetConnectAddress());
		pSteam->ClearConnectAddress();
	}

	pClient->Engine()->InitLogfile();

	// run the client
	dbg_msg("client", "starting...");
	pClient->Run();

	bool Restarting = pClient->State() == CClient::STATE_RESTARTING;

	pClient->~CClient();
	free(pClient);

	NotificationsUninit();

	if(Restarting)
	{
		char aBuf[512];
		shell_execute(pStorage->GetBinaryPath(PLAT_CLIENT_EXEC, aBuf, sizeof aBuf));
	}

	delete pKernel;

#ifdef CONF_PLATFORM_ANDROID
	// properly close this native thread, so globals are destructed
	std::exit(0);
#endif

	return 0;
}

// DDRace

const char *CClient::GetCurrentMap() const
{
	return m_aCurrentMap;
}

const char *CClient::GetCurrentMapPath() const
{
	return m_aCurrentMapPath;
}

SHA256_DIGEST CClient::GetCurrentMapSha256() const
{
	return m_pMap->Sha256();
}

unsigned CClient::GetCurrentMapCrc() const
{
	return m_pMap->Crc();
}

void CClient::RaceRecord_Start(const char *pFilename)
{
	if(State() != IClient::STATE_ONLINE)
		m_pConsole->Print(IConsole::OUTPUT_LEVEL_STANDARD, "demorec/record", "client is not online");
	else
	{
		SHA256_DIGEST Sha256 = m_pMap->Sha256();
		m_DemoRecorder[RECORDER_RACE].Start(Storage(), m_pConsole, pFilename, GameClient()->NetVersion(), m_aCurrentMap, &Sha256, m_pMap->Crc(), "client", m_pMap->MapSize(), 0, m_pMap->File());
	}
}

void CClient::RaceRecord_Stop()
{
	if(m_DemoRecorder[RECORDER_RACE].IsRecording())
		m_DemoRecorder[RECORDER_RACE].Stop();
}

bool CClient::RaceRecord_IsRecording()
{
	return m_DemoRecorder[RECORDER_RACE].IsRecording();
}

void CClient::RequestDDNetInfo()
{
	char aUrl[256];
	str_copy(aUrl, "https://info2.ddnet.tw/info", sizeof(aUrl));

	if(g_Config.m_BrIndicateFinished)
	{
		char aEscaped[128];
		EscapeUrl(aEscaped, sizeof(aEscaped), PlayerName());
		str_append(aUrl, "?name=", sizeof(aUrl));
		str_append(aUrl, aEscaped, sizeof(aUrl));
	}

	m_pDDNetInfoTask = std::make_shared<CGetFile>(Storage(), aUrl, m_aDDNetInfoTmp, IStorage::TYPE_SAVE, CTimeout{10000, 500, 10});
	Engine()->AddJob(m_pDDNetInfoTask);
}

int CClient::GetPredictionTime()
{
	int64_t Now = time_get();
	return (int)((m_PredictedTime.Get(Now) - m_GameTime[g_Config.m_ClDummy].Get(Now)) * 1000 / (float)time_freq());
}

void CClient::GetSmoothTick(int *pSmoothTick, float *pSmoothIntraTick, float MixAmount)
{
	int64_t GameTime = m_GameTime[g_Config.m_ClDummy].Get(time_get());
	int64_t PredTime = m_PredictedTime.Get(time_get());
	int64_t SmoothTime = clamp(GameTime + (int64_t)(MixAmount * (PredTime - GameTime)), GameTime, PredTime);

	*pSmoothTick = (int)(SmoothTime * 50 / time_freq()) + 1;
	*pSmoothIntraTick = (SmoothTime - (*pSmoothTick - 1) * time_freq() / 50) / (float)(time_freq() / 50);
}

SWarning *CClient::GetCurWarning()
{
	if(m_Warnings.empty())
	{
		return NULL;
	}
	else if(m_Warnings[0].m_WasShown)
	{
		m_Warnings.erase(m_Warnings.begin());
		return NULL;
	}
	else
	{
		return &m_Warnings[0];
	}
}
