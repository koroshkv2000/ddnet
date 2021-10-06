/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "pickup.h"
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/player.h>

#include <game/server/teams.h>

#include "character.h"

CPickup::CPickup(CGameWorld *pGameWorld, int Type, int SubType, int Layer, int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_PICKUP, vec2(0, 0), PickupPhysSize)
{
	m_Type = Type;
	m_Subtype = SubType;

	m_Layer = Layer;
	m_Number = Number;

	Reset();

	GameWorld()->InsertEntity(this);
}

void CPickup::Reset()
{
	/*if (g_pData->m_aPickups[m_Type].m_Spawndelay > 0)
		m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * g_pData->m_aPickups[m_Type].m_Spawndelay;
	else
		m_SpawnTick = -1;*/
}

void CPickup::Tick()
{
	Move();
	/*// wait for respawn
	if(m_SpawnTick > 0)
	{
		if(Server()->Tick() > m_SpawnTick)
		{
			// respawn
			m_SpawnTick = -1;

			if(m_Type == POWERUP_WEAPON)
				GameServer()->CreateSound(m_Pos, SOUND_WEAPON_SPAWN);
		}
		else
			return;
	}*/
	// Check if a player intersected us
	CCharacter *apEnts[MAX_CLIENTS];
	int Num = GameWorld()->FindEntities(m_Pos, 20.0f, (CEntity **)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	for(int i = 0; i < Num; ++i)
	{
		CCharacter *pChr = apEnts[i];

		if(pChr && pChr->IsAlive())
		{
			if(m_Layer == LAYER_SWITCH && m_Number > 0 && !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[pChr->Team()])
				continue;
			bool Sound = false;
			// player picked us up, is someone was hooking us, let them go
			switch(m_Type)
			{
			case POWERUP_HEALTH:
				if(pChr->Freeze())
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_HEALTH, pChr->TeamMask());
				break;

			case POWERUP_ARMOR:
				if(pChr->Team() == TEAM_SUPER)
					continue;
				for(int i = WEAPON_SHOTGUN; i < NUM_WEAPONS; i++)
				{
					if(pChr->GetWeaponGot(i))
					{
						pChr->SetWeaponGot(i, false);
						pChr->SetWeaponAmmo(i, 0);
						Sound = true;
					}
				}
				pChr->SetNinjaActivationDir(vec2(0, 0));
				pChr->SetNinjaActivationTick(-500);
				pChr->SetNinjaCurrentMoveTime(0);
				if(Sound)
				{
					pChr->SetLastWeapon(WEAPON_GUN);
					GameServer()->CreateSound(m_Pos, SOUND_PICKUP_ARMOR, pChr->TeamMask());
				}
				if(pChr->GetActiveWeapon() >= WEAPON_SHOTGUN)
					pChr->SetActiveWeapon(WEAPON_HAMMER);
				break;

			case POWERUP_WEAPON:

				if(m_Subtype >= 0 && m_Subtype < NUM_WEAPONS && (!pChr->GetWeaponGot(m_Subtype) || pChr->GetWeaponAmmo(m_Subtype) != -1))
				{
					pChr->GiveWeapon(m_Subtype);

					//RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;

					if(m_Subtype == WEAPON_GRENADE)
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_GRENADE, pChr->TeamMask());
					else if(m_Subtype == WEAPON_SHOTGUN)
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_SHOTGUN, pChr->TeamMask());
					else if(m_Subtype == WEAPON_LASER)
						GameServer()->CreateSound(m_Pos, SOUND_PICKUP_SHOTGUN, pChr->TeamMask());

					if(pChr->GetPlayer())
						GameServer()->SendWeaponPickup(pChr->GetPlayer()->GetCID(), m_Subtype);
				}
				break;

			case POWERUP_NINJA:
			{
				// activate ninja on target player
				pChr->GiveNinja();
				//RespawnTime = g_pData->m_aPickups[m_Type].m_Respawntime;

				/*// loop through all players, setting their emotes
					CCharacter *pC = static_cast<CCharacter *>(GameServer()->m_World.FindFirst(CGameWorld::ENTTYPE_CHARACTER));
					for(; pC; pC = (CCharacter *)pC->TypeNext())
					{
						if (pC != pChr)
							pC->SetEmote(EMOTE_SURPRISE, Server()->Tick() + Server()->TickSpeed());
					}*/
				break;
			}
			default:
				break;
			};

			/*if(RespawnTime >= 0)
			{
				char aBuf[256];
				str_format(aBuf, sizeof(aBuf), "pickup player='%d:%s' item=%d/%d",
					pChr->GetPlayer()->GetCID(), Server()->ClientName(pChr->GetPlayer()->GetCID()), m_Type, m_Subtype);
				GameServer()->Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);
				m_SpawnTick = Server()->Tick() + Server()->TickSpeed() * RespawnTime;
			}*/
		}
	}
}

void CPickup::TickPaused()
{
	/*if(m_SpawnTick != -1)
		++m_SpawnTick;*/
}

void CPickup::Snap(int SnappingClient)
{
	/*if(m_SpawnTick != -1 || NetworkClipped(SnappingClient))
		return;*/

	CCharacter *Char = GameServer()->GetPlayerChar(SnappingClient);

	if(SnappingClient > -1 && (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == -1 || GameServer()->m_apPlayers[SnappingClient]->IsPaused()) && GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != SPEC_FREEVIEW)
		Char = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID);

	int Tick = (Server()->Tick() % Server()->TickSpeed()) % 11;
	if(Char && Char->IsAlive() &&
		(m_Layer == LAYER_SWITCH && m_Number > 0 &&
			!GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Char->Team()]) &&
		(!Tick))
		return;

	int Size = Server()->IsSixup(SnappingClient) ? 3 * 4 : sizeof(CNetObj_Pickup);
	CNetObj_Pickup *pP = static_cast<CNetObj_Pickup *>(Server()->SnapNewItem(NETOBJTYPE_PICKUP, GetID(), Size));
	if(!pP)
		return;

	pP->m_X = (int)m_Pos.x;
	pP->m_Y = (int)m_Pos.y;
	pP->m_Type = m_Type;
	if(Server()->IsSixup(SnappingClient))
	{
		if(m_Type == POWERUP_WEAPON)
			pP->m_Type = m_Subtype == WEAPON_SHOTGUN ? 3 : m_Subtype == WEAPON_GRENADE ? 2 : 4;
		else if(m_Type == POWERUP_NINJA)
			pP->m_Type = 5;
	}
	else
		pP->m_Subtype = m_Subtype;
}

void CPickup::Move()
{
	if(Server()->Tick() % int(Server()->TickSpeed() * 0.15f) == 0)
	{
		int Flags;
		int index = GameServer()->Collision()->IsMover(m_Pos.x, m_Pos.y, &Flags);
		if(index)
		{
			m_Core = GameServer()->Collision()->CpSpeed(index, Flags);
		}
		m_Pos += m_Core;
	}
}
