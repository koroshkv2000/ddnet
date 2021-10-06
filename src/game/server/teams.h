/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_TEAMS_H
#define GAME_SERVER_TEAMS_H

#include <engine/shared/config.h>
#include <game/server/gamecontext.h>
#include <game/server/score.h>
#include <game/teamscore.h>

#include <utility>

class CGameTeams
{
	int m_TeamState[MAX_CLIENTS];
	// `m_TeeStarted` is used to keep track whether a given tee has hit the
	// start of the map yet. If a tee that leaves hasn't hit the start line
	// yet, the team will be marked as "not allowed to finish"
	// (`TEAMSTATE_STARTED_UNFINISHABLE`). If this were not the case, tees
	// could go around the startline on a map, leave one tee behind at
	// start, go to the finish line, let the tee start and kill, allowing
	// the team to finish instantly.
	bool m_TeeStarted[MAX_CLIENTS];
	bool m_TeeFinished[MAX_CLIENTS];
	bool m_TeamLocked[MAX_CLIENTS];
	uint64_t m_Invited[MAX_CLIENTS];
	bool m_Practice[MAX_CLIENTS];
	std::shared_ptr<CScoreSaveResult> m_pSaveTeamResult[MAX_CLIENTS];
	uint64_t m_LastSwap[MAX_CLIENTS];
	bool m_TeamSentStartWarning[MAX_CLIENTS];
	// `m_TeamUnfinishableKillTick` is -1 by default and gets set when a
	// team becomes unfinishable. If the team hasn't entered practice mode
	// by that time, it'll get killed to prevent people not understanding
	// the message from playing for a long time in an unfinishable team.
	int m_TeamUnfinishableKillTick[MAX_CLIENTS];

	class CGameContext *m_pGameContext;

	// Kill the whole team, making the player `NewStrongID` have strong
	// hook on everyone else. `NewStrongID` can be -1 to get the normal
	// spawning order.
	void KillTeam(int Team, int NewStrongID);
	bool TeamFinished(int Team);
	void OnTeamFinish(CPlayer **Players, unsigned int Size, float Time, const char *pTimestamp);
	void OnFinish(CPlayer *Player, float Time, const char *pTimestamp);

public:
	enum
	{
		TEAMSTATE_EMPTY,
		TEAMSTATE_OPEN,
		TEAMSTATE_STARTED,
		// Happens when a tee that hasn't hit the start tiles leaves
		// the team.
		TEAMSTATE_STARTED_UNFINISHABLE,
		TEAMSTATE_FINISHED
	};

	CTeamsCore m_Core;

	CGameTeams(CGameContext *pGameContext);

	// helper methods
	CCharacter *Character(int ClientID)
	{
		return GameServer()->GetPlayerChar(ClientID);
	}
	CPlayer *GetPlayer(int ClientID)
	{
		return GameServer()->m_apPlayers[ClientID];
	}

	class CGameContext *GameServer()
	{
		return m_pGameContext;
	}
	class IServer *Server()
	{
		return m_pGameContext->Server();
	}

	void OnCharacterStart(int ClientID);
	void OnCharacterFinish(int ClientID);
	void OnCharacterSpawn(int ClientID);
	void OnCharacterDeath(int ClientID, int Weapon);
	void Tick();

	// returns nullptr if successful, error string if failed
	const char *SetCharacterTeam(int ClientID, int Team);
	void CheckTeamFinished(int ClientID);

	void ChangeTeamState(int Team, int State);

	int64_t TeamMask(int Team, int ExceptID = -1, int Asker = -1);

	int Count(int Team) const;

	// need to be very careful using this method. SERIOUSLY...
	void SetForceCharacterTeam(int ClientID, int Team);

	void Reset();
	void ResetRoundState(int Team);
	void ResetSwitchers(int Team);

	void SendTeamsState(int ClientID);
	void SetTeamLock(int Team, bool Lock);
	void ResetInvited(int Team);
	void SetClientInvited(int Team, int ClientID, bool Invited);

	int m_LastChat[MAX_CLIENTS];

	int GetDDRaceState(CPlayer *Player);
	int GetStartTime(CPlayer *Player);
	float *GetCpCurrent(CPlayer *Player);
	void SetDDRaceState(CPlayer *Player, int DDRaceState);
	void SetStartTime(CPlayer *Player, int StartTime);
	void SetCpActive(CPlayer *Player, int CpActive);
	void KillSavedTeam(int ClientID, int Team);
	void ResetSavedTeam(int ClientID, int Team);
	void RequestTeamSwap(CPlayer *pPlayer, CPlayer *pTargetPlayer, int Team);
	void SwapTeamCharacters(CPlayer *pPlayer, CPlayer *pTargetPlayer, int Team);
	void ProcessSaveTeam();

	int GetFirstEmptyTeam() const;

	bool TeeStarted(int ClientID)
	{
		return m_TeeStarted[ClientID];
	}

	bool TeeFinished(int ClientID)
	{
		return m_TeeFinished[ClientID];
	}

	int GetTeamState(int Team)
	{
		return m_TeamState[Team];
	}

	bool TeamLocked(int Team)
	{
		if(Team <= TEAM_FLOCK || Team >= TEAM_SUPER)
			return false;

		return m_TeamLocked[Team];
	}

	bool IsInvited(int Team, int ClientID)
	{
		return m_Invited[Team] & 1LL << ClientID;
	}

	bool IsStarted(int Team)
	{
		return m_TeamState[Team] == CGameTeams::TEAMSTATE_STARTED;
	}

	void SetStarted(int ClientID, bool Started)
	{
		m_TeeStarted[ClientID] = Started;
	}

	void SetFinished(int ClientID, bool Finished)
	{
		m_TeeFinished[ClientID] = Finished;
	}

	void SetSaving(int TeamID, std::shared_ptr<CScoreSaveResult> &SaveResult)
	{
		m_pSaveTeamResult[TeamID] = SaveResult;
	}

	bool GetSaving(int TeamID)
	{
		if(TeamID < TEAM_FLOCK || TeamID >= TEAM_SUPER)
			return false;
		if(g_Config.m_SvTeam != 3 && TeamID == TEAM_FLOCK)
			return false;

		return m_pSaveTeamResult[TeamID] != nullptr;
	}

	void EnablePractice(int Team)
	{
		if(Team < TEAM_FLOCK || Team >= TEAM_SUPER)
			return;
		if(g_Config.m_SvTeam != 3 && Team == TEAM_FLOCK)
			return;

		m_Practice[Team] = true;
	}

	bool IsPractice(int Team)
	{
		if(Team < TEAM_FLOCK || Team >= TEAM_SUPER)
			return false;
		if(g_Config.m_SvTeam != 3 && Team == TEAM_FLOCK)
			return false;

		return m_Practice[Team];
	}
};

#endif
