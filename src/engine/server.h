/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_H
#define ENGINE_SERVER_H

#include <type_traits>

#include <base/hash.h>
#include <base/math.h>

#include "kernel.h"
#include "message.h"
#include <engine/shared/protocol.h>
#include <game/generated/protocol.h>
#include <game/generated/protocol7.h>
#include <game/generated/protocolglue.h>

struct CAntibotRoundData;

class IServer : public IInterface
{
	MACRO_INTERFACE("server", 0)
protected:
	int m_CurrentGameTick;
	int m_TickSpeed;

public:
	/*
		Structure: CClientInfo
	*/
	struct CClientInfo
	{
		const char *m_pName;
		int m_Latency;
		bool m_GotDDNetVersion;
		int m_DDNetVersion;
		const char *m_pDDNetVersionStr;
		const CUuid *m_pConnectionID;
	};

	int Tick() const { return m_CurrentGameTick; }
	int TickSpeed() const { return m_TickSpeed; }

	virtual int Port() const = 0;
	virtual int MaxClients() const = 0;
	virtual int ClientCount() const = 0;
	virtual int DistinctClientCount() const = 0;
	virtual const char *ClientName(int ClientID) const = 0;
	virtual const char *ClientClan(int ClientID) const = 0;
	virtual int ClientCountry(int ClientID) const = 0;
	virtual bool ClientIngame(int ClientID) const = 0;
	virtual bool ClientAuthed(int ClientID) const = 0;
	virtual int GetClientInfo(int ClientID, CClientInfo *pInfo) const = 0;
	virtual void SetClientDDNetVersion(int ClientID, int DDNetVersion) = 0;
	virtual void GetClientAddr(int ClientID, char *pAddrStr, int Size) const = 0;
	virtual void RestrictRconOutput(int ClientID) = 0;

	virtual int SendMsg(CMsgPacker *pMsg, int Flags, int ClientID) = 0;

	template<class T, typename std::enable_if<!protocol7::is_sixup<T>::value, int>::type = 0>
	inline int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		T tmp;
		if(ClientID == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(ClientIngame(i))
				{
					mem_copy(&tmp, pMsg, sizeof(T));
					Result = SendPackMsgTranslate(&tmp, Flags, i);
				}
		}
		else
		{
			mem_copy(&tmp, pMsg, sizeof(T));
			Result = SendPackMsgTranslate(&tmp, Flags, ClientID);
		}
		return Result;
	}

	template<class T, typename std::enable_if<protocol7::is_sixup<T>::value, int>::type = 1>
	inline int SendPackMsg(T *pMsg, int Flags, int ClientID)
	{
		int Result = 0;
		if(ClientID == -1)
		{
			for(int i = 0; i < MAX_CLIENTS; i++)
				if(ClientIngame(i) && IsSixup(i))
					Result = SendPackMsgOne(pMsg, Flags, i);
		}
		else if(IsSixup(ClientID))
			Result = SendPackMsgOne(pMsg, Flags, ClientID);

		return Result;
	}

	template<class T>
	int SendPackMsgTranslate(T *pMsg, int Flags, int ClientID)
	{
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_Emoticon *pMsg, int Flags, int ClientID)
	{
		return Translate(pMsg->m_ClientID, ClientID) && SendPackMsgOne(pMsg, Flags, ClientID);
	}

	char msgbuf[1000];

	int SendPackMsgTranslate(CNetMsg_Sv_Chat *pMsg, int Flags, int ClientID)
	{
		if(pMsg->m_ClientID >= 0 && !Translate(pMsg->m_ClientID, ClientID))
		{
			str_format(msgbuf, sizeof(msgbuf), "%s: %s", ClientName(pMsg->m_ClientID), pMsg->m_pMessage);
			pMsg->m_pMessage = msgbuf;
			pMsg->m_ClientID = VANILLA_MAX_CLIENTS - 1;
		}

		if(IsSixup(ClientID))
		{
			protocol7::CNetMsg_Sv_Chat Msg7;
			Msg7.m_ClientID = pMsg->m_ClientID;
			Msg7.m_pMessage = pMsg->m_pMessage;
			Msg7.m_Mode = pMsg->m_Team > 0 ? protocol7::CHAT_TEAM : protocol7::CHAT_ALL;
			Msg7.m_TargetID = -1;
			return SendPackMsgOne(&Msg7, Flags, ClientID);
		}

		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	int SendPackMsgTranslate(CNetMsg_Sv_KillMsg *pMsg, int Flags, int ClientID)
	{
		if(!Translate(pMsg->m_Victim, ClientID))
			return 0;
		if(!Translate(pMsg->m_Killer, ClientID))
			pMsg->m_Killer = pMsg->m_Victim;
		return SendPackMsgOne(pMsg, Flags, ClientID);
	}

	template<class T>
	int SendPackMsgOne(T *pMsg, int Flags, int ClientID)
	{
		dbg_assert(ClientID != -1, "SendPackMsgOne called with -1");
		CMsgPacker Packer(pMsg->MsgID(), false, protocol7::is_sixup<T>::value);

		if(pMsg->Pack(&Packer))
			return -1;
		return SendMsg(&Packer, Flags, ClientID);
	}

	bool Translate(int &Target, int Client)
	{
		if(IsSixup(Client))
			return true;
		CClientInfo Info;
		GetClientInfo(Client, &Info);
		if(Info.m_DDNetVersion >= VERSION_DDNET_OLD)
			return true;
		int *pMap = GetIdMap(Client);
		bool Found = false;
		for(int i = 0; i < VANILLA_MAX_CLIENTS; i++)
		{
			if(Target == pMap[i])
			{
				Target = i;
				Found = true;
				break;
			}
		}
		return Found;
	}

	bool ReverseTranslate(int &Target, int Client)
	{
		if(IsSixup(Client))
			return true;
		CClientInfo Info;
		GetClientInfo(Client, &Info);
		if(Info.m_DDNetVersion >= VERSION_DDNET_OLD)
			return true;
		Target = clamp(Target, 0, VANILLA_MAX_CLIENTS - 1);
		int *pMap = GetIdMap(Client);
		if(pMap[Target] == -1)
			return false;
		Target = pMap[Target];
		return true;
	}

	virtual void GetMapInfo(char *pMapName, int MapNameSize, int *pMapSize, SHA256_DIGEST *pSha256, int *pMapCrc) = 0;

	virtual bool WouldClientNameChange(int ClientID, const char *pNameRequest) = 0;
	virtual void SetClientName(int ClientID, char const *pName) = 0;
	virtual void SetClientClan(int ClientID, char const *pClan) = 0;
	virtual void SetClientCountry(int ClientID, int Country) = 0;
	virtual void SetClientScore(int ClientID, int Score) = 0;
	virtual void SetClientFlags(int ClientID, int Flags) = 0;

	virtual int SnapNewID() = 0;
	virtual void SnapFreeID(int ID) = 0;
	virtual void *SnapNewItem(int Type, int ID, int Size) = 0;

	virtual void SnapSetStaticsize(int ItemType, int Size) = 0;

	enum
	{
		RCON_CID_SERV = -1,
		RCON_CID_VOTE = -2,
	};
	virtual void SetRconCID(int ClientID) = 0;
	virtual int GetAuthedState(int ClientID) const = 0;
	virtual const char *GetAuthName(int ClientID) const = 0;
	virtual void Kick(int ClientID, const char *pReason) = 0;
	virtual void Ban(int ClientID, int Seconds, const char *pReason) = 0;

	virtual void DemoRecorder_HandleAutoStart() = 0;
	virtual bool DemoRecorder_IsRecording() = 0;

	// DDRace

	virtual void SaveDemo(int ClientID, float Time) = 0;
	virtual void StartRecord(int ClientID) = 0;
	virtual void StopRecord(int ClientID) = 0;
	virtual bool IsRecording(int ClientID) = 0;

	virtual void GetClientAddr(int ClientID, NETADDR *pAddr) const = 0;

	virtual int *GetIdMap(int ClientID) = 0;

	virtual bool DnsblWhite(int ClientID) = 0;
	virtual bool DnsblPending(int ClientID) = 0;
	virtual bool DnsblBlack(int ClientID) = 0;
	virtual const char *GetAnnouncementLine(char const *FileName) = 0;
	virtual bool ClientPrevIngame(int ClientID) = 0;
	virtual const char *GetNetErrorString(int ClientID) = 0;
	virtual void ResetNetErrorString(int ClientID) = 0;
	virtual bool SetTimedOut(int ClientID, int OrigID) = 0;
	virtual void SetTimeoutProtected(int ClientID) = 0;

	virtual void SetErrorShutdown(const char *pReason) = 0;
	virtual void ExpireServerInfo() = 0;

	virtual void SendMsgRaw(int ClientID, const void *pData, int Size, int Flags) = 0;

	virtual char *GetMapName() const = 0;

	virtual bool IsSixup(int ClientID) const = 0;
};

class IGameServer : public IInterface
{
	MACRO_INTERFACE("gameserver", 0)
protected:
public:
	virtual void OnInit() = 0;
	virtual void OnConsoleInit() = 0;
	virtual void OnMapChange(char *pNewMapName, int MapNameSize) = 0;

	// FullShutdown is true if the program is about to exit (not if the map is changed)
	virtual void OnShutdown() = 0;

	virtual void OnTick() = 0;
	virtual void OnPreSnap() = 0;
	virtual void OnSnap(int ClientID) = 0;
	virtual void OnPostSnap() = 0;

	virtual void OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID) = 0;

	// Called before map reload, for any data that the game wants to
	// persist to the next map.
	//
	// Has the size of the return value of `PersistentClientDataSize()`.
	//
	// Returns whether the game should be supplied with the data when the
	// client connects for the next map.
	virtual bool OnClientDataPersist(int ClientID, void *pData) = 0;

	// Called when a client connects.
	//
	// If it is reconnecting to the game after a map change, the
	// `pPersistentData` point is nonnull and contains the data the game
	// previously stored.
	virtual void OnClientConnected(int ClientID, void *pPersistentData) = 0;

	virtual void OnClientEnter(int ClientID) = 0;
	virtual void OnClientDrop(int ClientID, const char *pReason) = 0;
	virtual void OnClientDirectInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedInput(int ClientID, void *pInput) = 0;
	virtual void OnClientPredictedEarlyInput(int ClientID, void *pInput) = 0;

	virtual bool IsClientReady(int ClientID) const = 0;
	virtual bool IsClientPlayer(int ClientID) const = 0;

	virtual int PersistentClientDataSize() const = 0;

	virtual CUuid GameUuid() const = 0;
	virtual const char *GameType() const = 0;
	virtual const char *Version() const = 0;
	virtual const char *NetVersion() const = 0;

	// DDRace

	virtual void OnSetAuthed(int ClientID, int Level) = 0;
	virtual bool PlayerExists(int ClientID) const = 0;

	virtual void OnClientEngineJoin(int ClientID, bool Sixup) = 0;
	virtual void OnClientEngineDrop(int ClientID, const char *pReason) = 0;

	virtual void FillAntibot(CAntibotRoundData *pData) = 0;
};

extern IGameServer *CreateGameServer();
#endif
