/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#ifndef GAME_SERVER_ENTITIES_DOOR_H
#define GAME_SERVER_ENTITIES_DOOR_H

#include <game/server/entity.h>

class CTrigger;

class CDoor : public CEntity
{
	vec2 m_To;
	void ResetCollision();
	int m_Length;
	vec2 m_Direction;

public:
	CDoor(CGameWorld *pGameWorld, vec2 Pos, float Rotation, int Length,
		int Number);

	virtual void Snap(int SnappingClient);
};

#endif // GAME_SERVER_ENTITIES_DOOR_H
