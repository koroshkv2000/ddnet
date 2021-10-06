/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_SERVER_REGISTER_H
#define ENGINE_SERVER_REGISTER_H

#include <engine/masterserver.h>
#include <engine/shared/network.h>

class CRegister
{
	enum
	{
		REGISTERSTATE_START = 0,
		REGISTERSTATE_UPDATE_ADDRS,
		REGISTERSTATE_QUERY_COUNT,
		REGISTERSTATE_HEARTBEAT,
		REGISTERSTATE_REGISTERED,
		REGISTERSTATE_ERROR
	};

	struct CMasterserverInfo
	{
		NETADDR m_Addr;
		int m_Count;
		int m_Valid;
		int64_t m_LastSend;
		SECURITY_TOKEN m_Token;
	};

	class CNetServer *m_pNetServer;
	class IEngineMasterServer *m_pMasterServer;
	class CConfig *m_pConfig;
	class IConsole *m_pConsole;

	bool m_Sixup;
	const char *m_pName;
	int64_t m_LastTokenRequest;

	int m_RegisterState;
	int64_t m_RegisterStateStart;
	int m_RegisterFirst;
	int m_RegisterCount;

	CMasterserverInfo m_aMasterserverInfo[IMasterServer::MAX_MASTERSERVERS];
	int m_RegisterRegisteredServer;

	void RegisterNewState(int State);
	void RegisterSendFwcheckresponse(NETADDR *pAddr, SECURITY_TOKEN ResponseToken);
	void RegisterSendHeartbeat(NETADDR Addr, SECURITY_TOKEN ResponseToken);
	void RegisterSendCountRequest(NETADDR Addr, SECURITY_TOKEN ResponseToken);
	void RegisterGotCount(struct CNetChunk *pChunk);

public:
	CRegister(bool Sixup);
	void Init(class CNetServer *pNetServer, class IEngineMasterServer *pMasterServer, class CConfig *pConfig, class IConsole *pConsole);
	void RegisterUpdate(int Nettype);
	int RegisterProcessPacket(struct CNetChunk *pPacket, SECURITY_TOKEN ResponseToken = 0);
	void FeedToken(NETADDR Addr, SECURITY_TOKEN ResponseToken);
};

#endif
