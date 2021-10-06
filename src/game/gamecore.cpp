/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "gamecore.h"

#include <engine/shared/config.h>

const char *CTuningParams::ms_apNames[] =
	{
#define MACRO_TUNING_PARAM(Name, ScriptName, Value, Description) #ScriptName,
#include "tuning.h"
#undef MACRO_TUNING_PARAM
};

bool CTuningParams::Set(int Index, float Value)
{
	if(Index < 0 || Index >= Num())
		return false;
	((CTuneParam *)this)[Index] = Value;
	return true;
}

bool CTuningParams::Get(int Index, float *pValue) const
{
	if(Index < 0 || Index >= Num())
		return false;
	*pValue = (float)((CTuneParam *)this)[Index];
	return true;
}

bool CTuningParams::Set(const char *pName, float Value)
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, ms_apNames[i]) == 0)
			return Set(i, Value);
	return false;
}

bool CTuningParams::Get(const char *pName, float *pValue) const
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, ms_apNames[i]) == 0)
			return Get(i, pValue);

	return false;
}

float HermiteBasis1(float v)
{
	return 2 * v * v * v - 3 * v * v + 1;
}

float VelocityRamp(float Value, float Start, float Range, float Curvature)
{
	if(Value < Start)
		return 1.0f;
	return 1.0f / powf(Curvature, (Value - Start) / Range);
}

void CCharacterCore::Init(CWorldCore *pWorld, CCollision *pCollision, CTeamsCore *pTeams, std::map<int, std::vector<vec2>> *pTeleOuts)
{
	m_pWorld = pWorld;
	m_pCollision = pCollision;
	m_pTeleOuts = pTeleOuts;

	m_pTeams = pTeams;
	m_Id = -1;

	// fail safe, if core's tuning didn't get updated at all, just fallback to world tuning.
	m_Tuning = m_pWorld->m_Tuning[g_Config.m_ClDummy];
	Reset();
}

void CCharacterCore::Reset()
{
	m_Pos = vec2(0, 0);
	m_Vel = vec2(0, 0);
	m_NewHook = false;
	m_HookPos = vec2(0, 0);
	m_HookDir = vec2(0, 0);
	m_HookTick = 0;
	m_HookState = HOOK_IDLE;
	m_HookedPlayer = -1;
	m_Jumped = 0;
	m_JumpedTotal = 0;
	m_Jumps = 2;
	m_TriggeredEvents = 0;
	m_Hook = true;
	m_Collision = true;

	// DDNet Character
	m_Solo = false;
	m_Jetpack = false;
	m_NoCollision = false;
	m_EndlessHook = false;
	m_EndlessJump = false;
	m_NoHammerHit = false;
	m_NoGrenadeHit = false;
	m_NoLaserHit = false;
	m_NoShotgunHit = false;
	m_NoHookHit = false;
	m_Super = false;
	m_HasTelegunGun = false;
	m_HasTelegunGrenade = false;
	m_HasTelegunLaser = false;
	m_FreezeEnd = 0;
	m_DeepFrozen = false;

	// never initialize both to 0
	m_Input.m_TargetX = 0;
	m_Input.m_TargetY = -1;
}

void CCharacterCore::Tick(bool UseInput)
{
	float PhysSize = 28.0f;
	m_MoveRestrictions = m_pCollision->GetMoveRestrictions(UseInput ? IsSwitchActiveCb : 0, this, m_Pos);
	m_TriggeredEvents = 0;

	// get ground state
	bool Grounded = false;
	if(m_pCollision->CheckPoint(m_Pos.x + PhysSize / 2, m_Pos.y + PhysSize / 2 + 5))
		Grounded = true;
	if(m_pCollision->CheckPoint(m_Pos.x - PhysSize / 2, m_Pos.y + PhysSize / 2 + 5))
		Grounded = true;

	vec2 TargetDirection = normalize(vec2(m_Input.m_TargetX, m_Input.m_TargetY));

	m_Vel.y += m_Tuning.m_Gravity;

	float MaxSpeed = Grounded ? m_Tuning.m_GroundControlSpeed : m_Tuning.m_AirControlSpeed;
	float Accel = Grounded ? m_Tuning.m_GroundControlAccel : m_Tuning.m_AirControlAccel;
	float Friction = Grounded ? m_Tuning.m_GroundFriction : m_Tuning.m_AirFriction;

	// handle input
	if(UseInput)
	{
		m_Direction = m_Input.m_Direction;

		// setup angle
		float a = 0;
		if(m_Input.m_TargetX == 0)
			a = atanf((float)m_Input.m_TargetY);
		else
			a = atanf((float)m_Input.m_TargetY / (float)m_Input.m_TargetX);

		if(m_Input.m_TargetX < 0)
			a = a + pi;

		m_Angle = (int)(a * 256.0f);

		// handle jump
		if(m_Input.m_Jump)
		{
			if(!(m_Jumped & 1))
			{
				if(Grounded)
				{
					m_TriggeredEvents |= COREEVENT_GROUND_JUMP;
					m_Vel.y = -m_Tuning.m_GroundJumpImpulse;
					m_Jumped |= 1;
					m_JumpedTotal = 1;
				}
				else if(!(m_Jumped & 2))
				{
					m_TriggeredEvents |= COREEVENT_AIR_JUMP;
					m_Vel.y = -m_Tuning.m_AirJumpImpulse;
					m_Jumped |= 3;
					m_JumpedTotal++;
				}
			}
		}
		else
			m_Jumped &= ~1;

		// handle hook
		if(m_Input.m_Hook)
		{
			if(m_HookState == HOOK_IDLE)
			{
				m_HookState = HOOK_FLYING;
				m_HookPos = m_Pos + TargetDirection * PhysSize * 1.5f;
				m_HookDir = TargetDirection;
				m_HookedPlayer = -1;
				m_HookTick = SERVER_TICK_SPEED * (1.25f - m_Tuning.m_HookDuration);
				m_TriggeredEvents |= COREEVENT_HOOK_LAUNCH;
			}
		}
		else
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_IDLE;
			m_HookPos = m_Pos;
		}
	}

	// add the speed modification according to players wanted direction
	if(m_Direction < 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, -Accel);
	if(m_Direction > 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, Accel);
	if(m_Direction == 0)
		m_Vel.x *= Friction;

	// handle jumping
	// 1 bit = to keep track if a jump has been made on this input (player is holding space bar)
	// 2 bit = to keep track if a air-jump has been made (tee gets dark feet)
	if(Grounded)
	{
		m_Jumped &= ~2;
		m_JumpedTotal = 0;
	}

	// do hook
	if(m_HookState == HOOK_IDLE)
	{
		m_HookedPlayer = -1;
		m_HookState = HOOK_IDLE;
		m_HookPos = m_Pos;
	}
	else if(m_HookState >= HOOK_RETRACT_START && m_HookState < HOOK_RETRACT_END)
	{
		m_HookState++;
	}
	else if(m_HookState == HOOK_RETRACT_END)
	{
		m_HookState = HOOK_RETRACTED;
		m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		m_HookState = HOOK_RETRACTED;
	}
	else if(m_HookState == HOOK_FLYING)
	{
		vec2 NewPos = m_HookPos + m_HookDir * m_Tuning.m_HookFireSpeed;
		if((!m_NewHook && distance(m_Pos, NewPos) > m_Tuning.m_HookLength) || (m_NewHook && distance(m_HookTeleBase, NewPos) > m_Tuning.m_HookLength))
		{
			m_HookState = HOOK_RETRACT_START;
			NewPos = m_Pos + normalize(NewPos - m_Pos) * m_Tuning.m_HookLength;
			m_pReset = true;
		}

		// make sure that the hook doesn't go though the ground
		bool GoingToHitGround = false;
		bool GoingToRetract = false;
		bool GoingThroughTele = false;
		int teleNr = 0;
		int Hit = m_pCollision->IntersectLineTeleHook(m_HookPos, NewPos, &NewPos, 0, &teleNr);

		//m_NewHook = false;

		if(Hit)
		{
			if(Hit == TILE_NOHOOK)
				GoingToRetract = true;
			else if(Hit == TILE_TELEINHOOK)
				GoingThroughTele = true;
			else
				GoingToHitGround = true;
			m_pReset = true;
		}

		// Check against other players first
		if(this->m_Hook && m_pWorld && m_Tuning.m_PlayerHooking)
		{
			float Distance = 0.0f;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
				if(!pCharCore || pCharCore == this || (!(m_Super || pCharCore->m_Super) && ((m_Id != -1 && !m_pTeams->CanCollide(i, m_Id)) || pCharCore->m_Solo || m_Solo)))
					continue;

				vec2 ClosestPoint;
				if(closest_point_on_line(m_HookPos, NewPos, pCharCore->m_Pos, ClosestPoint))
				{
					if(distance(pCharCore->m_Pos, ClosestPoint) < PhysSize + 2.0f)
					{
						if(m_HookedPlayer == -1 || distance(m_HookPos, pCharCore->m_Pos) < Distance)
						{
							m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_PLAYER;
							m_HookState = HOOK_GRABBED;
							m_HookedPlayer = i;
							Distance = distance(m_HookPos, pCharCore->m_Pos);
						}
					}
				}
			}
		}

		if(m_HookState == HOOK_FLYING)
		{
			// check against ground
			if(GoingToHitGround)
			{
				m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_GROUND;
				m_HookState = HOOK_GRABBED;
			}
			else if(GoingToRetract)
			{
				m_TriggeredEvents |= COREEVENT_HOOK_HIT_NOHOOK;
				m_HookState = HOOK_RETRACT_START;
			}

			if(GoingThroughTele && m_pWorld && m_pTeleOuts && m_pTeleOuts->size() && (*m_pTeleOuts)[teleNr - 1].size())
			{
				m_TriggeredEvents = 0;
				m_HookedPlayer = -1;

				m_NewHook = true;
				int RandomOut = m_pWorld->RandomOr0((*m_pTeleOuts)[teleNr - 1].size());
				m_HookPos = (*m_pTeleOuts)[teleNr - 1][RandomOut] + TargetDirection * PhysSize * 1.5f;
				m_HookDir = TargetDirection;
				m_HookTeleBase = m_HookPos;
			}
			else
			{
				m_HookPos = NewPos;
			}
		}
	}

	if(m_HookState == HOOK_GRABBED)
	{
		if(m_HookedPlayer != -1 && m_pWorld)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[m_HookedPlayer];
			if(pCharCore && m_Id != -1 && m_pTeams->CanKeepHook(m_Id, pCharCore->m_Id))
				m_HookPos = pCharCore->m_Pos;
			else
			{
				// release hook
				m_HookedPlayer = -1;
				m_HookState = HOOK_RETRACTED;
				m_HookPos = m_Pos;
			}

			// keep players hooked for a max of 1.5sec
			//if(Server()->Tick() > hook_tick+(Server()->TickSpeed()*3)/2)
			//release_hooked();
		}

		// don't do this hook rutine when we are hook to a player
		if(m_HookedPlayer == -1 && distance(m_HookPos, m_Pos) > 46.0f)
		{
			vec2 HookVel = normalize(m_HookPos - m_Pos) * m_Tuning.m_HookDragAccel;
			// the hook as more power to drag you up then down.
			// this makes it easier to get on top of an platform
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;

			// the hook will boost it's power if the player wants to move
			// in that direction. otherwise it will dampen everything abit
			if((HookVel.x < 0 && m_Direction < 0) || (HookVel.x > 0 && m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;

			vec2 NewVel = m_Vel + HookVel;

			// check if we are under the legal limit for the hook
			if(length(NewVel) < m_Tuning.m_HookDragSpeed || length(NewVel) < length(m_Vel))
				m_Vel = NewVel; // no problem. apply
		}

		// release hook (max default hook time is 1.25 s)
		m_HookTick++;
		if(m_HookedPlayer != -1 && (m_HookTick > SERVER_TICK_SPEED + SERVER_TICK_SPEED / 5 || (m_pWorld && !m_pWorld->m_apCharacters[m_HookedPlayer])))
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_RETRACTED;
			m_HookPos = m_Pos;
		}
	}

	if(m_pWorld)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
			if(!pCharCore)
				continue;

			//player *p = (player*)ent;
			//if(pCharCore == this) // || !(p->flags&FLAG_ALIVE)

			if(pCharCore == this || (m_Id != -1 && !m_pTeams->CanCollide(m_Id, i)))
				continue; // make sure that we don't nudge our self

			if(!(m_Super || pCharCore->m_Super) && (m_Solo || pCharCore->m_Solo))
				continue;

			// handle player <-> player collision
			float Distance = distance(m_Pos, pCharCore->m_Pos);
			if(Distance > 0)
			{
				vec2 Dir = normalize(m_Pos - pCharCore->m_Pos);

				bool CanCollide = (m_Super || pCharCore->m_Super) || (pCharCore->m_Collision && m_Collision && !m_NoCollision && !pCharCore->m_NoCollision && m_Tuning.m_PlayerCollision);

				if(CanCollide && Distance < PhysSize * 1.25f && Distance > 0.0f)
				{
					float a = (PhysSize * 1.45f - Distance);
					float Velocity = 0.5f;

					// make sure that we don't add excess force by checking the
					// direction against the current velocity. if not zero.
					if(length(m_Vel) > 0.0001)
						Velocity = 1 - (dot(normalize(m_Vel), Dir) + 1) / 2;

					m_Vel += Dir * a * (Velocity * 0.75f);
					m_Vel *= 0.85f;
				}

				// handle hook influence
				if(m_Hook && m_HookedPlayer == i && m_Tuning.m_PlayerHooking)
				{
					if(Distance > PhysSize * 1.50f) // TODO: fix tweakable variable
					{
						float Accel = m_Tuning.m_HookDragAccel * (Distance / m_Tuning.m_HookLength);
						float DragSpeed = m_Tuning.m_HookDragSpeed;

						vec2 Temp;
						// add force to the hooked player
						Temp.x = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.x, Accel * Dir.x * 1.5f);
						Temp.y = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.y, Accel * Dir.y * 1.5f);
						pCharCore->m_Vel = ClampVel(pCharCore->m_MoveRestrictions, Temp);
						// add a little bit force to the guy who has the grip
						Temp.x = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.x, -Accel * Dir.x * 0.25f);
						Temp.y = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.y, -Accel * Dir.y * 0.25f);
						m_Vel = ClampVel(m_MoveRestrictions, Temp);
					}
				}
			}
		}

		if(m_HookState != HOOK_FLYING)
		{
			m_NewHook = false;
		}
	}

	// clamp the velocity to something sane
	if(length(m_Vel) > 6000)
		m_Vel = normalize(m_Vel) * 6000;
}

void CCharacterCore::Move()
{
	float RampValue = VelocityRamp(length(m_Vel) * 50, m_Tuning.m_VelrampStart, m_Tuning.m_VelrampRange, m_Tuning.m_VelrampCurvature);

	m_Vel.x = m_Vel.x * RampValue;

	vec2 NewPos = m_Pos;

	vec2 OldVel = m_Vel;
	m_pCollision->MoveBox(&NewPos, &m_Vel, vec2(28.0f, 28.0f), 0);

	m_Colliding = 0;
	if(m_Vel.x < 0.001f && m_Vel.x > -0.001f)
	{
		if(OldVel.x > 0)
			m_Colliding = 1;
		else if(OldVel.x < 0)
			m_Colliding = 2;
	}
	else
		m_LeftWall = true;

	m_Vel.x = m_Vel.x * (1.0f / RampValue);

	if(m_pWorld && (m_Super || (m_Tuning.m_PlayerCollision && m_Collision && !m_NoCollision && !m_Solo)))
	{
		// check player collision
		float Distance = distance(m_Pos, NewPos);
		if(Distance > 0)
		{
			int End = Distance + 1;
			vec2 LastPos = m_Pos;
			for(int i = 0; i < End; i++)
			{
				float a = i / Distance;
				vec2 Pos = mix(m_Pos, NewPos, a);
				for(int p = 0; p < MAX_CLIENTS; p++)
				{
					CCharacterCore *pCharCore = m_pWorld->m_apCharacters[p];
					if(!pCharCore || pCharCore == this)
						continue;
					if((!(pCharCore->m_Super || m_Super) && (m_Solo || pCharCore->m_Solo || !pCharCore->m_Collision || pCharCore->m_NoCollision || (m_Id != -1 && !m_pTeams->CanCollide(m_Id, p)))))
						continue;
					float D = distance(Pos, pCharCore->m_Pos);
					if(D < 28.0f && D >= 0.0f)
					{
						if(a > 0.0f)
							m_Pos = LastPos;
						else if(distance(NewPos, pCharCore->m_Pos) > D)
							m_Pos = NewPos;
						return;
					}
				}
				LastPos = Pos;
			}
		}
	}

	m_Pos = NewPos;
}

void CCharacterCore::Write(CNetObj_CharacterCore *pObjCore)
{
	pObjCore->m_X = round_to_int(m_Pos.x);
	pObjCore->m_Y = round_to_int(m_Pos.y);

	pObjCore->m_VelX = round_to_int(m_Vel.x * 256.0f);
	pObjCore->m_VelY = round_to_int(m_Vel.y * 256.0f);
	pObjCore->m_HookState = m_HookState;
	pObjCore->m_HookTick = m_HookTick;
	pObjCore->m_HookX = round_to_int(m_HookPos.x);
	pObjCore->m_HookY = round_to_int(m_HookPos.y);
	pObjCore->m_HookDx = round_to_int(m_HookDir.x * 256.0f);
	pObjCore->m_HookDy = round_to_int(m_HookDir.y * 256.0f);
	pObjCore->m_HookedPlayer = m_HookedPlayer;
	pObjCore->m_Jumped = m_Jumped;
	pObjCore->m_Direction = m_Direction;
	pObjCore->m_Angle = m_Angle;
}

void CCharacterCore::Read(const CNetObj_CharacterCore *pObjCore)
{
	m_Pos.x = pObjCore->m_X;
	m_Pos.y = pObjCore->m_Y;
	m_Vel.x = pObjCore->m_VelX / 256.0f;
	m_Vel.y = pObjCore->m_VelY / 256.0f;
	m_HookState = pObjCore->m_HookState;
	m_HookTick = pObjCore->m_HookTick;
	m_HookPos.x = pObjCore->m_HookX;
	m_HookPos.y = pObjCore->m_HookY;
	m_HookDir.x = pObjCore->m_HookDx / 256.0f;
	m_HookDir.y = pObjCore->m_HookDy / 256.0f;
	m_HookedPlayer = pObjCore->m_HookedPlayer;
	m_Jumped = pObjCore->m_Jumped;
	m_Direction = pObjCore->m_Direction;
	m_Angle = pObjCore->m_Angle;
}

void CCharacterCore::ReadDDNet(const CNetObj_DDNetCharacter *pObjDDNet)
{
	// Collision
	m_Solo = pObjDDNet->m_Flags & CHARACTERFLAG_SOLO;
	m_Jetpack = pObjDDNet->m_Flags & CHARACTERFLAG_JETPACK;
	m_NoCollision = pObjDDNet->m_Flags & CHARACTERFLAG_NO_COLLISION;
	m_NoHammerHit = pObjDDNet->m_Flags & CHARACTERFLAG_NO_HAMMER_HIT;
	m_NoGrenadeHit = pObjDDNet->m_Flags & CHARACTERFLAG_NO_GRENADE_HIT;
	m_NoLaserHit = pObjDDNet->m_Flags & CHARACTERFLAG_NO_LASER_HIT;
	m_NoShotgunHit = pObjDDNet->m_Flags & CHARACTERFLAG_NO_SHOTGUN_HIT;
	m_NoHookHit = pObjDDNet->m_Flags & CHARACTERFLAG_NO_HOOK;
	m_Super = pObjDDNet->m_Flags & CHARACTERFLAG_SUPER;

	m_Hook = !m_NoHookHit;
	m_Collision = !m_NoCollision;

	// Endless
	m_EndlessHook = pObjDDNet->m_Flags & CHARACTERFLAG_ENDLESS_HOOK;
	m_EndlessJump = pObjDDNet->m_Flags & CHARACTERFLAG_ENDLESS_JUMP;

	// Freeze
	m_FreezeEnd = pObjDDNet->m_FreezeEnd;
	m_DeepFrozen = pObjDDNet->m_FreezeEnd == -1;

	// Telegun
	m_HasTelegunGrenade = pObjDDNet->m_Flags & CHARACTERFLAG_TELEGUN_GRENADE;
	m_HasTelegunGun = pObjDDNet->m_Flags & CHARACTERFLAG_TELEGUN_GUN;
	m_HasTelegunLaser = pObjDDNet->m_Flags & CHARACTERFLAG_TELEGUN_LASER;

	m_Jumps = pObjDDNet->m_Jumps;
}

void CCharacterCore::Quantize()
{
	CNetObj_CharacterCore Core;
	Write(&Core);
	Read(&Core);
}

// DDRace

void CCharacterCore::SetTeamsCore(CTeamsCore *pTeams)
{
	m_pTeams = pTeams;
}

void CCharacterCore::SetTeleOuts(std::map<int, std::vector<vec2>> *pTeleOuts)
{
	m_pTeleOuts = pTeleOuts;
}

bool CCharacterCore::IsSwitchActiveCb(int Number, void *pUser)
{
	CCharacterCore *pThis = (CCharacterCore *)pUser;
	if(pThis->Collision()->m_pSwitchers)
		if(pThis->m_Id != -1 && pThis->m_pTeams->Team(pThis->m_Id) != (pThis->m_pTeams->m_IsDDRace16 ? VANILLA_TEAM_SUPER : TEAM_SUPER))
			return pThis->Collision()->m_pSwitchers[Number].m_Status[pThis->m_pTeams->Team(pThis->m_Id)];
	return false;
}
