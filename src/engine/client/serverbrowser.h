/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#ifndef ENGINE_CLIENT_SERVERBROWSER_H
#define ENGINE_CLIENT_SERVERBROWSER_H

#include <engine/client/http.h>
#include <engine/config.h>
#include <engine/console.h>
#include <engine/external/json-parser/json.h>
#include <engine/masterserver.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/shared/memheap.h>

class IServerBrowserHttp;
class IServerBrowserPingCache;

class CServerBrowser : public IServerBrowser
{
public:
	class CServerEntry
	{
	public:
		NETADDR m_Addr;
		int64_t m_RequestTime;
		bool m_RequestIgnoreInfo;
		int m_GotInfo;
		bool m_Request64Legacy;
		CServerInfo m_Info;

		CServerEntry *m_pNextIp; // ip hashed list

		CServerEntry *m_pPrevReq; // request list
		CServerEntry *m_pNextReq;
	};

	struct CNetworkCountry
	{
		enum
		{
			MAX_SERVERS = 1024
		};

		char m_aName[256];
		int m_FlagID;
		NETADDR m_aServers[MAX_SERVERS];
		char m_aTypes[MAX_SERVERS][32];
		int m_NumServers;

		void Reset()
		{
			m_NumServers = 0;
			m_FlagID = -1;
			m_aName[0] = '\0';
		};
		/*void Add(NETADDR Addr, char* pType) {
			if (m_NumServers < MAX_SERVERS)
			{
				m_aServers[m_NumServers] = Addr;
				str_copy(m_aTypes[m_NumServers], pType, sizeof(m_aTypes[0]));
				m_NumServers++;
			}
		};*/
	};

	enum
	{
		MAX_FAVORITES = 2048,
		MAX_COUNTRIES = 32,
		MAX_TYPES = 32,
	};

	struct CNetwork
	{
		CNetworkCountry m_aCountries[MAX_COUNTRIES];
		int m_NumCountries;

		char m_aTypes[MAX_TYPES][32];
		int m_NumTypes;
	};

	CServerBrowser();
	virtual ~CServerBrowser();

	// interface functions
	void Refresh(int Type);
	bool IsRefreshing() const;
	bool IsGettingServerlist() const;
	int LoadingProgression() const;

	int NumServers() const { return m_NumServers; }

	int Players(const CServerInfo &Item) const
	{
		return g_Config.m_BrFilterSpectators ? Item.m_NumPlayers : Item.m_NumClients;
	}

	int Max(const CServerInfo &Item) const
	{
		return g_Config.m_BrFilterSpectators ? Item.m_MaxPlayers : Item.m_MaxClients;
	}

	int NumSortedServers() const { return m_NumSortedServers; }
	const CServerInfo *SortedGet(int Index) const;

	bool GotInfo(const NETADDR &Addr) const;
	bool IsFavorite(const NETADDR &Addr) const;
	bool IsFavoritePingAllowed(const NETADDR &Addr) const;
	void AddFavorite(const NETADDR &Addr);
	void FavoriteAllowPing(const NETADDR &Addr, bool AllowPing);
	void RemoveFavorite(const NETADDR &Addr);

	void LoadDDNetRanks();
	void RecheckOfficial();
	void LoadDDNetServers();
	void LoadDDNetInfoJson();
	const json_value *LoadDDNetInfo();
	int HasRank(const char *pMap);
	int NumCountries(int Network) { return m_aNetworks[Network].m_NumCountries; };
	int GetCountryFlag(int Network, int Index) { return m_aNetworks[Network].m_aCountries[Index].m_FlagID; };
	const char *GetCountryName(int Network, int Index) { return m_aNetworks[Network].m_aCountries[Index].m_aName; };

	int NumTypes(int Network) { return m_aNetworks[Network].m_NumTypes; };
	const char *GetType(int Network, int Index) { return m_aNetworks[Network].m_aTypes[Index]; };

	void DDNetFilterAdd(char *pFilter, const char *pName);
	void DDNetFilterRem(char *pFilter, const char *pName);
	bool DDNetFiltered(char *pFilter, const char *pName);
	void CountryFilterClean(int Network);
	void TypeFilterClean(int Network);

	//
	void Update(bool ForceResort);
	void Set(const NETADDR &Addr, int Type, int Token, const CServerInfo *pInfo);
	void RequestCurrentServer(const NETADDR &Addr) const;
	void RequestCurrentServerWithRandomToken(const NETADDR &Addr, int *pBasicToken, int *pToken) const;
	void SetCurrentServerPing(const NETADDR &Addr, int Ping);

	void SetBaseInfo(class CNetClient *pClient, const char *pNetVersion);
	void OnInit();

	void RequestImpl64(const NETADDR &Addr, CServerEntry *pEntry) const;
	void QueueRequest(CServerEntry *pEntry);
	CServerEntry *Find(const NETADDR &Addr);
	int GetCurrentType() { return m_ServerlistType; };

private:
	CNetClient *m_pNetClient;
	class IConsole *m_pConsole;
	class IEngine *m_pEngine;
	class IFriends *m_pFriends;
	class IStorage *m_pStorage;
	char m_aNetVersion[128];

	bool m_RefreshingHttp = false;
	IServerBrowserHttp *m_pHttp = nullptr;
	IServerBrowserPingCache *m_pPingCache = nullptr;
	const char *m_pHttpPrevBestUrl = nullptr;

	CHeap m_ServerlistHeap;
	CServerEntry **m_ppServerlist;
	int *m_pSortedServerlist;

	NETADDR m_aFavoriteServers[MAX_FAVORITES];
	bool m_aFavoriteServersAllowPing[MAX_FAVORITES];
	int m_NumFavoriteServers;

	CNetwork m_aNetworks[NUM_NETWORKS];
	int m_OwnLocation = CServerInfo::LOC_UNKNOWN;

	json_value *m_pDDNetInfo;

	CServerEntry *m_aServerlistIp[256]; // ip hash list

	CServerEntry *m_pFirstReqServer; // request list
	CServerEntry *m_pLastReqServer;
	int m_NumRequests;

	// used instead of g_Config.br_max_requests to get more servers
	int m_CurrentMaxRequests;

	int m_NeedRefresh;

	int m_NumSortedServers;
	int m_NumSortedServersCapacity;
	int m_NumServers;
	int m_NumServerCapacity;

	int m_Sorthash;
	char m_aFilterString[64];
	char m_aFilterGametypeString[128];

	int m_ServerlistType;
	int64_t m_BroadcastTime;
	unsigned char m_aTokenSeed[16];

	bool m_SortOnNextUpdate;

	int FindFavorite(const NETADDR &Addr) const;

	int GenerateToken(const NETADDR &Addr) const;
	static int GetBasicToken(int Token);
	static int GetExtraToken(int Token);

	// sorting criteria
	bool SortCompareName(int Index1, int Index2) const;
	bool SortCompareMap(int Index1, int Index2) const;
	bool SortComparePing(int Index1, int Index2) const;
	bool SortCompareGametype(int Index1, int Index2) const;
	bool SortCompareNumPlayers(int Index1, int Index2) const;
	bool SortCompareNumClients(int Index1, int Index2) const;
	bool SortCompareNumPlayersAndPing(int Index1, int Index2) const;

	//
	void Filter();
	void Sort();
	int SortHash() const;

	void CleanUp();

	void UpdateFromHttp();
	CServerEntry *Add(const NETADDR &Addr);

	void RemoveRequest(CServerEntry *pEntry);

	void RequestImpl(const NETADDR &Addr, CServerEntry *pEntry, int *pBasicToken, int *pToken, bool RandomToken) const;

	void RegisterCommands();
	static void Con_LeakIpAddress(IConsole::IResult *pResult, void *pUserData);

	void SetInfo(CServerEntry *pEntry, const CServerInfo &Info);
	void SetLatency(const NETADDR Addr, int Latency);

	static void ConfigSaveCallback(IConfigManager *pConfigManager, void *pUserData);
};

#endif
