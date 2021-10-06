/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <engine/demo.h>
#include <engine/graphics.h>
#include <engine/keys.h>
#include <engine/serverbrowser.h>
#include <engine/shared/config.h>
#include <engine/storage.h>

#include <game/client/component.h>
#include <game/client/gameclient.h>
#include <game/client/render.h>
#include <game/layers.h>

#include <game/client/components/camera.h>
#include <game/client/components/mapimages.h>

#include <game/generated/client_data.h>

#include "maplayers.h"

CMapLayers::CMapLayers(int t, bool OnlineOnly)
{
	m_Type = t;
	m_pLayers = 0;
	m_CurrentLocalTick = 0;
	m_LastLocalTick = 0;
	m_EnvelopeUpdate = false;
	m_OnlineOnly = OnlineOnly;
}

void CMapLayers::OnInit()
{
	m_pLayers = Layers();
	m_pImages = &m_pClient->m_MapImages;
}

CCamera *CMapLayers::GetCurCamera()
{
	return &m_pClient->m_Camera;
}

void CMapLayers::EnvelopeUpdate()
{
	if(Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		const IDemoPlayer::CInfo *pInfo = DemoPlayer()->BaseInfo();
		m_CurrentLocalTick = pInfo->m_CurrentTick;
		m_LastLocalTick = pInfo->m_CurrentTick;
		m_EnvelopeUpdate = true;
	}
}

void CMapLayers::MapScreenToGroup(float CenterX, float CenterY, CMapItemGroup *pGroup, float Zoom)
{
	float Points[4];
	RenderTools()->MapscreenToWorld(CenterX, CenterY, pGroup->m_ParallaxX, pGroup->m_ParallaxY,
		pGroup->m_OffsetX, pGroup->m_OffsetY, Graphics()->ScreenAspect(), Zoom, Points);
	Graphics()->MapScreen(Points[0], Points[1], Points[2], Points[3]);
}

void CMapLayers::EnvelopeEval(int TimeOffsetMillis, int Env, float *pChannels, void *pUser)
{
	CMapLayers *pThis = (CMapLayers *)pUser;
	pChannels[0] = 0;
	pChannels[1] = 0;
	pChannels[2] = 0;
	pChannels[3] = 0;

	CEnvPoint *pPoints = 0;

	{
		int Start, Num;
		pThis->m_pLayers->Map()->GetType(MAPITEMTYPE_ENVPOINTS, &Start, &Num);
		if(Num)
			pPoints = (CEnvPoint *)pThis->m_pLayers->Map()->GetItem(Start, 0, 0);
	}

	int Start, Num;
	pThis->m_pLayers->Map()->GetType(MAPITEMTYPE_ENVELOPE, &Start, &Num);

	if(Env >= Num)
		return;

	CMapItemEnvelope *pItem = (CMapItemEnvelope *)pThis->m_pLayers->Map()->GetItem(Start + Env, 0, 0);

	const int64_t TickToMicroSeconds = (1000000ll / (int64_t)pThis->Client()->GameTickSpeed());

	static int64_t s_Time = 0;
	static int64_t s_LastLocalTime = time_get_microseconds();
	if(pThis->Client()->State() == IClient::STATE_DEMOPLAYBACK)
	{
		const IDemoPlayer::CInfo *pInfo = pThis->DemoPlayer()->BaseInfo();

		if(!pInfo->m_Paused || pThis->m_EnvelopeUpdate)
		{
			if(pThis->m_CurrentLocalTick != pInfo->m_CurrentTick)
			{
				pThis->m_LastLocalTick = pThis->m_CurrentLocalTick;
				pThis->m_CurrentLocalTick = pInfo->m_CurrentTick;
			}
			if(pItem->m_Version < 2 || pItem->m_Synchronized)
			{
				// get the lerp of the current tick and prev
				int MinTick = pThis->Client()->PrevGameTick(g_Config.m_ClDummy) - pThis->m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick;
				int CurTick = pThis->Client()->GameTick(g_Config.m_ClDummy) - pThis->m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick;
				s_Time = (int64_t)(mix<double>(
							   0,
							   (CurTick - MinTick),
							   pThis->Client()->IntraGameTick(g_Config.m_ClDummy)) *
						   TickToMicroSeconds) +
					 MinTick * TickToMicroSeconds;
			}
			else
			{
				int MinTick = pThis->m_LastLocalTick;
				s_Time = (int64_t)(mix<double>(0,
							   pThis->m_CurrentLocalTick - MinTick,
							   pThis->Client()->IntraGameTick(g_Config.m_ClDummy)) *
						   TickToMicroSeconds) +
					 MinTick * TickToMicroSeconds;
			}
		}
		pThis->RenderTools()->RenderEvalEnvelope(pPoints + pItem->m_StartPoint, pItem->m_NumPoints, 4, s_Time + (int64_t)TimeOffsetMillis * 1000ll, pChannels);
	}
	else
	{
		if(pThis->m_OnlineOnly && (pItem->m_Version < 2 || pItem->m_Synchronized))
		{
			if(pThis->m_pClient->m_Snap.m_pGameInfoObj) // && !(pThis->m_pClient->m_Snap.m_pGameInfoObj->m_GameStateFlags&GAMESTATEFLAG_PAUSED))
			{
				// get the lerp of the current tick and prev
				int MinTick = pThis->Client()->PrevGameTick(g_Config.m_ClDummy) - pThis->m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick;
				int CurTick = pThis->Client()->GameTick(g_Config.m_ClDummy) - pThis->m_pClient->m_Snap.m_pGameInfoObj->m_RoundStartTick;
				s_Time = (int64_t)(mix<double>(
							   0,
							   (CurTick - MinTick),
							   pThis->Client()->IntraGameTick(g_Config.m_ClDummy)) *
						   TickToMicroSeconds) +
					 MinTick * TickToMicroSeconds;
			}
		}
		else
		{
			int64_t CurTime = time_get_microseconds();
			s_Time += CurTime - s_LastLocalTime;
			s_LastLocalTime = CurTime;
		}
		pThis->RenderTools()->RenderEvalEnvelope(pPoints + pItem->m_StartPoint, pItem->m_NumPoints, 4, s_Time + (int64_t)TimeOffsetMillis * 1000ll, pChannels);
	}
}

void FillTmpTileSpeedup(SGraphicTile *pTmpTile, SGraphicTileTexureCoords *pTmpTex, bool As3DTextureCoord, unsigned char Flags, unsigned char Index, int x, int y, int Scale, CMapItemGroup *pGroup, short AngleRotate)
{
	if(pTmpTex)
	{
		unsigned char x0 = 0;
		unsigned char y0 = 0;
		unsigned char x1 = x0 + 1;
		unsigned char y1 = y0;
		unsigned char x2 = x0 + 1;
		unsigned char y2 = y0 + 1;
		unsigned char x3 = x0;
		unsigned char y3 = y0 + 1;

		pTmpTex->m_TexCoordTopLeft.x = x0;
		pTmpTex->m_TexCoordTopLeft.y = y0;
		pTmpTex->m_TexCoordBottomLeft.x = x3;
		pTmpTex->m_TexCoordBottomLeft.y = y3;
		pTmpTex->m_TexCoordTopRight.x = x1;
		pTmpTex->m_TexCoordTopRight.y = y1;
		pTmpTex->m_TexCoordBottomRight.x = x2;
		pTmpTex->m_TexCoordBottomRight.y = y2;

		if(As3DTextureCoord)
		{
			pTmpTex->m_TexCoordTopLeft.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordBottomLeft.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordTopRight.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordBottomRight.z = ((float)Index + 0.5f) / 256.f;
		}
		else
		{
			pTmpTex->m_TexCoordTopLeft.z = Index;
			pTmpTex->m_TexCoordBottomLeft.z = Index;
			pTmpTex->m_TexCoordTopRight.z = Index;
			pTmpTex->m_TexCoordBottomRight.z = Index;
		}
	}

	//same as in rotate from Graphics()
	float Angle = (float)AngleRotate * (3.14159265f / 180.0f);
	float c = cosf(Angle);
	float s = sinf(Angle);
	float xR, yR;
	int i;

	int ScaleSmaller = 2;
	pTmpTile->m_TopLeft.x = x * Scale + ScaleSmaller;
	pTmpTile->m_TopLeft.y = y * Scale + ScaleSmaller;
	pTmpTile->m_BottomLeft.x = x * Scale + ScaleSmaller;
	pTmpTile->m_BottomLeft.y = y * Scale + Scale - ScaleSmaller;
	pTmpTile->m_TopRight.x = x * Scale + Scale - ScaleSmaller;
	pTmpTile->m_TopRight.y = y * Scale + ScaleSmaller;
	pTmpTile->m_BottomRight.x = x * Scale + Scale - ScaleSmaller;
	pTmpTile->m_BottomRight.y = y * Scale + Scale - ScaleSmaller;

	float *pTmpTileVertices = (float *)pTmpTile;

	vec2 Center;
	Center.x = pTmpTile->m_TopLeft.x + (Scale - ScaleSmaller) / 2.f;
	Center.y = pTmpTile->m_TopLeft.y + (Scale - ScaleSmaller) / 2.f;

	for(i = 0; i < 4; i++)
	{
		xR = pTmpTileVertices[i * 2] - Center.x;
		yR = pTmpTileVertices[i * 2 + 1] - Center.y;
		pTmpTileVertices[i * 2] = xR * c - yR * s + Center.x;
		pTmpTileVertices[i * 2 + 1] = xR * s + yR * c + Center.y;
	}
}

void FillTmpTile(SGraphicTile *pTmpTile, SGraphicTileTexureCoords *pTmpTex, bool As3DTextureCoord, unsigned char Flags, unsigned char Index, int x, int y, int Scale, CMapItemGroup *pGroup)
{
	if(pTmpTex)
	{
		unsigned char x0 = 0;
		unsigned char y0 = 0;
		unsigned char x1 = x0 + 1;
		unsigned char y1 = y0;
		unsigned char x2 = x0 + 1;
		unsigned char y2 = y0 + 1;
		unsigned char x3 = x0;
		unsigned char y3 = y0 + 1;

		if(Flags & TILEFLAG_VFLIP)
		{
			x0 = x2;
			x1 = x3;
			x2 = x3;
			x3 = x0;
		}

		if(Flags & TILEFLAG_HFLIP)
		{
			y0 = y3;
			y2 = y1;
			y3 = y1;
			y1 = y0;
		}

		if(Flags & TILEFLAG_ROTATE)
		{
			unsigned char Tmp = x0;
			x0 = x3;
			x3 = x2;
			x2 = x1;
			x1 = Tmp;
			Tmp = y0;
			y0 = y3;
			y3 = y2;
			y2 = y1;
			y1 = Tmp;
		}

		pTmpTex->m_TexCoordTopLeft.x = x0;
		pTmpTex->m_TexCoordTopLeft.y = y0;
		pTmpTex->m_TexCoordBottomLeft.x = x3;
		pTmpTex->m_TexCoordBottomLeft.y = y3;
		pTmpTex->m_TexCoordTopRight.x = x1;
		pTmpTex->m_TexCoordTopRight.y = y1;
		pTmpTex->m_TexCoordBottomRight.x = x2;
		pTmpTex->m_TexCoordBottomRight.y = y2;

		if(As3DTextureCoord)
		{
			pTmpTex->m_TexCoordTopLeft.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordBottomLeft.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordTopRight.z = ((float)Index + 0.5f) / 256.f;
			pTmpTex->m_TexCoordBottomRight.z = ((float)Index + 0.5f) / 256.f;
		}
		else
		{
			pTmpTex->m_TexCoordTopLeft.z = Index;
			pTmpTex->m_TexCoordBottomLeft.z = Index;
			pTmpTex->m_TexCoordTopRight.z = Index;
			pTmpTex->m_TexCoordBottomRight.z = Index;
		}
	}

	pTmpTile->m_TopLeft.x = x * Scale;
	pTmpTile->m_TopLeft.y = y * Scale;
	pTmpTile->m_BottomLeft.x = x * Scale;
	pTmpTile->m_BottomLeft.y = y * Scale + Scale;
	pTmpTile->m_TopRight.x = x * Scale + Scale;
	pTmpTile->m_TopRight.y = y * Scale;
	pTmpTile->m_BottomRight.x = x * Scale + Scale;
	pTmpTile->m_BottomRight.y = y * Scale + Scale;
}

bool CMapLayers::STileLayerVisuals::Init(unsigned int Width, unsigned int Height)
{
	m_Width = Width;
	m_Height = Height;
	if(Width == 0 || Height == 0)
		return false;

	m_TilesOfLayer = new CMapLayers::STileLayerVisuals::STileVisual[Height * Width];

	if(Width > 2)
	{
		m_BorderTop = new CMapLayers::STileLayerVisuals::STileVisual[Width - 2];
		m_BorderBottom = new CMapLayers::STileLayerVisuals::STileVisual[Width - 2];
	}
	if(Height > 2)
	{
		m_BorderLeft = new CMapLayers::STileLayerVisuals::STileVisual[Height - 2];
		m_BorderRight = new CMapLayers::STileLayerVisuals::STileVisual[Height - 2];
	}
	return true;
}

CMapLayers::STileLayerVisuals::~STileLayerVisuals()
{
	if(m_TilesOfLayer)
	{
		delete[] m_TilesOfLayer;
	}

	if(m_BorderTop)
		delete[] m_BorderTop;
	if(m_BorderBottom)
		delete[] m_BorderBottom;
	if(m_BorderLeft)
		delete[] m_BorderLeft;
	if(m_BorderRight)
		delete[] m_BorderRight;

	m_TilesOfLayer = NULL;
	m_BorderTop = NULL;
	m_BorderBottom = NULL;
	m_BorderLeft = NULL;
	m_BorderRight = NULL;
}

bool AddTile(std::vector<SGraphicTile> &TmpTiles, std::vector<SGraphicTileTexureCoords> &TmpTileTexCoords, bool As3DTextureCoord, unsigned char Index, unsigned char Flags, int x, int y, CMapItemGroup *pGroup, bool DoTextureCoords, bool FillSpeedup = false, int AngleRotate = -1)
{
	if(Index)
	{
		TmpTiles.push_back(SGraphicTile());
		SGraphicTile &Tile = TmpTiles.back();
		SGraphicTileTexureCoords *pTileTex = NULL;
		if(DoTextureCoords)
		{
			TmpTileTexCoords.push_back(SGraphicTileTexureCoords());
			SGraphicTileTexureCoords &TileTex = TmpTileTexCoords.back();
			pTileTex = &TileTex;
		}
		if(FillSpeedup)
			FillTmpTileSpeedup(&Tile, pTileTex, As3DTextureCoord, Flags, 0, x, y, 32.f, pGroup, AngleRotate);
		else
			FillTmpTile(&Tile, pTileTex, As3DTextureCoord, Flags, Index, x, y, 32.f, pGroup);

		return true;
	}
	return false;
}

struct STmpQuadVertexTextured
{
	float m_X, m_Y, m_CenterX, m_CenterY;
	unsigned char m_R, m_G, m_B, m_A;
	float m_U, m_V;
};

struct STmpQuadVertex
{
	float m_X, m_Y, m_CenterX, m_CenterY;
	unsigned char m_R, m_G, m_B, m_A;
};

struct STmpQuad
{
	STmpQuadVertex m_aVertices[4];
};

struct STmpQuadTextured
{
	STmpQuadVertexTextured m_aVertices[4];
};

void mem_copy_special(void *pDest, void *pSource, size_t Size, size_t Count, size_t Steps)
{
	size_t CurStep = 0;
	for(size_t i = 0; i < Count; ++i)
	{
		mem_copy(((char *)pDest) + CurStep + i * Size, ((char *)pSource) + i * Size, Size);
		CurStep += Steps;
	}
}

CMapLayers::~CMapLayers()
{
	//clear everything and destroy all buffers
	if(m_TileLayerVisuals.size() != 0)
	{
		int s = m_TileLayerVisuals.size();
		for(int i = 0; i < s; ++i)
		{
			delete m_TileLayerVisuals[i];
		}
	}
	if(m_QuadLayerVisuals.size() != 0)
	{
		int s = m_QuadLayerVisuals.size();
		for(int i = 0; i < s; ++i)
		{
			delete m_QuadLayerVisuals[i];
		}
	}
}

void CMapLayers::OnMapLoad()
{
	if(!Graphics()->IsTileBufferingEnabled() && !Graphics()->IsQuadBufferingEnabled())
		return;
	//clear everything and destroy all buffers
	if(m_TileLayerVisuals.size() != 0)
	{
		int s = m_TileLayerVisuals.size();
		for(int i = 0; i < s; ++i)
		{
			Graphics()->DeleteBufferContainer(m_TileLayerVisuals[i]->m_BufferContainerIndex, true);
			delete m_TileLayerVisuals[i];
		}
		m_TileLayerVisuals.clear();
	}
	if(m_QuadLayerVisuals.size() != 0)
	{
		int s = m_QuadLayerVisuals.size();
		for(int i = 0; i < s; ++i)
		{
			Graphics()->DeleteBufferContainer(m_QuadLayerVisuals[i]->m_BufferContainerIndex, true);
			delete m_QuadLayerVisuals[i];
		}
		m_QuadLayerVisuals.clear();
	}

	bool PassedGameLayer = false;
	//prepare all visuals for all tile layers
	std::vector<SGraphicTile> tmpTiles;
	std::vector<SGraphicTileTexureCoords> tmpTileTexCoords;
	std::vector<SGraphicTile> tmpBorderTopTiles;
	std::vector<SGraphicTileTexureCoords> tmpBorderTopTilesTexCoords;
	std::vector<SGraphicTile> tmpBorderLeftTiles;
	std::vector<SGraphicTileTexureCoords> tmpBorderLeftTilesTexCoords;
	std::vector<SGraphicTile> tmpBorderRightTiles;
	std::vector<SGraphicTileTexureCoords> tmpBorderRightTilesTexCoords;
	std::vector<SGraphicTile> tmpBorderBottomTiles;
	std::vector<SGraphicTileTexureCoords> tmpBorderBottomTilesTexCoords;
	std::vector<SGraphicTile> tmpBorderCorners;
	std::vector<SGraphicTileTexureCoords> tmpBorderCornersTexCoords;

	std::vector<STmpQuad> tmpQuads;
	std::vector<STmpQuadTextured> tmpQuadsTextured;

	bool As3DTextureCoords = !Graphics()->HasTextureArrays();

	for(int g = 0; g < m_pLayers->NumGroups(); g++)
	{
		CMapItemGroup *pGroup = m_pLayers->GetGroup(g);
		if(!pGroup)
		{
			dbg_msg("maplayers", "error group was null, group number = %d, total groups = %d", g, m_pLayers->NumGroups());
			dbg_msg("maplayers", "this is here to prevent a crash but the source of this is unknown, please report this for it to get fixed");
			dbg_msg("maplayers", "we need mapname and crc and the map that caused this if possible, and anymore info you think is relevant");
			continue;
		}

		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = m_pLayers->GetLayer(pGroup->m_StartLayer + l);
			bool IsFrontLayer = false;
			bool IsSwitchLayer = false;
			bool IsTeleLayer = false;
			bool IsSpeedupLayer = false;
			bool IsTuneLayer = false;
			bool IsGameLayer = false;
			bool IsEntityLayer = false;

			if(pLayer == (CMapItemLayer *)m_pLayers->GameLayer())
			{
				IsGameLayer = true;
				IsEntityLayer = true;
				PassedGameLayer = true;
			}

			if(pLayer == (CMapItemLayer *)m_pLayers->FrontLayer())
				IsEntityLayer = IsFrontLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->SwitchLayer())
				IsEntityLayer = IsSwitchLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->TeleLayer())
				IsEntityLayer = IsTeleLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->SpeedupLayer())
				IsEntityLayer = IsSpeedupLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->TuneLayer())
				IsEntityLayer = IsTuneLayer = true;

			if(m_Type <= TYPE_BACKGROUND_FORCE)
			{
				if(PassedGameLayer)
					return;
			}
			else if(m_Type == TYPE_FOREGROUND)
			{
				if(!PassedGameLayer)
					continue;
			}

			if(pLayer->m_Type == LAYERTYPE_TILES && Graphics()->IsTileBufferingEnabled())
			{
				bool DoTextureCoords = false;
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				if(pTMap->m_Image == -1)
				{
					if(IsEntityLayer)
						DoTextureCoords = true;
				}
				else
					DoTextureCoords = true;

				int DataIndex = 0;
				unsigned int TileSize = 0;
				int OverlayCount = 0;
				if(IsFrontLayer)
				{
					DataIndex = pTMap->m_Front;
					TileSize = sizeof(CTile);
				}
				else if(IsSwitchLayer)
				{
					DataIndex = pTMap->m_Switch;
					TileSize = sizeof(CSwitchTile);
					OverlayCount = 2;
				}
				else if(IsTeleLayer)
				{
					DataIndex = pTMap->m_Tele;
					TileSize = sizeof(CTeleTile);
					OverlayCount = 1;
				}
				else if(IsSpeedupLayer)
				{
					DataIndex = pTMap->m_Speedup;
					TileSize = sizeof(CSpeedupTile);
					OverlayCount = 2;
				}
				else if(IsTuneLayer)
				{
					DataIndex = pTMap->m_Tune;
					TileSize = sizeof(CTuneTile);
				}
				else
				{
					DataIndex = pTMap->m_Data;
					TileSize = sizeof(CTile);
				}
				unsigned int Size = m_pLayers->Map()->GetDataSize(DataIndex);
				void *pTiles = m_pLayers->Map()->GetData(DataIndex);

				if(Size >= pTMap->m_Width * pTMap->m_Height * TileSize)
				{
					int CurOverlay = 0;
					while(CurOverlay < OverlayCount + 1)
					{
						// We can later just count the tile layers to get the idx in the vector
						m_TileLayerVisuals.push_back(new STileLayerVisuals());
						STileLayerVisuals &Visuals = *m_TileLayerVisuals.back();
						if(!Visuals.Init(pTMap->m_Width, pTMap->m_Height))
						{
							++CurOverlay;
							continue;
						}
						Visuals.m_IsTextured = DoTextureCoords;

						tmpTiles.clear();
						tmpTileTexCoords.clear();

						tmpBorderTopTiles.clear();
						tmpBorderLeftTiles.clear();
						tmpBorderRightTiles.clear();
						tmpBorderBottomTiles.clear();
						tmpBorderCorners.clear();
						tmpBorderTopTilesTexCoords.clear();
						tmpBorderLeftTilesTexCoords.clear();
						tmpBorderRightTilesTexCoords.clear();
						tmpBorderBottomTilesTexCoords.clear();
						tmpBorderCornersTexCoords.clear();

						if(!DoTextureCoords)
						{
							tmpTiles.reserve((size_t)pTMap->m_Width * pTMap->m_Height);
							tmpBorderTopTiles.reserve((size_t)pTMap->m_Width);
							tmpBorderBottomTiles.reserve((size_t)pTMap->m_Width);
							tmpBorderLeftTiles.reserve((size_t)pTMap->m_Height);
							tmpBorderRightTiles.reserve((size_t)pTMap->m_Height);
							tmpBorderCorners.reserve((size_t)4);
						}
						else
						{
							tmpTileTexCoords.reserve((size_t)pTMap->m_Width * pTMap->m_Height);
							tmpBorderTopTilesTexCoords.reserve((size_t)pTMap->m_Width);
							tmpBorderBottomTilesTexCoords.reserve((size_t)pTMap->m_Width);
							tmpBorderLeftTilesTexCoords.reserve((size_t)pTMap->m_Height);
							tmpBorderRightTilesTexCoords.reserve((size_t)pTMap->m_Height);
							tmpBorderCornersTexCoords.reserve((size_t)4);
						}

						int x = 0;
						int y = 0;
						for(y = 0; y < pTMap->m_Height; ++y)
						{
							for(x = 0; x < pTMap->m_Width; ++x)
							{
								unsigned char Index = 0;
								unsigned char Flags = 0;
								int AngleRotate = -1;
								if(IsEntityLayer)
								{
									if(IsGameLayer)
									{
										Index = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Index;
										Flags = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Flags;
									}
									if(IsFrontLayer)
									{
										Index = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Index;
										Flags = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Flags;
									}
									if(IsSwitchLayer)
									{
										Flags = 0;
										Index = ((CSwitchTile *)pTiles)[y * pTMap->m_Width + x].m_Type;
										if(CurOverlay == 0)
										{
											Flags = ((CSwitchTile *)pTiles)[y * pTMap->m_Width + x].m_Flags;
											if(Index == TILE_SWITCHTIMEDOPEN)
												Index = 8;
										}
										else if(CurOverlay == 1)
											Index = ((CSwitchTile *)pTiles)[y * pTMap->m_Width + x].m_Number;
										else if(CurOverlay == 2)
											Index = ((CSwitchTile *)pTiles)[y * pTMap->m_Width + x].m_Delay;
									}
									if(IsTeleLayer)
									{
										Index = ((CTeleTile *)pTiles)[y * pTMap->m_Width + x].m_Type;
										Flags = 0;
										if(CurOverlay == 1)
										{
											if(Index != TILE_TELECHECKIN && Index != TILE_TELECHECKINEVIL)
												Index = ((CTeleTile *)pTiles)[y * pTMap->m_Width + x].m_Number;
											else
												Index = 0;
										}
									}
									if(IsSpeedupLayer)
									{
										Index = ((CSpeedupTile *)pTiles)[y * pTMap->m_Width + x].m_Type;
										Flags = 0;
										AngleRotate = ((CSpeedupTile *)pTiles)[y * pTMap->m_Width + x].m_Angle;
										if(((CSpeedupTile *)pTiles)[y * pTMap->m_Width + x].m_Force == 0)
											Index = 0;
										else if(CurOverlay == 1)
											Index = ((CSpeedupTile *)pTiles)[y * pTMap->m_Width + x].m_Force;
										else if(CurOverlay == 2)
											Index = ((CSpeedupTile *)pTiles)[y * pTMap->m_Width + x].m_MaxSpeed;
									}
									if(IsTuneLayer)
									{
										Index = ((CTuneTile *)pTiles)[y * pTMap->m_Width + x].m_Type;
										Flags = 0;
									}
								}
								else
								{
									Index = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Index;
									Flags = ((CTile *)pTiles)[y * pTMap->m_Width + x].m_Flags;
								}

								//the amount of tiles handled before this tile
								int TilesHandledCount = tmpTiles.size();
								Visuals.m_TilesOfLayer[y * pTMap->m_Width + x].SetIndexBufferByteOffset((offset_ptr32)(TilesHandledCount * 6 * sizeof(unsigned int)));

								bool AddAsSpeedup = false;
								if(IsSpeedupLayer && CurOverlay == 0)
									AddAsSpeedup = true;

								if(AddTile(tmpTiles, tmpTileTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
									Visuals.m_TilesOfLayer[y * pTMap->m_Width + x].Draw(true);

								//do the border tiles
								if(x == 0)
								{
									if(y == 0)
									{
										Visuals.m_BorderTopLeft.SetIndexBufferByteOffset((offset_ptr32)(tmpBorderCorners.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderCorners, tmpBorderCornersTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderTopLeft.Draw(true);
									}
									else if(y == pTMap->m_Height - 1)
									{
										Visuals.m_BorderBottomLeft.SetIndexBufferByteOffset((offset_ptr32)(tmpBorderCorners.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderCorners, tmpBorderCornersTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderBottomLeft.Draw(true);
									}
									else
									{
										Visuals.m_BorderLeft[y - 1].SetIndexBufferByteOffset((offset_ptr32)(tmpBorderLeftTiles.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderLeftTiles, tmpBorderLeftTilesTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderLeft[y - 1].Draw(true);
									}
								}
								else if(x == pTMap->m_Width - 1)
								{
									if(y == 0)
									{
										Visuals.m_BorderTopRight.SetIndexBufferByteOffset((offset_ptr32)(tmpBorderCorners.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderCorners, tmpBorderCornersTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderTopRight.Draw(true);
									}
									else if(y == pTMap->m_Height - 1)
									{
										Visuals.m_BorderBottomRight.SetIndexBufferByteOffset((offset_ptr32)(tmpBorderCorners.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderCorners, tmpBorderCornersTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderBottomRight.Draw(true);
									}
									else
									{
										Visuals.m_BorderRight[y - 1].SetIndexBufferByteOffset((offset_ptr32)(tmpBorderRightTiles.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderRightTiles, tmpBorderRightTilesTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderRight[y - 1].Draw(true);
									}
								}
								else if(y == 0)
								{
									if(x > 0 && x < pTMap->m_Width - 1)
									{
										Visuals.m_BorderTop[x - 1].SetIndexBufferByteOffset((offset_ptr32)(tmpBorderTopTiles.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderTopTiles, tmpBorderTopTilesTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderTop[x - 1].Draw(true);
									}
								}
								else if(y == pTMap->m_Height - 1)
								{
									if(x > 0 && x < pTMap->m_Width - 1)
									{
										Visuals.m_BorderBottom[x - 1].SetIndexBufferByteOffset((offset_ptr32)(tmpBorderBottomTiles.size() * 6 * sizeof(unsigned int)));
										if(AddTile(tmpBorderBottomTiles, tmpBorderBottomTilesTexCoords, As3DTextureCoords, Index, Flags, x, y, pGroup, DoTextureCoords, AddAsSpeedup, AngleRotate))
											Visuals.m_BorderBottom[x - 1].Draw(true);
									}
								}
							}
						}

						//append one kill tile to the gamelayer
						if(IsGameLayer)
						{
							Visuals.m_BorderKillTile.SetIndexBufferByteOffset((offset_ptr32)(tmpTiles.size() * 6 * sizeof(unsigned int)));
							if(AddTile(tmpTiles, tmpTileTexCoords, As3DTextureCoords, TILE_DEATH, 0, 0, 0, pGroup, DoTextureCoords))
								Visuals.m_BorderKillTile.Draw(true);
						}

						//add the border corners, then the borders and fix their byte offsets
						int TilesHandledCount = tmpTiles.size();
						Visuals.m_BorderTopLeft.AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
						Visuals.m_BorderTopRight.AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
						Visuals.m_BorderBottomLeft.AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
						Visuals.m_BorderBottomRight.AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
						//add the Corners to the tiles
						tmpTiles.insert(tmpTiles.end(), tmpBorderCorners.begin(), tmpBorderCorners.end());
						tmpTileTexCoords.insert(tmpTileTexCoords.end(), tmpBorderCornersTexCoords.begin(), tmpBorderCornersTexCoords.end());

						//now the borders
						TilesHandledCount = tmpTiles.size();
						if(pTMap->m_Width > 2)
						{
							for(int i = 0; i < pTMap->m_Width - 2; ++i)
							{
								Visuals.m_BorderTop[i].AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
							}
						}
						tmpTiles.insert(tmpTiles.end(), tmpBorderTopTiles.begin(), tmpBorderTopTiles.end());
						tmpTileTexCoords.insert(tmpTileTexCoords.end(), tmpBorderTopTilesTexCoords.begin(), tmpBorderTopTilesTexCoords.end());

						TilesHandledCount = tmpTiles.size();
						if(pTMap->m_Width > 2)
						{
							for(int i = 0; i < pTMap->m_Width - 2; ++i)
							{
								Visuals.m_BorderBottom[i].AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
							}
						}
						tmpTiles.insert(tmpTiles.end(), tmpBorderBottomTiles.begin(), tmpBorderBottomTiles.end());
						tmpTileTexCoords.insert(tmpTileTexCoords.end(), tmpBorderBottomTilesTexCoords.begin(), tmpBorderBottomTilesTexCoords.end());

						TilesHandledCount = tmpTiles.size();
						if(pTMap->m_Height > 2)
						{
							for(int i = 0; i < pTMap->m_Height - 2; ++i)
							{
								Visuals.m_BorderLeft[i].AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
							}
						}
						tmpTiles.insert(tmpTiles.end(), tmpBorderLeftTiles.begin(), tmpBorderLeftTiles.end());
						tmpTileTexCoords.insert(tmpTileTexCoords.end(), tmpBorderLeftTilesTexCoords.begin(), tmpBorderLeftTilesTexCoords.end());

						TilesHandledCount = tmpTiles.size();
						if(pTMap->m_Height > 2)
						{
							for(int i = 0; i < pTMap->m_Height - 2; ++i)
							{
								Visuals.m_BorderRight[i].AddIndexBufferByteOffset(TilesHandledCount * 6 * sizeof(unsigned int));
							}
						}
						tmpTiles.insert(tmpTiles.end(), tmpBorderRightTiles.begin(), tmpBorderRightTiles.end());
						tmpTileTexCoords.insert(tmpTileTexCoords.end(), tmpBorderRightTilesTexCoords.begin(), tmpBorderRightTilesTexCoords.end());

						//setup params
						float *pTmpTiles = (tmpTiles.size() == 0) ? NULL : (float *)&tmpTiles[0];
						unsigned char *pTmpTileTexCoords = (tmpTileTexCoords.size() == 0) ? NULL : (unsigned char *)&tmpTileTexCoords[0];

						Visuals.m_BufferContainerIndex = -1;
						size_t UploadDataSize = tmpTileTexCoords.size() * sizeof(SGraphicTileTexureCoords) + tmpTiles.size() * sizeof(SGraphicTile);
						if(UploadDataSize > 0)
						{
							char *pUploadData = (char *)malloc(sizeof(char) * UploadDataSize);

							mem_copy_special(pUploadData, pTmpTiles, sizeof(vec2), tmpTiles.size() * 4, (DoTextureCoords ? sizeof(vec3) : 0));
							if(DoTextureCoords)
							{
								mem_copy_special(pUploadData + sizeof(vec2), pTmpTileTexCoords, sizeof(vec3), tmpTiles.size() * 4, (DoTextureCoords ? (sizeof(vec2)) : 0));
							}

							// first create the buffer object
							int BufferObjectIndex = Graphics()->CreateBufferObject(UploadDataSize, pUploadData, true);

							// then create the buffer container
							SBufferContainerInfo ContainerInfo;
							ContainerInfo.m_Stride = (DoTextureCoords ? (sizeof(float) * 2 + sizeof(vec3)) : 0);
							ContainerInfo.m_Attributes.push_back(SBufferContainerInfo::SAttribute());
							SBufferContainerInfo::SAttribute *pAttr = &ContainerInfo.m_Attributes.back();
							pAttr->m_DataTypeCount = 2;
							pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
							pAttr->m_Normalized = false;
							pAttr->m_pOffset = 0;
							pAttr->m_FuncType = 0;
							pAttr->m_VertBufferBindingIndex = BufferObjectIndex;
							if(DoTextureCoords)
							{
								ContainerInfo.m_Attributes.push_back(SBufferContainerInfo::SAttribute());
								pAttr = &ContainerInfo.m_Attributes.back();
								pAttr->m_DataTypeCount = 3;
								pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
								pAttr->m_Normalized = false;
								pAttr->m_pOffset = (void *)(sizeof(vec2));
								pAttr->m_FuncType = 0;
								pAttr->m_VertBufferBindingIndex = BufferObjectIndex;
							}

							Visuals.m_BufferContainerIndex = Graphics()->CreateBufferContainer(&ContainerInfo);
							// and finally inform the backend how many indices are required
							Graphics()->IndicesNumRequiredNotify(tmpTiles.size() * 6);
						}

						++CurOverlay;
					}
				}
			}
			else if(pLayer->m_Type == LAYERTYPE_QUADS && Graphics()->IsQuadBufferingEnabled())
			{
				CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;

				m_QuadLayerVisuals.push_back(new SQuadLayerVisuals());
				SQuadLayerVisuals *pQLayerVisuals = m_QuadLayerVisuals.back();

				bool Textured = (pQLayer->m_Image == -1 ? false : true);

				tmpQuads.clear();
				tmpQuadsTextured.clear();

				if(Textured)
					tmpQuadsTextured.resize(pQLayer->m_NumQuads);
				else
					tmpQuads.resize(pQLayer->m_NumQuads);

				CQuad *pQuads = (CQuad *)m_pLayers->Map()->GetDataSwapped(pQLayer->m_Data);
				for(int i = 0; i < pQLayer->m_NumQuads; ++i)
				{
					CQuad *q = &pQuads[i];
					for(int j = 0; j < 4; ++j)
					{
						int QuadIDX = j;
						if(j == 2)
							QuadIDX = 3;
						else if(j == 3)
							QuadIDX = 2;
						if(!Textured)
						{
							// ignore the conversion for the position coordinates
							tmpQuads[i].m_aVertices[j].m_X = (q->m_aPoints[QuadIDX].x);
							tmpQuads[i].m_aVertices[j].m_Y = (q->m_aPoints[QuadIDX].y);
							tmpQuads[i].m_aVertices[j].m_CenterX = (q->m_aPoints[4].x);
							tmpQuads[i].m_aVertices[j].m_CenterY = (q->m_aPoints[4].y);
							tmpQuads[i].m_aVertices[j].m_R = (unsigned char)q->m_aColors[QuadIDX].r;
							tmpQuads[i].m_aVertices[j].m_G = (unsigned char)q->m_aColors[QuadIDX].g;
							tmpQuads[i].m_aVertices[j].m_B = (unsigned char)q->m_aColors[QuadIDX].b;
							tmpQuads[i].m_aVertices[j].m_A = (unsigned char)q->m_aColors[QuadIDX].a;
						}
						else
						{
							// ignore the conversion for the position coordinates
							tmpQuadsTextured[i].m_aVertices[j].m_X = (q->m_aPoints[QuadIDX].x);
							tmpQuadsTextured[i].m_aVertices[j].m_Y = (q->m_aPoints[QuadIDX].y);
							tmpQuadsTextured[i].m_aVertices[j].m_CenterX = (q->m_aPoints[4].x);
							tmpQuadsTextured[i].m_aVertices[j].m_CenterY = (q->m_aPoints[4].y);
							tmpQuadsTextured[i].m_aVertices[j].m_U = fx2f(q->m_aTexcoords[QuadIDX].x);
							tmpQuadsTextured[i].m_aVertices[j].m_V = fx2f(q->m_aTexcoords[QuadIDX].y);
							tmpQuadsTextured[i].m_aVertices[j].m_R = (unsigned char)q->m_aColors[QuadIDX].r;
							tmpQuadsTextured[i].m_aVertices[j].m_G = (unsigned char)q->m_aColors[QuadIDX].g;
							tmpQuadsTextured[i].m_aVertices[j].m_B = (unsigned char)q->m_aColors[QuadIDX].b;
							tmpQuadsTextured[i].m_aVertices[j].m_A = (unsigned char)q->m_aColors[QuadIDX].a;
						}
					}
				}

				size_t UploadDataSize = 0;
				if(Textured)
					UploadDataSize = tmpQuadsTextured.size() * sizeof(STmpQuadTextured);
				else
					UploadDataSize = tmpQuads.size() * sizeof(STmpQuad);

				if(UploadDataSize > 0)
				{
					void *pUploadData = NULL;
					if(Textured)
						pUploadData = &tmpQuadsTextured[0];
					else
						pUploadData = &tmpQuads[0];
					// create the buffer object
					int BufferObjectIndex = Graphics()->CreateBufferObject(UploadDataSize, pUploadData);
					// then create the buffer container
					SBufferContainerInfo ContainerInfo;
					ContainerInfo.m_Stride = (Textured ? (sizeof(STmpQuadTextured) / 4) : (sizeof(STmpQuad) / 4));
					ContainerInfo.m_Attributes.push_back(SBufferContainerInfo::SAttribute());
					SBufferContainerInfo::SAttribute *pAttr = &ContainerInfo.m_Attributes.back();
					pAttr->m_DataTypeCount = 4;
					pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
					pAttr->m_Normalized = false;
					pAttr->m_pOffset = 0;
					pAttr->m_FuncType = 0;
					pAttr->m_VertBufferBindingIndex = BufferObjectIndex;
					ContainerInfo.m_Attributes.push_back(SBufferContainerInfo::SAttribute());
					pAttr = &ContainerInfo.m_Attributes.back();
					pAttr->m_DataTypeCount = 4;
					pAttr->m_Type = GRAPHICS_TYPE_UNSIGNED_BYTE;
					pAttr->m_Normalized = true;
					pAttr->m_pOffset = (void *)(sizeof(float) * 4);
					pAttr->m_FuncType = 0;
					pAttr->m_VertBufferBindingIndex = BufferObjectIndex;
					if(Textured)
					{
						ContainerInfo.m_Attributes.push_back(SBufferContainerInfo::SAttribute());
						pAttr = &ContainerInfo.m_Attributes.back();
						pAttr->m_DataTypeCount = 2;
						pAttr->m_Type = GRAPHICS_TYPE_FLOAT;
						pAttr->m_Normalized = false;
						pAttr->m_pOffset = (void *)(sizeof(float) * 4 + sizeof(unsigned char) * 4);
						pAttr->m_FuncType = 0;
						pAttr->m_VertBufferBindingIndex = BufferObjectIndex;
					}

					pQLayerVisuals->m_BufferContainerIndex = Graphics()->CreateBufferContainer(&ContainerInfo);
					// and finally inform the backend how many indices are required
					Graphics()->IndicesNumRequiredNotify(pQLayer->m_NumQuads * 6);
				}
			}
		}
	}
}

void CMapLayers::RenderTileLayer(int LayerIndex, ColorRGBA *pColor, CMapItemLayerTilemap *pTileLayer, CMapItemGroup *pGroup)
{
	STileLayerVisuals &Visuals = *m_TileLayerVisuals[LayerIndex];
	if(Visuals.m_BufferContainerIndex == -1)
		return; //no visuals were created

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	float r = 1, g = 1, b = 1, a = 1;
	if(pTileLayer->m_ColorEnv >= 0)
	{
		float aChannels[4];
		EnvelopeEval(pTileLayer->m_ColorEnvOffset, pTileLayer->m_ColorEnv, aChannels, this);
		r = aChannels[0];
		g = aChannels[1];
		b = aChannels[2];
		a = aChannels[3];
	}

	int BorderX0, BorderY0, BorderX1, BorderY1;
	bool DrawBorder = false;

	int Y0 = BorderY0 = (int)floorf((ScreenY0) / 32);
	int X0 = BorderX0 = (int)floorf((ScreenX0) / 32);
	int Y1 = BorderY1 = (int)floorf((ScreenY1) / 32);
	int X1 = BorderX1 = (int)floorf((ScreenX1) / 32);

	if(X0 <= 0)
	{
		X0 = 0;
		DrawBorder = true;
	}
	if(Y0 <= 0)
	{
		Y0 = 0;
		DrawBorder = true;
	}
	if(X1 >= pTileLayer->m_Width - 1)
	{
		X1 = pTileLayer->m_Width - 1;
		DrawBorder = true;
	}
	if(Y1 >= pTileLayer->m_Height - 1)
	{
		Y1 = pTileLayer->m_Height - 1;
		DrawBorder = true;
	}

	bool DrawLayer = true;
	if(X1 < 0)
		DrawLayer = false;
	if(Y1 < 0)
		DrawLayer = false;
	if(X0 >= pTileLayer->m_Width)
		DrawLayer = false;
	if(Y0 >= pTileLayer->m_Height)
		DrawLayer = false;

	if(DrawLayer)
	{
		//create the indice buffers we want to draw -- reuse them
		static std::vector<char *> s_IndexOffsets;
		static std::vector<unsigned int> s_DrawCounts;

		s_IndexOffsets.clear();
		s_DrawCounts.clear();

		unsigned long long Reserve = absolute(Y1 - Y0) + 1;
		s_IndexOffsets.reserve(Reserve);
		s_DrawCounts.reserve(Reserve);

		for(int y = Y0; y <= Y1; ++y)
		{
			if(X0 > X1)
				continue;

			dbg_assert(Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X1].IndexBufferByteOffset() >= Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X0].IndexBufferByteOffset(), "Tile count wrong.");

			unsigned int NumVertices = ((Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X1].IndexBufferByteOffset() - Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X0].IndexBufferByteOffset()) / sizeof(unsigned int)) + (Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X1].DoDraw() ? 6lu : 0lu);

			if(NumVertices)
			{
				s_IndexOffsets.push_back((offset_ptr_size)Visuals.m_TilesOfLayer[y * pTileLayer->m_Width + X0].IndexBufferByteOffset());
				s_DrawCounts.push_back(NumVertices);
			}
		}

		pColor->x *= r;
		pColor->y *= g;
		pColor->z *= b;
		pColor->w *= a;

		int DrawCount = s_IndexOffsets.size();
		if(DrawCount != 0)
		{
			Graphics()->RenderTileLayer(Visuals.m_BufferContainerIndex, (float *)pColor, &s_IndexOffsets[0], &s_DrawCounts[0], DrawCount);
		}
	}

	if(DrawBorder)
		RenderTileBorder(LayerIndex, pColor, pTileLayer, pGroup, BorderX0, BorderY0, BorderX1, BorderY1, (int)(-floorf((-ScreenX1) / 32.f)) - BorderX0, (int)(-floorf((-ScreenY1) / 32.f)) - BorderY0);
}

void CMapLayers::RenderTileBorderCornerTiles(int WidthOffsetToOrigin, int HeightOffsetToOrigin, int TileCountWidth, int TileCountHeight, int BufferContainerIndex, float *pColor, offset_ptr_size IndexBufferOffset, float *pOffset, float *pDir)
{
	// if border is still in range of the original corner, it doesn't needs to be redrawn
	bool CornerVisible = (WidthOffsetToOrigin - 1 < TileCountWidth) && (HeightOffsetToOrigin - 1 < TileCountHeight);

	int CountX = minimum(WidthOffsetToOrigin, TileCountWidth);
	int CountY = minimum(HeightOffsetToOrigin, TileCountHeight);

	int Count = (CountX * CountY) - (CornerVisible ? 1 : 0); // Don't draw the corner again

	Graphics()->RenderBorderTiles(BufferContainerIndex, pColor, IndexBufferOffset, pOffset, pDir, CountX, Count);
}

void CMapLayers::RenderTileBorder(int LayerIndex, ColorRGBA *pColor, CMapItemLayerTilemap *pTileLayer, CMapItemGroup *pGroup, int BorderX0, int BorderY0, int BorderX1, int BorderY1, int ScreenWidthTileCount, int ScreenHeightTileCount)
{
	STileLayerVisuals &Visuals = *m_TileLayerVisuals[LayerIndex];

	int Y0 = BorderY0;
	int X0 = BorderX0;
	int Y1 = BorderY1;
	int X1 = BorderX1;

	int CountWidth = ScreenWidthTileCount;
	int CountHeight = ScreenHeightTileCount;

	if(X0 < 1)
		X0 = 1;
	if(Y0 < 1)
		Y0 = 1;
	if(X1 >= pTileLayer->m_Width - 1)
		X1 = pTileLayer->m_Width - 2;
	if(Y1 >= pTileLayer->m_Height - 1)
		Y1 = pTileLayer->m_Height - 2;

	if(BorderX0 <= 0)
	{
		// Draw corners on left side
		if(BorderY0 <= 0)
		{
			if(Visuals.m_BorderTopLeft.DoDraw())
			{
				vec2 Offset;
				Offset.x = BorderX0 * 32.f;
				Offset.y = BorderY0 * 32.f;
				vec2 Dir;
				Dir.x = 32.f;
				Dir.y = 32.f;

				RenderTileBorderCornerTiles(absolute(BorderX0) + 1, absolute(BorderY0) + 1, CountWidth, CountHeight, Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderTopLeft.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir);
			}
		}
		if(BorderY1 >= pTileLayer->m_Height - 1)
		{
			if(Visuals.m_BorderBottomLeft.DoDraw())
			{
				vec2 Offset;
				Offset.x = BorderX0 * 32.f;
				Offset.y = (BorderY1 - (pTileLayer->m_Height - 1)) * 32.f;
				vec2 Dir;
				Dir.x = 32.f;
				Dir.y = -32.f;

				RenderTileBorderCornerTiles(absolute(BorderX0) + 1, (BorderY1 - (pTileLayer->m_Height - 1)) + 1, CountWidth, CountHeight, Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderBottomLeft.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir);
			}
		}
	}
	if(BorderX0 < 0)
	{
		// Draw left border
		if(Y0 < pTileLayer->m_Height - 1 && Y1 > 0)
		{
			unsigned int DrawNum = ((Visuals.m_BorderLeft[Y1 - 1].IndexBufferByteOffset() - Visuals.m_BorderLeft[Y0 - 1].IndexBufferByteOffset()) / sizeof(unsigned int)) + (Visuals.m_BorderLeft[Y1 - 1].DoDraw() ? 6lu : 0lu);
			offset_ptr_size pOffset = (offset_ptr_size)Visuals.m_BorderLeft[Y0 - 1].IndexBufferByteOffset();
			vec2 Offset;
			Offset.x = 32.f * BorderX0;
			Offset.y = 0.f;
			vec2 Dir;
			Dir.x = 32.f;
			Dir.y = 0.f;
			Graphics()->RenderBorderTileLines(Visuals.m_BufferContainerIndex, (float *)pColor, pOffset, (float *)&Offset, (float *)&Dir, DrawNum, minimum(absolute(BorderX0), CountWidth));
		}
	}

	if(BorderX1 >= pTileLayer->m_Width - 1)
	{
		// Draw corners on right side
		if(BorderY0 <= 0)
		{
			if(Visuals.m_BorderTopRight.DoDraw())
			{
				vec2 Offset;
				Offset.x = (BorderX1 - (pTileLayer->m_Width - 1)) * 32.f;
				Offset.y = BorderY0 * 32.f;
				vec2 Dir;
				Dir.x = -32.f;
				Dir.y = 32.f;

				RenderTileBorderCornerTiles((BorderX1 - (pTileLayer->m_Width - 1)) + 1, absolute(BorderY0) + 1, CountWidth, CountHeight, Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderTopRight.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir);
			}
		}
		if(BorderY1 >= pTileLayer->m_Height - 1)
		{
			if(Visuals.m_BorderBottomRight.DoDraw())
			{
				vec2 Offset;
				Offset.x = (BorderX1 - (pTileLayer->m_Width - 1)) * 32.f;
				Offset.y = (BorderY1 - (pTileLayer->m_Height - 1)) * 32.f;
				vec2 Dir;
				Dir.x = -32.f;
				Dir.y = -32.f;

				RenderTileBorderCornerTiles((BorderX1 - (pTileLayer->m_Width - 1)) + 1, (BorderY1 - (pTileLayer->m_Height - 1)) + 1, CountWidth, CountHeight, Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderBottomRight.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir);
			}
		}
	}
	if(BorderX1 > pTileLayer->m_Width - 1)
	{
		// Draw right border
		if(Y0 < pTileLayer->m_Height - 1 && Y1 > 0)
		{
			unsigned int DrawNum = ((Visuals.m_BorderRight[Y1 - 1].IndexBufferByteOffset() - Visuals.m_BorderRight[Y0 - 1].IndexBufferByteOffset()) / sizeof(unsigned int)) + (Visuals.m_BorderRight[Y1 - 1].DoDraw() ? 6lu : 0lu);
			offset_ptr_size pOffset = (offset_ptr_size)Visuals.m_BorderRight[Y0 - 1].IndexBufferByteOffset();
			vec2 Offset;
			Offset.x = 32.f * (BorderX1 - (pTileLayer->m_Width - 1));
			Offset.y = 0.f;
			vec2 Dir;
			Dir.x = -32.f;
			Dir.y = 0.f;
			Graphics()->RenderBorderTileLines(Visuals.m_BufferContainerIndex, (float *)pColor, pOffset, (float *)&Offset, (float *)&Dir, DrawNum, minimum((BorderX1 - (pTileLayer->m_Width - 1)), CountWidth));
		}
	}
	if(BorderY0 < 0)
	{
		// Draw top border
		if(X0 < pTileLayer->m_Width - 1 && X1 > 0)
		{
			unsigned int DrawNum = ((Visuals.m_BorderTop[X1 - 1].IndexBufferByteOffset() - Visuals.m_BorderTop[X0 - 1].IndexBufferByteOffset()) / sizeof(unsigned int)) + (Visuals.m_BorderTop[X1 - 1].DoDraw() ? 6lu : 0lu);
			offset_ptr_size pOffset = (offset_ptr_size)Visuals.m_BorderTop[X0 - 1].IndexBufferByteOffset();
			vec2 Offset;
			Offset.x = 0.f;
			Offset.y = 32.f * BorderY0;
			vec2 Dir;
			Dir.x = 0.f;
			Dir.y = 32.f;
			Graphics()->RenderBorderTileLines(Visuals.m_BufferContainerIndex, (float *)pColor, pOffset, (float *)&Offset, (float *)&Dir, DrawNum, minimum(absolute(BorderY0), CountHeight));
		}
	}
	if(BorderY1 >= pTileLayer->m_Height)
	{
		// Draw bottom border
		if(X0 < pTileLayer->m_Width - 1 && X1 > 0)
		{
			unsigned int DrawNum = ((Visuals.m_BorderBottom[X1 - 1].IndexBufferByteOffset() - Visuals.m_BorderBottom[X0 - 1].IndexBufferByteOffset()) / sizeof(unsigned int)) + (Visuals.m_BorderBottom[X1 - 1].DoDraw() ? 6lu : 0lu);
			offset_ptr_size pOffset = (offset_ptr_size)Visuals.m_BorderBottom[X0 - 1].IndexBufferByteOffset();
			vec2 Offset;
			Offset.x = 0.f;
			Offset.y = 32.f * (BorderY1 - (pTileLayer->m_Height - 1));
			vec2 Dir;
			Dir.x = 0.f;
			Dir.y = -32.f;
			Graphics()->RenderBorderTileLines(Visuals.m_BufferContainerIndex, (float *)pColor, pOffset, (float *)&Offset, (float *)&Dir, DrawNum, minimum((BorderY1 - (pTileLayer->m_Height - 1)), CountHeight));
		}
	}
}

void CMapLayers::RenderKillTileBorder(int LayerIndex, ColorRGBA *pColor, CMapItemLayerTilemap *pTileLayer, CMapItemGroup *pGroup)
{
	STileLayerVisuals &Visuals = *m_TileLayerVisuals[LayerIndex];
	if(Visuals.m_BufferContainerIndex == -1)
		return; //no visuals were created

	float ScreenX0, ScreenY0, ScreenX1, ScreenY1;
	Graphics()->GetScreen(&ScreenX0, &ScreenY0, &ScreenX1, &ScreenY1);

	bool DrawBorder = false;

	int BorderY0 = (int)(ScreenY0 / 32) - 1;
	int BorderX0 = (int)(ScreenX0 / 32) - 1;
	int BorderY1 = (int)(ScreenY1 / 32) + 1;
	int BorderX1 = (int)(ScreenX1 / 32) + 1;

	if(BorderX0 < -201)
		DrawBorder = true;
	if(BorderY0 < -201)
		DrawBorder = true;
	if(BorderX1 >= pTileLayer->m_Width + 201)
		DrawBorder = true;
	if(BorderY1 >= pTileLayer->m_Height + 201)
		DrawBorder = true;

	if(!DrawBorder)
		return;
	if(!Visuals.m_BorderKillTile.DoDraw())
		return;

	if(BorderX0 < -300)
		BorderX0 = -300;
	if(BorderY0 < -300)
		BorderY0 = -300;
	if(BorderX1 >= pTileLayer->m_Width + 300)
		BorderX1 = pTileLayer->m_Width + 299;
	if(BorderY1 >= pTileLayer->m_Height + 300)
		BorderY1 = pTileLayer->m_Height + 299;

	if(BorderX1 < -300)
		BorderX1 = -300;
	if(BorderY1 < -300)
		BorderY1 = -300;
	if(BorderX0 >= pTileLayer->m_Width + 300)
		BorderX0 = pTileLayer->m_Width + 299;
	if(BorderY0 >= pTileLayer->m_Height + 300)
		BorderY0 = pTileLayer->m_Height + 299;

	// Draw left kill tile border
	if(BorderX0 < -201)
	{
		vec2 Offset;
		Offset.x = BorderX0 * 32.f;
		Offset.y = BorderY0 * 32.f;
		vec2 Dir;
		Dir.x = 32.f;
		Dir.y = 32.f;

		int Count = (absolute(BorderX0) - 201) * (BorderY1 - BorderY0);

		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderKillTile.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir, (absolute(BorderX0) - 201), Count);
	}
	// Draw top kill tile border
	if(BorderY0 < -201)
	{
		vec2 Offset;
		int OffX0 = (BorderX0 < -201 ? -201 : BorderX0);
		int OffX1 = (BorderX1 >= pTileLayer->m_Width + 201 ? pTileLayer->m_Width + 201 : BorderX1);
		OffX0 = clamp(OffX0, -201, (int)pTileLayer->m_Width + 201);
		OffX1 = clamp(OffX1, -201, (int)pTileLayer->m_Width + 201);
		Offset.x = OffX0 * 32.f;
		Offset.y = BorderY0 * 32.f;
		vec2 Dir;
		Dir.x = 32.f;
		Dir.y = 32.f;

		int Count = (OffX1 - OffX0) * (absolute(BorderY0) - 201);

		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderKillTile.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir, (OffX1 - OffX0), Count);
	}
	if(BorderX1 >= pTileLayer->m_Width + 201)
	{
		vec2 Offset;
		Offset.x = (pTileLayer->m_Width + 201) * 32.f;
		Offset.y = BorderY0 * 32.f;
		vec2 Dir;
		Dir.x = 32.f;
		Dir.y = 32.f;

		int Count = (BorderX1 - (pTileLayer->m_Width + 201)) * (BorderY1 - BorderY0);

		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderKillTile.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir, (BorderX1 - (pTileLayer->m_Width + 201)), Count);
	}
	if(BorderY1 >= pTileLayer->m_Height + 201)
	{
		vec2 Offset;
		int OffX0 = (BorderX0 < -201 ? -201 : BorderX0);
		int OffX1 = (BorderX1 >= pTileLayer->m_Width + 201 ? pTileLayer->m_Width + 201 : BorderX1);
		OffX0 = clamp(OffX0, -201, (int)pTileLayer->m_Width + 201);
		OffX1 = clamp(OffX1, -201, (int)pTileLayer->m_Width + 201);
		Offset.x = OffX0 * 32.f;
		Offset.y = (pTileLayer->m_Height + 201) * 32.f;
		vec2 Dir;
		Dir.x = 32.f;
		Dir.y = 32.f;

		int Count = (OffX1 - OffX0) * (BorderY1 - (pTileLayer->m_Height + 201));

		Graphics()->RenderBorderTiles(Visuals.m_BufferContainerIndex, (float *)pColor, (offset_ptr_size)Visuals.m_BorderKillTile.IndexBufferByteOffset(), (float *)&Offset, (float *)&Dir, (OffX1 - OffX0), Count);
	}
}

void CMapLayers::RenderQuadLayer(int LayerIndex, CMapItemLayerQuads *pQuadLayer, CMapItemGroup *pGroup, bool Force)
{
	SQuadLayerVisuals &Visuals = *m_QuadLayerVisuals[LayerIndex];
	if(Visuals.m_BufferContainerIndex == -1)
		return; //no visuals were created

	if(!Force && (!g_Config.m_ClShowQuads || g_Config.m_ClOverlayEntities == 100))
		return;

	CQuad *pQuads = (CQuad *)m_pLayers->Map()->GetDataSwapped(pQuadLayer->m_Data);

	static std::vector<SQuadRenderInfo> s_QuadRenderInfo;

	s_QuadRenderInfo.resize(pQuadLayer->m_NumQuads);
	size_t QuadsRenderCount = 0;
	size_t CurQuadOffset = 0;
	for(int i = 0; i < pQuadLayer->m_NumQuads; ++i)
	{
		CQuad *q = &pQuads[i];

		float aColor[4];
		aColor[0] = aColor[1] = aColor[2] = aColor[3] = 1.f;
		if(q->m_ColorEnv >= 0)
		{
			EnvelopeEval(q->m_ColorEnvOffset, q->m_ColorEnv, aColor, this);
		}

		float OffsetX = 0;
		float OffsetY = 0;
		float Rot = 0;

		if(q->m_PosEnv >= 0)
		{
			float aChannels[4];
			EnvelopeEval(q->m_PosEnvOffset, q->m_PosEnv, aChannels, this);
			OffsetX = aChannels[0];
			OffsetY = aChannels[1];
			Rot = aChannels[2] / 180.0f * pi;
		}

		if(aColor[3] > 0)
		{
			SQuadRenderInfo &QInfo = s_QuadRenderInfo[QuadsRenderCount++];
			mem_copy(QInfo.m_aColor, aColor, sizeof(aColor));
			QInfo.m_aOffsets[0] = OffsetX;
			QInfo.m_aOffsets[1] = OffsetY;
			QInfo.m_Rotation = Rot;
		}
		else
		{
			// render quads of the current offset directly(cancel batching)
			Graphics()->RenderQuadLayer(Visuals.m_BufferContainerIndex, &s_QuadRenderInfo[0], QuadsRenderCount, CurQuadOffset);
			QuadsRenderCount = 0;
			// since this quad is ignored, the offset is the next quad
			CurQuadOffset = i + 1;
		}
	}
	Graphics()->RenderQuadLayer(Visuals.m_BufferContainerIndex, &s_QuadRenderInfo[0], QuadsRenderCount, CurQuadOffset);
}

void CMapLayers::LayersOfGroupCount(CMapItemGroup *pGroup, int &TileLayerCount, int &QuadLayerCount, bool &PassedGameLayer)
{
	int TileLayerCounter = 0;
	int QuadLayerCounter = 0;
	for(int l = 0; l < pGroup->m_NumLayers; l++)
	{
		CMapItemLayer *pLayer = m_pLayers->GetLayer(pGroup->m_StartLayer + l);
		bool IsFrontLayer = false;
		bool IsSwitchLayer = false;
		bool IsTeleLayer = false;
		bool IsSpeedupLayer = false;
		bool IsTuneLayer = false;

		if(pLayer == (CMapItemLayer *)m_pLayers->GameLayer())
		{
			PassedGameLayer = true;
		}

		if(pLayer == (CMapItemLayer *)m_pLayers->FrontLayer())
			IsFrontLayer = true;

		if(pLayer == (CMapItemLayer *)m_pLayers->SwitchLayer())
			IsSwitchLayer = true;

		if(pLayer == (CMapItemLayer *)m_pLayers->TeleLayer())
			IsTeleLayer = true;

		if(pLayer == (CMapItemLayer *)m_pLayers->SpeedupLayer())
			IsSpeedupLayer = true;

		if(pLayer == (CMapItemLayer *)m_pLayers->TuneLayer())
			IsTuneLayer = true;

		if(m_Type <= TYPE_BACKGROUND_FORCE)
		{
			if(PassedGameLayer)
				break;
		}
		else if(m_Type == TYPE_FOREGROUND)
		{
			if(!PassedGameLayer)
				continue;
		}

		if(pLayer->m_Type == LAYERTYPE_TILES)
		{
			CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
			int DataIndex = 0;
			unsigned int TileSize = 0;
			int TileLayerAndOverlayCount = 0;
			if(IsFrontLayer)
			{
				DataIndex = pTMap->m_Front;
				TileSize = sizeof(CTile);
				TileLayerAndOverlayCount = 1;
			}
			else if(IsSwitchLayer)
			{
				DataIndex = pTMap->m_Switch;
				TileSize = sizeof(CSwitchTile);
				TileLayerAndOverlayCount = 3;
			}
			else if(IsTeleLayer)
			{
				DataIndex = pTMap->m_Tele;
				TileSize = sizeof(CTeleTile);
				TileLayerAndOverlayCount = 2;
			}
			else if(IsSpeedupLayer)
			{
				DataIndex = pTMap->m_Speedup;
				TileSize = sizeof(CSpeedupTile);
				TileLayerAndOverlayCount = 3;
			}
			else if(IsTuneLayer)
			{
				DataIndex = pTMap->m_Tune;
				TileSize = sizeof(CTuneTile);
				TileLayerAndOverlayCount = 1;
			}
			else
			{
				DataIndex = pTMap->m_Data;
				TileSize = sizeof(CTile);
				TileLayerAndOverlayCount = 1;
			}

			unsigned int Size = m_pLayers->Map()->GetDataSize(DataIndex);
			if(Size >= pTMap->m_Width * pTMap->m_Height * TileSize)
			{
				TileLayerCounter += TileLayerAndOverlayCount;
			}
		}
		else if(pLayer->m_Type == LAYERTYPE_QUADS)
		{
			++QuadLayerCounter;
		}
	}

	TileLayerCount += TileLayerCounter;
	QuadLayerCount += QuadLayerCounter;
}

void CMapLayers::OnRender()
{
	if(m_OnlineOnly && Client()->State() != IClient::STATE_ONLINE && Client()->State() != IClient::STATE_DEMOPLAYBACK)
		return;

	CUIRect Screen;
	Graphics()->GetScreen(&Screen.x, &Screen.y, &Screen.w, &Screen.h);

	vec2 Center = GetCurCamera()->m_Center;

	bool PassedGameLayer = false;
	int TileLayerCounter = 0;
	int QuadLayerCounter = 0;

	for(int g = 0; g < m_pLayers->NumGroups(); g++)
	{
		CMapItemGroup *pGroup = m_pLayers->GetGroup(g);

		if(!pGroup)
		{
			dbg_msg("maplayers", "error group was null, group number = %d, total groups = %d", g, m_pLayers->NumGroups());
			dbg_msg("maplayers", "this is here to prevent a crash but the source of this is unknown, please report this for it to get fixed");
			dbg_msg("maplayers", "we need mapname and crc and the map that caused this if possible, and anymore info you think is relevant");
			continue;
		}

		if((!g_Config.m_GfxNoclip || m_Type == TYPE_FULL_DESIGN) && pGroup->m_Version >= 2 && pGroup->m_UseClipping)
		{
			// set clipping
			float Points[4];
			MapScreenToGroup(Center.x, Center.y, m_pLayers->GameGroup(), GetCurCamera()->m_Zoom);
			Graphics()->GetScreen(&Points[0], &Points[1], &Points[2], &Points[3]);
			float x0 = (pGroup->m_ClipX - Points[0]) / (Points[2] - Points[0]);
			float y0 = (pGroup->m_ClipY - Points[1]) / (Points[3] - Points[1]);
			float x1 = ((pGroup->m_ClipX + pGroup->m_ClipW) - Points[0]) / (Points[2] - Points[0]);
			float y1 = ((pGroup->m_ClipY + pGroup->m_ClipH) - Points[1]) / (Points[3] - Points[1]);

			if(x1 < 0.0f || x0 > 1.0f || y1 < 0.0f || y0 > 1.0f)
			{
				//check tile layer count of this group
				LayersOfGroupCount(pGroup, TileLayerCounter, QuadLayerCounter, PassedGameLayer);
				continue;
			}

			Graphics()->ClipEnable((int)(x0 * Graphics()->ScreenWidth()), (int)(y0 * Graphics()->ScreenHeight()),
				(int)((x1 - x0) * Graphics()->ScreenWidth()), (int)((y1 - y0) * Graphics()->ScreenHeight()));
		}

		if((!g_Config.m_ClZoomBackgroundLayers || m_Type == TYPE_FULL_DESIGN) && !pGroup->m_ParallaxX && !pGroup->m_ParallaxY)
		{
			MapScreenToGroup(Center.x, Center.y, pGroup, 1.0f);
		}
		else
			MapScreenToGroup(Center.x, Center.y, pGroup, GetCurCamera()->m_Zoom);

		for(int l = 0; l < pGroup->m_NumLayers; l++)
		{
			CMapItemLayer *pLayer = m_pLayers->GetLayer(pGroup->m_StartLayer + l);
			bool Render = false;
			bool IsGameLayer = false;
			bool IsFrontLayer = false;
			bool IsSwitchLayer = false;
			bool IsTeleLayer = false;
			bool IsSpeedupLayer = false;
			bool IsTuneLayer = false;
			bool IsEntityLayer = false;

			if(pLayer == (CMapItemLayer *)m_pLayers->GameLayer())
			{
				IsEntityLayer = IsGameLayer = true;
				PassedGameLayer = true;
			}

			if(pLayer == (CMapItemLayer *)m_pLayers->FrontLayer())
				IsEntityLayer = IsFrontLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->SwitchLayer())
				IsEntityLayer = IsSwitchLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->TeleLayer())
				IsEntityLayer = IsTeleLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->SpeedupLayer())
				IsEntityLayer = IsSpeedupLayer = true;

			if(pLayer == (CMapItemLayer *)m_pLayers->TuneLayer())
				IsEntityLayer = IsTuneLayer = true;

			if(m_Type == -1)
				Render = true;
			else if(m_Type <= TYPE_BACKGROUND_FORCE)
			{
				if(PassedGameLayer)
					return;
				Render = true;

				if(m_Type == TYPE_BACKGROUND_FORCE)
				{
					if(pLayer->m_Type == LAYERTYPE_TILES && !g_Config.m_ClBackgroundShowTilesLayers)
						continue;
				}
			}
			else if(m_Type == TYPE_FOREGROUND)
			{
				if(PassedGameLayer && !IsGameLayer)
					Render = true;
			}
			else if(m_Type == TYPE_FULL_DESIGN)
			{
				if(!IsGameLayer)
					Render = true;
			}

			if(Render && pLayer->m_Type == LAYERTYPE_TILES && Input()->KeyIsPressed(KEY_LCTRL) && Input()->KeyIsPressed(KEY_LSHIFT) && Input()->KeyPress(KEY_KP_0))
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				CTile *pTiles = (CTile *)m_pLayers->Map()->GetData(pTMap->m_Data);
				CServerInfo CurrentServerInfo;
				Client()->GetServerInfo(&CurrentServerInfo);
				char aFilename[IO_MAX_PATH_LENGTH];
				str_format(aFilename, sizeof(aFilename), "dumps/tilelayer_dump_%s-%d-%d-%dx%d.txt", CurrentServerInfo.m_aMap, g, l, pTMap->m_Width, pTMap->m_Height);
				IOHANDLE File = Storage()->OpenFile(aFilename, IOFLAG_WRITE, IStorage::TYPE_SAVE);
				if(File)
				{
					for(int y = 0; y < pTMap->m_Height; y++)
					{
						for(int x = 0; x < pTMap->m_Width; x++)
							io_write(File, &(pTiles[y * pTMap->m_Width + x].m_Index), sizeof(pTiles[y * pTMap->m_Width + x].m_Index));
						io_write_newline(File);
					}
					io_close(File);
				}
			}

			if((Render || IsGameLayer) && pLayer->m_Type == LAYERTYPE_TILES)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				int DataIndex = 0;
				unsigned int TileSize = 0;
				int TileLayerAndOverlayCount = 0;
				if(IsFrontLayer)
				{
					DataIndex = pTMap->m_Front;
					TileSize = sizeof(CTile);
					TileLayerAndOverlayCount = 1;
				}
				else if(IsSwitchLayer)
				{
					DataIndex = pTMap->m_Switch;
					TileSize = sizeof(CSwitchTile);
					TileLayerAndOverlayCount = 3;
				}
				else if(IsTeleLayer)
				{
					DataIndex = pTMap->m_Tele;
					TileSize = sizeof(CTeleTile);
					TileLayerAndOverlayCount = 2;
				}
				else if(IsSpeedupLayer)
				{
					DataIndex = pTMap->m_Speedup;
					TileSize = sizeof(CSpeedupTile);
					TileLayerAndOverlayCount = 3;
				}
				else if(IsTuneLayer)
				{
					DataIndex = pTMap->m_Tune;
					TileSize = sizeof(CTuneTile);
					TileLayerAndOverlayCount = 1;
				}
				else
				{
					DataIndex = pTMap->m_Data;
					TileSize = sizeof(CTile);
					TileLayerAndOverlayCount = 1;
				}

				unsigned int Size = m_pLayers->Map()->GetDataSize(DataIndex);
				if(Size >= pTMap->m_Width * pTMap->m_Height * TileSize)
				{
					TileLayerCounter += TileLayerAndOverlayCount;
				}
			}
			else if(Render && pLayer->m_Type == LAYERTYPE_QUADS)
			{
				++QuadLayerCounter;
			}

			// skip rendering if detail layers if not wanted, or is entity layer and we are a background map
			if((pLayer->m_Flags & LAYERFLAG_DETAIL && (!g_Config.m_GfxHighDetail && !(m_Type == TYPE_FULL_DESIGN)) && !IsGameLayer) || (m_Type == TYPE_BACKGROUND_FORCE && IsEntityLayer) || (m_Type == TYPE_FULL_DESIGN && IsEntityLayer))
				continue;

			int EntityOverlayVal = g_Config.m_ClOverlayEntities;
			if(m_Type == TYPE_FULL_DESIGN)
				EntityOverlayVal = 0;

			if((Render && EntityOverlayVal < 100 && !IsGameLayer && !IsFrontLayer && !IsSwitchLayer && !IsTeleLayer && !IsSpeedupLayer && !IsTuneLayer) || (EntityOverlayVal && IsGameLayer) || (m_Type == TYPE_BACKGROUND_FORCE))
			{
				if(pLayer->m_Type == LAYERTYPE_TILES)
				{
					CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
					if(pTMap->m_Image == -1)
					{
						if(!IsGameLayer)
							Graphics()->TextureClear();
						else
							Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_GAME));
					}
					else
						Graphics()->TextureSet(m_pImages->Get(pTMap->m_Image));

					CTile *pTiles = (CTile *)m_pLayers->Map()->GetData(pTMap->m_Data);
					unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Data);

					if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTile))
					{
						ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f);
						if(IsGameLayer && EntityOverlayVal)
							Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
						else if(!IsGameLayer && EntityOverlayVal && !(m_Type == TYPE_BACKGROUND_FORCE))
							Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * (100 - EntityOverlayVal) / 100.0f);
						if(!Graphics()->IsTileBufferingEnabled())
						{
							Graphics()->BlendNone();
							RenderTools()->RenderTilemap(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE,
								EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);

							Graphics()->BlendNormal();

							// draw kill tiles outside the entity clipping rectangle
							if(IsGameLayer)
							{
								// slow blinking to hint that it's not a part of the map
								double Seconds = time_get() / (double)time_freq();
								ColorRGBA ColorHint = ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f + 0.7f * (1.0f + sin(2.0f * pi * Seconds / 3.f)) / 2.0f);

								RenderTools()->RenderTileRectangle(-201, -201, pTMap->m_Width + 402, pTMap->m_Height + 402,
									0, TILE_DEATH, // display air inside, death outside
									32.0f, Color.v4() * ColorHint.v4(), TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT,
									EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
							}

							RenderTools()->RenderTilemap(pTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT,
								EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
						}
						else
						{
							Graphics()->BlendNormal();
							// draw kill tiles outside the entity clipping rectangle
							if(IsGameLayer)
							{
								// slow blinking to hint that it's not a part of the map
								double Seconds = time_get() / (double)time_freq();
								ColorRGBA ColorHint = ColorRGBA(1.0f, 1.0f, 1.0f, 0.3f + 0.7f * (1.0 + sin(2.0f * pi * Seconds / 3.f)) / 2.0f);

								ColorRGBA ColorKill(Color.x * ColorHint.x, Color.y * ColorHint.y, Color.z * ColorHint.z, Color.w * ColorHint.w);
								RenderKillTileBorder(TileLayerCounter - 1, &ColorKill, pTMap, pGroup);
							}
							RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
						}
					}
				}
				else if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					CMapItemLayerQuads *pQLayer = (CMapItemLayerQuads *)pLayer;
					if(pQLayer->m_Image == -1)
						Graphics()->TextureClear();
					else
						Graphics()->TextureSet(m_pImages->Get(pQLayer->m_Image));

					CQuad *pQuads = (CQuad *)m_pLayers->Map()->GetDataSwapped(pQLayer->m_Data);
					if(m_Type == TYPE_BACKGROUND_FORCE || m_Type == TYPE_FULL_DESIGN)
					{
						if(g_Config.m_ClShowQuads || m_Type == TYPE_FULL_DESIGN)
						{
							if(!Graphics()->IsQuadBufferingEnabled())
							{
								//Graphics()->BlendNone();
								//RenderTools()->ForceRenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_OPAQUE, EnvelopeEval, this, 1.f);
								Graphics()->BlendNormal();
								RenderTools()->ForceRenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_TRANSPARENT, EnvelopeEval, this, 1.f);
							}
							else
							{
								RenderQuadLayer(QuadLayerCounter - 1, pQLayer, pGroup, true);
							}
						}
					}
					else
					{
						if(!Graphics()->IsQuadBufferingEnabled())
						{
							//Graphics()->BlendNone();
							//RenderTools()->RenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_OPAQUE, EnvelopeEval, this);
							Graphics()->BlendNormal();
							RenderTools()->RenderQuads(pQuads, pQLayer->m_NumQuads, LAYERRENDERFLAG_TRANSPARENT, EnvelopeEval, this);
						}
						else
						{
							RenderQuadLayer(QuadLayerCounter - 1, pQLayer, pGroup, false);
						}
					}
				}
			}
			else if(Render && EntityOverlayVal && IsFrontLayer)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_FRONT));

				CTile *pFrontTiles = (CTile *)m_pLayers->Map()->GetData(pTMap->m_Front);
				unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Front);

				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTile))
				{
					ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
					if(!Graphics()->IsTileBufferingEnabled())
					{
						Graphics()->BlendNone();
						RenderTools()->RenderTilemap(pFrontTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE,
							EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
						Graphics()->BlendNormal();
						RenderTools()->RenderTilemap(pFrontTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT,
							EnvelopeEval, this, pTMap->m_ColorEnv, pTMap->m_ColorEnvOffset);
					}
					else
					{
						Graphics()->BlendNormal();
						RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
					}
				}
			}
			else if(Render && EntityOverlayVal && IsSwitchLayer)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_SWITCH));

				CSwitchTile *pSwitchTiles = (CSwitchTile *)m_pLayers->Map()->GetData(pTMap->m_Switch);
				unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Switch);

				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CSwitchTile))
				{
					ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
					if(!Graphics()->IsTileBufferingEnabled())
					{
						Graphics()->BlendNone();
						RenderTools()->RenderSwitchmap(pSwitchTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE);
						Graphics()->BlendNormal();
						RenderTools()->RenderSwitchmap(pSwitchTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT);
						RenderTools()->RenderSwitchOverlay(pSwitchTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, EntityOverlayVal / 100.0f);
					}
					else
					{
						Graphics()->BlendNormal();
						RenderTileLayer(TileLayerCounter - 3, &Color, pTMap, pGroup);
						if(g_Config.m_ClTextEntities)
						{
							Graphics()->TextureSet(m_pImages->GetOverlayBottom());
							RenderTileLayer(TileLayerCounter - 2, &Color, pTMap, pGroup);
							Graphics()->TextureSet(m_pImages->GetOverlayTop());
							RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
						}
					}
				}
			}
			else if(Render && EntityOverlayVal && IsTeleLayer)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_TELE));

				CTeleTile *pTeleTiles = (CTeleTile *)m_pLayers->Map()->GetData(pTMap->m_Tele);
				unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Tele);

				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTeleTile))
				{
					ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
					if(!Graphics()->IsTileBufferingEnabled())
					{
						Graphics()->BlendNone();
						RenderTools()->RenderTelemap(pTeleTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE);
						Graphics()->BlendNormal();
						RenderTools()->RenderTelemap(pTeleTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT);
						RenderTools()->RenderTeleOverlay(pTeleTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, EntityOverlayVal / 100.0f);
					}
					else
					{
						Graphics()->BlendNormal();
						RenderTileLayer(TileLayerCounter - 2, &Color, pTMap, pGroup);
						if(g_Config.m_ClTextEntities)
						{
							Graphics()->TextureSet(m_pImages->GetOverlayCenter());
							RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
						}
					}
				}
			}
			else if(Render && EntityOverlayVal && IsSpeedupLayer)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_SPEEDUP));

				CSpeedupTile *pSpeedupTiles = (CSpeedupTile *)m_pLayers->Map()->GetData(pTMap->m_Speedup);
				unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Speedup);

				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CSpeedupTile))
				{
					ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
					if(!Graphics()->IsTileBufferingEnabled())
					{
						Graphics()->BlendNone();
						RenderTools()->RenderSpeedupmap(pSpeedupTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE);
						Graphics()->BlendNormal();
						RenderTools()->RenderSpeedupmap(pSpeedupTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT);
						RenderTools()->RenderSpeedupOverlay(pSpeedupTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, EntityOverlayVal / 100.0f);
					}
					else
					{
						Graphics()->BlendNormal();

						// draw arrow -- clamp to the edge of the arrow image
						Graphics()->WrapClamp();
						Graphics()->TextureSet(m_pImages->GetSpeedupArrow());
						RenderTileLayer(TileLayerCounter - 3, &Color, pTMap, pGroup);
						Graphics()->WrapNormal();
						if(g_Config.m_ClTextEntities)
						{
							Graphics()->TextureSet(m_pImages->GetOverlayBottom());
							RenderTileLayer(TileLayerCounter - 2, &Color, pTMap, pGroup);
							Graphics()->TextureSet(m_pImages->GetOverlayTop());
							RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
						}
					}
				}
			}
			else if(Render && EntityOverlayVal && IsTuneLayer)
			{
				CMapItemLayerTilemap *pTMap = (CMapItemLayerTilemap *)pLayer;
				Graphics()->TextureSet(m_pImages->GetEntities(MAP_IMAGE_ENTITY_LAYER_TYPE_TUNE));

				CTuneTile *pTuneTiles = (CTuneTile *)m_pLayers->Map()->GetData(pTMap->m_Tune);
				unsigned int Size = m_pLayers->Map()->GetDataSize(pTMap->m_Tune);

				if(Size >= (size_t)pTMap->m_Width * pTMap->m_Height * sizeof(CTuneTile))
				{
					ColorRGBA Color = ColorRGBA(pTMap->m_Color.r / 255.0f, pTMap->m_Color.g / 255.0f, pTMap->m_Color.b / 255.0f, pTMap->m_Color.a / 255.0f * EntityOverlayVal / 100.0f);
					if(!Graphics()->IsTileBufferingEnabled())
					{
						Graphics()->BlendNone();
						RenderTools()->RenderTunemap(pTuneTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_OPAQUE);
						Graphics()->BlendNormal();
						RenderTools()->RenderTunemap(pTuneTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, Color, TILERENDERFLAG_EXTEND | LAYERRENDERFLAG_TRANSPARENT);
						//RenderTools()->RenderTuneOverlay(pTuneTiles, pTMap->m_Width, pTMap->m_Height, 32.0f, EntityOverlayVal/100.0f);
					}
					else
					{
						Graphics()->BlendNormal();
						RenderTileLayer(TileLayerCounter - 1, &Color, pTMap, pGroup);
					}
				}
			}
		}
		if(!g_Config.m_GfxNoclip || m_Type == TYPE_FULL_DESIGN)
			Graphics()->ClipDisable();
	}

	if(!g_Config.m_GfxNoclip || m_Type == TYPE_FULL_DESIGN)
		Graphics()->ClipDisable();

	// reset the screen like it was before
	Graphics()->MapScreen(Screen.x, Screen.y, Screen.w, Screen.h);
}
