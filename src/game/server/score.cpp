#include "score.h"
#include "entities/character.h"
#include "gamemodes/DDRace.h"
#include "player.h"
#include "save.h"

#include <base/system.h>
#include <engine/server/databases/connection.h>
#include <engine/server/databases/connection_pool.h>
#include <engine/server/sql_string_helpers.h>
#include <engine/shared/config.h>
#include <engine/shared/console.h>
#include <engine/shared/linereader.h>
#include <engine/storage.h>
#include <game/generated/wordlist.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <random>

// "6b407e81-8b77-3e04-a207-8da17f37d000"
// "save-no-save-id@ddnet.tw"
static const CUuid UUID_NO_SAVE_ID =
	{{0x6b, 0x40, 0x7e, 0x81, 0x8b, 0x77, 0x3e, 0x04,
		0xa2, 0x07, 0x8d, 0xa1, 0x7f, 0x37, 0xd0, 0x00}};

CScorePlayerResult::CScorePlayerResult()
{
	SetVariant(Variant::DIRECT);
}

void CScorePlayerResult::SetVariant(Variant v)
{
	m_MessageKind = v;
	switch(v)
	{
	case DIRECT:
	case ALL:
		for(auto &aMessage : m_Data.m_aaMessages)
			aMessage[0] = 0;
		break;
	case BROADCAST:
		m_Data.m_aBroadcast[0] = 0;
		break;
	case MAP_VOTE:
		m_Data.m_MapVote.m_aMap[0] = '\0';
		m_Data.m_MapVote.m_aReason[0] = '\0';
		m_Data.m_MapVote.m_aServer[0] = '\0';
		break;
	case PLAYER_INFO:
		m_Data.m_Info.m_Score = -9999;
		m_Data.m_Info.m_Birthday = 0;
		m_Data.m_Info.m_HasFinishScore = false;
		m_Data.m_Info.m_Time = 0;
		for(float &CpTime : m_Data.m_Info.m_CpTime)
			CpTime = 0;
	}
}

CTeamrank::CTeamrank() :
	m_NumNames(0)
{
	for(auto &aName : m_aaNames)
		aName[0] = '\0';
	mem_zero(&m_TeamID.m_aData, sizeof(m_TeamID));
}

bool CTeamrank::NextSqlResult(IDbConnection *pSqlServer, bool *pEnd, char *pError, int ErrorSize)
{
	pSqlServer->GetBlob(1, m_TeamID.m_aData, sizeof(m_TeamID.m_aData));
	pSqlServer->GetString(2, m_aaNames[0], sizeof(m_aaNames[0]));
	m_NumNames = 1;
	bool End = false;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		CUuid TeamID;
		pSqlServer->GetBlob(1, TeamID.m_aData, sizeof(TeamID.m_aData));
		if(m_TeamID != TeamID)
		{
			*pEnd = false;
			return false;
		}
		pSqlServer->GetString(2, m_aaNames[m_NumNames], sizeof(m_aaNames[m_NumNames]));
		m_NumNames++;
	}
	if(!End)
	{
		return true;
	}
	*pEnd = true;
	return false;
}

bool CTeamrank::SamePlayers(const std::vector<std::string> *aSortedNames)
{
	if(aSortedNames->size() != m_NumNames)
		return false;
	for(unsigned int i = 0; i < m_NumNames; i++)
	{
		if(str_comp(aSortedNames->at(i).c_str(), m_aaNames[i]) != 0)
			return false;
	}
	return true;
}

std::shared_ptr<CScorePlayerResult> CScore::NewSqlPlayerResult(int ClientID)
{
	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientID];
	if(pCurPlayer->m_ScoreQueryResult != nullptr) // TODO: send player a message: "too many requests"
		return nullptr;
	pCurPlayer->m_ScoreQueryResult = std::make_shared<CScorePlayerResult>();
	return pCurPlayer->m_ScoreQueryResult;
}

void CScore::ExecPlayerThread(
	bool (*pFuncPtr)(IDbConnection *, const ISqlData *, char *pError, int ErrorSize),
	const char *pThreadName,
	int ClientID,
	const char *pName,
	int Offset)
{
	auto pResult = NewSqlPlayerResult(ClientID);
	if(pResult == nullptr)
		return;
	auto Tmp = std::unique_ptr<CSqlPlayerRequest>(new CSqlPlayerRequest(pResult));
	str_copy(Tmp->m_aName, pName, sizeof(Tmp->m_aName));
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));
	str_copy(Tmp->m_aServer, g_Config.m_SvSqlServerName, sizeof(Tmp->m_aServer));
	str_copy(Tmp->m_aRequestingPlayer, Server()->ClientName(ClientID), sizeof(Tmp->m_aRequestingPlayer));
	Tmp->m_Offset = Offset;

	m_pPool->Execute(pFuncPtr, std::move(Tmp), pThreadName);
}

bool CScore::RateLimitPlayer(int ClientID)
{
	CPlayer *pPlayer = GameServer()->m_apPlayers[ClientID];
	if(pPlayer == 0)
		return true;
	if(pPlayer->m_LastSQLQuery + (int64_t)g_Config.m_SvSqlQueriesDelay * Server()->TickSpeed() >= Server()->Tick())
		return true;
	pPlayer->m_LastSQLQuery = Server()->Tick();
	return false;
}

void CScore::GeneratePassphrase(char *pBuf, int BufSize)
{
	for(int i = 0; i < 3; i++)
	{
		if(i != 0)
			str_append(pBuf, " ", BufSize);
		// TODO: decide if the slight bias towards lower numbers is ok
		int Rand = m_Prng.RandomBits() % m_aWordlist.size();
		str_append(pBuf, m_aWordlist[Rand].c_str(), BufSize);
	}
}

CScore::CScore(CGameContext *pGameServer, CDbConnectionPool *pPool) :
	m_pPool(pPool),
	m_pGameServer(pGameServer),
	m_pServer(pGameServer->Server())
{
	auto InitResult = std::make_shared<CScoreInitResult>();
	auto Tmp = std::unique_ptr<CSqlInitData>(new CSqlInitData(InitResult));
	((CGameControllerDDRace *)(pGameServer->m_pController))->m_pInitResult = InitResult;
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));

	uint64_t aSeed[2];
	secure_random_fill(aSeed, sizeof(aSeed));
	m_Prng.Seed(aSeed);

	IOHANDLE File = GameServer()->Storage()->OpenFile("wordlist.txt", IOFLAG_READ, IStorage::TYPE_ALL);
	if(File)
	{
		CLineReader LineReader;
		LineReader.Init(File);
		char *pLine;
		while((pLine = LineReader.Get()))
		{
			char aWord[32] = {0};
			sscanf(pLine, "%*s %31s", aWord);
			aWord[31] = 0;
			m_aWordlist.push_back(aWord);
		}
	}
	else
	{
		dbg_msg("sql", "failed to open wordlist, using fallback");
		m_aWordlist.assign(std::begin(g_aFallbackWordlist), std::end(g_aFallbackWordlist));
	}

	if(m_aWordlist.size() < 1000)
	{
		dbg_msg("sql", "too few words in wordlist");
		Server()->SetErrorShutdown("sql too few words in wordlist");
		return;
	}

	m_pPool->Execute(Init, std::move(Tmp), "load best time");
}

bool CScore::Init(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlInitData *pData = dynamic_cast<const CSqlInitData *>(pGameData);
	CScoreInitResult *pResult = dynamic_cast<CScoreInitResult *>(pGameData->m_pResult.get());

	char aBuf[512];
	// get the best time
	str_format(aBuf, sizeof(aBuf),
		"SELECT Time FROM %s_race WHERE Map=? ORDER BY `Time` ASC LIMIT 1;",
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		pResult->m_CurrentRecord = pSqlServer->GetFloat(1);
	}

	return false;
}

void CScore::LoadPlayerData(int ClientID)
{
	ExecPlayerThread(LoadPlayerDataThread, "load player data", ClientID, "", 0);
}

// update stuff
bool CScore::LoadPlayerDataThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	pResult->SetVariant(CScorePlayerResult::PLAYER_INFO);

	char aBuf[512];
	// get best race time
	str_format(aBuf, sizeof(aBuf),
		"SELECT Time, cp1, cp2, cp3, cp4, cp5, cp6, cp7, cp8, cp9, cp10, "
		"  cp11, cp12, cp13, cp14, cp15, cp16, cp17, cp18, cp19, cp20, "
		"  cp21, cp22, cp23, cp24, cp25 "
		"FROM %s_race "
		"WHERE Map = ? AND Name = ? "
		"ORDER BY Time ASC "
		"LIMIT 1;",
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aRequestingPlayer);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		// get the best time
		float Time = pSqlServer->GetFloat(1);
		pResult->m_Data.m_Info.m_Time = Time;
		pResult->m_Data.m_Info.m_Score = -Time;
		pResult->m_Data.m_Info.m_HasFinishScore = true;

		if(g_Config.m_SvCheckpointSave)
		{
			for(int i = 0; i < NUM_CHECKPOINTS; i++)
			{
				pResult->m_Data.m_Info.m_CpTime[i] = pSqlServer->GetFloat(i + 2);
			}
		}
	}

	// birthday check
	str_format(aBuf, sizeof(aBuf),
		"SELECT CURRENT_TIMESTAMP AS Current, MIN(Timestamp) AS Stamp "
		"FROM %s_race "
		"WHERE Name = ?",
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aRequestingPlayer);

	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End && !pSqlServer->IsNull(2))
	{
		char aCurrent[TIMESTAMP_STR_LENGTH];
		pSqlServer->GetString(1, aCurrent, sizeof(aCurrent));
		char aStamp[TIMESTAMP_STR_LENGTH];
		pSqlServer->GetString(2, aStamp, sizeof(aStamp));
		int CurrentYear, CurrentMonth, CurrentDay;
		int StampYear, StampMonth, StampDay;
		if(sscanf(aCurrent, "%d-%d-%d", &CurrentYear, &CurrentMonth, &CurrentDay) == 3 && sscanf(aStamp, "%d-%d-%d", &StampYear, &StampMonth, &StampDay) == 3 && CurrentMonth == StampMonth && CurrentDay == StampDay)
			pResult->m_Data.m_Info.m_Birthday = CurrentYear - StampYear;
	}
	return false;
}

void CScore::MapVote(int ClientID, const char *MapName)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(MapVoteThread, "map vote", ClientID, MapName, 0);
}

bool CScore::MapVoteThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	char aFuzzyMap[128];
	str_copy(aFuzzyMap, pData->m_aName, sizeof(aFuzzyMap));
	sqlstr::FuzzyString(aFuzzyMap, sizeof(aFuzzyMap));

	char aMapPrefix[128];
	str_copy(aMapPrefix, pData->m_aName, sizeof(aMapPrefix));
	str_append(aMapPrefix, "%", sizeof(aMapPrefix));

	char aBuf[768];
	str_format(aBuf, sizeof(aBuf),
		"SELECT Map, Server "
		"FROM %s_maps "
		"WHERE Map LIKE %s "
		"ORDER BY "
		"  CASE WHEN Map = ? THEN 0 ELSE 1 END, "
		"  CASE WHEN Map LIKE ? THEN 0 ELSE 1 END, "
		"  LENGTH(Map), Map "
		"LIMIT 1;",
		pSqlServer->GetPrefix(), pSqlServer->CollateNocase());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, aFuzzyMap);
	pSqlServer->BindString(2, pData->m_aName);
	pSqlServer->BindString(3, aMapPrefix);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		pResult->SetVariant(CScorePlayerResult::MAP_VOTE);
		auto *MapVote = &pResult->m_Data.m_MapVote;
		pSqlServer->GetString(1, MapVote->m_aMap, sizeof(MapVote->m_aMap));
		pSqlServer->GetString(2, MapVote->m_aServer, sizeof(MapVote->m_aServer));
		str_copy(MapVote->m_aReason, "/map", sizeof(MapVote->m_aReason));

		for(char *p = MapVote->m_aServer; *p; p++) // lower case server
			*p = tolower(*p);
	}
	else
	{
		pResult->SetVariant(CScorePlayerResult::DIRECT);
		str_format(paMessages[0], sizeof(paMessages[0]),
			"No map like \"%s\" found. "
			"Try adding a '%%' at the start if you don't know the first character. "
			"Example: /map %%castle for \"Out of Castle\"",
			pData->m_aName);
	}
	return false;
}

void CScore::MapInfo(int ClientID, const char *MapName)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(MapInfoThread, "map info", ClientID, MapName, 0);
}

bool CScore::MapInfoThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());

	char aFuzzyMap[128];
	str_copy(aFuzzyMap, pData->m_aName, sizeof(aFuzzyMap));
	sqlstr::FuzzyString(aFuzzyMap, sizeof(aFuzzyMap));

	char aMapPrefix[128];
	str_copy(aMapPrefix, pData->m_aName, sizeof(aMapPrefix));
	str_append(aMapPrefix, "%", sizeof(aMapPrefix));

	char aCurrentTimestamp[512];
	pSqlServer->ToUnixTimestamp("CURRENT_TIMESTAMP", aCurrentTimestamp, sizeof(aCurrentTimestamp));
	char aTimestamp[512];
	pSqlServer->ToUnixTimestamp("l.Timestamp", aTimestamp, sizeof(aTimestamp));

	char aMedianMapTime[2048];
	char aBuf[4096];
	str_format(aBuf, sizeof(aBuf),
		"SELECT l.Map, l.Server, Mapper, Points, Stars, "
		"  (SELECT COUNT(Name) FROM %s_race WHERE Map = l.Map) AS Finishes, "
		"  (SELECT COUNT(DISTINCT Name) FROM %s_race WHERE Map = l.Map) AS Finishers, "
		"  (%s) AS Median, "
		"  %s AS Stamp, "
		"  %s-%s AS Ago, "
		"  (SELECT MIN(Time) FROM %s_race WHERE Map = l.Map AND Name = ?) AS OwnTime "
		"FROM ("
		"  SELECT * FROM %s_maps "
		"  WHERE Map LIKE %s "
		"  ORDER BY "
		"    CASE WHEN Map = ? THEN 0 ELSE 1 END, "
		"    CASE WHEN Map LIKE ? THEN 0 ELSE 1 END, "
		"    LENGTH(Map), "
		"    Map "
		"  LIMIT 1"
		") as l;",
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(),
		pSqlServer->MedianMapTime(aMedianMapTime, sizeof(aMedianMapTime)),
		aTimestamp, aCurrentTimestamp, aTimestamp,
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(),
		pSqlServer->CollateNocase());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aRequestingPlayer);
	pSqlServer->BindString(2, aFuzzyMap);
	pSqlServer->BindString(3, pData->m_aName);
	pSqlServer->BindString(4, aMapPrefix);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		char aMap[MAX_MAP_LENGTH];
		pSqlServer->GetString(1, aMap, sizeof(aMap));
		char aServer[32];
		pSqlServer->GetString(2, aServer, sizeof(aServer));
		char aMapper[128];
		pSqlServer->GetString(3, aMapper, sizeof(aMapper));
		int Points = pSqlServer->GetInt(4);
		int Stars = pSqlServer->GetInt(5);
		int Finishes = pSqlServer->GetInt(6);
		int Finishers = pSqlServer->GetInt(7);
		float Median = !pSqlServer->IsNull(8) ? pSqlServer->GetInt(8) : -1.0f;
		int Stamp = pSqlServer->GetInt(9);
		int Ago = pSqlServer->GetInt(10);
		float OwnTime = !pSqlServer->IsNull(11) ? pSqlServer->GetFloat(11) : -1.0f;

		char aAgoString[40] = "\0";
		char aReleasedString[60] = "\0";
		if(Stamp != 0)
		{
			sqlstr::AgoTimeToString(Ago, aAgoString, sizeof(aAgoString));
			str_format(aReleasedString, sizeof(aReleasedString), ", released %s ago", aAgoString);
		}

		char aMedianString[60] = "\0";
		if(Median > 0)
		{
			str_time((int64_t)Median * 100, TIME_HOURS, aBuf, sizeof(aBuf));
			str_format(aMedianString, sizeof(aMedianString), " in %s median", aBuf);
		}

		char aStars[20];
		switch(Stars)
		{
		case 0: str_copy(aStars, "✰✰✰✰✰", sizeof(aStars)); break;
		case 1: str_copy(aStars, "★✰✰✰✰", sizeof(aStars)); break;
		case 2: str_copy(aStars, "★★✰✰✰", sizeof(aStars)); break;
		case 3: str_copy(aStars, "★★★✰✰", sizeof(aStars)); break;
		case 4: str_copy(aStars, "★★★★✰", sizeof(aStars)); break;
		case 5: str_copy(aStars, "★★★★★", sizeof(aStars)); break;
		default: aStars[0] = '\0';
		}

		char aOwnFinishesString[40] = "\0";
		if(OwnTime > 0)
		{
			str_time_float(OwnTime, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
			str_format(aOwnFinishesString, sizeof(aOwnFinishesString),
				", your time: %s", aBuf);
		}

		str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
			"\"%s\" by %s on %s, %s, %d %s%s, %d %s by %d %s%s%s",
			aMap, aMapper, aServer, aStars,
			Points, Points == 1 ? "point" : "points",
			aReleasedString,
			Finishes, Finishes == 1 ? "finish" : "finishes",
			Finishers, Finishers == 1 ? "tee" : "tees",
			aMedianString, aOwnFinishesString);
	}
	else
	{
		str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
			"No map like \"%s\" found.", pData->m_aName);
	}
	return false;
}

void CScore::SaveScore(int ClientID, float Time, const char *pTimestamp, float CpTime[NUM_CHECKPOINTS], bool NotEligible)
{
	CConsole *pCon = (CConsole *)GameServer()->Console();
	if(pCon->m_Cheated || NotEligible)
		return;

	CPlayer *pCurPlayer = GameServer()->m_apPlayers[ClientID];
	if(pCurPlayer->m_ScoreFinishResult != nullptr)
		dbg_msg("sql", "WARNING: previous save score result didn't complete, overwriting it now");
	pCurPlayer->m_ScoreFinishResult = std::make_shared<CScorePlayerResult>();
	auto Tmp = std::unique_ptr<CSqlScoreData>(new CSqlScoreData(pCurPlayer->m_ScoreFinishResult));
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));
	FormatUuid(GameServer()->GameUuid(), Tmp->m_aGameUuid, sizeof(Tmp->m_aGameUuid));
	Tmp->m_ClientID = ClientID;
	str_copy(Tmp->m_aName, Server()->ClientName(ClientID), sizeof(Tmp->m_aName));
	Tmp->m_Time = Time;
	str_copy(Tmp->m_aTimestamp, pTimestamp, sizeof(Tmp->m_aTimestamp));
	for(int i = 0; i < NUM_CHECKPOINTS; i++)
		Tmp->m_aCpCurrent[i] = CpTime[i];

	m_pPool->ExecuteWrite(SaveScoreThread, std::move(Tmp), "save score");
}

bool CScore::SaveScoreThread(IDbConnection *pSqlServer, const ISqlData *pGameData, bool Failure, char *pError, int ErrorSize)
{
	const CSqlScoreData *pData = dynamic_cast<const CSqlScoreData *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	char aBuf[1024];

	str_format(aBuf, sizeof(aBuf),
		"SELECT COUNT(*) AS NumFinished FROM %s_race WHERE Map=? AND Name=? ORDER BY time ASC LIMIT 1;",
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aName);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	int NumFinished = pSqlServer->GetInt(1);
	if(NumFinished == 0)
	{
		str_format(aBuf, sizeof(aBuf), "SELECT Points FROM %s_maps WHERE Map=?", pSqlServer->GetPrefix());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aMap);

		bool End;
		if(pSqlServer->Step(&End, pError, ErrorSize))
		{
			return true;
		}
		if(!End)
		{
			int Points = pSqlServer->GetInt(1);
			if(pSqlServer->AddPoints(pData->m_aName, Points, pError, ErrorSize))
			{
				return true;
			}
			str_format(paMessages[0], sizeof(paMessages[0]),
				"You earned %d point%s for finishing this map!",
				Points, Points == 1 ? "" : "s");
		}
	}

	// save score. Can't fail, because no UNIQUE/PRIMARY KEY constrain is defined.
	str_format(aBuf, sizeof(aBuf),
		"%s INTO %s_race("
		"	Map, Name, Timestamp, Time, Server, "
		"	cp1, cp2, cp3, cp4, cp5, cp6, cp7, cp8, cp9, cp10, cp11, cp12, cp13, "
		"	cp14, cp15, cp16, cp17, cp18, cp19, cp20, cp21, cp22, cp23, cp24, cp25, "
		"	GameID, DDNet7) "
		"VALUES (?, ?, %s, %.2f, ?, "
		"	%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, "
		"	%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, "
		"	%.2f, %.2f, %.2f, %.2f, %.2f, %.2f, %.2f, "
		"	?, false);",
		pSqlServer->InsertIgnore(), pSqlServer->GetPrefix(),
		pSqlServer->InsertTimestampAsUtc(), pData->m_Time,
		pData->m_aCpCurrent[0], pData->m_aCpCurrent[1], pData->m_aCpCurrent[2],
		pData->m_aCpCurrent[3], pData->m_aCpCurrent[4], pData->m_aCpCurrent[5],
		pData->m_aCpCurrent[6], pData->m_aCpCurrent[7], pData->m_aCpCurrent[8],
		pData->m_aCpCurrent[9], pData->m_aCpCurrent[10], pData->m_aCpCurrent[11],
		pData->m_aCpCurrent[12], pData->m_aCpCurrent[13], pData->m_aCpCurrent[14],
		pData->m_aCpCurrent[15], pData->m_aCpCurrent[16], pData->m_aCpCurrent[17],
		pData->m_aCpCurrent[18], pData->m_aCpCurrent[19], pData->m_aCpCurrent[20],
		pData->m_aCpCurrent[21], pData->m_aCpCurrent[22], pData->m_aCpCurrent[23],
		pData->m_aCpCurrent[24]);
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aName);
	pSqlServer->BindString(3, pData->m_aTimestamp);
	pSqlServer->BindString(4, g_Config.m_SvSqlServerName);
	pSqlServer->BindString(5, pData->m_aGameUuid);
	pSqlServer->Print();
	int NumInserted;
	if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
	{
		return true;
	}
	return false;
}

void CScore::SaveTeamScore(int *aClientIDs, unsigned int Size, float Time, const char *pTimestamp)
{
	CConsole *pCon = (CConsole *)GameServer()->Console();
	if(pCon->m_Cheated)
		return;
	for(unsigned int i = 0; i < Size; i++)
	{
		if(GameServer()->m_apPlayers[aClientIDs[i]]->m_NotEligibleForFinish)
			return;
	}
	auto Tmp = std::unique_ptr<CSqlTeamScoreData>(new CSqlTeamScoreData());
	for(unsigned int i = 0; i < Size; i++)
		str_copy(Tmp->m_aaNames[i], Server()->ClientName(aClientIDs[i]), sizeof(Tmp->m_aaNames[i]));
	Tmp->m_Size = Size;
	Tmp->m_Time = Time;
	str_copy(Tmp->m_aTimestamp, pTimestamp, sizeof(Tmp->m_aTimestamp));
	FormatUuid(GameServer()->GameUuid(), Tmp->m_aGameUuid, sizeof(Tmp->m_aGameUuid));
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));

	m_pPool->ExecuteWrite(SaveTeamScoreThread, std::move(Tmp), "save team score");
}

bool CScore::SaveTeamScoreThread(IDbConnection *pSqlServer, const ISqlData *pGameData, bool Failure, char *pError, int ErrorSize)
{
	const CSqlTeamScoreData *pData = dynamic_cast<const CSqlTeamScoreData *>(pGameData);

	char aBuf[512];

	// get the names sorted in a tab separated string
	std::vector<std::string> aNames;
	for(unsigned int i = 0; i < pData->m_Size; i++)
		aNames.push_back(pData->m_aaNames[i]);

	std::sort(aNames.begin(), aNames.end());
	str_format(aBuf, sizeof(aBuf),
		"SELECT l.ID, Name, Time "
		"FROM (" // preselect teams with first name in team
		"  SELECT ID "
		"  FROM %s_teamrace "
		"  WHERE Map = ? AND Name = ? AND DDNet7 = false"
		") as l INNER JOIN %s_teamrace AS r ON l.ID = r.ID "
		"ORDER BY l.ID, Name COLLATE %s;",
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pSqlServer->BinaryCollate());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aaNames[0]);

	bool FoundTeam = false;
	float Time;
	CTeamrank Teamrank;
	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		bool SearchTeamEnd = false;
		while(!SearchTeamEnd)
		{
			Time = pSqlServer->GetFloat(3);
			if(Teamrank.NextSqlResult(pSqlServer, &SearchTeamEnd, pError, ErrorSize))
			{
				return true;
			}
			if(Teamrank.SamePlayers(&aNames))
			{
				FoundTeam = true;
				break;
			}
		}
	}
	if(FoundTeam)
	{
		dbg_msg("sql", "found team rank from same team (old time: %f, new time: %f)", Time, pData->m_Time);
		if(pData->m_Time < Time)
		{
			str_format(aBuf, sizeof(aBuf),
				"UPDATE %s_teamrace SET Time=%.2f, Timestamp=?, DDNet7=false, GameID=? WHERE ID = ?;",
				pSqlServer->GetPrefix(), pData->m_Time);
			if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			{
				return true;
			}
			pSqlServer->BindString(1, pData->m_aTimestamp);
			pSqlServer->BindString(2, pData->m_aGameUuid);
			pSqlServer->BindBlob(3, Teamrank.m_TeamID.m_aData, sizeof(Teamrank.m_TeamID.m_aData));
			pSqlServer->Print();
			int NumUpdated;
			if(pSqlServer->ExecuteUpdate(&NumUpdated, pError, ErrorSize))
			{
				return true;
			}
		}
	}
	else
	{
		CUuid GameID = RandomUuid();
		for(unsigned int i = 0; i < pData->m_Size; i++)
		{
			// if no entry found... create a new one
			str_format(aBuf, sizeof(aBuf),
				"%s INTO %s_teamrace(Map, Name, Timestamp, Time, ID, GameID, DDNet7) "
				"VALUES (?, ?, %s, %.2f, ?, ?, false);",
				pSqlServer->InsertIgnore(), pSqlServer->GetPrefix(),
				pSqlServer->InsertTimestampAsUtc(), pData->m_Time);
			if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
			{
				return true;
			}
			pSqlServer->BindString(1, pData->m_aMap);
			pSqlServer->BindString(2, pData->m_aaNames[i]);
			pSqlServer->BindString(3, pData->m_aTimestamp);
			pSqlServer->BindBlob(4, GameID.m_aData, sizeof(GameID.m_aData));
			pSqlServer->BindString(5, pData->m_aGameUuid);
			pSqlServer->Print();
			int NumInserted;
			if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
			{
				return true;
			}
		}
	}
	return false;
}

void CScore::ShowRank(int ClientID, const char *pName)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowRankThread, "show rank", ClientID, pName, 0);
}

bool CScore::ShowRankThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());

	char aServerLike[16];
	str_format(aServerLike, sizeof(aServerLike), "%%%s%%", pData->m_aServer);

	// check sort method
	char aBuf[600];
	str_format(aBuf, sizeof(aBuf),
		"SELECT Rank, Time, PercentRank "
		"FROM ("
		"  SELECT RANK() OVER w AS Rank, PERCENT_RANK() OVER w as PercentRank, Name, MIN(Time) AS Time "
		"  FROM %s_race "
		"  WHERE Map = ? "
		"  AND Server LIKE ?"
		"  GROUP BY Name "
		"  WINDOW w AS (ORDER BY Time)"
		") as a "
		"WHERE Name = ?;",
		pSqlServer->GetPrefix());

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, aServerLike);
	pSqlServer->BindString(3, pData->m_aName);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}

	char aRegionalRank[16];
	if(End)
	{
		str_copy(aRegionalRank, "unranked", sizeof(aRegionalRank));
	}
	else
	{
		str_format(aRegionalRank, sizeof(aRegionalRank), "rank %d", pSqlServer->GetInt(1));
	}

	const char *pAny = "%";

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pAny);
	pSqlServer->BindString(3, pData->m_aName);

	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}

	if(!End)
	{
		int Rank = pSqlServer->GetInt(1);
		float Time = pSqlServer->GetFloat(2);
		// CEIL and FLOOR are not supported in SQLite
		int BetterThanPercent = std::floor(100.0 - 100.0 * pSqlServer->GetFloat(3));
		str_time_float(Time, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
		if(g_Config.m_SvHideScore)
		{
			str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
				"Your time: %s, better than %d%%", aBuf, BetterThanPercent);
		}
		else
		{
			pResult->m_MessageKind = CScorePlayerResult::ALL;

			if(str_comp_nocase(pData->m_aRequestingPlayer, pData->m_aName) == 0)
			{
				str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
					"%s - %s - better than %d%%",
					pData->m_aName, aBuf, BetterThanPercent);
			}
			else
			{
				str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
					"%s - %s - better than %d%% - requested by %s",
					pData->m_aName, aBuf, BetterThanPercent, pData->m_aRequestingPlayer);
			}

			str_format(pResult->m_Data.m_aaMessages[1], sizeof(pResult->m_Data.m_aaMessages[1]),
				"Global rank %d - %s %s",
				Rank, pData->m_aServer, aRegionalRank);
		}
	}
	else
	{
		str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
			"%s is not ranked", pData->m_aName);
	}
	return false;
}

void CScore::ShowTeamRank(int ClientID, const char *pName)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTeamRankThread, "show team rank", ClientID, pName, 0);
}

bool CScore::ShowTeamRankThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());

	// check sort method
	char aBuf[2400];

	str_format(aBuf, sizeof(aBuf),
		"SELECT l.ID, Name, Time, Rank, PercentRank "
		"FROM (" // teamrank score board
		"  SELECT RANK() OVER w AS Rank, PERCENT_RANK() OVER w AS PercentRank, ID "
		"  FROM %s_teamrace "
		"  WHERE Map = ? "
		"  GROUP BY ID "
		"  WINDOW w AS (ORDER BY Time)"
		") AS TeamRank INNER JOIN (" // select rank with Name in team
		"  SELECT ID "
		"  FROM %s_teamrace "
		"  WHERE Map = ? AND Name = ? "
		"  ORDER BY Time "
		"  LIMIT 1"
		") AS l ON TeamRank.ID = l.ID "
		"INNER JOIN %s_teamrace AS r ON l.ID = r.ID ",
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aMap);
	pSqlServer->BindString(3, pData->m_aName);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		float Time = pSqlServer->GetFloat(3);
		str_time_float(Time, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
		int Rank = pSqlServer->GetInt(4);
		// CEIL and FLOOR are not supported in SQLite
		int BetterThanPercent = std::floor(100.0 - 100.0 * pSqlServer->GetFloat(5));
		CTeamrank Teamrank;
		if(Teamrank.NextSqlResult(pSqlServer, &End, pError, ErrorSize))
		{
			return true;
		}

		char aFormattedNames[512] = "";
		for(unsigned int Name = 0; Name < Teamrank.m_NumNames; Name++)
		{
			str_append(aFormattedNames, Teamrank.m_aaNames[Name], sizeof(aFormattedNames));

			if(Name < Teamrank.m_NumNames - 2)
				str_append(aFormattedNames, ", ", sizeof(aFormattedNames));
			else if(Name < Teamrank.m_NumNames - 1)
				str_append(aFormattedNames, " & ", sizeof(aFormattedNames));
		}

		if(g_Config.m_SvHideScore)
		{
			str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
				"Your team time: %s, better than %d%%", aBuf, BetterThanPercent);
		}
		else
		{
			pResult->m_MessageKind = CScorePlayerResult::ALL;
			str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
				"%d. %s Team time: %s, better than %d%%, requested by %s",
				Rank, aFormattedNames, aBuf, BetterThanPercent, pData->m_aRequestingPlayer);
		}
	}
	else
	{
		str_format(pResult->m_Data.m_aaMessages[0], sizeof(pResult->m_Data.m_aaMessages[0]),
			"%s has no team ranks", pData->m_aName);
	}
	return false;
}

void CScore::ShowTop(int ClientID, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTopThread, "show top5", ClientID, "", Offset);
}

bool CScore::ShowTopThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());

	int LimitStart = maximum(abs(pData->m_Offset) - 1, 0);
	const char *pOrder = pData->m_Offset >= 0 ? "ASC" : "DESC";
	const char *pAny = "%";

	// check sort method
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SELECT Name, Time, Rank, Server "
		"FROM ("
		"  SELECT RANK() OVER w AS Rank, Name, MIN(Time) AS Time, Server "
		"  FROM %s_race "
		"  WHERE Map = ? "
		"  AND Server LIKE ? "
		"  GROUP BY Name "
		"  WINDOW w AS (ORDER BY Time)"
		") as a "
		"ORDER BY Rank %s "
		"LIMIT %d, ?;",
		pSqlServer->GetPrefix(),
		pOrder,
		LimitStart);

	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pAny);
	pSqlServer->BindInt(3, 5);

	// show top
	int Line = 0;
	str_copy(pResult->m_Data.m_aaMessages[Line], "------------ Global Top ------------", sizeof(pResult->m_Data.m_aaMessages[Line]));
	Line++;

	char aTime[32];
	bool End = false;
	bool HasLocal = false;

	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		char aName[MAX_NAME_LENGTH];
		pSqlServer->GetString(1, aName, sizeof(aName));
		float Time = pSqlServer->GetFloat(2);
		str_time_float(Time, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
		int Rank = pSqlServer->GetInt(3);
		str_format(pResult->m_Data.m_aaMessages[Line], sizeof(pResult->m_Data.m_aaMessages[Line]),
			"%d. %s Time: %s", Rank, aName, aTime);

		char aRecordServer[6];
		pSqlServer->GetString(4, aRecordServer, sizeof(aRecordServer));

		HasLocal = HasLocal || str_comp(aRecordServer, pData->m_aServer) == 0;

		Line++;
	}

	if(!HasLocal)
	{
		char aServerLike[16];
		str_format(aServerLike, sizeof(aServerLike), "%%%s%%", pData->m_aServer);

		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aMap);
		pSqlServer->BindString(2, aServerLike);
		pSqlServer->BindInt(3, 3);

		str_format(pResult->m_Data.m_aaMessages[Line], sizeof(pResult->m_Data.m_aaMessages[Line]),
			"------------ %s Top ------------", pData->m_aServer);
		Line++;

		// show top
		while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
		{
			char aName[MAX_NAME_LENGTH];
			pSqlServer->GetString(1, aName, sizeof(aName));
			float Time = pSqlServer->GetFloat(2);
			str_time_float(Time, TIME_HOURS_CENTISECS, aTime, sizeof(aTime));
			int Rank = pSqlServer->GetInt(3);
			str_format(pResult->m_Data.m_aaMessages[Line], sizeof(pResult->m_Data.m_aaMessages[Line]),
				"%d. %s Time: %s", Rank, aName, aTime);
			Line++;
		}
	}
	else
	{
		str_copy(pResult->m_Data.m_aaMessages[Line], "---------------------------------------",
			sizeof(pResult->m_Data.m_aaMessages[Line]));
	}

	if(!End)
	{
		return true;
	}

	return false;
}

void CScore::ShowTeamTop5(int ClientID, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTeamTop5Thread, "show team top5", ClientID, "", Offset);
}

bool CScore::ShowTeamTop5Thread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	int LimitStart = maximum(abs(pData->m_Offset) - 1, 0);
	const char *pOrder = pData->m_Offset >= 0 ? "ASC" : "DESC";

	// check sort method
	char aBuf[512];

	str_format(aBuf, sizeof(aBuf),
		"SELECT Name, Time, Rank, TeamSize "
		"FROM (" // limit to 5
		"  SELECT TeamSize, Rank, ID "
		"  FROM (" // teamrank score board
		"    SELECT RANK() OVER w AS Rank, ID, COUNT(*) AS Teamsize "
		"    FROM %s_teamrace "
		"    WHERE Map = ? "
		"    GROUP BY Id "
		"    WINDOW w AS (ORDER BY Time)"
		"  ) as l1 "
		"  ORDER BY Rank %s "
		"  LIMIT %d, 5"
		") as l2 "
		"INNER JOIN %s_teamrace as r ON l2.ID = r.ID "
		"ORDER BY Rank %s, r.ID, Name ASC;",
		pSqlServer->GetPrefix(), pOrder, LimitStart, pSqlServer->GetPrefix(), pOrder);
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);

	// show teamtop5
	int Line = 0;
	str_copy(paMessages[Line++], "------- Team Top 5 -------", sizeof(paMessages[Line]));

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		for(Line = 1; Line < 6; Line++) // print
		{
			bool Last = false;
			float Time = pSqlServer->GetFloat(2);
			str_time_float(Time, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
			int Rank = pSqlServer->GetInt(3);
			int TeamSize = pSqlServer->GetInt(4);

			char aNames[2300] = {0};
			for(int i = 0; i < TeamSize; i++)
			{
				char aName[MAX_NAME_LENGTH];
				pSqlServer->GetString(1, aName, sizeof(aName));
				str_append(aNames, aName, sizeof(aNames));
				if(i < TeamSize - 2)
					str_append(aNames, ", ", sizeof(aNames));
				else if(i == TeamSize - 2)
					str_append(aNames, " & ", sizeof(aNames));
				if(pSqlServer->Step(&Last, pError, ErrorSize))
				{
					return true;
				}
				if(Last)
				{
					break;
				}
			}
			str_format(paMessages[Line], sizeof(paMessages[Line]), "%d. %s Team Time: %s",
				Rank, aNames, aBuf);
			if(Last)
			{
				Line++;
				break;
			}
		}
	}

	str_copy(paMessages[Line], "-------------------------------", sizeof(paMessages[Line]));
	return false;
}

void CScore::ShowTeamTop5(int ClientID, const char *pName, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowPlayerTeamTop5Thread, "show team top5 player", ClientID, pName, Offset);
}

bool CScore::ShowPlayerTeamTop5Thread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	int LimitStart = maximum(abs(pData->m_Offset) - 1, 0);
	const char *pOrder = pData->m_Offset >= 0 ? "ASC" : "DESC";

	// check sort method
	char aBuf[2400];

	str_format(aBuf, sizeof(aBuf),
		"SELECT l.ID, Name, Time, Rank "
		"FROM (" // teamrank score board
		"  SELECT RANK() OVER w AS Rank, ID "
		"  FROM %s_teamrace "
		"  WHERE Map = ? "
		"  GROUP BY ID "
		"  WINDOW w AS (ORDER BY Time)"
		") AS TeamRank INNER JOIN (" // select rank with Name in team
		"  SELECT ID "
		"  FROM %s_teamrace "
		"  WHERE Map = ? AND Name = ? "
		"  ORDER BY Time %s "
		"  LIMIT %d, 5 "
		") AS l ON TeamRank.ID = l.ID "
		"INNER JOIN %s_teamrace AS r ON l.ID = r.ID "
		"ORDER BY Time %s, l.ID ",
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pOrder, LimitStart, pSqlServer->GetPrefix(), pOrder);
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, pData->m_aMap);
	pSqlServer->BindString(3, pData->m_aName);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		// show teamtop5
		int Line = 0;
		str_copy(paMessages[Line++], "------- Team Top 5 -------", sizeof(paMessages[Line]));

		for(Line = 1; Line < 6; Line++) // print
		{
			float Time = pSqlServer->GetFloat(3);
			str_time_float(Time, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
			int Rank = pSqlServer->GetInt(4);
			CTeamrank Teamrank;
			bool Last;
			if(Teamrank.NextSqlResult(pSqlServer, &Last, pError, ErrorSize))
			{
				return true;
			}

			char aFormattedNames[512] = "";
			for(unsigned int Name = 0; Name < Teamrank.m_NumNames; Name++)
			{
				str_append(aFormattedNames, Teamrank.m_aaNames[Name], sizeof(aFormattedNames));

				if(Name < Teamrank.m_NumNames - 2)
					str_append(aFormattedNames, ", ", sizeof(aFormattedNames));
				else if(Name < Teamrank.m_NumNames - 1)
					str_append(aFormattedNames, " & ", sizeof(aFormattedNames));
			}

			str_format(paMessages[Line], sizeof(paMessages[Line]), "%d. %s Team Time: %s",
				Rank, aFormattedNames, aBuf);
			if(Last)
			{
				Line++;
				break;
			}
		}
		str_copy(paMessages[Line], "-------------------------------", sizeof(paMessages[Line]));
	}
	else
	{
		if(pData->m_Offset == 0)
			str_format(paMessages[0], sizeof(paMessages[0]), "%s has no team ranks", pData->m_aName);
		else
			str_format(paMessages[0], sizeof(paMessages[0]), "%s has no team ranks in the specified range", pData->m_aName);
	}
	return false;
}

void CScore::ShowTimes(int ClientID, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTimesThread, "show times", ClientID, "", Offset);
}

void CScore::ShowTimes(int ClientID, const char *pName, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTimesThread, "show times", ClientID, pName, Offset);
}

bool CScore::ShowTimesThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	int LimitStart = maximum(abs(pData->m_Offset) - 1, 0);
	const char *pOrder = pData->m_Offset >= 0 ? "DESC" : "ASC";

	char aCurrentTimestamp[512];
	pSqlServer->ToUnixTimestamp("CURRENT_TIMESTAMP", aCurrentTimestamp, sizeof(aCurrentTimestamp));
	char aTimestamp[512];
	pSqlServer->ToUnixTimestamp("Timestamp", aTimestamp, sizeof(aTimestamp));
	char aBuf[512];
	if(pData->m_aName[0] != '\0') // last 5 times of a player
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Time, (%s-%s) as Ago, %s as Stamp, Server "
			"FROM %s_race "
			"WHERE Map = ? AND Name = ? "
			"ORDER BY Timestamp %s "
			"LIMIT ?, 5;",
			aCurrentTimestamp, aTimestamp, aTimestamp,
			pSqlServer->GetPrefix(), pOrder);
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aMap);
		pSqlServer->BindString(2, pData->m_aName);
		pSqlServer->BindInt(3, LimitStart);
	}
	else // last 5 times of server
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Time, (%s-%s) as Ago, %s as Stamp, Server, Name "
			"FROM %s_race "
			"WHERE Map = ? "
			"ORDER BY Timestamp %s "
			"LIMIT ?, 5;",
			aCurrentTimestamp, aTimestamp, aTimestamp,
			pSqlServer->GetPrefix(), pOrder);
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aMap);
		pSqlServer->BindInt(2, LimitStart);
	}

	// show top5
	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(End)
	{
		str_copy(paMessages[0], "There are no times in the specified range", sizeof(paMessages[0]));
		return false;
	}

	str_copy(paMessages[0], "------------- Last Times -------------", sizeof(paMessages[0]));
	int Line = 1;

	do
	{
		float Time = pSqlServer->GetFloat(1);
		str_time_float(Time, TIME_HOURS_CENTISECS, aBuf, sizeof(aBuf));
		int Ago = pSqlServer->GetInt(2);
		int Stamp = pSqlServer->GetInt(3);
		char aServer[5];
		pSqlServer->GetString(4, aServer, sizeof(aServer));
		char aServerFormatted[8] = "\0";
		if(str_comp(aServer, "UNK") != 0)
			str_format(aServerFormatted, sizeof(aServerFormatted), "[%s] ", aServer);

		char aAgoString[40] = "\0";
		sqlstr::AgoTimeToString(Ago, aAgoString, sizeof(aAgoString));

		if(pData->m_aName[0] != '\0') // last 5 times of a player
		{
			if(Stamp == 0) // stamp is 00:00:00 cause it's an old entry from old times where there where no stamps yet
				str_format(paMessages[Line], sizeof(paMessages[Line]),
					"%s%s, don't know how long ago", aServerFormatted, aBuf);
			else
				str_format(paMessages[Line], sizeof(paMessages[Line]),
					"%s%s ago, %s", aServerFormatted, aAgoString, aBuf);
		}
		else // last 5 times of the server
		{
			char aName[MAX_NAME_LENGTH];
			pSqlServer->GetString(5, aName, sizeof(aName));
			if(Stamp == 0) // stamp is 00:00:00 cause it's an old entry from old times where there where no stamps yet
			{
				str_format(paMessages[Line], sizeof(paMessages[Line]),
					"%s%s, %s, don't know when", aServerFormatted, aName, aBuf);
			}
			else
			{
				str_format(paMessages[Line], sizeof(paMessages[Line]),
					"%s%s, %s ago, %s", aServerFormatted, aName, aAgoString, aBuf);
			}
		}
		Line++;
	} while(!pSqlServer->Step(&End, pError, ErrorSize) && !End);
	if(!End)
	{
		return true;
	}
	str_copy(paMessages[Line], "----------------------------------------------------", sizeof(paMessages[Line]));

	return false;
}

void CScore::ShowPoints(int ClientID, const char *pName)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowPointsThread, "show points", ClientID, pName, 0);
}

bool CScore::ShowPointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SELECT ("
		"  SELECT COUNT(Name) + 1 FROM %s_points WHERE Points > ("
		"    SELECT points FROM %s_points WHERE Name = ?"
		")) as Rank, Points, Name "
		"FROM %s_points WHERE Name = ?;",
		pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aName);
	pSqlServer->BindString(2, pData->m_aName);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		int Rank = pSqlServer->GetInt(1);
		int Count = pSqlServer->GetInt(2);
		char aName[MAX_NAME_LENGTH];
		pSqlServer->GetString(3, aName, sizeof(aName));
		pResult->m_MessageKind = CScorePlayerResult::ALL;
		str_format(paMessages[0], sizeof(paMessages[0]),
			"%d. %s Points: %d, requested by %s",
			Rank, aName, Count, pData->m_aRequestingPlayer);
	}
	else
	{
		str_format(paMessages[0], sizeof(paMessages[0]),
			"%s has not collected any points so far", pData->m_aName);
	}
	return false;
}

void CScore::ShowTopPoints(int ClientID, int Offset)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(ShowTopPointsThread, "show top points", ClientID, "", Offset);
}

bool CScore::ShowTopPointsThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	int LimitStart = maximum(pData->m_Offset - 1, 0);

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SELECT RANK() OVER (ORDER BY a.Points DESC) as Rank, Points, Name "
		"FROM ("
		"  SELECT Points, Name "
		"  FROM %s_points "
		"  ORDER BY Points DESC LIMIT ?"
		") as a "
		"LIMIT ?, 5;",
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindInt(1, LimitStart + 5);
	pSqlServer->BindInt(2, LimitStart);

	// show top points
	str_copy(paMessages[0], "-------- Top Points --------", sizeof(paMessages[0]));

	bool End = false;
	int Line = 1;
	while(!pSqlServer->Step(&End, pError, ErrorSize) && !End)
	{
		int Rank = pSqlServer->GetInt(1);
		int Points = pSqlServer->GetInt(2);
		char aName[MAX_NAME_LENGTH];
		pSqlServer->GetString(3, aName, sizeof(aName));
		str_format(paMessages[Line], sizeof(paMessages[Line]),
			"%d. %s Points: %d", Rank, aName, Points);
		Line++;
	}
	if(!End)
	{
		return true;
	}
	str_copy(paMessages[Line], "-------------------------------", sizeof(paMessages[Line]));

	return false;
}

void CScore::RandomMap(int ClientID, int Stars)
{
	auto pResult = std::make_shared<CScoreRandomMapResult>(ClientID);
	GameServer()->m_SqlRandomMapResult = pResult;

	auto Tmp = std::unique_ptr<CSqlRandomMapRequest>(new CSqlRandomMapRequest(pResult));
	Tmp->m_Stars = Stars;
	str_copy(Tmp->m_aCurrentMap, g_Config.m_SvMap, sizeof(Tmp->m_aCurrentMap));
	str_copy(Tmp->m_aServerType, g_Config.m_SvServerType, sizeof(Tmp->m_aServerType));
	str_copy(Tmp->m_aRequestingPlayer, GameServer()->Server()->ClientName(ClientID), sizeof(Tmp->m_aRequestingPlayer));

	m_pPool->Execute(RandomMapThread, std::move(Tmp), "random map");
}

bool CScore::RandomMapThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlRandomMapRequest *pData = dynamic_cast<const CSqlRandomMapRequest *>(pGameData);
	CScoreRandomMapResult *pResult = dynamic_cast<CScoreRandomMapResult *>(pGameData->m_pResult.get());

	char aBuf[512];
	if(0 <= pData->m_Stars && pData->m_Stars <= 5)
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Map FROM %s_maps "
			"WHERE Server = ? AND Map != ? AND Stars = ? "
			"ORDER BY %s LIMIT 1;",
			pSqlServer->GetPrefix(), pSqlServer->Random());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindInt(3, pData->m_Stars);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Map FROM %s_maps "
			"WHERE Server = ? AND Map != ? "
			"ORDER BY %s LIMIT 1;",
			pSqlServer->GetPrefix(), pSqlServer->Random());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
	}
	pSqlServer->BindString(1, pData->m_aServerType);
	pSqlServer->BindString(2, pData->m_aCurrentMap);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		pSqlServer->GetString(1, pResult->m_aMap, sizeof(pResult->m_aMap));
	}
	else
	{
		str_copy(pResult->m_aMessage, "No maps found on this server!", sizeof(pResult->m_aMessage));
	}
	return false;
}

void CScore::RandomUnfinishedMap(int ClientID, int Stars)
{
	auto pResult = std::make_shared<CScoreRandomMapResult>(ClientID);
	GameServer()->m_SqlRandomMapResult = pResult;

	auto Tmp = std::unique_ptr<CSqlRandomMapRequest>(new CSqlRandomMapRequest(pResult));
	Tmp->m_Stars = Stars;
	str_copy(Tmp->m_aCurrentMap, g_Config.m_SvMap, sizeof(Tmp->m_aCurrentMap));
	str_copy(Tmp->m_aServerType, g_Config.m_SvServerType, sizeof(Tmp->m_aServerType));
	str_copy(Tmp->m_aRequestingPlayer, GameServer()->Server()->ClientName(ClientID), sizeof(Tmp->m_aRequestingPlayer));

	m_pPool->Execute(RandomUnfinishedMapThread, std::move(Tmp), "random unfinished map");
}

bool CScore::RandomUnfinishedMapThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlRandomMapRequest *pData = dynamic_cast<const CSqlRandomMapRequest *>(pGameData);
	CScoreRandomMapResult *pResult = dynamic_cast<CScoreRandomMapResult *>(pGameData->m_pResult.get());

	char aBuf[512];
	if(pData->m_Stars >= 0)
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Map "
			"FROM %s_maps "
			"WHERE Server = ? AND Map != ? AND Stars = ? AND Map NOT IN ("
			"  SELECT Map "
			"  FROM %s_race "
			"  WHERE Name = ?"
			") ORDER BY %s "
			"LIMIT 1;",
			pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pSqlServer->Random());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aServerType);
		pSqlServer->BindString(2, pData->m_aCurrentMap);
		pSqlServer->BindInt(3, pData->m_Stars);
		pSqlServer->BindString(4, pData->m_aRequestingPlayer);
	}
	else
	{
		str_format(aBuf, sizeof(aBuf),
			"SELECT Map "
			"FROM %s_maps AS maps "
			"WHERE Server = ? AND Map != ? AND Map NOT IN ("
			"  SELECT Map "
			"  FROM %s_race as race "
			"  WHERE Name = ?"
			") ORDER BY %s "
			"LIMIT 1;",
			pSqlServer->GetPrefix(), pSqlServer->GetPrefix(), pSqlServer->Random());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pData->m_aServerType);
		pSqlServer->BindString(2, pData->m_aCurrentMap);
		pSqlServer->BindString(3, pData->m_aRequestingPlayer);
	}

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		pSqlServer->GetString(1, pResult->m_aMap, sizeof(pResult->m_aMap));
	}
	else
	{
		str_copy(pResult->m_aMessage, "You have no more unfinished maps on this server!", sizeof(pResult->m_aMessage));
	}
	return false;
}

void CScore::SaveTeam(int ClientID, const char *Code, const char *Server)
{
	if(RateLimitPlayer(ClientID))
		return;
	auto *pController = ((CGameControllerDDRace *)(GameServer()->m_pController));
	int Team = pController->m_Teams.m_Core.Team(ClientID);
	if(pController->m_Teams.GetSaving(Team))
		return;

	auto SaveResult = std::make_shared<CScoreSaveResult>(ClientID, pController);
	SaveResult->m_SaveID = RandomUuid();
	int Result = SaveResult->m_SavedTeam.Save(Team);
	if(CSaveTeam::HandleSaveError(Result, ClientID, GameServer()))
		return;
	pController->m_Teams.SetSaving(Team, SaveResult);

	auto Tmp = std::unique_ptr<CSqlTeamSave>(new CSqlTeamSave(SaveResult));
	str_copy(Tmp->m_aCode, Code, sizeof(Tmp->m_aCode));
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));
	str_copy(Tmp->m_aServer, Server, sizeof(Tmp->m_aServer));
	str_copy(Tmp->m_aClientName, this->Server()->ClientName(ClientID), sizeof(Tmp->m_aClientName));
	Tmp->m_aGeneratedCode[0] = '\0';
	GeneratePassphrase(Tmp->m_aGeneratedCode, sizeof(Tmp->m_aGeneratedCode));

	pController->m_Teams.KillSavedTeam(ClientID, Team);
	m_pPool->ExecuteWrite(SaveTeamThread, std::move(Tmp), "save team");
}

bool CScore::SaveTeamThread(IDbConnection *pSqlServer, const ISqlData *pGameData, bool Failure, char *pError, int ErrorSize)
{
	const CSqlTeamSave *pData = dynamic_cast<const CSqlTeamSave *>(pGameData);
	CScoreSaveResult *pResult = dynamic_cast<CScoreSaveResult *>(pGameData->m_pResult.get());

	char aSaveID[UUID_MAXSTRSIZE];
	FormatUuid(pResult->m_SaveID, aSaveID, UUID_MAXSTRSIZE);

	char *pSaveState = pResult->m_SavedTeam.GetString();
	char aBuf[65536];

	dbg_msg("score/dbg", "code=%s failure=%d", pData->m_aCode, (int)Failure);
	bool UseGeneratedCode = pData->m_aCode[0] == '\0' || Failure;

	bool Retry = false;
	// two tries, first use the user provided code, then the autogenerated
	do
	{
		Retry = false;
		char aCode[128] = {0};
		if(UseGeneratedCode)
			str_copy(aCode, pData->m_aGeneratedCode, sizeof(aCode));
		else
			str_copy(aCode, pData->m_aCode, sizeof(aCode));

		str_format(aBuf, sizeof(aBuf),
			"%s INTO %s_saves(Savegame, Map, Code, Timestamp, Server, SaveID, DDNet7) "
			"VALUES (?, ?, ?, CURRENT_TIMESTAMP, ?, ?, false)",
			pSqlServer->InsertIgnore(), pSqlServer->GetPrefix());
		if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
		{
			return true;
		}
		pSqlServer->BindString(1, pSaveState);
		pSqlServer->BindString(2, pData->m_aMap);
		pSqlServer->BindString(3, aCode);
		pSqlServer->BindString(4, pData->m_aServer);
		pSqlServer->BindString(5, aSaveID);
		pSqlServer->Print();
		int NumInserted;
		if(pSqlServer->ExecuteUpdate(&NumInserted, pError, ErrorSize))
		{
			return true;
		}
		if(NumInserted == 1)
		{
			if(!Failure)
			{
				if(str_comp(pData->m_aServer, g_Config.m_SvSqlServerName) == 0)
				{
					str_format(pResult->m_aMessage, sizeof(pResult->m_aMessage),
						"Team successfully saved by %s. Use '/load %s' to continue",
						pData->m_aClientName, aCode);
				}
				else
				{
					str_format(pResult->m_aMessage, sizeof(pResult->m_aMessage),
						"Team successfully saved by %s. Use '/load %s' on %s to continue",
						pData->m_aClientName, aCode, pData->m_aServer);
				}
			}
			else
			{
				str_copy(pResult->m_aBroadcast,
					"Database connection failed, teamsave written to a file instead. Admins will add it manually in a few days.",
					sizeof(pResult->m_aBroadcast));
				if(str_comp(pData->m_aServer, g_Config.m_SvSqlServerName) == 0)
				{
					str_format(pResult->m_aMessage, sizeof(pResult->m_aMessage),
						"Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' to continue",
						pData->m_aClientName, aCode);
				}
				else
				{
					str_format(pResult->m_aMessage, sizeof(pResult->m_aMessage),
						"Team successfully saved by %s. The database connection failed, using generated save code instead to avoid collisions. Use '/load %s' on %s to continue",
						pData->m_aClientName, aCode, pData->m_aServer);
				}
			}

			pResult->m_Status = CScoreSaveResult::SAVE_SUCCESS;
		}
		else if(!UseGeneratedCode)
		{
			UseGeneratedCode = true;
			Retry = true;
		}
	} while(Retry);

	if(pResult->m_Status != CScoreSaveResult::SAVE_SUCCESS)
	{
		dbg_msg("sql", "ERROR: This save-code already exists");
		pResult->m_Status = CScoreSaveResult::SAVE_FAILED;
		str_copy(pResult->m_aMessage, "This save-code already exists", sizeof(pResult->m_aMessage));
	}
	return false;
}

void CScore::LoadTeam(const char *Code, int ClientID)
{
	if(RateLimitPlayer(ClientID))
		return;
	auto *pController = ((CGameControllerDDRace *)(GameServer()->m_pController));
	int Team = pController->m_Teams.m_Core.Team(ClientID);
	if(pController->m_Teams.GetSaving(Team))
		return;
	if(Team < TEAM_FLOCK || Team >= MAX_CLIENTS || (g_Config.m_SvTeam != 3 && Team == TEAM_FLOCK))
	{
		GameServer()->SendChatTarget(ClientID, "You have to be in a team (from 1-63)");
		return;
	}
	if(pController->m_Teams.GetTeamState(Team) != CGameTeams::TEAMSTATE_OPEN)
	{
		GameServer()->SendChatTarget(ClientID, "Team can't be loaded while racing");
		return;
	}
	auto SaveResult = std::make_shared<CScoreSaveResult>(ClientID, pController);
	SaveResult->m_Status = CScoreSaveResult::LOAD_FAILED;
	pController->m_Teams.SetSaving(Team, SaveResult);
	auto Tmp = std::unique_ptr<CSqlTeamLoad>(new CSqlTeamLoad(SaveResult));
	str_copy(Tmp->m_aCode, Code, sizeof(Tmp->m_aCode));
	str_copy(Tmp->m_aMap, g_Config.m_SvMap, sizeof(Tmp->m_aMap));
	Tmp->m_ClientID = ClientID;
	str_copy(Tmp->m_aRequestingPlayer, Server()->ClientName(ClientID), sizeof(Tmp->m_aRequestingPlayer));
	Tmp->m_NumPlayer = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(pController->m_Teams.m_Core.Team(i) == Team)
		{
			// put all names at the beginning of the array
			str_copy(Tmp->m_aClientNames[Tmp->m_NumPlayer], Server()->ClientName(i), sizeof(Tmp->m_aClientNames[Tmp->m_NumPlayer]));
			Tmp->m_aClientID[Tmp->m_NumPlayer] = i;
			Tmp->m_NumPlayer++;
		}
	}
	m_pPool->ExecuteWrite(LoadTeamThread, std::move(Tmp), "load team");
}

bool CScore::LoadTeamThread(IDbConnection *pSqlServer, const ISqlData *pGameData, bool Failure, char *pError, int ErrorSize)
{
	const CSqlTeamLoad *pData = dynamic_cast<const CSqlTeamLoad *>(pGameData);
	CScoreSaveResult *pResult = dynamic_cast<CScoreSaveResult *>(pGameData->m_pResult.get());
	pResult->m_Status = CScoreSaveResult::LOAD_FAILED;

	char aCurrentTimestamp[512];
	pSqlServer->ToUnixTimestamp("CURRENT_TIMESTAMP", aCurrentTimestamp, sizeof(aCurrentTimestamp));
	char aTimestamp[512];
	pSqlServer->ToUnixTimestamp("Timestamp", aTimestamp, sizeof(aTimestamp));

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SELECT Savegame, %s-%s AS Ago, SaveID "
		"FROM %s_saves "
		"where Code = ? AND Map = ? AND DDNet7 = false;",
		aCurrentTimestamp, aTimestamp,
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aCode);
	pSqlServer->BindString(2, pData->m_aMap);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(End)
	{
		str_copy(pResult->m_aMessage, "No such savegame for this map", sizeof(pResult->m_aMessage));
		return false;
	}

	pResult->m_SaveID = UUID_NO_SAVE_ID;
	if(!pSqlServer->IsNull(3))
	{
		char aSaveID[UUID_MAXSTRSIZE];
		pSqlServer->GetString(3, aSaveID, sizeof(aSaveID));
		if(ParseUuid(&pResult->m_SaveID, aSaveID) || pResult->m_SaveID == UUID_NO_SAVE_ID)
		{
			str_copy(pResult->m_aMessage, "Unable to load savegame: SaveID corrupted", sizeof(pResult->m_aMessage));
			return false;
		}
	}

	char aSaveString[65536];
	pSqlServer->GetString(1, aSaveString, sizeof(aSaveString));
	int Num = pResult->m_SavedTeam.FromString(aSaveString);

	if(Num != 0)
	{
		str_copy(pResult->m_aMessage, "Unable to load savegame: data corrupted", sizeof(pResult->m_aMessage));
		return false;
	}

	bool Found = false;
	for(int i = 0; i < pResult->m_SavedTeam.GetMembersCount(); i++)
	{
		if(str_comp(pResult->m_SavedTeam.m_pSavedTees[i].GetName(), pData->m_aRequestingPlayer) == 0)
		{
			Found = true;
			break;
		}
	}
	if(!Found)
	{
		str_copy(pResult->m_aMessage, "You don't belong to this team", sizeof(pResult->m_aMessage));
		return false;
	}

	int Since = pSqlServer->GetInt(2);
	if(Since < g_Config.m_SvSaveSwapGamesDelay)
	{
		str_format(pResult->m_aMessage, sizeof(pResult->m_aMessage),
			"You have to wait %d seconds until you can load this savegame",
			g_Config.m_SvSaveSwapGamesDelay - Since);
		return false;
	}

	bool CanLoad = pResult->m_SavedTeam.MatchPlayers(
		pData->m_aClientNames, pData->m_aClientID, pData->m_NumPlayer,
		pResult->m_aMessage, sizeof(pResult->m_aMessage));

	if(!CanLoad)
		return false;

	str_format(aBuf, sizeof(aBuf),
		"DELETE FROM %s_saves "
		"WHERE Code = ? AND Map = ? AND SaveID %s;",
		pSqlServer->GetPrefix(),
		pResult->m_SaveID != UUID_NO_SAVE_ID ? "= ?" : "IS NULL");
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aCode);
	pSqlServer->BindString(2, pData->m_aMap);
	char aUuid[UUID_MAXSTRSIZE];
	if(pResult->m_SaveID != UUID_NO_SAVE_ID)
	{
		FormatUuid(pResult->m_SaveID, aUuid, sizeof(aUuid));
		pSqlServer->BindString(3, aUuid);
	}
	pSqlServer->Print();
	int NumDeleted;
	if(pSqlServer->ExecuteUpdate(&NumDeleted, pError, ErrorSize))
	{
		return true;
	}

	if(NumDeleted != 1)
	{
		str_copy(pResult->m_aMessage, "Unable to load savegame: loaded on a different server", sizeof(pResult->m_aMessage));
		return false;
	}

	pResult->m_Status = CScoreSaveResult::LOAD_SUCCESS;
	str_copy(pResult->m_aMessage, "Loading successfully done", sizeof(pResult->m_aMessage));
	return false;
}

void CScore::GetSaves(int ClientID)
{
	if(RateLimitPlayer(ClientID))
		return;
	ExecPlayerThread(GetSavesThread, "get saves", ClientID, "", 0);
}

bool CScore::GetSavesThread(IDbConnection *pSqlServer, const ISqlData *pGameData, char *pError, int ErrorSize)
{
	const CSqlPlayerRequest *pData = dynamic_cast<const CSqlPlayerRequest *>(pGameData);
	CScorePlayerResult *pResult = dynamic_cast<CScorePlayerResult *>(pGameData->m_pResult.get());
	auto *paMessages = pResult->m_Data.m_aaMessages;

	char aSaveLike[128] = "";
	str_append(aSaveLike, "%\n", sizeof(aSaveLike));
	sqlstr::EscapeLike(aSaveLike + str_length(aSaveLike),
		pData->m_aRequestingPlayer,
		sizeof(aSaveLike) - str_length(aSaveLike));
	str_append(aSaveLike, "\t%", sizeof(aSaveLike));

	char aCurrentTimestamp[512];
	pSqlServer->ToUnixTimestamp("CURRENT_TIMESTAMP", aCurrentTimestamp, sizeof(aCurrentTimestamp));
	char aMaxTimestamp[512];
	pSqlServer->ToUnixTimestamp("MAX(Timestamp)", aMaxTimestamp, sizeof(aMaxTimestamp));

	char aBuf[512];
	str_format(aBuf, sizeof(aBuf),
		"SELECT COUNT(*) AS NumSaves, %s-%s AS Ago "
		"FROM %s_saves "
		"WHERE Map = ? AND Savegame LIKE ?;",
		aCurrentTimestamp, aMaxTimestamp,
		pSqlServer->GetPrefix());
	if(pSqlServer->PrepareStatement(aBuf, pError, ErrorSize))
	{
		return true;
	}
	pSqlServer->BindString(1, pData->m_aMap);
	pSqlServer->BindString(2, aSaveLike);

	bool End;
	if(pSqlServer->Step(&End, pError, ErrorSize))
	{
		return true;
	}
	if(!End)
	{
		int NumSaves = pSqlServer->GetInt(1);
		int Ago = pSqlServer->GetInt(2);
		char aAgoString[40] = "\0";
		char aLastSavedString[60] = "\0";
		if(Ago)
		{
			sqlstr::AgoTimeToString(Ago, aAgoString, sizeof(aAgoString));
			str_format(aLastSavedString, sizeof(aLastSavedString), ", last saved %s ago", aAgoString);
		}

		str_format(paMessages[0], sizeof(paMessages[0]),
			"%s has %d save%s on %s%s",
			pData->m_aRequestingPlayer,
			NumSaves, NumSaves == 1 ? "" : "s",
			pData->m_aMap, aLastSavedString);
	}
	return false;
}
