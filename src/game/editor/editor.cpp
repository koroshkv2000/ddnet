/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */

#include <algorithm>

#include <base/color.h>
#include <base/system.h>
#include <base/tl/array.h>

#if defined(CONF_FAMILY_UNIX)
#include <pthread.h>
#endif

#include <engine/client.h>
#include <engine/console.h>
#include <engine/graphics.h>
#include <engine/input.h>
#include <engine/keys.h>
#include <engine/shared/config.h>
#include <engine/shared/datafile.h>
#include <engine/storage.h>
#include <engine/textrender.h>

#include <game/client/components/camera.h>
#include <game/client/gameclient.h>
#include <game/client/lineinput.h>
#include <game/client/render.h>
#include <game/client/ui.h>
#include <game/gamecore.h>
#include <game/generated/client_data.h>
#include <game/localization.h>

#include <engine/shared/image_manipulation.h>

#include "auto_map.h"
#include "editor.h"

#include <limits>

static const char *VANILLA_IMAGES[] = {
	"bg_cloud1",
	"bg_cloud2",
	"bg_cloud3",
	"desert_doodads",
	"desert_main",
	"desert_mountains",
	"desert_mountains2",
	"desert_sun",
	"generic_deathtiles",
	"generic_unhookable",
	"grass_doodads",
	"grass_main",
	"jungle_background",
	"jungle_deathtiles",
	"jungle_doodads",
	"jungle_main",
	"jungle_midground",
	"jungle_unhookables",
	"moon",
	"mountains",
	"snow",
	"stars",
	"sun",
	"winter_doodads",
	"winter_main",
	"winter_mountains",
	"winter_mountains2",
	"winter_mountains3"};

static bool IsVanillaImage(const char *pImage)
{
	for(const auto *pVanillaImage : VANILLA_IMAGES)
	{
		if(str_comp(pImage, pVanillaImage) == 0)
		{
			return true;
		}
	}
	return false;
}

const void *CEditor::ms_pUiGotContext;

ColorHSVA CEditor::ms_PickerColor;
int CEditor::ms_SVPicker;
int CEditor::ms_HuePicker;

enum
{
	BUTTON_CONTEXT = 1,
};

CEditorImage::~CEditorImage()
{
	m_pEditor->Graphics()->UnloadTexture(m_Texture);
	if(m_pData)
	{
		free(m_pData);
		m_pData = 0;
	}
}

CEditorSound::~CEditorSound()
{
	m_pEditor->Sound()->UnloadSample(m_SoundID);
	if(m_pData)
	{
		free(m_pData);
		m_pData = 0x0;
	}
}

CLayerGroup::CLayerGroup()
{
	m_aName[0] = 0;
	m_Visible = true;
	m_Collapse = false;
	m_GameGroup = false;
	m_OffsetX = 0;
	m_OffsetY = 0;
	m_ParallaxX = 100;
	m_ParallaxY = 100;

	m_UseClipping = 0;
	m_ClipX = 0;
	m_ClipY = 0;
	m_ClipW = 0;
	m_ClipH = 0;
}

CLayerGroup::~CLayerGroup()
{
	Clear();
}

void CLayerGroup::Convert(CUIRect *pRect)
{
	pRect->x += m_OffsetX;
	pRect->y += m_OffsetY;
}

void CLayerGroup::Mapping(float *pPoints)
{
	m_pMap->m_pEditor->RenderTools()->MapscreenToWorld(
		m_pMap->m_pEditor->m_WorldOffsetX, m_pMap->m_pEditor->m_WorldOffsetY,
		m_ParallaxX, m_ParallaxY, m_OffsetX, m_OffsetY,
		m_pMap->m_pEditor->Graphics()->ScreenAspect(), m_pMap->m_pEditor->m_WorldZoom, pPoints);

	pPoints[0] += m_pMap->m_pEditor->m_EditorOffsetX;
	pPoints[1] += m_pMap->m_pEditor->m_EditorOffsetY;
	pPoints[2] += m_pMap->m_pEditor->m_EditorOffsetX;
	pPoints[3] += m_pMap->m_pEditor->m_EditorOffsetY;
}

void CLayerGroup::MapScreen()
{
	float aPoints[4];
	Mapping(aPoints);
	m_pMap->m_pEditor->Graphics()->MapScreen(aPoints[0], aPoints[1], aPoints[2], aPoints[3]);
}

void CLayerGroup::Render()
{
	MapScreen();
	IGraphics *pGraphics = m_pMap->m_pEditor->Graphics();

	if(m_UseClipping)
	{
		float aPoints[4];
		m_pMap->m_pGameGroup->Mapping(aPoints);
		float x0 = (m_ClipX - aPoints[0]) / (aPoints[2] - aPoints[0]);
		float y0 = (m_ClipY - aPoints[1]) / (aPoints[3] - aPoints[1]);
		float x1 = ((m_ClipX + m_ClipW) - aPoints[0]) / (aPoints[2] - aPoints[0]);
		float y1 = ((m_ClipY + m_ClipH) - aPoints[1]) / (aPoints[3] - aPoints[1]);

		pGraphics->ClipEnable((int)(x0 * pGraphics->ScreenWidth()), (int)(y0 * pGraphics->ScreenHeight()),
			(int)((x1 - x0) * pGraphics->ScreenWidth()), (int)((y1 - y0) * pGraphics->ScreenHeight()));
	}

	for(int i = 0; i < m_lLayers.size(); i++)
	{
		if(m_lLayers[i]->m_Visible)
		{
			if(m_lLayers[i]->m_Type == LAYERTYPE_TILES)
			{
				CLayerTiles *pTiles = static_cast<CLayerTiles *>(m_lLayers[i]);
				if(pTiles->m_Game || pTiles->m_Front || pTiles->m_Tele || pTiles->m_Speedup || pTiles->m_Tune || pTiles->m_Switch)
					continue;
			}
			if(m_pMap->m_pEditor->m_ShowDetail || !(m_lLayers[i]->m_Flags & LAYERFLAG_DETAIL))
				m_lLayers[i]->Render();
		}
	}

	for(int i = 0; i < m_lLayers.size(); i++)
	{
		if(m_lLayers[i]->m_Visible && m_lLayers[i]->m_Type == LAYERTYPE_TILES && m_lLayers[i] != m_pMap->m_pGameLayer && m_lLayers[i] != m_pMap->m_pFrontLayer && m_lLayers[i] != m_pMap->m_pTeleLayer && m_lLayers[i] != m_pMap->m_pSpeedupLayer && m_lLayers[i] != m_pMap->m_pSwitchLayer && m_lLayers[i] != m_pMap->m_pTuneLayer)
		{
			CLayerTiles *pTiles = static_cast<CLayerTiles *>(m_lLayers[i]);
			if(pTiles->m_Game || pTiles->m_Front || pTiles->m_Tele || pTiles->m_Speedup || pTiles->m_Tune || pTiles->m_Switch)
			{
				m_lLayers[i]->Render();
			}
		}
	}

	pGraphics->ClipDisable();
}

void CLayerGroup::AddLayer(CLayer *l)
{
	m_pMap->m_Modified = true;
	m_lLayers.add(l);
}

void CLayerGroup::DeleteLayer(int Index)
{
	if(Index < 0 || Index >= m_lLayers.size())
		return;
	delete m_lLayers[Index];
	m_lLayers.remove_index(Index);
	m_pMap->m_Modified = true;
	m_pMap->m_UndoModified++;
}

void CLayerGroup::GetSize(float *w, float *h) const
{
	*w = 0;
	*h = 0;
	for(int i = 0; i < m_lLayers.size(); i++)
	{
		float lw, lh;
		m_lLayers[i]->GetSize(&lw, &lh);
		*w = maximum(*w, lw);
		*h = maximum(*h, lh);
	}
}

int CLayerGroup::SwapLayers(int Index0, int Index1)
{
	if(Index0 < 0 || Index0 >= m_lLayers.size())
		return Index0;
	if(Index1 < 0 || Index1 >= m_lLayers.size())
		return Index0;
	if(Index0 == Index1)
		return Index0;
	m_pMap->m_Modified = true;
	m_pMap->m_UndoModified++;
	swap(m_lLayers[Index0], m_lLayers[Index1]);
	return Index1;
}

void CEditorImage::AnalyseTileFlags()
{
	mem_zero(m_aTileFlags, sizeof(m_aTileFlags));

	int tw = m_Width / 16; // tilesizes
	int th = m_Height / 16;
	if(tw == th && m_Format == CImageInfo::FORMAT_RGBA)
	{
		unsigned char *pPixelData = (unsigned char *)m_pData;

		int TileID = 0;
		for(int ty = 0; ty < 16; ty++)
			for(int tx = 0; tx < 16; tx++, TileID++)
			{
				bool Opaque = true;
				for(int x = 0; x < tw; x++)
					for(int y = 0; y < th; y++)
					{
						int p = (ty * tw + y) * m_Width + tx * tw + x;
						if(pPixelData[p * 4 + 3] < 250)
						{
							Opaque = false;
							break;
						}
					}

				if(Opaque)
					m_aTileFlags[TileID] |= TILEFLAG_OPAQUE;
			}
	}
}

void CEditor::EnvelopeEval(int TimeOffsetMillis, int Env, float *pChannels, void *pUser)
{
	CEditor *pThis = (CEditor *)pUser;
	if(Env < 0 || Env >= pThis->m_Map.m_lEnvelopes.size())
	{
		pChannels[0] = 0;
		pChannels[1] = 0;
		pChannels[2] = 0;
		pChannels[3] = 0;
		return;
	}

	CEnvelope *e = pThis->m_Map.m_lEnvelopes[Env];
	float t = pThis->m_AnimateTime + (TimeOffsetMillis / 1000.0f);
	t *= pThis->m_AnimateSpeed;
	e->Eval(t, pChannels);
}

/********************************************************
 OTHER
*********************************************************/

int CEditor::DoClearableEditBox(void *pID, void *pClearID, const CUIRect *pRect, char *pStr, unsigned StrSize, float FontSize, float *Offset, bool Hidden, int Corners)
{
	bool ReturnValue = false;
	CUIRect EditBox;
	CUIRect ClearButton;
	pRect->VSplitRight(15.0f, &EditBox, &ClearButton);
	if(DoEditBox(pID, &EditBox, pStr, StrSize, FontSize, Offset, Hidden, Corners & ~CUI::CORNER_R))
	{
		ReturnValue = true;
	}

	RenderTools()->DrawUIRect(&ClearButton, ColorRGBA(1, 1, 1, 0.33f * ButtonColorMul(pClearID)), Corners & ~CUI::CORNER_L, 3.0f);
	UI()->DoLabel(&ClearButton, "×", ClearButton.h * 0.8f, 0);
	if(UI()->DoButtonLogic(pClearID, "×", 0, &ClearButton))
	{
		pStr[0] = 0;
		UI()->SetActiveItem(pID);
		ReturnValue = true;
	}
	return ReturnValue;
}

int CEditor::DoEditBox(void *pID, const CUIRect *pRect, char *pStr, unsigned StrSize, float FontSize, float *Offset, bool Hidden, int Corners)
{
	if(UI()->LastActiveItem() == pID)
		m_EditBoxActive = 2;
	return m_UIEx.DoEditBox(pID, pRect, pStr, StrSize, FontSize, Offset, Hidden, Corners);
}

float CEditor::ButtonColorMul(const void *pID)
{
	if(UI()->ActiveItem() == pID)
		return 0.5f;
	else if(UI()->HotItem() == pID)
		return 1.5f;
	return 1.0f;
}

float CEditor::UiDoScrollbarV(const void *pID, const CUIRect *pRect, float Current)
{
	CUIRect Handle;
	static float s_OffsetY;
	pRect->HSplitTop(33, &Handle, 0);

	Handle.y += (pRect->h - Handle.h) * Current;

	// logic
	float Ret = Current;
	int Inside = UI()->MouseInside(&Handle);

	if(UI()->ActiveItem() == pID)
	{
		if(!UI()->MouseButton(0))
			UI()->SetActiveItem(0);

		float Min = pRect->y;
		float Max = pRect->h - Handle.h;
		float Cur = UI()->MouseY() - s_OffsetY;
		Ret = (Cur - Min) / Max;
		if(Ret < 0.0f)
			Ret = 0.0f;
		if(Ret > 1.0f)
			Ret = 1.0f;
	}
	else if(UI()->HotItem() == pID)
	{
		if(UI()->MouseButton(0))
		{
			UI()->SetActiveItem(pID);
			s_OffsetY = UI()->MouseY() - Handle.y;
		}
	}

	if(Inside)
		UI()->SetHotItem(pID);

	// render
	CUIRect Rail;
	pRect->VMargin(5.0f, &Rail);
	RenderTools()->DrawUIRect(&Rail, ColorRGBA(1, 1, 1, 0.25f), 0, 0.0f);

	CUIRect Slider = Handle;
	Slider.w = Rail.x - Slider.x;
	RenderTools()->DrawUIRect(&Slider, ColorRGBA(1, 1, 1, 0.25f), CUI::CORNER_L, 2.5f);
	Slider.x = Rail.x + Rail.w;
	RenderTools()->DrawUIRect(&Slider, ColorRGBA(1, 1, 1, 0.25f), CUI::CORNER_R, 2.5f);

	Slider = Handle;
	Slider.Margin(5.0f, &Slider);
	RenderTools()->DrawUIRect(&Slider, ColorRGBA(1, 1, 1, 0.25f * ButtonColorMul(pID)), CUI::CORNER_ALL, 2.5f);

	return Ret;
}

ColorRGBA CEditor::GetButtonColor(const void *pID, int Checked)
{
	if(Checked < 0)
		return ColorRGBA(0, 0, 0, 0.5f);

	switch(Checked)
	{
	case 7: // selected + game layers
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 0, 0, 0.4f);
		return ColorRGBA(1, 0, 0, 0.2f);

	case 6: // game layers
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 1, 1, 0.4f);
		return ColorRGBA(1, 1, 1, 0.2f);

	case 5: // selected + image/sound should be embedded
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 0, 0, 0.75f);
		return ColorRGBA(1, 0, 0, 0.5f);

	case 4: // image/sound should be embedded
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 0, 0, 1.0f);
		return ColorRGBA(1, 0, 0, 0.875f);

	case 3: // selected + unused image/sound
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 0, 1, 0.75f);
		return ColorRGBA(1, 0, 1, 0.5f);

	case 2: // unused image/sound
		if(UI()->HotItem() == pID)
			return ColorRGBA(0, 0, 1, 0.75f);
		return ColorRGBA(0, 0, 1, 0.5f);

	case 1: // selected
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 0, 0, 0.75f);
		return ColorRGBA(1, 0, 0, 0.5f);

	default: // regular
		if(UI()->HotItem() == pID)
			return ColorRGBA(1, 1, 1, 0.75f);
		return ColorRGBA(1, 1, 1, 0.5f);
	}
}

int CEditor::DoButton_Editor_Common(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	if(UI()->MouseInside(pRect))
	{
		if(Flags & BUTTON_CONTEXT)
			ms_pUiGotContext = pID;
		if(m_pTooltip)
			m_pTooltip = pToolTip;
	}

	if(UI()->HotItem() == pID && pToolTip)
		m_pTooltip = pToolTip;

	return UI()->DoButtonLogic(pID, pText, Checked, pRect);

	// Draw here
	//return UI()->DoButton(id, text, checked, r, draw_func, 0);
}

int CEditor::DoButton_Editor(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip, int AlignVert)
{
	RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_ALL, 3.0f);
	CUIRect NewRect = *pRect;
	UI()->DoLabel(&NewRect, pText, 10.f, 0, -1, AlignVert);
	Checked %= 2;
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_Env(const void *pID, const char *pText, int Checked, const CUIRect *pRect, const char *pToolTip, ColorRGBA BaseColor)
{
	float Bright = Checked ? 1.0f : 0.5f;
	float Alpha = UI()->HotItem() == pID ? 1.0f : 0.75f;
	ColorRGBA Color = ColorRGBA(BaseColor.r * Bright, BaseColor.g * Bright, BaseColor.b * Bright, Alpha);

	RenderTools()->DrawUIRect(pRect, Color, CUI::CORNER_ALL, 3.0f);
	UI()->DoLabel(pRect, pText, 10.f, 0, -1);
	Checked %= 2;
	return DoButton_Editor_Common(pID, pText, Checked, pRect, 0, pToolTip);
}

int CEditor::DoButton_File(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	if(Checked)
		RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_ALL, 3.0f);

	CUIRect t = *pRect;
	t.VMargin(5.0f, &t);
	UI()->DoLabel(&t, pText, 10, -1, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_Menu(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	CUIRect r = *pRect;
	RenderTools()->DrawUIRect(&r, ColorRGBA(0.5f, 0.5f, 0.5f, 1.0f), CUI::CORNER_T, 3.0f);

	r = *pRect;
	r.VMargin(5.0f, &r);
	UI()->DoLabel(&r, pText, 10, -1, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_MenuItem(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	if(UI()->HotItem() == pID || Checked)
		RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_ALL, 3.0f);

	CUIRect t = *pRect;
	t.VMargin(5.0f, &t);
	UI()->DoLabel(&t, pText, 10, -1, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_Tab(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_T, 5.0f);
	CUIRect NewRect = *pRect;
	UI()->DoLabel(&NewRect, pText, 10, 0, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_Ex(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip, int Corners, float FontSize, int AlignVert)
{
	RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), Corners, 3.0f);
	CUIRect NewRect = *pRect;
	UI()->DoLabel(&NewRect, pText, FontSize, 0, -1, AlignVert);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_ButtonInc(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_R, 3.0f);
	UI()->DoLabel(pRect, pText ? pText : "+", 10, 0, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_ButtonDec(const void *pID, const char *pText, int Checked, const CUIRect *pRect, int Flags, const char *pToolTip)
{
	RenderTools()->DrawUIRect(pRect, GetButtonColor(pID, Checked), CUI::CORNER_L, 3.0f);
	UI()->DoLabel(pRect, pText ? pText : "-", 10, 0, -1);
	return DoButton_Editor_Common(pID, pText, Checked, pRect, Flags, pToolTip);
}

int CEditor::DoButton_ColorPicker(const void *pID, const CUIRect *pRect, ColorRGBA *pColor, const char *pToolTip)
{
	RenderTools()->DrawUIRect(pRect, *pColor, 0, 0.0f);
	return DoButton_Editor_Common(pID, 0x0, 0, pRect, 0, pToolTip);
}

void CEditor::RenderGrid(CLayerGroup *pGroup)
{
	if(!m_GridActive)
		return;

	float aGroupPoints[4];
	pGroup->Mapping(aGroupPoints);

	float w = UI()->Screen()->w;
	float h = UI()->Screen()->h;

	int LineDistance = GetLineDistance();

	int XOffset = aGroupPoints[0] / LineDistance;
	int YOffset = aGroupPoints[1] / LineDistance;
	int XGridOffset = XOffset % m_GridFactor;
	int YGridOffset = YOffset % m_GridFactor;

	Graphics()->TextureClear();
	Graphics()->LinesBegin();

	for(int i = 0; i < (int)w; i++)
	{
		if((i + YGridOffset) % m_GridFactor == 0)
			Graphics()->SetColor(1.0f, 0.3f, 0.3f, 0.3f);
		else
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.15f);

		IGraphics::CLineItem Line = IGraphics::CLineItem(LineDistance * XOffset, LineDistance * i + LineDistance * YOffset, w + aGroupPoints[2], LineDistance * i + LineDistance * YOffset);
		Graphics()->LinesDraw(&Line, 1);

		if((i + XGridOffset) % m_GridFactor == 0)
			Graphics()->SetColor(1.0f, 0.3f, 0.3f, 0.3f);
		else
			Graphics()->SetColor(1.0f, 1.0f, 1.0f, 0.15f);

		Line = IGraphics::CLineItem(LineDistance * i + LineDistance * XOffset, LineDistance * YOffset, LineDistance * i + LineDistance * XOffset, h + aGroupPoints[3]);
		Graphics()->LinesDraw(&Line, 1);
	}
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->LinesEnd();
}

void CEditor::RenderBackground(CUIRect View, IGraphics::CTextureHandle Texture, float Size, float Brightness)
{
	Graphics()->TextureSet(Texture);
	Graphics()->BlendNormal();
	Graphics()->QuadsBegin();
	Graphics()->SetColor(Brightness, Brightness, Brightness, 1.0f);
	Graphics()->QuadsSetSubset(0, 0, View.w / Size, View.h / Size);
	IGraphics::CQuadItem QuadItem(View.x, View.y, View.w, View.h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();
}

int CEditor::UiDoValueSelector(void *pID, CUIRect *pRect, const char *pLabel, int Current, int Min, int Max, int Step, float Scale, const char *pToolTip, bool IsDegree, bool IsHex, int Corners, ColorRGBA *Color)
{
	// logic
	static float s_Value;
	static char s_aNumStr[64];
	static bool s_TextMode = false;
	static void *s_LastTextpID = pID;
	int Inside = UI()->MouseInside(pRect);

	if(UI()->MouseButton(1) && UI()->HotItem() == pID)
	{
		s_LastTextpID = pID;
		s_TextMode = true;
		if(IsHex)
			str_format(s_aNumStr, sizeof(s_aNumStr), "%06X", Current);
		else
			str_format(s_aNumStr, sizeof(s_aNumStr), "%d", Current);
	}

	if(UI()->ActiveItem() == pID)
	{
		if(!UI()->MouseButton(0))
		{
			m_LockMouse = false;
			UI()->SetActiveItem(0);
			s_TextMode = false;
		}
	}

	if(s_TextMode && s_LastTextpID == pID)
	{
		m_pTooltip = "Type your number";

		static float s_NumberBoxID = 0;
		DoEditBox(&s_NumberBoxID, pRect, s_aNumStr, sizeof(s_aNumStr), 10.0f, &s_NumberBoxID, false, Corners);

		UI()->SetActiveItem(&s_NumberBoxID);

		if(Input()->KeyIsPressed(KEY_RETURN) || Input()->KeyIsPressed(KEY_KP_ENTER) ||
			((UI()->MouseButton(1) || UI()->MouseButton(0)) && !Inside))
		{
			if(IsHex)
				Current = clamp(str_toint_base(s_aNumStr, 16), Min, Max);
			else
				Current = clamp(str_toint(s_aNumStr), Min, Max);
			m_LockMouse = false;
			UI()->SetActiveItem(0);
			s_TextMode = false;
		}

		if(Input()->KeyIsPressed(KEY_ESCAPE))
		{
			m_LockMouse = false;
			UI()->SetActiveItem(0);
			s_TextMode = false;
		}
	}
	else
	{
		if(UI()->ActiveItem() == pID)
		{
			if(UI()->MouseButton(0))
			{
				if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
					s_Value += m_MouseDeltaX * 0.05f;
				else
					s_Value += m_MouseDeltaX;

				if(absolute(s_Value) > Scale)
				{
					int Count = (int)(s_Value / Scale);
					s_Value = fmod(s_Value, Scale);
					Current += Step * Count;
					Current = clamp(Current, Min, Max);

					// Constrain to discrete steps
					if(Count > 0)
						Current = Current / Step * Step;
					else
						Current = round_ceil(Current / (float)Step) * Step;
				}
			}
			if(pToolTip && !s_TextMode)
				m_pTooltip = pToolTip;
		}
		else if(UI()->HotItem() == pID)
		{
			if(UI()->MouseButton(0))
			{
				m_LockMouse = true;
				s_Value = 0;
				UI()->SetActiveItem(pID);
			}
			if(pToolTip && !s_TextMode)
				m_pTooltip = pToolTip;
		}

		if(Inside)
			UI()->SetHotItem(pID);

		// render
		char aBuf[128];
		if(pLabel[0] != '\0')
			str_format(aBuf, sizeof(aBuf), "%s %d", pLabel, Current);
		else if(IsDegree)
			str_format(aBuf, sizeof(aBuf), "%d°", Current);
		else if(IsHex)
			str_format(aBuf, sizeof(aBuf), "#%06X", Current);
		else
			str_format(aBuf, sizeof(aBuf), "%d", Current);
		RenderTools()->DrawUIRect(pRect, Color ? *Color : GetButtonColor(pID, 0), Corners, 5.0f);
		UI()->DoLabel(pRect, aBuf, 10, 0, -1);
	}

	return Current;
}

CLayerGroup *CEditor::GetSelectedGroup() const
{
	if(m_SelectedGroup >= 0 && m_SelectedGroup < m_Map.m_lGroups.size())
		return m_Map.m_lGroups[m_SelectedGroup];
	return 0x0;
}

CLayer *CEditor::GetSelectedLayer(int Index) const
{
	CLayerGroup *pGroup = GetSelectedGroup();
	if(!pGroup)
		return 0x0;

	if(Index < 0 || Index >= m_lSelectedLayers.size())
		return 0x0;

	int LayerIndex = m_lSelectedLayers[Index];

	if(LayerIndex >= 0 && LayerIndex < m_Map.m_lGroups[m_SelectedGroup]->m_lLayers.size())
		return pGroup->m_lLayers[LayerIndex];
	return 0x0;
}

CLayer *CEditor::GetSelectedLayerType(int Index, int Type) const
{
	CLayer *p = GetSelectedLayer(Index);
	if(p && p->m_Type == Type)
		return p;
	return 0x0;
}

array<CQuad *> CEditor::GetSelectedQuads()
{
	CLayerQuads *ql = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
	array<CQuad *> lQuads;
	if(!ql)
		return lQuads;
	lQuads.set_size(m_lSelectedQuads.size());
	for(int i = 0; i < m_lSelectedQuads.size(); ++i)
		lQuads[i] = &ql->m_lQuads[m_lSelectedQuads[i]];
	return lQuads;
}

CSoundSource *CEditor::GetSelectedSource()
{
	CLayerSounds *pSounds = (CLayerSounds *)GetSelectedLayerType(0, LAYERTYPE_SOUNDS);
	if(!pSounds)
		return 0;
	if(m_SelectedSource >= 0 && m_SelectedSource < pSounds->m_lSources.size())
		return &pSounds->m_lSources[m_SelectedSource];
	return 0;
}

void CEditor::SelectLayer(int LayerIndex, int GroupIndex)
{
	if(GroupIndex != -1)
		m_SelectedGroup = GroupIndex;

	m_lSelectedLayers.clear();
	m_lSelectedLayers.add(LayerIndex);
}

void CEditor::SelectQuad(int Index)
{
	m_lSelectedQuads.clear();
	m_lSelectedQuads.add(Index);
}

void CEditor::DeleteSelectedQuads()
{
	CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
	if(!pLayer)
		return;

	for(int i = 0; i < m_lSelectedQuads.size(); ++i)
	{
		pLayer->m_lQuads.remove_index(m_lSelectedQuads[i]);
		for(int j = i + 1; j < m_lSelectedQuads.size(); ++j)
			if(m_lSelectedQuads[j] > m_lSelectedQuads[i])
				m_lSelectedQuads[j]--;

		m_lSelectedQuads.remove_index(i--);
	}
}

bool CEditor::IsQuadSelected(int Index) const
{
	for(int i = 0; i < m_lSelectedQuads.size(); ++i)
		if(m_lSelectedQuads[i] == Index)
			return true;
	return false;
}

int CEditor::FindSelectedQuadIndex(int Index) const
{
	for(int i = 0; i < m_lSelectedQuads.size(); ++i)
		if(m_lSelectedQuads[i] == Index)
			return i;
	return -1;
}

void CEditor::CallbackOpenMap(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	if(pEditor->Load(pFileName, StorageType))
	{
		pEditor->m_ValidSaveFilename = StorageType == IStorage::TYPE_SAVE && pEditor->m_pFileDialogPath == pEditor->m_aFileDialogCurrentFolder;
		pEditor->m_Dialog = DIALOG_NONE;
	}
}

void CEditor::CallbackAppendMap(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	if(pEditor->Append(pFileName, StorageType))
		pEditor->m_aFileName[0] = 0;
	else
		pEditor->SortImages();

	pEditor->m_Dialog = DIALOG_NONE;
}

void CEditor::CallbackSaveMap(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = static_cast<CEditor *>(pUser);
	char aBuf[1024];
	// add map extension
	if(!str_endswith(pFileName, ".map"))
	{
		str_format(aBuf, sizeof(aBuf), "%s.map", pFileName);
		pFileName = aBuf;
	}

	if(pEditor->Save(pFileName))
	{
		str_copy(pEditor->m_aFileName, pFileName, sizeof(pEditor->m_aFileName));
		pEditor->m_ValidSaveFilename = StorageType == IStorage::TYPE_SAVE && pEditor->m_pFileDialogPath == pEditor->m_aFileDialogCurrentFolder;
		pEditor->m_Map.m_Modified = false;
		pEditor->m_Map.m_UndoModified = 0;
		pEditor->m_LastUndoUpdateTime = time_get();
	}

	pEditor->m_Dialog = DIALOG_NONE;
}

void CEditor::CallbackSaveCopyMap(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = static_cast<CEditor *>(pUser);
	char aBuf[1024];
	// add map extension
	if(!str_endswith(pFileName, ".map"))
	{
		str_format(aBuf, sizeof(aBuf), "%s.map", pFileName);
		pFileName = aBuf;
	}

	if(pEditor->Save(pFileName))
	{
		pEditor->m_Map.m_Modified = false;
		pEditor->m_Map.m_UndoModified = 0;
		pEditor->m_LastUndoUpdateTime = time_get();
	}

	pEditor->m_Dialog = DIALOG_NONE;
}

static int EntitiesListdirCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	if(!IsDir && str_endswith(pName, ".png"))
	{
		std::string Name = pName;
		pEditor->m_SelectEntitiesFiles.push_back(Name.substr(0, Name.length() - 4));
	}

	return 0;
}

void CEditor::DoToolbar(CUIRect ToolBar)
{
	bool CtrlPressed = Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL);
	bool ShiftPressed = Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT);

	CUIRect TB_Top, TB_Bottom;
	CUIRect Button;

	ToolBar.HSplitTop(ToolBar.h / 2.0f, &TB_Top, &TB_Bottom);

	TB_Top.HSplitBottom(2.5f, &TB_Top, 0);
	TB_Bottom.HSplitTop(2.5f, 0, &TB_Bottom);

	// top line buttons
	{
		// detail button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_HqButton = 0;
		if(DoButton_Editor(&s_HqButton, "HD", m_ShowDetail, &Button, 0, "[ctrl+h] Toggle High Detail") ||
			(Input()->KeyPress(KEY_H) && CtrlPressed))
		{
			m_ShowDetail = !m_ShowDetail;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// animation button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_AnimateButton = 0;
		if(DoButton_Editor(&s_AnimateButton, "Anim", m_Animate, &Button, 0, "[ctrl+m] Toggle animation") ||
			(Input()->KeyPress(KEY_M) && CtrlPressed))
		{
			m_AnimateStart = time_get();
			m_Animate = !m_Animate;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// proof button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_ProofButton = 0;
		if(DoButton_Editor(&s_ProofButton, "Proof", m_ProofBorders, &Button, 0, "[ctrl+p] Toggles proof borders. These borders represent what a player maximum can see.") ||
			(Input()->KeyPress(KEY_P) && CtrlPressed))
		{
			m_ProofBorders = !m_ProofBorders;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// grid button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_GridButton = 0;
		if(DoButton_Editor(&s_GridButton, "Grid", m_GridActive, &Button, 0, "[ctrl+g] Toggle Grid") ||
			(Input()->KeyPress(KEY_G) && CtrlPressed))
		{
			m_GridActive = !m_GridActive;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// tile info button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_TileInfoButton = 0;
		if(DoButton_Editor(&s_TileInfoButton, "Info", m_ShowTileInfo, &Button, 0, "[ctrl+i] Show tile information") ||
			(Input()->KeyPress(KEY_I) && CtrlPressed))
		{
			m_ShowTileInfo = !m_ShowTileInfo;
			m_ShowEnvelopePreview = 0;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// allow place unused tiles button
		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_AllowPlaceUnusedTilesButton = 0;
		if(DoButton_Editor(&s_AllowPlaceUnusedTilesButton, "Unused", m_AllowPlaceUnusedTiles, &Button, 0, "[ctrl+u] Allow placing unused tiles") ||
			(Input()->KeyPress(KEY_U) && CtrlPressed))
		{
			m_AllowPlaceUnusedTiles = !m_AllowPlaceUnusedTiles;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		TB_Top.VSplitLeft(40.0f, &Button, &TB_Top);
		static int s_ColorBrushButton = 0;
		if(DoButton_Editor(&s_ColorBrushButton, "Color", m_BrushColorEnabled, &Button, 0, "Toggle brush coloring"))
		{
			m_BrushColorEnabled = !m_BrushColorEnabled;
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		TB_Top.VSplitLeft(45.0f, &Button, &TB_Top);
		static int s_EntitiesButtonID = 0;
		if(DoButton_Editor(&s_EntitiesButtonID, "Entities", 0, &Button, 0, "Choose game layer entities image for different gametypes"))
		{
			m_SelectEntitiesFiles.clear();
			Storage()->ListDirectory(IStorage::TYPE_ALL, "editor/entities", EntitiesListdirCallback, this);
			std::sort(m_SelectEntitiesFiles.begin(), m_SelectEntitiesFiles.end());

			static int s_EntitiesPopupID = 0;
			UiInvokePopupMenu(&s_EntitiesPopupID, 0, Button.x, Button.y + 18.0f,
				250, m_SelectEntitiesFiles.size() * 14 + 10, PopupEntities);
		}

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// zoom group
		TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
		static int s_ZoomOutButton = 0;
		if(DoButton_Ex(&s_ZoomOutButton, "ZO", 0, &Button, 0, "[NumPad-] Zoom out", CUI::CORNER_L))
			m_ZoomLevel += 50;

		TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
		static int s_ZoomNormalButton = 0;
		if(DoButton_Ex(&s_ZoomNormalButton, "1:1", 0, &Button, 0, "[NumPad*] Zoom to normal and remove editor offset", 0))
		{
			m_EditorOffsetX = 0;
			m_EditorOffsetY = 0;
			m_ZoomLevel = 100;
		}

		TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
		static int s_ZoomInButton = 0;
		if(DoButton_Ex(&s_ZoomInButton, "ZI", 0, &Button, 0, "[NumPad+] Zoom in", CUI::CORNER_R))
			m_ZoomLevel -= 50;

		TB_Top.VSplitLeft(5.0f, 0, &TB_Top);

		// brush manipulation
		{
			int Enabled = m_Brush.IsEmpty() ? -1 : 0;

			// flip buttons
			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_FlipXButton = 0;
			if(DoButton_Ex(&s_FlipXButton, "X/X", Enabled, &Button, 0, "[N] Flip brush horizontal", CUI::CORNER_L) || (Input()->KeyPress(KEY_N) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
			{
				for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					m_Brush.m_lLayers[i]->BrushFlipX();
			}

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_FlipyButton = 0;
			if(DoButton_Ex(&s_FlipyButton, "Y/Y", Enabled, &Button, 0, "[M] Flip brush vertical", CUI::CORNER_R) || (Input()->KeyPress(KEY_M) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
			{
				for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					m_Brush.m_lLayers[i]->BrushFlipY();
			}

			// rotate buttons
			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_RotationAmount = 90;
			bool TileLayer = false;
			// check for tile layers in brush selection
			for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
				if(m_Brush.m_lLayers[i]->m_Type == LAYERTYPE_TILES)
				{
					TileLayer = true;
					s_RotationAmount = maximum(90, (s_RotationAmount / 90) * 90);
					break;
				}
			s_RotationAmount = UiDoValueSelector(&s_RotationAmount, &Button, "", s_RotationAmount, TileLayer ? 90 : 1, 359, TileLayer ? 90 : 1, TileLayer ? 10.0f : 2.0f, "Rotation of the brush in degrees. Use left mouse button to drag and change the value. Hold shift to be more precise.", true);

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_CcwButton = 0;
			if(DoButton_Ex(&s_CcwButton, "CCW", Enabled, &Button, 0, "[R] Rotates the brush counter clockwise", CUI::CORNER_L) || (Input()->KeyPress(KEY_R) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
			{
				for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					m_Brush.m_lLayers[i]->BrushRotate(-s_RotationAmount / 360.0f * pi * 2);
			}

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_CwButton = 0;
			if(DoButton_Ex(&s_CwButton, "CW", Enabled, &Button, 0, "[T] Rotates the brush clockwise", CUI::CORNER_R) || (Input()->KeyPress(KEY_T) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
			{
				for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					m_Brush.m_lLayers[i]->BrushRotate(s_RotationAmount / 360.0f * pi * 2);
			}

			TB_Top.VSplitLeft(5.0f, &Button, &TB_Top);
		}

		// animation speed
		if(m_Animate)
		{
			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_AnimFasterButton = 0;
			if(DoButton_Ex(&s_AnimFasterButton, "A+", 0, &Button, 0, "Increase animation speed", CUI::CORNER_L))
				m_AnimateSpeed += 0.5f;

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_AnimNormalButton = 0;
			if(DoButton_Ex(&s_AnimNormalButton, "1", 0, &Button, 0, "Normal animation speed", 0))
				m_AnimateSpeed = 1.0f;

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_AnimSlowerButton = 0;
			if(DoButton_Ex(&s_AnimSlowerButton, "A-", 0, &Button, 0, "Decrease animation speed", CUI::CORNER_R))
			{
				if(m_AnimateSpeed > 0.5f)
					m_AnimateSpeed -= 0.5f;
			}

			TB_Top.VSplitLeft(5.0f, &Button, &TB_Top);
		}

		// grid zoom
		if(m_GridActive)
		{
			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_GridIncreaseButton = 0;
			if(DoButton_Ex(&s_GridIncreaseButton, "G-", 0, &Button, 0, "Decrease grid", CUI::CORNER_L))
			{
				if(m_GridFactor > 1)
					m_GridFactor--;
			}

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);
			static int s_GridNormalButton = 0;
			if(DoButton_Ex(&s_GridNormalButton, "1", 0, &Button, 0, "Normal grid", 0))
				m_GridFactor = 1;

			TB_Top.VSplitLeft(30.0f, &Button, &TB_Top);

			static int s_GridDecreaseButton = 0;
			if(DoButton_Ex(&s_GridDecreaseButton, "G+", 0, &Button, 0, "Increase grid", CUI::CORNER_R))
			{
				if(m_GridFactor < 15)
					m_GridFactor++;
			}
			TB_Top.VSplitLeft(5.0f, &Button, &TB_Top);
		}
	}

	// Bottom line buttons
	{
		// refocus button
		{
			TB_Bottom.VSplitLeft(45.0f, &Button, &TB_Bottom);
			static int s_RefocusButton = 0;
			if(DoButton_Editor(&s_RefocusButton, "Refocus", m_WorldOffsetX && m_WorldOffsetY ? 0 : -1, &Button, 0, "[HOME] Restore map focus") || (m_EditBoxActive == 0 && Input()->KeyPress(KEY_HOME)))
			{
				m_WorldOffsetX = 0;
				m_WorldOffsetY = 0;
			}
			TB_Bottom.VSplitLeft(5.0f, 0, &TB_Bottom);
		}

		// tile manipulation
		{
			static int s_BorderBut = 0;
			CLayerTiles *pT = (CLayerTiles *)GetSelectedLayerType(0, LAYERTYPE_TILES);

			// no border for tele layer, speedup, front and switch
			if(pT && (pT->m_Tele || pT->m_Speedup || pT->m_Switch || pT->m_Front || pT->m_Tune))
				pT = 0;

			if(pT)
			{
				TB_Bottom.VSplitLeft(60.0f, &Button, &TB_Bottom);
				if(DoButton_Ex(&s_BorderBut, "Border", 0, &Button, 0, "Place tiles in a 2-tile wide border at the edges of the layer", CUI::CORNER_ALL))
				{
					m_PopupEventType = POPEVENT_PLACE_BORDER_TILES;
					m_PopupEventActivated = true;
				}
				TB_Bottom.VSplitLeft(5.0f, &Button, &TB_Bottom);
			}

			// do tele/tune/switch/speedup button
			{
				int (*pPopupFunc)(CEditor * peditor, CUIRect View, void *pContext) = NULL;
				const char *aButtonName = 0;
				float Height = 0.0f;
				CLayerTiles *pS = (CLayerTiles *)GetSelectedLayerType(0, LAYERTYPE_TILES);
				if(pS)
				{
					if(pS == m_Map.m_pSwitchLayer)
					{
						aButtonName = "Switch";
						pPopupFunc = PopupSwitch;
						Height = 36;
					}
					else if(pS == m_Map.m_pSpeedupLayer)
					{
						aButtonName = "Speedup";
						pPopupFunc = PopupSpeedup;
						Height = 53;
					}
					else if(pS == m_Map.m_pTuneLayer)
					{
						aButtonName = "Tune";
						pPopupFunc = PopupTune;
						Height = 23;
					}
					else if(pS == m_Map.m_pTeleLayer)
					{
						aButtonName = "Tele";
						pPopupFunc = PopupTele;
						Height = 23;
					}

					if(aButtonName != 0)
					{
						static char aBuf[64];
						str_format(aBuf, sizeof(aBuf), "[ctrl+a] %s", aButtonName);

						TB_Bottom.VSplitLeft(60.0f, &Button, &TB_Bottom);
						static int s_ModifierButton = 0;
						if(DoButton_Ex(&s_ModifierButton, aButtonName, 0, &Button, 0, aBuf, CUI::CORNER_ALL) || (CtrlPressed && Input()->KeyPress(KEY_A)))
						{
							static int s_ModifierPopupID = 0;
							if(!UiPopupExists(&s_ModifierPopupID))
							{
								UiInvokePopupMenu(&s_ModifierPopupID, 0, Button.x, Button.y + Button.h, 120, Height, pPopupFunc);
							}
						}
						TB_Bottom.VSplitLeft(5.0f, 0, &TB_Bottom);
					}
				}
			}
		}

		// do add quad/sound button
		CLayer *pLayer = GetSelectedLayer(0);
		if(pLayer && (pLayer->m_Type == LAYERTYPE_QUADS || pLayer->m_Type == LAYERTYPE_SOUNDS))
		{
			TB_Bottom.VSplitLeft(60.0f, &Button, &TB_Bottom);

			bool Invoked = false;
			static int s_AddItemButton = 0;

			if(pLayer->m_Type == LAYERTYPE_QUADS)
			{
				Invoked = DoButton_Editor(&s_AddItemButton, "Add Quad", 0, &Button, 0, "[ctrl+q] Add a new quad") ||
					  (Input()->KeyPress(KEY_Q) && CtrlPressed);
			}
			else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
			{
				Invoked = DoButton_Editor(&s_AddItemButton, "Add Sound", 0, &Button, 0, "[ctrl+q] Add a new sound source") ||
					  (Input()->KeyPress(KEY_Q) && CtrlPressed);
			}

			if(Invoked)
			{
				CLayerGroup *pGroup = GetSelectedGroup();

				float Mapping[4];
				pGroup->Mapping(Mapping);
				int x = Mapping[0] + (Mapping[2] - Mapping[0]) / 2;
				int y = Mapping[1] + (Mapping[3] - Mapping[1]) / 2;
				if(Input()->KeyPress(KEY_Q) && CtrlPressed)
				{
					x += UI()->MouseWorldX() - (m_WorldOffsetX * pGroup->m_ParallaxX / 100) - pGroup->m_OffsetX;
					y += UI()->MouseWorldY() - (m_WorldOffsetY * pGroup->m_ParallaxY / 100) - pGroup->m_OffsetY;
				}

				if(pLayer->m_Type == LAYERTYPE_QUADS)
				{
					CLayerQuads *pQuadLayer = (CLayerQuads *)pLayer;

					int Width = 64;
					int Height = 64;
					if(pQuadLayer->m_Image >= 0)
					{
						Width = m_Map.m_lImages[pQuadLayer->m_Image]->m_Width;
						Height = m_Map.m_lImages[pQuadLayer->m_Image]->m_Height;
					}

					pQuadLayer->NewQuad(x, y, Width, Height);
				}
				else if(pLayer->m_Type == LAYERTYPE_SOUNDS)
				{
					CLayerSounds *pSoundLayer = (CLayerSounds *)pLayer;
					pSoundLayer->NewSource(x, y);
				}
			}
			TB_Bottom.VSplitLeft(5.0f, &Button, &TB_Bottom);
		}

		// Brush draw mode button
		{
			TB_Bottom.VSplitLeft(65.0f, &Button, &TB_Bottom);
			static int s_BrushDrawModeButton = 0;
			if(DoButton_Editor(&s_BrushDrawModeButton, "Destructive", m_BrushDrawDestructive, &Button, 0, "[ctrl+d] Toggle brush draw mode") ||
				(Input()->KeyPress(KEY_D) && CtrlPressed && !ShiftPressed))
				m_BrushDrawDestructive = !m_BrushDrawDestructive;
			TB_Bottom.VSplitLeft(5.0f, &Button, &TB_Bottom);
		}
	}
}

static void Rotate(const CPoint *pCenter, CPoint *pPoint, float Rotation)
{
	int x = pPoint->x - pCenter->x;
	int y = pPoint->y - pCenter->y;
	pPoint->x = (int)(x * cosf(Rotation) - y * sinf(Rotation) + pCenter->x);
	pPoint->y = (int)(x * sinf(Rotation) + y * cosf(Rotation) + pCenter->y);
}

void CEditor::DoSoundSource(CSoundSource *pSource, int Index)
{
	enum
	{
		OP_NONE = 0,
		OP_MOVE,
		OP_CONTEXT_MENU,
	};

	void *pID = &pSource->m_Position;

	static int s_Operation = OP_NONE;

	float wx = UI()->MouseWorldX();
	float wy = UI()->MouseWorldY();

	float CenterX = fx2f(pSource->m_Position.x);
	float CenterY = fx2f(pSource->m_Position.y);

	float dx = (CenterX - wx) / m_WorldZoom;
	float dy = (CenterY - wy) / m_WorldZoom;
	if(dx * dx + dy * dy < 50)
		UI()->SetHotItem(pID);

	bool IgnoreGrid;
	if(Input()->KeyIsPressed(KEY_LALT) || Input()->KeyIsPressed(KEY_RALT))
		IgnoreGrid = true;
	else
		IgnoreGrid = false;

	if(UI()->ActiveItem() == pID)
	{
		if(m_MouseDeltaWx * m_MouseDeltaWx + m_MouseDeltaWy * m_MouseDeltaWy > 0.0f)
		{
			if(s_Operation == OP_MOVE)
			{
				if(m_GridActive && !IgnoreGrid)
				{
					int LineDistance = GetLineDistance();

					float x = 0.0f;
					float y = 0.0f;
					if(wx >= 0)
						x = (int)((wx + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						x = (int)((wx - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					if(wy >= 0)
						y = (int)((wy + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						y = (int)((wy - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);

					pSource->m_Position.x = f2fx(x);
					pSource->m_Position.y = f2fx(y);
				}
				else
				{
					pSource->m_Position.x = f2fx(wx);
					pSource->m_Position.y = f2fx(wy);
				}
			}
		}

		if(s_Operation == OP_CONTEXT_MENU)
		{
			if(!UI()->MouseButton(1))
			{
				if(m_lSelectedLayers.size() == 1)
				{
					m_Map.m_UndoModified++;

					static int s_SourcePopupID = 0;
					UiInvokePopupMenu(&s_SourcePopupID, 0, UI()->MouseX(), UI()->MouseY(), 120, 200, PopupSource);
					m_LockMouse = false;
				}
				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}
		else
		{
			if(!UI()->MouseButton(0))
			{
				if(s_Operation == OP_MOVE)
				{
					m_Map.m_UndoModified++;
				}

				m_LockMouse = false;
				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}

		Graphics()->SetColor(1, 1, 1, 1);
	}
	else if(UI()->HotItem() == pID)
	{
		ms_pUiGotContext = pID;

		Graphics()->SetColor(1, 1, 1, 1);
		m_pTooltip = "Left mouse button to move. Hold alt to ignore grid.";

		if(UI()->MouseButton(0))
		{
			s_Operation = OP_MOVE;

			UI()->SetActiveItem(pID);
			m_SelectedSource = Index;
		}

		if(UI()->MouseButton(1))
		{
			m_SelectedSource = Index;
			s_Operation = OP_CONTEXT_MENU;
			UI()->SetActiveItem(pID);
		}
	}
	else
	{
		Graphics()->SetColor(0, 1, 0, 1);
	}

	IGraphics::CQuadItem QuadItem(CenterX, CenterY, 5.0f * m_WorldZoom, 5.0f * m_WorldZoom);
	Graphics()->QuadsDraw(&QuadItem, 1);
}

void CEditor::DoQuad(CQuad *q, int Index)
{
	enum
	{
		OP_NONE = 0,
		OP_MOVE_ALL,
		OP_MOVE_PIVOT,
		OP_ROTATE,
		OP_CONTEXT_MENU,
		OP_DELETE,
	};

	// some basic values
	void *pID = &q->m_aPoints[4]; // use pivot addr as id
	static array<array<CPoint>> s_lRotatePoints;
	static int s_Operation = OP_NONE;
	static float s_RotateAngle = 0;
	float wx = UI()->MouseWorldX();
	float wy = UI()->MouseWorldY();

	// get pivot
	float CenterX = fx2f(q->m_aPoints[4].x);
	float CenterY = fx2f(q->m_aPoints[4].y);

	float dx = (CenterX - wx) / m_WorldZoom;
	float dy = (CenterY - wy) / m_WorldZoom;
	if(dx * dx + dy * dy < 50)
		UI()->SetHotItem(pID);

	bool IgnoreGrid;
	if(Input()->KeyIsPressed(KEY_LALT) || Input()->KeyIsPressed(KEY_RALT))
		IgnoreGrid = true;
	else
		IgnoreGrid = false;

	// draw selection background
	if(IsQuadSelected(Index))
	{
		Graphics()->SetColor(0, 0, 0, 1);
		IGraphics::CQuadItem QuadItem(CenterX, CenterY, 7.0f * m_WorldZoom, 7.0f * m_WorldZoom);
		Graphics()->QuadsDraw(&QuadItem, 1);
	}

	if(UI()->ActiveItem() == pID)
	{
		if(m_MouseDeltaWx * m_MouseDeltaWx + m_MouseDeltaWy * m_MouseDeltaWy > 0.0f)
		{
			// check if we only should move pivot
			if(s_Operation == OP_MOVE_PIVOT)
			{
				if(m_GridActive && !IgnoreGrid)
				{
					int LineDistance = GetLineDistance();

					float x = 0.0f;
					float y = 0.0f;
					if(wx >= 0)
						x = (int)((wx + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						x = (int)((wx - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					if(wy >= 0)
						y = (int)((wy + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						y = (int)((wy - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);

					q->m_aPoints[4].x = f2fx(x);
					q->m_aPoints[4].y = f2fx(y);
				}
				else
				{
					q->m_aPoints[4].x = f2fx(wx);
					q->m_aPoints[4].y = f2fx(wy);
				}
			}
			else if(s_Operation == OP_MOVE_ALL)
			{
				CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
				// move all points including pivot
				if(m_GridActive && !IgnoreGrid)
				{
					int LineDistance = GetLineDistance();

					float x = 0.0f;
					float y = 0.0f;
					if(wx >= 0)
						x = (int)((wx + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						x = (int)((wx - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					if(wy >= 0)
						y = (int)((wy + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						y = (int)((wy - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);

					int OffsetX = f2fx(x) - q->m_aPoints[4].x;
					int OffsetY = f2fx(y) - q->m_aPoints[4].y;

					for(int i = 0; i < m_lSelectedQuads.size(); ++i)
					{
						CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
						for(auto &Point : pCurrentQuad->m_aPoints)
						{
							Point.x += OffsetX;
							Point.y += OffsetY;
						}
					}
				}
				else
				{
					int OffsetX = f2fx(wx) - q->m_aPoints[4].x;
					int OffsetY = f2fx(wy) - q->m_aPoints[4].y;

					for(int i = 0; i < m_lSelectedQuads.size(); ++i)
					{
						CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
						for(auto &Point : pCurrentQuad->m_aPoints)
						{
							Point.x += OffsetX;
							Point.y += OffsetY;
						}
					}
				}
			}
			else if(s_Operation == OP_ROTATE)
			{
				CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
				for(int i = 0; i < m_lSelectedQuads.size(); ++i)
				{
					CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
					for(int v = 0; v < 4; v++)
					{
						pCurrentQuad->m_aPoints[v] = s_lRotatePoints[i][v];
						Rotate(&pCurrentQuad->m_aPoints[4], &pCurrentQuad->m_aPoints[v], s_RotateAngle);
					}
				}
			}
		}

		s_RotateAngle += (m_MouseDeltaX)*0.002f;

		if(s_Operation == OP_CONTEXT_MENU)
		{
			if(!UI()->MouseButton(1))
			{
				if(m_lSelectedLayers.size() == 1)
				{
					m_Map.m_UndoModified++;

					m_SelectedQuadIndex = FindSelectedQuadIndex(Index);

					static int s_QuadPopupID = 0;
					UiInvokePopupMenu(&s_QuadPopupID, 0, UI()->MouseX(), UI()->MouseY(), 120, 180, PopupQuad);
					m_LockMouse = false;
				}
				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}
		else if(s_Operation == OP_DELETE)
		{
			if(!UI()->MouseButton(1))
			{
				if(m_lSelectedLayers.size() == 1)
				{
					m_Map.m_UndoModified++;
					m_LockMouse = false;
					m_Map.m_Modified = true;
					DeleteSelectedQuads();
				}
				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}
		else
		{
			if(!UI()->MouseButton(0))
			{
				if(s_Operation == OP_ROTATE || s_Operation == OP_MOVE_ALL || s_Operation == OP_MOVE_PIVOT)
				{
					m_Map.m_UndoModified++;
				}

				m_LockMouse = false;
				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}

		Graphics()->SetColor(1, 1, 1, 1);
	}
	else if(UI()->HotItem() == pID)
	{
		ms_pUiGotContext = pID;

		Graphics()->SetColor(1, 1, 1, 1);
		m_pTooltip = "Left mouse button to move. Hold shift to move pivot. Hold ctrl to rotate. Hold alt to ignore grid. Hold shift and right click to delete.";

		if(UI()->MouseButton(0))
		{
			if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
			{
				s_Operation = OP_MOVE_PIVOT;

				SelectQuad(Index);
			}
			else if(Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL))
			{
				m_LockMouse = true;
				s_Operation = OP_ROTATE;
				s_RotateAngle = 0;

				if(!IsQuadSelected(Index))
					SelectQuad(Index);

				CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
				s_lRotatePoints.clear();
				s_lRotatePoints.set_size(m_lSelectedQuads.size());
				for(int i = 0; i < m_lSelectedQuads.size(); ++i)
				{
					CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];

					s_lRotatePoints[i].set_size(4);
					s_lRotatePoints[i][0] = pCurrentQuad->m_aPoints[0];
					s_lRotatePoints[i][1] = pCurrentQuad->m_aPoints[1];
					s_lRotatePoints[i][2] = pCurrentQuad->m_aPoints[2];
					s_lRotatePoints[i][3] = pCurrentQuad->m_aPoints[3];
				}
			}
			else
			{
				s_Operation = OP_MOVE_ALL;

				if(!IsQuadSelected(Index))
					SelectQuad(Index);
			}

			UI()->SetActiveItem(pID);
		}

		if(UI()->MouseButton(1))
		{
			if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
			{
				s_Operation = OP_DELETE;

				if(!IsQuadSelected(Index))
					SelectQuad(Index);

				UI()->SetActiveItem(pID);
			}
			else
			{
				s_Operation = OP_CONTEXT_MENU;

				if(!IsQuadSelected(Index))
					SelectQuad(Index);

				UI()->SetActiveItem(pID);
			}
		}
	}
	else
		Graphics()->SetColor(0, 1, 0, 1);

	IGraphics::CQuadItem QuadItem(CenterX, CenterY, 5.0f * m_WorldZoom, 5.0f * m_WorldZoom);
	Graphics()->QuadsDraw(&QuadItem, 1);
}

void CEditor::DoQuadPoint(CQuad *pQuad, int QuadIndex, int V)
{
	void *pID = &pQuad->m_aPoints[V];

	float wx = UI()->MouseWorldX();
	float wy = UI()->MouseWorldY();

	float px = fx2f(pQuad->m_aPoints[V].x);
	float py = fx2f(pQuad->m_aPoints[V].y);

	float dx = (px - wx) / m_WorldZoom;
	float dy = (py - wy) / m_WorldZoom;
	if(dx * dx + dy * dy < 50)
		UI()->SetHotItem(pID);

	// draw selection background
	if(IsQuadSelected(QuadIndex) && m_SelectedPoints & (1 << V))
	{
		Graphics()->SetColor(0, 0, 0, 1);
		IGraphics::CQuadItem QuadItem(px, py, 7.0f * m_WorldZoom, 7.0f * m_WorldZoom);
		Graphics()->QuadsDraw(&QuadItem, 1);
	}

	enum
	{
		OP_NONE = 0,
		OP_MOVEPOINT,
		OP_MOVEUV,
		OP_CONTEXT_MENU
	};

	static bool s_Moved;
	static int s_Operation = OP_NONE;

	bool IgnoreGrid;
	if(Input()->KeyIsPressed(KEY_LALT) || Input()->KeyIsPressed(KEY_RALT))
		IgnoreGrid = true;
	else
		IgnoreGrid = false;

	if(UI()->ActiveItem() == pID)
	{
		if(!s_Moved)
		{
			if(m_MouseDeltaWx * m_MouseDeltaWx + m_MouseDeltaWy * m_MouseDeltaWy > 0.0f)
				s_Moved = true;
		}

		if(s_Moved)
		{
			if(s_Operation == OP_MOVEPOINT)
			{
				CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
				if(m_GridActive && !IgnoreGrid)
				{
					int LineDistance = GetLineDistance();

					float x = 0.0f;
					float y = 0.0f;
					if(wx >= 0)
						x = (int)((wx + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						x = (int)((wx - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					if(wy >= 0)
						y = (int)((wy + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
					else
						y = (int)((wy - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);

					int OffsetX = f2fx(x) - pQuad->m_aPoints[V].x;
					int OffsetY = f2fx(y) - pQuad->m_aPoints[V].y;

					for(int i = 0; i < m_lSelectedQuads.size(); ++i)
					{
						CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
						for(int m = 0; m < 4; m++)
							if(m_SelectedPoints & (1 << m))
							{
								pCurrentQuad->m_aPoints[m].x += OffsetX;
								pCurrentQuad->m_aPoints[m].y += OffsetY;
							}
					}
				}
				else
				{
					int OffsetX = f2fx(wx) - pQuad->m_aPoints[V].x;
					int OffsetY = f2fx(wy) - pQuad->m_aPoints[V].y;

					for(int i = 0; i < m_lSelectedQuads.size(); ++i)
					{
						CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
						for(int m = 0; m < 4; m++)
							if(m_SelectedPoints & (1 << m))
							{
								pCurrentQuad->m_aPoints[m].x += OffsetX;
								pCurrentQuad->m_aPoints[m].y += OffsetY;
							}
					}
				}
			}
			else if(s_Operation == OP_MOVEUV)
			{
				CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
				for(int i = 0; i < m_lSelectedQuads.size(); ++i)
				{
					CQuad *pCurrentQuad = &pLayer->m_lQuads[m_lSelectedQuads[i]];
					for(int m = 0; m < 4; m++)
						if(m_SelectedPoints & (1 << m))
						{
							// 0,2;1,3 - line x
							// 0,1;2,3 - line y

							pCurrentQuad->m_aTexcoords[m].x += f2fx(m_MouseDeltaWx * 0.001f);
							pCurrentQuad->m_aTexcoords[(m + 2) % 4].x += f2fx(m_MouseDeltaWx * 0.001f);

							pCurrentQuad->m_aTexcoords[m].y += f2fx(m_MouseDeltaWy * 0.001f);
							pCurrentQuad->m_aTexcoords[m ^ 1].y += f2fx(m_MouseDeltaWy * 0.001f);
						}
				}
			}
		}

		if(s_Operation == OP_CONTEXT_MENU)
		{
			if(!UI()->MouseButton(1))
			{
				if(m_lSelectedLayers.size() == 1)
				{
					m_Map.m_UndoModified++;

					m_SelectedQuadPoint = V;
					m_SelectedQuadIndex = FindSelectedQuadIndex(QuadIndex);

					static int s_PointPopupID = 0;
					UiInvokePopupMenu(&s_PointPopupID, 0, UI()->MouseX(), UI()->MouseY(), 120, 150, PopupPoint);
				}
				UI()->SetActiveItem(0);
			}
		}
		else
		{
			if(!UI()->MouseButton(0))
			{
				if(!s_Moved)
				{
					if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
						m_SelectedPoints ^= 1 << V;
					else
						m_SelectedPoints = 1 << V;
				}

				if(s_Operation == OP_MOVEPOINT || s_Operation == OP_MOVEUV)
					m_Map.m_UndoModified++;

				m_LockMouse = false;
				UI()->SetActiveItem(0);
			}
		}

		Graphics()->SetColor(1, 1, 1, 1);
	}
	else if(UI()->HotItem() == pID)
	{
		ms_pUiGotContext = pID;

		Graphics()->SetColor(1, 1, 1, 1);
		m_pTooltip = "Left mouse button to move. Hold shift to move the texture. Hold alt to ignore grid.";

		if(UI()->MouseButton(0))
		{
			UI()->SetActiveItem(pID);
			s_Moved = false;
			if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
			{
				s_Operation = OP_MOVEUV;
				m_LockMouse = true;
			}
			else
				s_Operation = OP_MOVEPOINT;

			if(!(m_SelectedPoints & (1 << V)))
			{
				if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
					m_SelectedPoints |= 1 << V;
				else
					m_SelectedPoints = 1 << V;
				s_Moved = true;
			}

			if(!IsQuadSelected(QuadIndex))
				SelectQuad(QuadIndex);
		}
		else if(UI()->MouseButton(1))
		{
			s_Operation = OP_CONTEXT_MENU;

			if(!IsQuadSelected(QuadIndex))
				SelectQuad(QuadIndex);

			UI()->SetActiveItem(pID);
			if(!(m_SelectedPoints & (1 << V)))
			{
				if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
					m_SelectedPoints |= 1 << V;
				else
					m_SelectedPoints = 1 << V;
				s_Moved = true;
			}
		}
	}
	else
		Graphics()->SetColor(1, 0, 0, 1);

	IGraphics::CQuadItem QuadItem(px, py, 5.0f * m_WorldZoom, 5.0f * m_WorldZoom);
	Graphics()->QuadsDraw(&QuadItem, 1);
}

void CEditor::DoQuadEnvelopes(const array<CQuad> &lQuads, IGraphics::CTextureHandle Texture)
{
	int Num = lQuads.size();
	CEnvelope **apEnvelope = new CEnvelope *[Num];
	mem_zero(apEnvelope, sizeof(CEnvelope *) * Num); // NOLINT(bugprone-sizeof-expression)
	for(int i = 0; i < Num; i++)
	{
		if((m_ShowEnvelopePreview == 1 && lQuads[i].m_PosEnv == m_SelectedEnvelope) || m_ShowEnvelopePreview == 2)
			if(lQuads[i].m_PosEnv >= 0 && lQuads[i].m_PosEnv < m_Map.m_lEnvelopes.size())
				apEnvelope[i] = m_Map.m_lEnvelopes[lQuads[i].m_PosEnv];
	}

	//Draw Lines
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->SetColor(80.0f / 255, 150.0f / 255, 230.f / 255, 0.5f);
	for(int j = 0; j < Num; j++)
	{
		if(!apEnvelope[j])
			continue;

		//QuadParams
		const CPoint *pPoints = lQuads[j].m_aPoints;
		for(int i = 0; i < apEnvelope[j]->m_lPoints.size() - 1; i++)
		{
			float OffsetX = fx2f(apEnvelope[j]->m_lPoints[i].m_aValues[0]);
			float OffsetY = fx2f(apEnvelope[j]->m_lPoints[i].m_aValues[1]);
			vec2 Pos0 = vec2(fx2f(pPoints[4].x) + OffsetX, fx2f(pPoints[4].y) + OffsetY);

			OffsetX = fx2f(apEnvelope[j]->m_lPoints[i + 1].m_aValues[0]);
			OffsetY = fx2f(apEnvelope[j]->m_lPoints[i + 1].m_aValues[1]);
			vec2 Pos1 = vec2(fx2f(pPoints[4].x) + OffsetX, fx2f(pPoints[4].y) + OffsetY);

			IGraphics::CLineItem Line = IGraphics::CLineItem(Pos0.x, Pos0.y, Pos1.x, Pos1.y);
			Graphics()->LinesDraw(&Line, 1);
		}
	}
	Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	Graphics()->LinesEnd();

	//Draw Quads
	Graphics()->TextureSet(Texture);
	Graphics()->QuadsBegin();

	for(int j = 0; j < Num; j++)
	{
		if(!apEnvelope[j])
			continue;

		//QuadParams
		const CPoint *pPoints = lQuads[j].m_aPoints;

		for(int i = 0; i < apEnvelope[j]->m_lPoints.size(); i++)
		{
			//Calc Env Position
			float OffsetX = fx2f(apEnvelope[j]->m_lPoints[i].m_aValues[0]);
			float OffsetY = fx2f(apEnvelope[j]->m_lPoints[i].m_aValues[1]);
			float Rot = fx2f(apEnvelope[j]->m_lPoints[i].m_aValues[2]) / 360.0f * pi * 2;

			//Set Colours
			float Alpha = (m_SelectedQuadEnvelope == lQuads[j].m_PosEnv && m_SelectedEnvelopePoint == i) ? 0.65f : 0.35f;
			IGraphics::CColorVertex aArray[4] = {
				IGraphics::CColorVertex(0, lQuads[j].m_aColors[0].r, lQuads[j].m_aColors[0].g, lQuads[j].m_aColors[0].b, Alpha),
				IGraphics::CColorVertex(1, lQuads[j].m_aColors[1].r, lQuads[j].m_aColors[1].g, lQuads[j].m_aColors[1].b, Alpha),
				IGraphics::CColorVertex(2, lQuads[j].m_aColors[2].r, lQuads[j].m_aColors[2].g, lQuads[j].m_aColors[2].b, Alpha),
				IGraphics::CColorVertex(3, lQuads[j].m_aColors[3].r, lQuads[j].m_aColors[3].g, lQuads[j].m_aColors[3].b, Alpha)};
			Graphics()->SetColorVertex(aArray, 4);

			//Rotation
			if(Rot != 0)
			{
				static CPoint aRotated[4];
				aRotated[0] = lQuads[j].m_aPoints[0];
				aRotated[1] = lQuads[j].m_aPoints[1];
				aRotated[2] = lQuads[j].m_aPoints[2];
				aRotated[3] = lQuads[j].m_aPoints[3];
				pPoints = aRotated;

				Rotate(&lQuads[j].m_aPoints[4], &aRotated[0], Rot);
				Rotate(&lQuads[j].m_aPoints[4], &aRotated[1], Rot);
				Rotate(&lQuads[j].m_aPoints[4], &aRotated[2], Rot);
				Rotate(&lQuads[j].m_aPoints[4], &aRotated[3], Rot);
			}

			//Set Texture Coords
			Graphics()->QuadsSetSubsetFree(
				fx2f(lQuads[j].m_aTexcoords[0].x), fx2f(lQuads[j].m_aTexcoords[0].y),
				fx2f(lQuads[j].m_aTexcoords[1].x), fx2f(lQuads[j].m_aTexcoords[1].y),
				fx2f(lQuads[j].m_aTexcoords[2].x), fx2f(lQuads[j].m_aTexcoords[2].y),
				fx2f(lQuads[j].m_aTexcoords[3].x), fx2f(lQuads[j].m_aTexcoords[3].y));

			//Set Quad Coords & Draw
			IGraphics::CFreeformItem Freeform(
				fx2f(pPoints[0].x) + OffsetX, fx2f(pPoints[0].y) + OffsetY,
				fx2f(pPoints[1].x) + OffsetX, fx2f(pPoints[1].y) + OffsetY,
				fx2f(pPoints[2].x) + OffsetX, fx2f(pPoints[2].y) + OffsetY,
				fx2f(pPoints[3].x) + OffsetX, fx2f(pPoints[3].y) + OffsetY);
			Graphics()->QuadsDrawFreeform(&Freeform, 1);
		}
	}
	Graphics()->QuadsEnd();
	Graphics()->TextureClear();
	Graphics()->QuadsBegin();

	// Draw QuadPoints
	for(int j = 0; j < Num; j++)
	{
		if(!apEnvelope[j])
			continue;

		//QuadParams
		for(int i = 0; i < apEnvelope[j]->m_lPoints.size() - 1; i++)
			DoQuadEnvPoint(&lQuads[j], j, i);
	}
	Graphics()->QuadsEnd();
	delete[] apEnvelope;
}

void CEditor::DoQuadEnvPoint(const CQuad *pQuad, int QIndex, int PIndex)
{
	enum
	{
		OP_NONE = 0,
		OP_MOVE,
		OP_ROTATE,
	};

	// some basic values
	static int s_Operation = OP_NONE;
	float wx = UI()->MouseWorldX();
	float wy = UI()->MouseWorldY();
	CEnvelope *pEnvelope = m_Map.m_lEnvelopes[pQuad->m_PosEnv];
	void *pID = &pEnvelope->m_lPoints[PIndex];
	static int s_ActQIndex = -1;

	// get pivot
	float CenterX = fx2f(pQuad->m_aPoints[4].x) + fx2f(pEnvelope->m_lPoints[PIndex].m_aValues[0]);
	float CenterY = fx2f(pQuad->m_aPoints[4].y) + fx2f(pEnvelope->m_lPoints[PIndex].m_aValues[1]);

	float dx = (CenterX - wx) / m_WorldZoom;
	float dy = (CenterY - wy) / m_WorldZoom;
	if(dx * dx + dy * dy < 50.0f && UI()->ActiveItem() == 0)
	{
		UI()->SetHotItem(pID);
		s_ActQIndex = QIndex;
	}

	bool IgnoreGrid;
	if(Input()->KeyIsPressed(KEY_LALT) || Input()->KeyIsPressed(KEY_RALT))
		IgnoreGrid = true;
	else
		IgnoreGrid = false;

	if(UI()->ActiveItem() == pID && s_ActQIndex == QIndex)
	{
		if(s_Operation == OP_MOVE)
		{
			if(m_GridActive && !IgnoreGrid)
			{
				int LineDistance = GetLineDistance();

				float x = 0.0f;
				float y = 0.0f;
				if(wx >= 0)
					x = (int)((wx + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
				else
					x = (int)((wx - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
				if(wy >= 0)
					y = (int)((wy + (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);
				else
					y = (int)((wy - (LineDistance / 2) * m_GridFactor) / (LineDistance * m_GridFactor)) * (LineDistance * m_GridFactor);

				pEnvelope->m_lPoints[PIndex].m_aValues[0] = f2fx(x) - pQuad->m_aPoints[4].x;
				pEnvelope->m_lPoints[PIndex].m_aValues[1] = f2fx(y) - pQuad->m_aPoints[4].y;
			}
			else
			{
				pEnvelope->m_lPoints[PIndex].m_aValues[0] = f2fx(wx);
				pEnvelope->m_lPoints[PIndex].m_aValues[1] = f2fx(wy);
			}
		}
		else if(s_Operation == OP_ROTATE)
			pEnvelope->m_lPoints[PIndex].m_aValues[2] += 10 * m_MouseDeltaX;

		if(!UI()->MouseButton(0))
		{
			m_LockMouse = false;
			s_Operation = OP_NONE;
			UI()->SetActiveItem(0);
		}

		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
	}
	else if(UI()->HotItem() == pID && s_ActQIndex == QIndex)
	{
		ms_pUiGotContext = pID;

		Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
		m_pTooltip = "Left mouse button to move. Hold ctrl to rotate. Hold alt to ignore grid.";

		if(UI()->MouseButton(0))
		{
			if(Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL))
			{
				m_LockMouse = true;
				s_Operation = OP_ROTATE;

				SelectQuad(QIndex);
			}
			else
			{
				s_Operation = OP_MOVE;

				SelectQuad(QIndex);
			}

			m_SelectedEnvelopePoint = PIndex;
			m_SelectedQuadEnvelope = pQuad->m_PosEnv;

			UI()->SetActiveItem(pID);
		}
		else
		{
			m_SelectedEnvelopePoint = -1;
			m_SelectedQuadEnvelope = -1;
		}
	}
	else
		Graphics()->SetColor(0.0f, 1.0f, 0.0f, 1.0f);

	IGraphics::CQuadItem QuadItem(CenterX, CenterY, 5.0f * m_WorldZoom, 5.0f * m_WorldZoom);
	Graphics()->QuadsDraw(&QuadItem, 1);
}

void CEditor::DoMapEditor(CUIRect View)
{
	// render all good stuff
	if(!m_ShowPicker)
	{
		for(int g = 0; g < m_Map.m_lGroups.size(); g++)
		{ // don't render the front, tele, speedup and switch layer now we will do it later to make them on top of others
			if(
				m_Map.m_lGroups[g] == (CLayerGroup *)m_Map.m_pFrontLayer ||
				m_Map.m_lGroups[g] == (CLayerGroup *)m_Map.m_pTeleLayer ||
				m_Map.m_lGroups[g] == (CLayerGroup *)m_Map.m_pSpeedupLayer ||
				m_Map.m_lGroups[g] == (CLayerGroup *)m_Map.m_pSwitchLayer ||
				m_Map.m_lGroups[g] == (CLayerGroup *)m_Map.m_pTuneLayer)
				continue;
			if(m_Map.m_lGroups[g]->m_Visible)
				m_Map.m_lGroups[g]->Render();
			//UI()->ClipEnable(&view);
		}

		// render the game, tele, speedup, front, tune and switch above everything else
		if(m_Map.m_pGameGroup->m_Visible)
		{
			m_Map.m_pGameGroup->MapScreen();
			for(int i = 0; i < m_Map.m_pGameGroup->m_lLayers.size(); i++)
			{
				if(
					m_Map.m_pGameGroup->m_lLayers[i]->m_Visible &&
					(m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pGameLayer ||
						m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pFrontLayer ||
						m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pTeleLayer ||
						m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pSpeedupLayer ||
						m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pSwitchLayer ||
						m_Map.m_pGameGroup->m_lLayers[i] == m_Map.m_pTuneLayer))
					m_Map.m_pGameGroup->m_lLayers[i]->Render();
			}
		}

		CLayerTiles *pT = static_cast<CLayerTiles *>(GetSelectedLayerType(0, LAYERTYPE_TILES));
		if(m_ShowTileInfo && pT && pT->m_Visible && m_ZoomLevel <= 300)
		{
			GetSelectedGroup()->MapScreen();
			pT->ShowInfo();
		}
	}
	else
	{
		// fix aspect ratio of the image in the picker
		float Max = minimum(View.w, View.h);
		View.w = View.h = Max;
	}

	static void *s_pEditorID = (void *)&s_pEditorID;
	int Inside = UI()->MouseInside(&View);

	// fetch mouse position
	float wx = UI()->MouseWorldX();
	float wy = UI()->MouseWorldY();
	float mx = UI()->MouseX();
	float my = UI()->MouseY();

	static float s_StartWx = 0;
	static float s_StartWy = 0;

	enum
	{
		OP_NONE = 0,
		OP_BRUSH_GRAB,
		OP_BRUSH_DRAW,
		OP_BRUSH_PAINT,
		OP_PAN_WORLD,
		OP_PAN_EDITOR,
	};

	// remap the screen so it can display the whole tileset
	if(m_ShowPicker)
	{
		CUIRect Screen = *UI()->Screen();
		float Size = 32.0 * 16.0f;
		float w = Size * (Screen.w / View.w);
		float h = Size * (Screen.h / View.h);
		float x = -(View.x / Screen.w) * w;
		float y = -(View.y / Screen.h) * h;
		wx = x + w * mx / Screen.w;
		wy = y + h * my / Screen.h;
		CLayerTiles *t = (CLayerTiles *)GetSelectedLayerType(0, LAYERTYPE_TILES);
		if(t)
		{
			Graphics()->MapScreen(x, y, x + w, y + h);
			m_TilesetPicker.m_Image = t->m_Image;
			m_TilesetPicker.m_Texture = t->m_Texture;
			if(m_BrushColorEnabled)
			{
				m_TilesetPicker.m_Color = t->m_Color;
				m_TilesetPicker.m_Color.a = 255;
			}
			else
			{
				m_TilesetPicker.m_Color = {255, 255, 255, 255};
			}

			m_TilesetPicker.m_Game = t->m_Game;
			m_TilesetPicker.m_Tele = t->m_Tele;
			m_TilesetPicker.m_Speedup = t->m_Speedup;
			m_TilesetPicker.m_Front = t->m_Front;
			m_TilesetPicker.m_Switch = t->m_Switch;
			m_TilesetPicker.m_Tune = t->m_Tune;

			m_TilesetPicker.Render(true);

			if(m_ShowTileInfo)
				m_TilesetPicker.ShowInfo();
		}
		else
		{
			CLayerQuads *t = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
			if(t)
			{
				m_QuadsetPicker.m_Image = t->m_Image;
				m_QuadsetPicker.m_lQuads[0].m_aPoints[0].x = f2fx(View.x);
				m_QuadsetPicker.m_lQuads[0].m_aPoints[0].y = f2fx(View.y);
				m_QuadsetPicker.m_lQuads[0].m_aPoints[1].x = f2fx((View.x + View.w));
				m_QuadsetPicker.m_lQuads[0].m_aPoints[1].y = f2fx(View.y);
				m_QuadsetPicker.m_lQuads[0].m_aPoints[2].x = f2fx(View.x);
				m_QuadsetPicker.m_lQuads[0].m_aPoints[2].y = f2fx((View.y + View.h));
				m_QuadsetPicker.m_lQuads[0].m_aPoints[3].x = f2fx((View.x + View.w));
				m_QuadsetPicker.m_lQuads[0].m_aPoints[3].y = f2fx((View.y + View.h));
				m_QuadsetPicker.m_lQuads[0].m_aPoints[4].x = f2fx((View.x + View.w / 2));
				m_QuadsetPicker.m_lQuads[0].m_aPoints[4].y = f2fx((View.y + View.h / 2));
				m_QuadsetPicker.Render();
			}
		}
	}

	static int s_Operation = OP_NONE;

	// draw layer borders
	CLayer *pEditLayers[128];
	int NumEditLayers = 0;

	if(m_ShowPicker && GetSelectedLayer(0) && GetSelectedLayer(0)->m_Type == LAYERTYPE_TILES)
	{
		pEditLayers[0] = &m_TilesetPicker;
		NumEditLayers++;
	}
	else if(m_ShowPicker)
	{
		pEditLayers[0] = &m_QuadsetPicker;
		NumEditLayers++;
	}
	else
	{
		// pick a type of layers to edit, prefering Tiles layers.
		int EditingType = -1;
		for(int i = 0; i < m_lSelectedLayers.size(); i++)
		{
			CLayer *Layer = GetSelectedLayer(i);
			if(Layer && (EditingType == -1 || Layer->m_Type == LAYERTYPE_TILES))
			{
				EditingType = Layer->m_Type;
				if(EditingType == LAYERTYPE_TILES)
					break;
			}
		}
		for(int i = 0; i < m_lSelectedLayers.size() && NumEditLayers < 128; i++)
		{
			pEditLayers[NumEditLayers] = GetSelectedLayerType(i, EditingType);
			if(pEditLayers[NumEditLayers])
			{
				NumEditLayers++;
			}
		}

		CLayerGroup *g = GetSelectedGroup();
		if(g)
		{
			g->MapScreen();

			RenderGrid(g);

			for(int i = 0; i < NumEditLayers; i++)
			{
				if(pEditLayers[i]->m_Type != LAYERTYPE_TILES)
					continue;

				float w, h;
				pEditLayers[i]->GetSize(&w, &h);

				IGraphics::CLineItem Array[4] = {
					IGraphics::CLineItem(0, 0, w, 0),
					IGraphics::CLineItem(w, 0, w, h),
					IGraphics::CLineItem(w, h, 0, h),
					IGraphics::CLineItem(0, h, 0, 0)};
				Graphics()->TextureClear();
				Graphics()->LinesBegin();
				Graphics()->LinesDraw(Array, 4);
				Graphics()->LinesEnd();
			}
		}
	}

	if(Inside)
	{
		UI()->SetHotItem(s_pEditorID);

		// do global operations like pan and zoom
		if(UI()->ActiveItem() == 0 && (UI()->MouseButton(0) || UI()->MouseButton(2)))
		{
			s_StartWx = wx;
			s_StartWy = wy;

			if(Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL) || UI()->MouseButton(2))
			{
				if(Input()->KeyIsPressed(KEY_LSHIFT))
					s_Operation = OP_PAN_EDITOR;
				else
					s_Operation = OP_PAN_WORLD;
				UI()->SetActiveItem(s_pEditorID);
			}
			else
				s_Operation = OP_NONE;
		}

		// brush editing
		if(UI()->HotItem() == s_pEditorID)
		{
			int Layer = NUM_LAYERS;
			if(m_ShowPicker)
			{
				CLayer *pLayer = GetSelectedLayer(0);
				if(pLayer == m_Map.m_pGameLayer)
					Layer = LAYER_GAME;
				else if(pLayer == m_Map.m_pFrontLayer)
					Layer = LAYER_FRONT;
				else if(pLayer == m_Map.m_pSwitchLayer)
					Layer = LAYER_SWITCH;
				else if(pLayer == m_Map.m_pTeleLayer)
					Layer = LAYER_TELE;
				else if(pLayer == m_Map.m_pSpeedupLayer)
					Layer = LAYER_SPEEDUP;
				else if(pLayer == m_Map.m_pTuneLayer)
					Layer = LAYER_TUNE;
			}
			if(m_ShowPicker && Layer != NUM_LAYERS)
			{
				if(m_SelectEntitiesImage == "DDNet")
					m_pTooltip = Explain(EXPLANATION_DDNET, (int)wx / 32 + (int)wy / 32 * 16, Layer);
				else if(m_SelectEntitiesImage == "FNG")
					m_pTooltip = Explain(EXPLANATION_FNG, (int)wx / 32 + (int)wy / 32 * 16, Layer);
				else if(m_SelectEntitiesImage == "Vanilla")
					m_pTooltip = Explain(EXPLANATION_VANILLA, (int)wx / 32 + (int)wy / 32 * 16, Layer);
			}
			else if(m_Brush.IsEmpty())
				m_pTooltip = "Use left mouse button to drag and create a brush. Hold shift to select multiple quads.";
			else
				m_pTooltip = "Use left mouse button to paint with the brush. Right button clears the brush.";

			if(UI()->ActiveItem() == s_pEditorID)
			{
				CUIRect r;
				r.x = s_StartWx;
				r.y = s_StartWy;
				r.w = wx - s_StartWx;
				r.h = wy - s_StartWy;
				if(r.w < 0)
				{
					r.x += r.w;
					r.w = -r.w;
				}

				if(r.h < 0)
				{
					r.y += r.h;
					r.h = -r.h;
				}

				if(s_Operation == OP_BRUSH_DRAW)
				{
					if(!m_Brush.IsEmpty())
					{
						// draw with brush
						for(int k = 0; k < NumEditLayers; k++)
						{
							int BrushIndex = k % m_Brush.m_lLayers.size();
							if(pEditLayers[k]->m_Type == m_Brush.m_lLayers[BrushIndex]->m_Type)
							{
								if(pEditLayers[k]->m_Type == LAYERTYPE_TILES)
								{
									CLayerTiles *l = (CLayerTiles *)pEditLayers[k];
									CLayerTiles *b = (CLayerTiles *)m_Brush.m_lLayers[BrushIndex];

									if(l->m_Tele <= b->m_Tele && l->m_Speedup <= b->m_Speedup && l->m_Front <= b->m_Front && l->m_Game <= b->m_Game && l->m_Switch <= b->m_Switch && l->m_Tune <= b->m_Tune)
										l->BrushDraw(b, wx, wy);
								}
								else
								{
									pEditLayers[k]->BrushDraw(m_Brush.m_lLayers[BrushIndex], wx, wy);
								}
							}
						}
					}
				}
				else if(s_Operation == OP_BRUSH_GRAB)
				{
					if(!UI()->MouseButton(0))
					{
						if(Input()->KeyIsPressed(KEY_LSHIFT))
						{
							CLayerQuads *t = (CLayerQuads *)GetSelectedLayerType(0, LAYERTYPE_QUADS);
							if(t)
							{
								for(int i = 0; i < t->m_lQuads.size(); i++)
								{
									CQuad *q = &t->m_lQuads[i];
									float px = fx2f(q->m_aPoints[4].x);
									float py = fx2f(q->m_aPoints[4].y);

									if(px > r.x && px < r.x + r.w && py > r.y && py < r.y + r.h)
										if(!IsQuadSelected(i))
											m_lSelectedQuads.add(i);
								}
							}
						}
						else
						{
							// grab brush
							char aBuf[256];
							str_format(aBuf, sizeof(aBuf), "grabbing %f %f %f %f", r.x, r.y, r.w, r.h);
							Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "editor", aBuf);

							// TODO: do all layers
							int Grabs = 0;
							for(int k = 0; k < NumEditLayers; k++)
								Grabs += pEditLayers[k]->BrushGrab(&m_Brush, r);
							if(Grabs == 0)
								m_Brush.Clear();

							for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
								m_Brush.m_lLayers[i]->m_BrushRefCount = 1;

							m_lSelectedQuads.clear();
							m_SelectedPoints = 0;
						}
					}
					else
					{
						//editor.map.groups[selected_group]->mapscreen();
						for(int k = 0; k < NumEditLayers; k++)
							pEditLayers[k]->BrushSelecting(r);
						UI()->MapScreen();
					}
				}
				else if(s_Operation == OP_BRUSH_PAINT)
				{
					if(!UI()->MouseButton(0))
					{
						for(int k = 0; k < NumEditLayers; k++)
						{
							int BrushIndex = k;
							if(m_Brush.m_lLayers.size() != NumEditLayers)
								BrushIndex = 0;
							pEditLayers[k]->FillSelection(m_Brush.IsEmpty(), m_Brush.m_lLayers[BrushIndex], r);
						}
					}
					else
					{
						//editor.map.groups[selected_group]->mapscreen();
						for(int k = 0; k < NumEditLayers; k++)
							pEditLayers[k]->BrushSelecting(r);
						UI()->MapScreen();
					}
				}
			}
			else
			{
				if(UI()->MouseButton(1))
				{
					for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					{
						if(m_Brush.m_lLayers[i]->m_BrushRefCount == 1)
							delete m_Brush.m_lLayers[i];
					}
					m_Brush.Clear();
				}

				if(UI()->MouseButton(0) && s_Operation == OP_NONE)
				{
					UI()->SetActiveItem(s_pEditorID);

					if(m_Brush.IsEmpty())
						s_Operation = OP_BRUSH_GRAB;
					else
					{
						s_Operation = OP_BRUSH_DRAW;
						for(int k = 0; k < NumEditLayers; k++)
						{
							int BrushIndex = k;
							if(m_Brush.m_lLayers.size() != NumEditLayers)
								BrushIndex = 0;
							if(pEditLayers[k]->m_Type == m_Brush.m_lLayers[BrushIndex]->m_Type)
								pEditLayers[k]->BrushPlace(m_Brush.m_lLayers[BrushIndex], wx, wy);
						}
					}

					CLayerTiles *pLayer = (CLayerTiles *)GetSelectedLayerType(0, LAYERTYPE_TILES);
					if((Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT)) && pLayer)
						s_Operation = OP_BRUSH_PAINT;
				}

				if(!m_Brush.IsEmpty())
				{
					m_Brush.m_OffsetX = -(int)wx;
					m_Brush.m_OffsetY = -(int)wy;
					for(int i = 0; i < m_Brush.m_lLayers.size(); i++)
					{
						if(m_Brush.m_lLayers[i]->m_Type == LAYERTYPE_TILES)
						{
							m_Brush.m_OffsetX = -(int)(wx / 32.0f) * 32;
							m_Brush.m_OffsetY = -(int)(wy / 32.0f) * 32;
							break;
						}
					}

					CLayerGroup *g = GetSelectedGroup();
					if(!m_ShowPicker && g)
					{
						m_Brush.m_OffsetX += g->m_OffsetX;
						m_Brush.m_OffsetY += g->m_OffsetY;
						m_Brush.m_ParallaxX = g->m_ParallaxX;
						m_Brush.m_ParallaxY = g->m_ParallaxY;
						m_Brush.Render();
						float w, h;
						m_Brush.GetSize(&w, &h);

						IGraphics::CLineItem Array[4] = {
							IGraphics::CLineItem(0, 0, w, 0),
							IGraphics::CLineItem(w, 0, w, h),
							IGraphics::CLineItem(w, h, 0, h),
							IGraphics::CLineItem(0, h, 0, 0)};
						Graphics()->TextureClear();
						Graphics()->LinesBegin();
						Graphics()->LinesDraw(Array, 4);
						Graphics()->LinesEnd();
					}
				}
			}
		}

		// quad & sound editing
		{
			if(!m_ShowPicker && m_Brush.IsEmpty())
			{
				// fetch layers
				CLayerGroup *g = GetSelectedGroup();
				if(g)
					g->MapScreen();

				for(int k = 0; k < NumEditLayers; k++)
				{
					if(pEditLayers[k]->m_Type == LAYERTYPE_QUADS)
					{
						CLayerQuads *pLayer = (CLayerQuads *)pEditLayers[k];

						if(!m_ShowEnvelopePreview)
							m_ShowEnvelopePreview = 2;

						Graphics()->TextureClear();
						Graphics()->QuadsBegin();
						for(int i = 0; i < pLayer->m_lQuads.size(); i++)
						{
							for(int v = 0; v < 4; v++)
								DoQuadPoint(&pLayer->m_lQuads[i], i, v);

							DoQuad(&pLayer->m_lQuads[i], i);
						}
						Graphics()->QuadsEnd();
					}

					if(pEditLayers[k]->m_Type == LAYERTYPE_SOUNDS)
					{
						CLayerSounds *pLayer = (CLayerSounds *)pEditLayers[k];

						Graphics()->TextureClear();
						Graphics()->QuadsBegin();
						for(int i = 0; i < pLayer->m_lSources.size(); i++)
						{
							DoSoundSource(&pLayer->m_lSources[i], i);
						}
						Graphics()->QuadsEnd();
					}
				}

				UI()->MapScreen();
			}
		}

		// do panning
		if(UI()->ActiveItem() == s_pEditorID)
		{
			if(s_Operation == OP_PAN_WORLD)
			{
				m_WorldOffsetX -= m_MouseDeltaX * m_WorldZoom;
				m_WorldOffsetY -= m_MouseDeltaY * m_WorldZoom;
			}
			else if(s_Operation == OP_PAN_EDITOR)
			{
				m_EditorOffsetX -= m_MouseDeltaX * m_WorldZoom;
				m_EditorOffsetY -= m_MouseDeltaY * m_WorldZoom;
			}

			// release mouse
			if(!UI()->MouseButton(0))
			{
				if(s_Operation == OP_BRUSH_DRAW || s_Operation == OP_BRUSH_PAINT)
					m_Map.m_UndoModified++;

				s_Operation = OP_NONE;
				UI()->SetActiveItem(0);
			}
		}
	}
	else if(UI()->ActiveItem() == s_pEditorID)
	{
		// release mouse
		if(!UI()->MouseButton(0))
		{
			s_Operation = OP_NONE;
			UI()->SetActiveItem(0);
		}
	}

	if(!m_ShowPicker && GetSelectedGroup() && GetSelectedGroup()->m_UseClipping)
	{
		CLayerGroup *g = m_Map.m_pGameGroup;
		g->MapScreen();

		Graphics()->TextureClear();
		Graphics()->LinesBegin();

		CUIRect r;
		r.x = GetSelectedGroup()->m_ClipX;
		r.y = GetSelectedGroup()->m_ClipY;
		r.w = GetSelectedGroup()->m_ClipW;
		r.h = GetSelectedGroup()->m_ClipH;

		IGraphics::CLineItem Array[4] = {
			IGraphics::CLineItem(r.x, r.y, r.x + r.w, r.y),
			IGraphics::CLineItem(r.x + r.w, r.y, r.x + r.w, r.y + r.h),
			IGraphics::CLineItem(r.x + r.w, r.y + r.h, r.x, r.y + r.h),
			IGraphics::CLineItem(r.x, r.y + r.h, r.x, r.y)};
		Graphics()->SetColor(1, 0, 0, 1);
		Graphics()->LinesDraw(Array, 4);

		Graphics()->LinesEnd();
	}

	// render screen sizes
	if(m_ProofBorders && !m_ShowPicker)
	{
		CLayerGroup *g = m_Map.m_pGameGroup;
		g->MapScreen();

		Graphics()->TextureClear();
		Graphics()->LinesBegin();

		// possible screen sizes (white border)
		float aLastPoints[4];
		float Start = 1.0f; //9.0f/16.0f;
		float End = 16.0f / 9.0f;
		const int NumSteps = 20;
		for(int i = 0; i <= NumSteps; i++)
		{
			float aPoints[4];
			float Aspect = Start + (End - Start) * (i / (float)NumSteps);

			RenderTools()->MapscreenToWorld(
				m_WorldOffsetX, m_WorldOffsetY,
				100.0f, 100.0f, 0.0f, 0.0f, Aspect, 1.0f, aPoints);

			if(i == 0)
			{
				IGraphics::CLineItem Array[2] = {
					IGraphics::CLineItem(aPoints[0], aPoints[1], aPoints[2], aPoints[1]),
					IGraphics::CLineItem(aPoints[0], aPoints[3], aPoints[2], aPoints[3])};
				Graphics()->LinesDraw(Array, 2);
			}

			if(i != 0)
			{
				IGraphics::CLineItem Array[4] = {
					IGraphics::CLineItem(aPoints[0], aPoints[1], aLastPoints[0], aLastPoints[1]),
					IGraphics::CLineItem(aPoints[2], aPoints[1], aLastPoints[2], aLastPoints[1]),
					IGraphics::CLineItem(aPoints[0], aPoints[3], aLastPoints[0], aLastPoints[3]),
					IGraphics::CLineItem(aPoints[2], aPoints[3], aLastPoints[2], aLastPoints[3])};
				Graphics()->LinesDraw(Array, 4);
			}

			if(i == NumSteps)
			{
				IGraphics::CLineItem Array[2] = {
					IGraphics::CLineItem(aPoints[0], aPoints[1], aPoints[0], aPoints[3]),
					IGraphics::CLineItem(aPoints[2], aPoints[1], aPoints[2], aPoints[3])};
				Graphics()->LinesDraw(Array, 2);
			}

			mem_copy(aLastPoints, aPoints, sizeof(aPoints));
		}

		// two screen sizes (green and red border)
		{
			Graphics()->SetColor(1, 0, 0, 1);
			for(int i = 0; i < 2; i++)
			{
				float aPoints[4];
				float aAspects[] = {4.0f / 3.0f, 16.0f / 10.0f, 5.0f / 4.0f, 16.0f / 9.0f};
				float Aspect = aAspects[i];

				RenderTools()->MapscreenToWorld(
					m_WorldOffsetX, m_WorldOffsetY,
					100.0f, 100.0f, 0.0f, 0.0f, Aspect, 1.0f, aPoints);

				CUIRect r;
				r.x = aPoints[0];
				r.y = aPoints[1];
				r.w = aPoints[2] - aPoints[0];
				r.h = aPoints[3] - aPoints[1];

				IGraphics::CLineItem Array[4] = {
					IGraphics::CLineItem(r.x, r.y, r.x + r.w, r.y),
					IGraphics::CLineItem(r.x + r.w, r.y, r.x + r.w, r.y + r.h),
					IGraphics::CLineItem(r.x + r.w, r.y + r.h, r.x, r.y + r.h),
					IGraphics::CLineItem(r.x, r.y + r.h, r.x, r.y)};
				Graphics()->LinesDraw(Array, 4);
				Graphics()->SetColor(0, 1, 0, 1);
			}
		}
		Graphics()->LinesEnd();

		// tee position (blue circle)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			Graphics()->SetColor(0, 0, 1, 0.3f);
			RenderTools()->DrawCircle(m_WorldOffsetX, m_WorldOffsetY - 3.0f, 20.0f, 32);
			Graphics()->QuadsEnd();
		}
	}

	if(!m_ShowPicker && m_ShowTileInfo && m_ShowEnvelopePreview != 0 && GetSelectedLayer(0) && GetSelectedLayer(0)->m_Type == LAYERTYPE_QUADS)
	{
		GetSelectedGroup()->MapScreen();

		CLayerQuads *pLayer = (CLayerQuads *)GetSelectedLayer(0);
		IGraphics::CTextureHandle Texture;
		if(pLayer->m_Image >= 0 && pLayer->m_Image < m_Map.m_lImages.size())
			Texture = m_Map.m_lImages[pLayer->m_Image]->m_Texture;

		DoQuadEnvelopes(pLayer->m_lQuads, Texture);
		m_ShowEnvelopePreview = 0;
	}

	UI()->MapScreen();
	//UI()->ClipDisable();
}

float CEditor::ScaleFontSize(char *pText, int TextSize, float FontSize, int Width)
{
	while(TextRender()->TextWidth(0, FontSize, pText, -1, -1.0f) > Width)
	{
		if(FontSize > 6.0f)
		{
			FontSize--;
		}
		else
		{
			pText[str_length(pText) - 4] = '\0';
			str_append(pText, "…", TextSize);
		}
	}
	return FontSize;
}

int CEditor::DoProperties(CUIRect *pToolBox, CProperty *pProps, int *pIDs, int *pNewVal, ColorRGBA Color)
{
	int Change = -1;

	for(int i = 0; pProps[i].m_pName; i++)
	{
		CUIRect Slot;
		pToolBox->HSplitTop(13.0f, &Slot, pToolBox);
		CUIRect Label, Shifter;
		Slot.VSplitMid(&Label, &Shifter);
		Shifter.HMargin(1.0f, &Shifter);
		UI()->DoLabel(&Label, pProps[i].m_pName, 10.0f, -1, -1);

		if(pProps[i].m_Type == PROPTYPE_INT_STEP)
		{
			CUIRect Inc, Dec;
			char aBuf[64];

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);
			str_format(aBuf, sizeof(aBuf), "%d", pProps[i].m_Value);
			int NewValue = UiDoValueSelector((char *)&pIDs[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, 1, 1.0f, "Use left mouse button to drag and change the value. Hold shift to be more precise. Rightclick to edit as text.", false, false, 0, &Color);
			if(NewValue != pProps[i].m_Value)
			{
				*pNewVal = NewValue;
				Change = i;
			}
			if(DoButton_ButtonDec((char *)&pIDs[i] + 1, 0, 0, &Dec, 0, "Decrease"))
			{
				*pNewVal = pProps[i].m_Value - 1;
				Change = i;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, 0, 0, &Inc, 0, "Increase"))
			{
				*pNewVal = pProps[i].m_Value + 1;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_BOOL)
		{
			CUIRect No, Yes;
			Shifter.VSplitMid(&No, &Yes);
			if(DoButton_ButtonDec(&pIDs[i], "No", !pProps[i].m_Value, &No, 0, ""))
			{
				*pNewVal = 0;
				Change = i;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 1, "Yes", pProps[i].m_Value, &Yes, 0, ""))
			{
				*pNewVal = 1;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_INT_SCROLL)
		{
			int NewValue = UiDoValueSelector(&pIDs[i], &Shifter, "", pProps[i].m_Value, pProps[i].m_Min, pProps[i].m_Max, 1, 1.0f, "Use left mouse button to drag and change the value. Hold shift to be more precise. Rightclick to edit as text.");
			if(NewValue != pProps[i].m_Value)
			{
				*pNewVal = NewValue;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ANGLE_SCROLL)
		{
			CUIRect Inc, Dec;
			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);
			bool Shift = Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT);
			int Step = Shift ? 1 : 45;
			int Value = pProps[i].m_Value;

			int NewValue = UiDoValueSelector(&pIDs[i], &Shifter, "", Value, pProps[i].m_Min, pProps[i].m_Max, Shift ? 1 : 45, Shift ? 1.0f : 10.0f, "Use left mouse button to drag and change the value. Hold shift to be more precise. Rightclick to edit as text.", false, false, 0);
			if(DoButton_ButtonDec(&pIDs[i] + 1, 0, 0, &Dec, 0, "Decrease"))
			{
				NewValue = (round_ceil((pProps[i].m_Value / (float)Step)) - 1) * Step;
				if(NewValue < 0)
					NewValue += 360;
			}
			if(DoButton_ButtonInc(&pIDs[i] + 2, 0, 0, &Inc, 0, "Increase"))
				NewValue = (pProps[i].m_Value + Step) / Step * Step;

			if(NewValue != pProps[i].m_Value)
			{
				*pNewVal = NewValue % 360;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_COLOR)
		{
			static const char *s_paTexts[4] = {"R", "G", "B", "A"};
			static int s_aShift[] = {24, 16, 8, 0};
			int NewColor = 0;

			// extra space
			CUIRect ColorBox, ColorSlots;

			pToolBox->HSplitTop(3.0f * 13.0f, &Slot, pToolBox);
			Slot.VSplitMid(&ColorBox, &ColorSlots);
			ColorBox.HMargin(1.0f, &ColorBox);
			ColorBox.VMargin(6.0f, &ColorBox);

			for(int c = 0; c < 4; c++)
			{
				int v = (pProps[i].m_Value >> s_aShift[c]) & 0xff;
				NewColor |= UiDoValueSelector(((char *)&pIDs[i]) + c, &Shifter, s_paTexts[c], v, 0, 255, 1, 1.0f, "Use left mouse button to drag and change the color value. Hold shift to be more precise. Rightclick to edit as text.") << s_aShift[c];

				if(c != 3)
				{
					ColorSlots.HSplitTop(13.0f, &Shifter, &ColorSlots);
					Shifter.HMargin(1.0f, &Shifter);
				}
			}

			// hex
			pToolBox->HSplitTop(13.0f, &Slot, pToolBox);
			Slot.VSplitMid(0x0, &Shifter);
			Shifter.HMargin(1.0f, &Shifter);

			int NewColorHex = pProps[i].m_Value & 0xff;
			NewColorHex |= UiDoValueSelector(((char *)&pIDs[i] - 1), &Shifter, "", (pProps[i].m_Value >> 8) & 0xFFFFFF, 0, 0xFFFFFF, 1, 1.0f, "Use left mouse button to drag and change the color value. Hold shift to be more precise. Rightclick to edit as text.", false, true) << 8;

			// color picker
			ColorRGBA Color = ColorRGBA(
				((pProps[i].m_Value >> s_aShift[0]) & 0xff) / 255.0f,
				((pProps[i].m_Value >> s_aShift[1]) & 0xff) / 255.0f,
				((pProps[i].m_Value >> s_aShift[2]) & 0xff) / 255.0f,
				1.0f);

			static int s_ColorPicker, s_ColorPickerID;
			if(DoButton_ColorPicker(&s_ColorPicker, &ColorBox, &Color))
			{
				ms_PickerColor = color_cast<ColorHSVA>(Color);
				UiInvokePopupMenu(&s_ColorPickerID, 0, UI()->MouseX(), UI()->MouseY(), 180, 150, PopupColorPicker);
			}

			if(UI()->HotItem() == &ms_SVPicker || UI()->HotItem() == &ms_HuePicker)
			{
				ColorRGBA c = color_cast<ColorRGBA>(ms_PickerColor);
				NewColor = ((int)(c.r * 255.0f) & 0xff) << 24 | ((int)(c.g * 255.0f) & 0xff) << 16 | ((int)(c.b * 255.0f) & 0xff) << 8 | (pProps[i].m_Value & 0xff);
			}

			//
			if(NewColor != pProps[i].m_Value)
			{
				*pNewVal = NewColor;
				Change = i;
			}
			else if(NewColorHex != pProps[i].m_Value)
			{
				*pNewVal = NewColorHex;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_IMAGE)
		{
			char aBuf[64];
			if(pProps[i].m_Value < 0)
				str_copy(aBuf, "None", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%s", m_Map.m_lImages[pProps[i].m_Value]->m_aName);

			float FontSize = ScaleFontSize(aBuf, sizeof(aBuf), 10.0f, Shifter.w);
			if(DoButton_Ex(&pIDs[i], aBuf, 0, &Shifter, 0, 0, CUI::CORNER_ALL, FontSize))
				PopupSelectImageInvoke(pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectImageResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SHIFT)
		{
			CUIRect Left, Right, Up, Down;
			Shifter.VSplitMid(&Left, &Up);
			Left.VSplitRight(1.0f, &Left, 0);
			Up.VSplitLeft(1.0f, 0, &Up);
			Left.VSplitLeft(10.0f, &Left, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Right);
			RenderTools()->DrawUIRect(&Shifter, ColorRGBA(1, 1, 1, 0.5f), 0, 0.0f);
			UI()->DoLabel(&Shifter, "X", 10.0f, 0, -1);
			Up.VSplitLeft(10.0f, &Up, &Shifter);
			Shifter.VSplitRight(10.0f, &Shifter, &Down);
			RenderTools()->DrawUIRect(&Shifter, ColorRGBA(1, 1, 1, 0.5f), 0, 0.0f);
			UI()->DoLabel(&Shifter, "Y", 10.0f, 0, -1);
			if(DoButton_ButtonDec(&pIDs[i], "-", 0, &Left, 0, "Left"))
			{
				*pNewVal = 1;
				Change = i;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 3, "+", 0, &Right, 0, "Right"))
			{
				*pNewVal = 2;
				Change = i;
			}
			if(DoButton_ButtonDec(((char *)&pIDs[i]) + 1, "-", 0, &Up, 0, "Up"))
			{
				*pNewVal = 4;
				Change = i;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, "+", 0, &Down, 0, "Down"))
			{
				*pNewVal = 8;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_SOUND)
		{
			char aBuf[64];
			if(pProps[i].m_Value < 0)
				str_copy(aBuf, "None", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%s", m_Map.m_lSounds[pProps[i].m_Value]->m_aName);

			float FontSize = ScaleFontSize(aBuf, sizeof(aBuf), 10.0f, Shifter.w);
			if(DoButton_Ex(&pIDs[i], aBuf, 0, &Shifter, 0, 0, CUI::CORNER_ALL, FontSize))
				PopupSelectSoundInvoke(pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectSoundResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_AUTOMAPPER)
		{
			char aBuf[64];
			if(pProps[i].m_Value < 0 || pProps[i].m_Min < 0 || pProps[i].m_Min >= m_Map.m_lImages.size())
				str_copy(aBuf, "None", sizeof(aBuf));
			else
				str_format(aBuf, sizeof(aBuf), "%s", m_Map.m_lImages[pProps[i].m_Min]->m_AutoMapper.GetConfigName(pProps[i].m_Value));

			float FontSize = ScaleFontSize(aBuf, sizeof(aBuf), 10.0f, Shifter.w);
			if(DoButton_Ex(&pIDs[i], aBuf, 0, &Shifter, 0, 0, CUI::CORNER_ALL, FontSize))
				PopupSelectConfigAutoMapInvoke(pProps[i].m_Value, UI()->MouseX(), UI()->MouseY());

			int r = PopupSelectConfigAutoMapResult();
			if(r >= -1)
			{
				*pNewVal = r;
				Change = i;
			}
		}
		else if(pProps[i].m_Type == PROPTYPE_ENVELOPE)
		{
			CUIRect Inc, Dec;
			char aBuf[64];
			int CurValue = pProps[i].m_Value;

			Shifter.VSplitRight(10.0f, &Shifter, &Inc);
			Shifter.VSplitLeft(10.0f, &Dec, &Shifter);

			if(CurValue <= 0)
				str_copy(aBuf, "None", sizeof(aBuf));
			else if(m_Map.m_lEnvelopes[CurValue - 1]->m_aName[0])
				str_format(aBuf, sizeof(aBuf), "%d: %s", CurValue, m_Map.m_lEnvelopes[CurValue - 1]->m_aName);
			else
				str_format(aBuf, sizeof(aBuf), "%d", CurValue);

			float FontSize = ScaleFontSize(aBuf, sizeof(aBuf), 10.0f, Shifter.w);
			RenderTools()->DrawUIRect(&Shifter, Color, 0, 5.0f);
			UI()->DoLabel(&Shifter, aBuf, FontSize, 0, -1);

			if(DoButton_ButtonDec((char *)&pIDs[i] + 1, 0, 0, &Dec, 0, "Previous Envelope"))
			{
				*pNewVal = pProps[i].m_Value - 1;
				Change = i;
			}
			if(DoButton_ButtonInc(((char *)&pIDs[i]) + 2, 0, 0, &Inc, 0, "Next Envelope"))
			{
				*pNewVal = pProps[i].m_Value + 1;
				Change = i;
			}
		}
	}

	return Change;
}

void CEditor::RenderLayers(CUIRect ToolBox, CUIRect View)
{
	CUIRect LayersBox = ToolBox;

	if(!m_GuiActive)
		return;

	CUIRect Slot, Button;
	char aBuf[64];

	float LayersHeight = 12.0f; // Height of AddGroup button
	static int s_ScrollBar = 0;
	static float s_ScrollValue = 0;

	for(int g = 0; g < m_Map.m_lGroups.size(); g++)
	{
		// Each group is 19.0f
		// Each layer is 14.0f
		LayersHeight += 19.0f;
		if(!m_Map.m_lGroups[g]->m_Collapse)
			LayersHeight += m_Map.m_lGroups[g]->m_lLayers.size() * 14.0f;
	}

	float ScrollDifference = LayersHeight - LayersBox.h;

	if(LayersHeight > LayersBox.h) // Do we even need a scrollbar?
	{
		CUIRect Scroll;
		LayersBox.VSplitRight(15.0f, &LayersBox, &Scroll);
		LayersBox.VSplitRight(3.0f, &LayersBox, 0); // extra spacing
		Scroll.HMargin(5.0f, &Scroll);
		s_ScrollValue = UiDoScrollbarV(&s_ScrollBar, &Scroll, s_ScrollValue);

		if(UI()->MouseInside(&Scroll) || UI()->MouseInside(&LayersBox))
		{
			int ScrollNum = (int)((LayersHeight - LayersBox.h) / 15.0f) + 1;
			if(ScrollNum > 0)
			{
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					s_ScrollValue = clamp(s_ScrollValue - 1.0f / ScrollNum, 0.0f, 1.0f);
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					s_ScrollValue = clamp(s_ScrollValue + 1.0f / ScrollNum, 0.0f, 1.0f);
			}
		}
	}

	float LayerStartAt = ScrollDifference * s_ScrollValue;
	if(LayerStartAt < 0.0f)
		LayerStartAt = 0.0f;

	float LayerStopAt = LayersHeight - ScrollDifference * (1 - s_ScrollValue);
	float LayerCur = 0;

	// render layers
	{
		for(int g = 0; g < m_Map.m_lGroups.size(); g++)
		{
			if(LayerCur > LayerStopAt)
				break;
			else if(LayerCur + m_Map.m_lGroups[g]->m_lLayers.size() * 14.0f + 19.0f < LayerStartAt)
			{
				LayerCur += m_Map.m_lGroups[g]->m_lLayers.size() * 14.0f + 19.0f;
				continue;
			}

			CUIRect VisibleToggle;
			if(LayerCur >= LayerStartAt)
			{
				LayersBox.HSplitTop(12.0f, &Slot, &LayersBox);
				Slot.VSplitLeft(12, &VisibleToggle, &Slot);
				if(DoButton_Ex(&m_Map.m_lGroups[g]->m_Visible, m_Map.m_lGroups[g]->m_Visible ? "V" : "H", m_Map.m_lGroups[g]->m_Collapse ? 1 : 0, &VisibleToggle, 0, "Toggle group visibility", CUI::CORNER_L, 10.0f, 0))
					m_Map.m_lGroups[g]->m_Visible = !m_Map.m_lGroups[g]->m_Visible;

				str_format(aBuf, sizeof(aBuf), "#%d %s", g, m_Map.m_lGroups[g]->m_aName);
				float FontSize = 10.0f;
				while(TextRender()->TextWidth(0, FontSize, aBuf, -1, -1.0f) > Slot.w)
					FontSize--;
				if(int Result = DoButton_Ex(&m_Map.m_lGroups[g], aBuf, g == m_SelectedGroup, &Slot,
					   BUTTON_CONTEXT, m_Map.m_lGroups[g]->m_Collapse ? "Select group. Shift click to select all layers. Double click to expand." : "Select group. Shift click to select all layers. Double click to collapse.", CUI::CORNER_R, FontSize))
				{
					if(g != m_SelectedGroup)
						SelectLayer(0, g);

					if((Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT)) && m_SelectedGroup == g)
					{
						m_lSelectedLayers.clear();
						for(int i = 0; i < m_Map.m_lGroups[g]->m_lLayers.size(); i++)
						{
							m_lSelectedLayers.add(i);
						}
					}

					static int s_GroupPopupId = 0;
					if(Result == 2)
						UiInvokePopupMenu(&s_GroupPopupId, 0, UI()->MouseX(), UI()->MouseY(), 145, 230, PopupGroup);

					if(m_Map.m_lGroups[g]->m_lLayers.size() && Input()->MouseDoubleClick())
						m_Map.m_lGroups[g]->m_Collapse ^= 1;
				}
				LayersBox.HSplitTop(2.0f, &Slot, &LayersBox);
			}
			LayerCur += 14.0f;

			for(int i = 0; i < m_Map.m_lGroups[g]->m_lLayers.size(); i++)
			{
				if(LayerCur > LayerStopAt)
					break;
				else if(LayerCur < LayerStartAt)
				{
					LayerCur += 14.0f;
					continue;
				}

				if(m_Map.m_lGroups[g]->m_Collapse)
					continue;

				//visible
				LayersBox.HSplitTop(12.0f, &Slot, &LayersBox);
				Slot.VSplitLeft(12.0f, 0, &Button);
				Button.VSplitLeft(15, &VisibleToggle, &Button);

				if(DoButton_Ex(&m_Map.m_lGroups[g]->m_lLayers[i]->m_Visible, m_Map.m_lGroups[g]->m_lLayers[i]->m_Visible ? "V" : "H", 0, &VisibleToggle, 0, "Toggle layer visibility", CUI::CORNER_L, 10.0f, 0))
					m_Map.m_lGroups[g]->m_lLayers[i]->m_Visible = !m_Map.m_lGroups[g]->m_lLayers[i]->m_Visible;

				if(m_Map.m_lGroups[g]->m_lLayers[i]->m_aName[0])
					str_copy(aBuf, m_Map.m_lGroups[g]->m_lLayers[i]->m_aName, sizeof(aBuf));
				else
				{
					if(m_Map.m_lGroups[g]->m_lLayers[i]->m_Type == LAYERTYPE_TILES)
					{
						CLayerTiles *pTiles = (CLayerTiles *)m_Map.m_lGroups[g]->m_lLayers[i];
						str_copy(aBuf, pTiles->m_Image >= 0 ? m_Map.m_lImages[pTiles->m_Image]->m_aName : "Tiles", sizeof(aBuf));
					}
					else if(m_Map.m_lGroups[g]->m_lLayers[i]->m_Type == LAYERTYPE_QUADS)
					{
						CLayerQuads *pQuads = (CLayerQuads *)m_Map.m_lGroups[g]->m_lLayers[i];
						str_copy(aBuf, pQuads->m_Image >= 0 ? m_Map.m_lImages[pQuads->m_Image]->m_aName : "Quads", sizeof(aBuf));
					}
					else if(m_Map.m_lGroups[g]->m_lLayers[i]->m_Type == LAYERTYPE_SOUNDS)
					{
						CLayerSounds *pSounds = (CLayerSounds *)m_Map.m_lGroups[g]->m_lLayers[i];
						str_copy(aBuf, pSounds->m_Sound >= 0 ? m_Map.m_lSounds[pSounds->m_Sound]->m_aName : "Sounds", sizeof(aBuf));
					}
					if(str_length(aBuf) > 11)
						str_format(aBuf, sizeof(aBuf), "%.8s...", aBuf);
				}

				float FontSize = 10.0f;
				while(TextRender()->TextWidth(0, FontSize, aBuf, -1, -1.0f) > Button.w)
					FontSize--;
				int Checked = 0;
				if(g == m_SelectedGroup)
				{
					for(int j = 0; j < m_lSelectedLayers.size(); j++)
					{
						if(m_lSelectedLayers[j] == i)
						{
							Checked = 1;
							break;
						}
					}
				}

				if(m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pGameLayer ||
					m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pFrontLayer ||
					m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pSwitchLayer ||
					m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pTuneLayer ||
					m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pSpeedupLayer ||
					m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pTeleLayer)
				{
					Checked += 6;
				}
				if(int Result = DoButton_Ex(m_Map.m_lGroups[g]->m_lLayers[i], aBuf, Checked, &Button,
					   BUTTON_CONTEXT, "Select layer. Shift click to select multiple.", CUI::CORNER_R, FontSize))
				{
					static CLayerPopupContext s_LayerPopupContext = {};
					if(Result == 1)
					{
						if((Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT)) && m_SelectedGroup == g)
						{
							if(!m_lSelectedLayers.remove(i))
								m_lSelectedLayers.add(i);
						}
						else if(!(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT)))
						{
							SelectLayer(i, g);
						}
					}
					else if(Result == 2)
					{
						bool IsLayerSelected = false;

						if(m_SelectedGroup == g)
						{
							for(int x = 0; x < m_lSelectedLayers.size(); x++)
							{
								if(m_lSelectedLayers[x] == i)
								{
									IsLayerSelected = true;
									break;
								}
							}
						}

						if(!IsLayerSelected)
						{
							SelectLayer(i, g);
						}

						if(m_lSelectedLayers.size() > 1)
						{
							bool AllTile = true;
							for(int i = 0; AllTile && i < m_lSelectedLayers.size(); i++)
							{
								if(m_Map.m_lGroups[m_SelectedGroup]->m_lLayers[m_lSelectedLayers[i]]->m_Type == LAYERTYPE_TILES)
									s_LayerPopupContext.m_aLayers.add((CLayerTiles *)m_Map.m_lGroups[m_SelectedGroup]->m_lLayers[m_lSelectedLayers[i]]);
								else
									AllTile = false;
							}

							// Don't allow editing if all selected layers are tile layers
							if(!AllTile)
								s_LayerPopupContext.m_aLayers.clear();
						}
						else
							s_LayerPopupContext.m_aLayers.clear();

						UiInvokePopupMenu(&s_LayerPopupContext, 0, UI()->MouseX(), UI()->MouseY(), 120, 300, PopupLayer, &s_LayerPopupContext);
					}
				}

				LayerCur += 14.0f;
				LayersBox.HSplitTop(2.0f, &Slot, &LayersBox);
			}
			if(LayerCur > LayerStartAt && LayerCur < LayerStopAt)
				LayersBox.HSplitTop(5.0f, &Slot, &LayersBox);
			LayerCur += 5.0f;
		}
	}

	if(Input()->KeyPress(KEY_DOWN) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0)
	{
		if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
		{
			if(m_lSelectedLayers[m_lSelectedLayers.size() - 1] < m_Map.m_lGroups[m_SelectedGroup]->m_lLayers.size() - 1)
				m_lSelectedLayers.add(m_lSelectedLayers[m_lSelectedLayers.size() - 1] + 1);
		}
		else
		{
			int CurrentLayer = 0;
			for(int i = 0; i < m_lSelectedLayers.size(); i++)
				CurrentLayer = maximum(m_lSelectedLayers[i], CurrentLayer);
			SelectLayer(CurrentLayer);

			if(m_lSelectedLayers[0] < m_Map.m_lGroups[m_SelectedGroup]->m_lLayers.size() - 1)
			{
				SelectLayer(m_lSelectedLayers[0] + 1);
			}
			else
			{
				for(int Group = m_SelectedGroup + 1; Group < m_Map.m_lGroups.size(); Group++)
				{
					if(m_Map.m_lGroups[Group]->m_lLayers.size() > 0)
					{
						SelectLayer(0, Group);
						break;
					}
				}
			}
		}
	}
	if(Input()->KeyPress(KEY_UP) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0)
	{
		if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
		{
			if(m_lSelectedLayers[m_lSelectedLayers.size() - 1] > 0)
				m_lSelectedLayers.add(m_lSelectedLayers[m_lSelectedLayers.size() - 1] - 1);
		}
		else
		{
			int CurrentLayer = std::numeric_limits<int>::max();
			for(int i = 0; i < m_lSelectedLayers.size(); i++)
				CurrentLayer = minimum(m_lSelectedLayers[i], CurrentLayer);
			SelectLayer(CurrentLayer);

			if(m_lSelectedLayers[0] > 0)
			{
				SelectLayer(m_lSelectedLayers[0] - 1);
			}
			else
			{
				for(int Group = m_SelectedGroup - 1; Group >= 0; Group--)
				{
					if(m_Map.m_lGroups[Group]->m_lLayers.size() > 0)
					{
						SelectLayer(m_Map.m_lGroups[Group]->m_lLayers.size() - 1, Group);
						break;
					}
				}
			}
		}
	}

	if(LayerCur <= LayerStopAt)
	{
		LayersBox.HSplitTop(12.0f, &Slot, &LayersBox);

		static int s_NewGroupButton = 0;
		if(DoButton_Editor(&s_NewGroupButton, "Add group", 0, &Slot, CUI::CORNER_R, "Adds a new group"))
		{
			m_Map.NewGroup();
			m_SelectedGroup = m_Map.m_lGroups.size() - 1;
		}
	}
}

void CEditor::ReplaceImage(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	CEditorImage ImgInfo(pEditor);
	if(!pEditor->Graphics()->LoadPNG(&ImgInfo, pFileName, StorageType))
		return;

	CEditorImage *pImg = pEditor->m_Map.m_lImages[pEditor->m_SelectedImage];
	pEditor->Graphics()->UnloadTexture(pImg->m_Texture);
	if(pImg->m_pData)
	{
		free(pImg->m_pData);
		pImg->m_pData = 0;
	}
	*pImg = ImgInfo;
	IStorage::StripPathAndExtension(pFileName, pImg->m_aName, sizeof(pImg->m_aName));
	pImg->m_External = IsVanillaImage(pImg->m_aName);

	if(!pImg->m_External && g_Config.m_ClEditorDilate == 1 && pImg->m_Format == CImageInfo::FORMAT_RGBA)
	{
		int ColorChannelCount = 0;
		if(ImgInfo.m_Format == CImageInfo::FORMAT_RGBA)
			ColorChannelCount = 4;

		DilateImage((unsigned char *)ImgInfo.m_pData, ImgInfo.m_Width, ImgInfo.m_Height, ColorChannelCount);
	}

	pImg->m_AutoMapper.Load(pImg->m_aName);
	int TextureLoadFlag = pEditor->Graphics()->HasTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
	if(ImgInfo.m_Width % 16 != 0 || ImgInfo.m_Height % 16 != 0)
		TextureLoadFlag = 0;
	pImg->m_Texture = pEditor->Graphics()->LoadTextureRaw(ImgInfo.m_Width, ImgInfo.m_Height, ImgInfo.m_Format, ImgInfo.m_pData, CImageInfo::FORMAT_AUTO, TextureLoadFlag, pFileName);
	ImgInfo.m_pData = 0;
	pEditor->SortImages();
	for(int i = 0; i < pEditor->m_Map.m_lImages.size(); ++i)
	{
		if(!str_comp(pEditor->m_Map.m_lImages[i]->m_aName, pImg->m_aName))
			pEditor->m_SelectedImage = i;
	}
	pEditor->m_Dialog = DIALOG_NONE;
}

void CEditor::AddImage(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	CEditorImage ImgInfo(pEditor);
	if(!pEditor->Graphics()->LoadPNG(&ImgInfo, pFileName, StorageType))
		return;

	// check if we have that image already
	char aBuf[128];
	IStorage::StripPathAndExtension(pFileName, aBuf, sizeof(aBuf));
	for(int i = 0; i < pEditor->m_Map.m_lImages.size(); ++i)
	{
		if(!str_comp(pEditor->m_Map.m_lImages[i]->m_aName, aBuf))
			return;
	}

	if(pEditor->m_Map.m_lImages.size() >= 64) // hard limit for teeworlds
	{
		pEditor->m_PopupEventType = pEditor->POPEVENT_IMAGE_MAX;
		pEditor->m_PopupEventActivated = true;
		return;
	}

	CEditorImage *pImg = new CEditorImage(pEditor);
	*pImg = ImgInfo;
	pImg->m_External = IsVanillaImage(aBuf);

	if(!pImg->m_External && g_Config.m_ClEditorDilate == 1 && pImg->m_Format == CImageInfo::FORMAT_RGBA)
	{
		int ColorChannelCount = 0;
		if(ImgInfo.m_Format == CImageInfo::FORMAT_RGBA)
			ColorChannelCount = 4;

		DilateImage((unsigned char *)ImgInfo.m_pData, ImgInfo.m_Width, ImgInfo.m_Height, ColorChannelCount);
	}

	int TextureLoadFlag = pEditor->Graphics()->HasTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
	if(ImgInfo.m_Width % 16 != 0 || ImgInfo.m_Height % 16 != 0)
		TextureLoadFlag = 0;
	pImg->m_Texture = pEditor->Graphics()->LoadTextureRaw(ImgInfo.m_Width, ImgInfo.m_Height, ImgInfo.m_Format, ImgInfo.m_pData, CImageInfo::FORMAT_AUTO, TextureLoadFlag, pFileName);
	ImgInfo.m_pData = 0;
	str_copy(pImg->m_aName, aBuf, sizeof(pImg->m_aName));
	pImg->m_AutoMapper.Load(pImg->m_aName);
	pEditor->m_Map.m_lImages.add(pImg);
	pEditor->SortImages();
	if(pEditor->m_SelectedImage > -1 && pEditor->m_SelectedImage < pEditor->m_Map.m_lImages.size())
	{
		for(int i = 0; i <= pEditor->m_SelectedImage; ++i)
			if(!str_comp(pEditor->m_Map.m_lImages[i]->m_aName, aBuf))
			{
				pEditor->m_SelectedImage++;
				break;
			}
	}
	pEditor->m_Dialog = DIALOG_NONE;
}

void CEditor::AddSound(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;

	// check if we have that sound already
	char aBuf[128];
	IStorage::StripPathAndExtension(pFileName, aBuf, sizeof(aBuf));
	for(int i = 0; i < pEditor->m_Map.m_lSounds.size(); ++i)
	{
		if(!str_comp(pEditor->m_Map.m_lSounds[i]->m_aName, aBuf))
			return;
	}

	// load external
	IOHANDLE SoundFile = pEditor->Storage()->OpenFile(pFileName, IOFLAG_READ, StorageType);
	if(!SoundFile)
	{
		dbg_msg("sound/opus", "failed to open file. filename='%s'", pFileName);
		return;
	}

	// read the whole file into memory
	int DataSize = io_length(SoundFile);

	if(DataSize <= 0)
	{
		io_close(SoundFile);
		dbg_msg("sound/opus", "failed to open file. filename='%s'", pFileName);
		return;
	}

	void *pData = malloc((unsigned)DataSize);
	io_read(SoundFile, pData, (unsigned)DataSize);
	io_close(SoundFile);

	// load sound
	int SoundId = pEditor->Sound()->LoadOpusFromMem(pData, (unsigned)DataSize, true);
	if(SoundId == -1)
		return;

	// add sound
	CEditorSound *pSound = new CEditorSound(pEditor);
	pSound->m_SoundID = SoundId;
	pSound->m_DataSize = (unsigned)DataSize;
	pSound->m_pData = pData;
	str_copy(pSound->m_aName, aBuf, sizeof(pSound->m_aName));
	pEditor->m_Map.m_lSounds.add(pSound);

	if(pEditor->m_SelectedSound > -1 && pEditor->m_SelectedSound < pEditor->m_Map.m_lSounds.size())
	{
		for(int i = 0; i <= pEditor->m_SelectedSound; ++i)
			if(!str_comp(pEditor->m_Map.m_lSounds[i]->m_aName, aBuf))
			{
				pEditor->m_SelectedSound++;
				break;
			}
	}

	pEditor->m_Dialog = DIALOG_NONE;
}

void CEditor::ReplaceSound(const char *pFileName, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;

	// load external
	IOHANDLE SoundFile = pEditor->Storage()->OpenFile(pFileName, IOFLAG_READ, StorageType);
	if(!SoundFile)
	{
		dbg_msg("sound/opus", "failed to open file. filename='%s'", pFileName);
		return;
	}

	// read the whole file into memory
	int DataSize = io_length(SoundFile);

	if(DataSize <= 0)
	{
		io_close(SoundFile);
		dbg_msg("sound/opus", "failed to open file. filename='%s'", pFileName);
		return;
	}

	void *pData = malloc((unsigned)DataSize);
	io_read(SoundFile, pData, (unsigned)DataSize);
	io_close(SoundFile);

	CEditorSound *pSound = pEditor->m_Map.m_lSounds[pEditor->m_SelectedSound];

	// unload sample
	pEditor->Sound()->UnloadSample(pSound->m_SoundID);
	if(pSound->m_pData)
	{
		free(pSound->m_pData);
		pSound->m_pData = 0x0;
	}

	// replace sound
	IStorage::StripPathAndExtension(pFileName, pSound->m_aName, sizeof(pSound->m_aName));
	pSound->m_SoundID = pEditor->Sound()->LoadOpusFromMem(pData, (unsigned)DataSize, true);
	pSound->m_pData = pData;
	pSound->m_DataSize = DataSize;

	pEditor->m_Dialog = DIALOG_NONE;
}

static int gs_ModifyIndexDeletedIndex;
static void ModifyIndexDeleted(int *pIndex)
{
	if(*pIndex == gs_ModifyIndexDeletedIndex)
		*pIndex = -1;
	else if(*pIndex > gs_ModifyIndexDeletedIndex)
		*pIndex = *pIndex - 1;
}

int CEditor::PopupImage(CEditor *pEditor, CUIRect View, void *pContext)
{
	static int s_ReplaceButton = 0;
	static int s_RemoveButton = 0;

	CUIRect Slot;
	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	CEditorImage *pImg = pEditor->m_Map.m_lImages[pEditor->m_SelectedImage];

	static int s_ExternalButton = 0;
	if(pImg->m_External)
	{
		if(pEditor->DoButton_MenuItem(&s_ExternalButton, "Embed", 0, &Slot, 0, "Embeds the image into the map file."))
		{
			pImg->m_External = 0;
			return 1;
		}
		View.HSplitTop(5.0f, &Slot, &View);
		View.HSplitTop(12.0f, &Slot, &View);
	}
	else if(IsVanillaImage(pImg->m_aName))
	{
		if(pEditor->DoButton_MenuItem(&s_ExternalButton, "Make external", 0, &Slot, 0, "Removes the image from the map file."))
		{
			pImg->m_External = 1;
			return 1;
		}
		View.HSplitTop(5.0f, &Slot, &View);
		View.HSplitTop(12.0f, &Slot, &View);
	}

	if(pEditor->DoButton_MenuItem(&s_ReplaceButton, "Replace", 0, &Slot, 0, "Replaces the image with a new one"))
	{
		pEditor->InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_IMG, "Replace Image", "Replace", "mapres", "", ReplaceImage, pEditor);
		return 1;
	}

	View.HSplitTop(5.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_RemoveButton, "Remove", 0, &Slot, 0, "Removes the image from the map"))
	{
		delete pImg;
		pEditor->m_Map.m_lImages.remove_index(pEditor->m_SelectedImage);
		gs_ModifyIndexDeletedIndex = pEditor->m_SelectedImage;
		pEditor->m_Map.ModifyImageIndex(ModifyIndexDeleted);
		return 1;
	}

	return 0;
}

int CEditor::PopupSound(CEditor *pEditor, CUIRect View, void *pContext)
{
	static int s_ReplaceButton = 0;
	static int s_RemoveButton = 0;

	CUIRect Slot;
	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ReplaceButton, "Replace", 0, &Slot, 0, "Replaces the sound with a new one"))
	{
		pEditor->InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_SOUND, "Replace sound", "Replace", "mapres", "", ReplaceSound, pEditor);
		return 1;
	}

	View.HSplitTop(5.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	CEditorSound *pSound = pEditor->m_Map.m_lSounds[pEditor->m_SelectedSound];
	if(pEditor->DoButton_MenuItem(&s_RemoveButton, "Remove", 0, &Slot, 0, "Removes the sound from the map"))
	{
		delete pSound;
		pEditor->m_Map.m_lSounds.remove_index(pEditor->m_SelectedSound);
		gs_ModifyIndexDeletedIndex = pEditor->m_SelectedSound;
		pEditor->m_Map.ModifySoundIndex(ModifyIndexDeleted);
		return 1;
	}

	return 0;
}

void CEditor::SelectGameLayer()
{
	for(int g = 0; g < m_Map.m_lGroups.size(); g++)
	{
		for(int i = 0; i < m_Map.m_lGroups[g]->m_lLayers.size(); i++)
		{
			if(m_Map.m_lGroups[g]->m_lLayers[i] == m_Map.m_pGameLayer)
			{
				SelectLayer(i, g);
				return;
			}
		}
	}
}

static bool ImageNameLess(const CEditorImage *const &a, const CEditorImage *const &b)
{
	return str_comp(a->m_aName, b->m_aName) < 0;
}

static int *gs_pSortedIndex = 0;
static void ModifySortedIndex(int *pIndex)
{
	if(*pIndex >= 0)
		*pIndex = gs_pSortedIndex[*pIndex];
}

void CEditor::SortImages()
{
	if(!std::is_sorted(m_Map.m_lImages.base_ptr(), m_Map.m_lImages.base_ptr() + m_Map.m_lImages.size(), ImageNameLess))
	{
		array<CEditorImage *> lTemp = m_Map.m_lImages;
		gs_pSortedIndex = new int[lTemp.size()];

		std::sort(m_Map.m_lImages.base_ptr(), m_Map.m_lImages.base_ptr() + m_Map.m_lImages.size(), ImageNameLess);
		for(int OldIndex = 0; OldIndex < lTemp.size(); OldIndex++)
		{
			for(int NewIndex = 0; NewIndex < m_Map.m_lImages.size(); NewIndex++)
			{
				if(lTemp[OldIndex] == m_Map.m_lImages[NewIndex])
				{
					gs_pSortedIndex[OldIndex] = NewIndex;
					break;
				}
			}
		}
		m_Map.ModifyImageIndex(ModifySortedIndex);

		delete[] gs_pSortedIndex;
		gs_pSortedIndex = 0;
	}
}

void CEditor::RenderImages(CUIRect ToolBox, CUIRect View)
{
	static int s_ScrollBar = 0;
	static float s_ScrollValue = 0;
	float ImagesHeight = 30.0f + 14.0f * m_Map.m_lImages.size() + 27.0f;
	float ScrollDifference = ImagesHeight - ToolBox.h;

	if(!m_GuiActive)
		return;

	if(ImagesHeight > ToolBox.h) // Do we even need a scrollbar?
	{
		CUIRect Scroll;
		ToolBox.VSplitRight(15.0f, &ToolBox, &Scroll);
		ToolBox.VSplitRight(3.0f, &ToolBox, 0); // extra spacing
		Scroll.HMargin(5.0f, &Scroll);
		s_ScrollValue = UiDoScrollbarV(&s_ScrollBar, &Scroll, s_ScrollValue);

		if(UI()->MouseInside(&Scroll) || UI()->MouseInside(&ToolBox))
		{
			int ScrollNum = (int)((ImagesHeight - ToolBox.h) / 14.0f) + 1;
			if(ScrollNum > 0)
			{
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					s_ScrollValue = clamp(s_ScrollValue - 1.0f / ScrollNum, 0.0f, 1.0f);
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					s_ScrollValue = clamp(s_ScrollValue + 1.0f / ScrollNum, 0.0f, 1.0f);
			}
		}
	}

	float ImageStartAt = ScrollDifference * s_ScrollValue;
	if(ImageStartAt < 0.0f)
		ImageStartAt = 0.0f;

	float ImageStopAt = ImagesHeight - ScrollDifference * (1 - s_ScrollValue);
	float ImageCur = 0.0f;

	for(int e = 0; e < 2; e++) // two passes, first embedded, then external
	{
		CUIRect Slot;

		if(ImageCur > ImageStopAt)
			break;
		else if(ImageCur >= ImageStartAt)
		{
			ToolBox.HSplitTop(15.0f, &Slot, &ToolBox);
			if(e == 0)
				UI()->DoLabel(&Slot, "Embedded", 12.0f, 0);
			else
				UI()->DoLabel(&Slot, "External", 12.0f, 0);
		}
		ImageCur += 15.0f;

		for(int i = 0; i < m_Map.m_lImages.size(); i++)
		{
			if((e && !m_Map.m_lImages[i]->m_External) ||
				(!e && m_Map.m_lImages[i]->m_External))
			{
				continue;
			}

			if(ImageCur > ImageStopAt)
				break;
			else if(ImageCur < ImageStartAt)
			{
				ImageCur += 14.0f;
				continue;
			}
			ImageCur += 14.0f;

			char aBuf[128];
			str_copy(aBuf, m_Map.m_lImages[i]->m_aName, sizeof(aBuf));
			ToolBox.HSplitTop(12.0f, &Slot, &ToolBox);

			int Selected = m_SelectedImage == i;

			for(int x = 0; x < m_Map.m_lGroups.size(); ++x)
				for(int j = 0; j < m_Map.m_lGroups[x]->m_lLayers.size(); ++j)
					if(m_Map.m_lGroups[x]->m_lLayers[j]->m_Type == LAYERTYPE_QUADS)
					{
						CLayerQuads *pLayer = static_cast<CLayerQuads *>(m_Map.m_lGroups[x]->m_lLayers[j]);
						if(pLayer->m_Image == i)
							goto done;
					}
					else if(m_Map.m_lGroups[x]->m_lLayers[j]->m_Type == LAYERTYPE_TILES)
					{
						CLayerTiles *pLayer = static_cast<CLayerTiles *>(m_Map.m_lGroups[x]->m_lLayers[j]);
						if(pLayer->m_Image == i)
							goto done;
					}

			Selected += 2; // Image is unused
		done:
			if(Selected < 2 && e == 1)
			{
				if(!IsVanillaImage(m_Map.m_lImages[i]->m_aName))
				{
					Selected += 4; // Image should be embedded
				}
			}

			float FontSize = 10.0f;
			while(TextRender()->TextWidth(0, FontSize, aBuf, -1, -1.0f) > Slot.w)
				FontSize--;

			if(int Result = DoButton_Ex(&m_Map.m_lImages[i], aBuf, Selected, &Slot,
				   BUTTON_CONTEXT, "Select image.", 0, FontSize))
			{
				m_SelectedImage = i;

				static int s_PopupImageID = 0;
				if(Result == 2)
				{
					CEditorImage *pImg = m_Map.m_lImages[m_SelectedImage];
					int Height;
					if(pImg->m_External || IsVanillaImage(pImg->m_aName))
						Height = 60;
					else
						Height = 43;
					UiInvokePopupMenu(&s_PopupImageID, 0, UI()->MouseX(), UI()->MouseY(), 120, Height, PopupImage);
				}
			}

			ToolBox.HSplitTop(2.0f, 0, &ToolBox);
		}

		// separator
		ToolBox.HSplitTop(5.0f, &Slot, &ToolBox);
		ImageCur += 5.0f;
		IGraphics::CLineItem LineItem(Slot.x, Slot.y + Slot.h / 2, Slot.x + Slot.w, Slot.y + Slot.h / 2);
		Graphics()->TextureClear();
		Graphics()->LinesBegin();
		Graphics()->LinesDraw(&LineItem, 1);
		Graphics()->LinesEnd();
	}

	if(Input()->KeyPress(KEY_DOWN) && m_Dialog == DIALOG_NONE)
	{
		int OldImage = m_SelectedImage;
		m_SelectedImage = clamp(m_SelectedImage, 0, m_Map.m_lImages.size() - 1);
		for(int i = m_SelectedImage + 1; i < m_Map.m_lImages.size(); i++)
		{
			if(m_Map.m_lImages[i]->m_External == m_Map.m_lImages[m_SelectedImage]->m_External)
			{
				m_SelectedImage = i;
				break;
			}
		}
		if(m_SelectedImage == OldImage && !m_Map.m_lImages[m_SelectedImage]->m_External)
		{
			for(int i = 0; i < m_Map.m_lImages.size(); i++)
			{
				if(m_Map.m_lImages[i]->m_External)
				{
					m_SelectedImage = i;
					break;
				}
			}
		}
	}
	if(Input()->KeyPress(KEY_UP) && m_Dialog == DIALOG_NONE)
	{
		int OldImage = m_SelectedImage;
		m_SelectedImage = clamp(m_SelectedImage, 0, m_Map.m_lImages.size() - 1);
		for(int i = m_SelectedImage - 1; i >= 0; i--)
		{
			if(m_Map.m_lImages[i]->m_External == m_Map.m_lImages[m_SelectedImage]->m_External)
			{
				m_SelectedImage = i;
				break;
			}
		}
		if(m_SelectedImage == OldImage && m_Map.m_lImages[m_SelectedImage]->m_External)
		{
			for(int i = m_Map.m_lImages.size() - 1; i >= 0; i--)
			{
				if(!m_Map.m_lImages[i]->m_External)
				{
					m_SelectedImage = i;
					break;
				}
			}
		}
	}

	// render image
	int i = m_SelectedImage;
	if(i != -1 && i < m_Map.m_lImages.size())
	{
		CUIRect r;
		View.Margin(10.0f, &r);
		if(r.h < r.w)
			r.w = r.h;
		else
			r.h = r.w;
		float Max = (float)(maximum(m_Map.m_lImages[i]->m_Width, m_Map.m_lImages[i]->m_Height));
		r.w *= m_Map.m_lImages[i]->m_Width / Max;
		r.h *= m_Map.m_lImages[i]->m_Height / Max;
		Graphics()->TextureSet(m_Map.m_lImages[i]->m_Texture);
		Graphics()->BlendNormal();
		Graphics()->WrapClamp();
		Graphics()->QuadsBegin();
		IGraphics::CQuadItem QuadItem(r.x, r.y, r.w, r.h);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	}
	//if(ImageCur + 27.0f > ImageStopAt)
	//	return;

	CUIRect Slot;
	ToolBox.HSplitTop(5.0f, &Slot, &ToolBox);

	// new image
	static int s_NewImageButton = 0;
	ToolBox.HSplitTop(12.0f, &Slot, &ToolBox);
	if(DoButton_Editor(&s_NewImageButton, "Add", 0, &Slot, 0, "Load a new image to use in the map"))
		InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_IMG, "Add Image", "Add", "mapres", "", AddImage, this);
}

void CEditor::RenderSounds(CUIRect ToolBox, CUIRect View)
{
	static int s_ScrollBar = 0;
	static float s_ScrollValue = 0;
	float SoundsHeight = 30.0f + 14.0f * m_Map.m_lSounds.size() + 27.0f;
	float ScrollDifference = SoundsHeight - ToolBox.h;

	if(!m_GuiActive)
		return;

	if(SoundsHeight > ToolBox.h) // Do we even need a scrollbar?
	{
		CUIRect Scroll;
		ToolBox.VSplitRight(15.0f, &ToolBox, &Scroll);
		ToolBox.VSplitRight(3.0f, &ToolBox, 0); // extra spacing
		Scroll.HMargin(5.0f, &Scroll);
		s_ScrollValue = UiDoScrollbarV(&s_ScrollBar, &Scroll, s_ScrollValue);

		if(UI()->MouseInside(&Scroll) || UI()->MouseInside(&ToolBox))
		{
			int ScrollNum = (int)((SoundsHeight - ToolBox.h) / 14.0f) + 1;
			if(ScrollNum > 0)
			{
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					s_ScrollValue = clamp(s_ScrollValue - 1.0f / ScrollNum, 0.0f, 1.0f);
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					s_ScrollValue = clamp(s_ScrollValue + 1.0f / ScrollNum, 0.0f, 1.0f);
			}
		}
	}

	float SoundStartAt = ScrollDifference * s_ScrollValue;
	if(SoundStartAt < 0.0f)
		SoundStartAt = 0.0f;

	float SoundStopAt = SoundsHeight - ScrollDifference * (1 - s_ScrollValue);
	float SoundCur = 0.0f;

	CUIRect Slot;

	ToolBox.HSplitTop(15.0f, &Slot, &ToolBox);
	UI()->DoLabel(&Slot, "Embedded", 12.0f, 0);
	SoundCur += 15.0f;

	for(int i = 0; i < m_Map.m_lSounds.size(); i++)
	{
		if(SoundCur > SoundStopAt)
			break;
		else if(SoundCur < SoundStartAt)
		{
			SoundCur += 14.0f;
			continue;
		}
		SoundCur += 14.0f;

		char aBuf[128];
		str_copy(aBuf, m_Map.m_lSounds[i]->m_aName, sizeof(aBuf));
		ToolBox.HSplitTop(12.0f, &Slot, &ToolBox);

		int Selected = m_SelectedSound == i;
		for(int x = 0; x < m_Map.m_lGroups.size(); ++x)
			for(int j = 0; j < m_Map.m_lGroups[x]->m_lLayers.size(); ++j)
				if(m_Map.m_lGroups[x]->m_lLayers[j]->m_Type == LAYERTYPE_SOUNDS)
				{
					CLayerSounds *pLayer = static_cast<CLayerSounds *>(m_Map.m_lGroups[x]->m_lLayers[j]);
					if(pLayer->m_Sound == i)
						goto used;
				}

		Selected += 2; // Sound is unused
	used:
		float FontSize = 10.0f;
		while(TextRender()->TextWidth(0, FontSize, aBuf, -1, -1.0f) > Slot.w)
			FontSize--;

		if(int Result = DoButton_Ex(&m_Map.m_lSounds[i], aBuf, Selected, &Slot,
			   BUTTON_CONTEXT, "Select sound.", 0, FontSize))
		{
			m_SelectedSound = i;

			static int s_PopupSoundID = 0;
			if(Result == 2)
				UiInvokePopupMenu(&s_PopupSoundID, 0, UI()->MouseX(), UI()->MouseY(), 120, 43, PopupSound);
		}

		ToolBox.HSplitTop(2.0f, 0, &ToolBox);
	}

	// separator
	ToolBox.HSplitTop(5.0f, &Slot, &ToolBox);
	IGraphics::CLineItem LineItem(Slot.x, Slot.y + Slot.h / 2, Slot.x + Slot.w, Slot.y + Slot.h / 2);
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->LinesDraw(&LineItem, 1);
	Graphics()->LinesEnd();

	if(Input()->KeyPress(KEY_DOWN) && m_Dialog == DIALOG_NONE)
	{
		m_SelectedSound = clamp(m_SelectedSound, 0, m_Map.m_lSounds.size() - 1);
		if(m_SelectedSound == m_Map.m_lSounds.size() - 1)
			m_SelectedSound = 0;
		else
			m_SelectedSound += 1;
	}
	if(Input()->KeyPress(KEY_UP) && m_Dialog == DIALOG_NONE)
	{
		m_SelectedSound = clamp(m_SelectedSound, 0, m_Map.m_lSounds.size() - 1);
		if(m_SelectedSound == 0 && m_Map.m_lSounds.size() != 0)
			m_SelectedSound = m_Map.m_lSounds.size() - 1;
		else
			m_SelectedSound -= 1;
	}

	//CUIRect Slot;
	ToolBox.HSplitTop(5.0f, &Slot, &ToolBox);

	// new Sound
	static int s_NewSoundButton = 0;
	ToolBox.HSplitTop(12.0f, &Slot, &ToolBox);
	if(DoButton_Editor(&s_NewSoundButton, "Add", 0, &Slot, 0, "Load a new sound to use in the map"))
		InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_SOUND, "Add Sound", "Add", "mapres", "", AddSound, this);
}

static int EditorListdirCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;
	if((pName[0] == '.' && (pName[1] == 0 ||
				       (pName[1] == '.' && pName[2] == 0 && (!str_comp(pEditor->m_pFileDialogPath, "maps") || !str_comp(pEditor->m_pFileDialogPath, "mapres"))))) ||
		(!IsDir && ((pEditor->m_FileDialogFileType == CEditor::FILETYPE_MAP && !str_endswith(pName, ".map")) ||
				   (pEditor->m_FileDialogFileType == CEditor::FILETYPE_IMG && !str_endswith(pName, ".png")) ||
				   (pEditor->m_FileDialogFileType == CEditor::FILETYPE_SOUND && !str_endswith(pName, ".opus")))))
		return 0;

	CEditor::CFilelistItem Item;
	str_copy(Item.m_aFilename, pName, sizeof(Item.m_aFilename));
	if(IsDir)
		str_format(Item.m_aName, sizeof(Item.m_aName), "%s/", pName);
	else
	{
		int LenEnding = pEditor->m_FileDialogFileType == CEditor::FILETYPE_SOUND ? 5 : 4;
		str_truncate(Item.m_aName, sizeof(Item.m_aName), pName, str_length(pName) - LenEnding);
	}
	Item.m_IsDir = IsDir != 0;
	Item.m_IsLink = false;
	Item.m_StorageType = StorageType;
	pEditor->m_FileList.add(Item);

	return 0;
}

void CEditor::AddFileDialogEntry(int Index, CUIRect *pView)
{
	m_FilesCur++;
	if(m_FilesCur - 1 < m_FilesStartAt || m_FilesCur >= m_FilesStopAt)
		return;

	CUIRect Button, FileIcon;
	pView->HSplitTop(15.0f, &Button, pView);
	pView->HSplitTop(2.0f, 0, pView);
	Button.VSplitLeft(Button.h, &FileIcon, &Button);
	Button.VSplitLeft(5.0f, 0, &Button);

	Graphics()->TextureSet(g_pData->m_aImages[IMAGE_FILEICONS].m_Id);
	Graphics()->QuadsBegin();
	RenderTools()->SelectSprite(m_FileList[Index].m_IsDir ? SPRITE_FILE_FOLDER : SPRITE_FILE_MAP2);
	IGraphics::CQuadItem QuadItem(FileIcon.x, FileIcon.y, FileIcon.w, FileIcon.h);
	Graphics()->QuadsDrawTL(&QuadItem, 1);
	Graphics()->QuadsEnd();

	if(DoButton_File(&m_FileList[Index], m_FileList[Index].m_aName, m_FilesSelectedIndex == Index, &Button, 0, 0))
	{
		if(!m_FileList[Index].m_IsDir)
			str_copy(m_aFileDialogFileName, m_FileList[Index].m_aFilename, sizeof(m_aFileDialogFileName));
		else
			m_aFileDialogFileName[0] = 0;
		m_FilesSelectedIndex = Index;
		m_PreviewImageIsLoaded = false;

		if(Input()->MouseDoubleClick())
			m_FileDialogActivate = true;
	}
}

void CEditor::RenderFileDialog()
{
	// GUI coordsys
	UI()->MapScreen();
	CUIRect View = *UI()->Screen();
	CUIRect Preview;
	float Width = View.w, Height = View.h;

	RenderTools()->DrawUIRect(&View, ColorRGBA(0, 0, 0, 0.25f), 0, 0);
	View.VMargin(150.0f, &View);
	View.HMargin(50.0f, &View);
	RenderTools()->DrawUIRect(&View, ColorRGBA(0, 0, 0, 0.75f), CUI::CORNER_ALL, 5.0f);
	View.Margin(10.0f, &View);

	CUIRect Title, FileBox, FileBoxLabel, ButtonBar, Scroll, PathBox;
	View.HSplitTop(18.0f, &Title, &View);
	View.HSplitTop(5.0f, 0, &View); // some spacing
	View.HSplitBottom(14.0f, &View, &ButtonBar);
	View.HSplitBottom(10.0f, &View, 0); // some spacing
	View.HSplitBottom(14.0f, &View, &PathBox);
	View.HSplitBottom(5.0f, &View, 0); // some spacing
	View.HSplitBottom(14.0f, &View, &FileBox);
	FileBox.VSplitLeft(55.0f, &FileBoxLabel, &FileBox);
	View.HSplitBottom(10.0f, &View, 0); // some spacing
	if(m_FileDialogFileType == CEditor::FILETYPE_IMG)
		View.VSplitMid(&View, &Preview);
	View.VSplitRight(15.0f, &View, &Scroll);

	// title
	RenderTools()->DrawUIRect(&Title, ColorRGBA(1, 1, 1, 0.25f), CUI::CORNER_ALL, 4.0f);
	Title.VMargin(10.0f, &Title);
	UI()->DoLabel(&Title, m_pFileDialogTitle, 12.0f, -1, -1);

	// pathbox
	char aPath[128], aBuf[128];
	if(m_FilesSelectedIndex != -1)
		Storage()->GetCompletePath(m_FileList[m_FilesSelectedIndex].m_StorageType, m_pFileDialogPath, aPath, sizeof(aPath));
	else
		aPath[0] = 0;
	str_format(aBuf, sizeof(aBuf), "Current path: %s", aPath);
	UI()->DoLabel(&PathBox, aBuf, 10.0f, -1, -1);

	if(m_FileDialogStorageType == IStorage::TYPE_SAVE)
	{
		// filebox
		static float s_FileBoxID = 0;
		UI()->DoLabel(&FileBoxLabel, "Filename:", 10.0f, -1, -1);
		if(DoEditBox(&s_FileBoxID, &FileBox, m_aFileDialogFileName, sizeof(m_aFileDialogFileName), 10.0f, &s_FileBoxID))
		{
			// remove '/' and '\'
			for(int i = 0; m_aFileDialogFileName[i]; ++i)
				if(m_aFileDialogFileName[i] == '/' || m_aFileDialogFileName[i] == '\\')
					str_copy(&m_aFileDialogFileName[i], &m_aFileDialogFileName[i + 1], (int)(sizeof(m_aFileDialogFileName)) - i);
			m_FilesSelectedIndex = -1;
		}
	}
	else
	{
		//searchbox
		FileBox.VSplitRight(250, &FileBox, 0);
		CUIRect ClearBox;
		FileBox.VSplitRight(15, &FileBox, &ClearBox);

		static float s_SearchBoxID = 0;
		UI()->DoLabel(&FileBoxLabel, "Search:", 10.0f, -1, -1);
		str_copy(m_aFileDialogPrevSearchText, m_aFileDialogSearchText, sizeof(m_aFileDialogPrevSearchText));
		DoEditBox(&s_SearchBoxID, &FileBox, m_aFileDialogSearchText, sizeof(m_aFileDialogSearchText), 10.0f, &s_SearchBoxID, false, CUI::CORNER_L);
		if(m_FileDialogOpening)
			UI()->SetActiveItem(&s_SearchBoxID);

		// clearSearchbox button
		{
			static int s_ClearButton = 0;
			RenderTools()->DrawUIRect(&ClearBox, ColorRGBA(1, 1, 1, 0.33f * ButtonColorMul(&s_ClearButton)), CUI::CORNER_R, 3.0f);
			UI()->DoLabel(&ClearBox, "×", 10.0f, 0);
			if(UI()->DoButtonLogic(&s_ClearButton, "×", 0, &ClearBox))
			{
				m_aFileDialogSearchText[0] = 0;
				UI()->SetActiveItem(&s_SearchBoxID);
			}
		}

		if(str_comp(m_aFileDialogPrevSearchText, m_aFileDialogSearchText))
			m_FileDialogScrollValue = 0.0f;
	}

	m_FileDialogOpening = false;

	int Num = (int)(View.h / 17.0f) + 1;
	static int ScrollBar = 0;
	Scroll.HMargin(5.0f, &Scroll);
	m_FileDialogScrollValue = UiDoScrollbarV(&ScrollBar, &Scroll, m_FileDialogScrollValue);

	int ScrollNum = 0;
	for(int i = 0; i < m_FileList.size(); i++)
	{
		m_FileList[i].m_IsVisible = false;
		if(!m_aFileDialogSearchText[0] || str_utf8_find_nocase(m_FileList[i].m_aName, m_aFileDialogSearchText))
		{
			AddFileDialogEntry(i, &View);
			m_FileList[i].m_IsVisible = true;
			ScrollNum++;
		}
	}
	ScrollNum -= Num - 1;

	if(ScrollNum > 0)
	{
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			m_FileDialogScrollValue -= 3.0f / ScrollNum;
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			m_FileDialogScrollValue += 3.0f / ScrollNum;
	}
	else
		ScrollNum = 0;

	if(m_FilesSelectedIndex > -1)
	{
		if(!m_FileList[m_FilesSelectedIndex].m_IsVisible)
		{
			m_FilesSelectedIndex = 0;
		}

		for(int i = 0; i < Input()->NumEvents(); i++)
		{
			int NewIndex = -1;
			if(Input()->GetEvent(i).m_Flags & IInput::FLAG_PRESS)
			{
				if(Input()->GetEvent(i).m_Key == KEY_DOWN)
				{
					for(NewIndex = m_FilesSelectedIndex + 1; NewIndex < m_FileList.size(); NewIndex++)
					{
						if(m_FileList[NewIndex].m_IsVisible)
							break;
					}
				}
				if(Input()->GetEvent(i).m_Key == KEY_UP)
				{
					for(NewIndex = m_FilesSelectedIndex - 1; NewIndex >= 0; NewIndex--)
					{
						if(m_FileList[NewIndex].m_IsVisible)
							break;
					}
				}
			}
			if(NewIndex > -1 && NewIndex < m_FileList.size())
			{
				//scroll
				float IndexY = View.y - m_FileDialogScrollValue * ScrollNum * 17.0f + NewIndex * 17.0f;
				int Scroll = View.y > IndexY ? -1 : View.y + View.h < IndexY + 17.0f ? 1 : 0;
				if(Scroll)
				{
					if(Scroll < 0)
						m_FileDialogScrollValue = ((float)(NewIndex) + 0.5f) / ScrollNum;
					else
						m_FileDialogScrollValue = ((float)(NewIndex - Num) + 2.5f) / ScrollNum;
				}

				if(!m_FileList[NewIndex].m_IsDir)
					str_copy(m_aFileDialogFileName, m_FileList[NewIndex].m_aFilename, sizeof(m_aFileDialogFileName));
				else
					m_aFileDialogFileName[0] = 0;
				m_FilesSelectedIndex = NewIndex;
				m_PreviewImageIsLoaded = false;
			}
		}

		if(m_FileDialogFileType == CEditor::FILETYPE_IMG && !m_PreviewImageIsLoaded && m_FilesSelectedIndex > -1)
		{
			if(str_endswith(m_FileList[m_FilesSelectedIndex].m_aFilename, ".png"))
			{
				char aBuffer[1024];
				str_format(aBuffer, sizeof(aBuffer), "%s/%s", m_pFileDialogPath, m_FileList[m_FilesSelectedIndex].m_aFilename);

				if(Graphics()->LoadPNG(&m_FilePreviewImageInfo, aBuffer, IStorage::TYPE_ALL))
				{
					m_FilePreviewImage = Graphics()->LoadTextureRaw(m_FilePreviewImageInfo.m_Width, m_FilePreviewImageInfo.m_Height, m_FilePreviewImageInfo.m_Format, m_FilePreviewImageInfo.m_pData, m_FilePreviewImageInfo.m_Format, 0);
					CImageInfo DummyInfo = m_FilePreviewImageInfo;
					m_FilePreviewImageInfo.m_pData = NULL;
					Graphics()->FreePNG(&DummyInfo);
					m_PreviewImageIsLoaded = true;
				}
			}
		}
		if(m_PreviewImageIsLoaded)
		{
			int w = m_FilePreviewImageInfo.m_Width;
			int h = m_FilePreviewImageInfo.m_Height;
			if(m_FilePreviewImageInfo.m_Width > Preview.w)
			{
				h = m_FilePreviewImageInfo.m_Height * Preview.w / m_FilePreviewImageInfo.m_Width;
				w = Preview.w;
			}
			if(h > Preview.h)
			{
				w = w * Preview.h / h,
				h = Preview.h;
			}

			Graphics()->TextureSet(m_FilePreviewImage);
			Graphics()->BlendNormal();
			Graphics()->QuadsBegin();
			IGraphics::CQuadItem QuadItem(Preview.x, Preview.y, w, h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}
	}

	for(int i = 0; i < Input()->NumEvents(); i++)
	{
		if(Input()->GetEvent(i).m_Flags & IInput::FLAG_PRESS)
		{
			if(Input()->GetEvent(i).m_Key == KEY_RETURN || Input()->GetEvent(i).m_Key == KEY_KP_ENTER)
				m_FileDialogActivate = true;
		}
	}

	if(m_FileDialogScrollValue < 0)
		m_FileDialogScrollValue = 0;
	if(m_FileDialogScrollValue > 1)
		m_FileDialogScrollValue = 1;

	m_FilesStartAt = (int)(ScrollNum * m_FileDialogScrollValue);
	if(m_FilesStartAt < 0)
		m_FilesStartAt = 0;

	m_FilesStopAt = m_FilesStartAt + Num;

	m_FilesCur = 0;

	// set clipping
	UI()->ClipEnable(&View);

	// disable clipping again
	UI()->ClipDisable();

	// the buttons
	static int s_OkButton = 0;
	static int s_CancelButton = 0;
	static int s_NewFolderButton = 0;
	static int s_MapInfoButton = 0;

	CUIRect Button;
	ButtonBar.VSplitRight(50.0f, &ButtonBar, &Button);
	bool IsDir = m_FilesSelectedIndex >= 0 && m_FileList[m_FilesSelectedIndex].m_IsDir;
	if(DoButton_Editor(&s_OkButton, IsDir ? "Open" : m_pFileDialogButtonText, 0, &Button, 0, 0) || m_FileDialogActivate)
	{
		m_FileDialogActivate = false;
		if(IsDir) // folder
		{
			if(str_comp(m_FileList[m_FilesSelectedIndex].m_aFilename, "..") == 0) // parent folder
			{
				if(fs_parent_dir(m_pFileDialogPath))
					m_pFileDialogPath = m_aFileDialogCurrentFolder; // leave the link
			}
			else // sub folder
			{
				if(m_FileList[m_FilesSelectedIndex].m_IsLink)
				{
					m_pFileDialogPath = m_aFileDialogCurrentLink; // follow the link
					str_copy(m_aFileDialogCurrentLink, m_FileList[m_FilesSelectedIndex].m_aFilename, sizeof(m_aFileDialogCurrentLink));
				}
				else
				{
					char aTemp[IO_MAX_PATH_LENGTH];
					str_copy(aTemp, m_pFileDialogPath, sizeof(aTemp));
					str_format(m_pFileDialogPath, IO_MAX_PATH_LENGTH, "%s/%s", aTemp, m_FileList[m_FilesSelectedIndex].m_aFilename);
				}
			}
			FilelistPopulate(!str_comp(m_pFileDialogPath, "maps") || !str_comp(m_pFileDialogPath, "mapres") ? m_FileDialogStorageType :
															  m_FileList[m_FilesSelectedIndex].m_StorageType);
			if(m_FilesSelectedIndex >= 0 && !m_FileList[m_FilesSelectedIndex].m_IsDir)
				str_copy(m_aFileDialogFileName, m_FileList[m_FilesSelectedIndex].m_aFilename, sizeof(m_aFileDialogFileName));
			else
				m_aFileDialogFileName[0] = 0;
		}
		else // file
		{
			str_format(m_aFileSaveName, sizeof(m_aFileSaveName), "%s/%s", m_pFileDialogPath, m_aFileDialogFileName);
			if(!str_comp(m_pFileDialogButtonText, "Save"))
			{
				IOHANDLE File = Storage()->OpenFile(m_aFileSaveName, IOFLAG_READ, IStorage::TYPE_SAVE);
				if(File)
				{
					io_close(File);
					m_PopupEventType = POPEVENT_SAVE;
					m_PopupEventActivated = true;
				}
				else if(m_pfnFileDialogFunc)
					m_pfnFileDialogFunc(m_aFileSaveName, m_FilesSelectedIndex >= 0 ? m_FileList[m_FilesSelectedIndex].m_StorageType : m_FileDialogStorageType, m_pFileDialogUser);
			}
			else if(m_pfnFileDialogFunc)
				m_pfnFileDialogFunc(m_aFileSaveName, m_FilesSelectedIndex >= 0 ? m_FileList[m_FilesSelectedIndex].m_StorageType : m_FileDialogStorageType, m_pFileDialogUser);
		}
	}

	ButtonBar.VSplitRight(40.0f, &ButtonBar, &Button);
	ButtonBar.VSplitRight(50.0f, &ButtonBar, &Button);
	if(DoButton_Editor(&s_CancelButton, "Cancel", 0, &Button, 0, 0) || Input()->KeyIsPressed(KEY_ESCAPE))
		m_Dialog = DIALOG_NONE;

	if(m_FileDialogStorageType == IStorage::TYPE_SAVE)
	{
		ButtonBar.VSplitLeft(70.0f, &Button, &ButtonBar);
		if(DoButton_Editor(&s_NewFolderButton, "New folder", 0, &Button, 0, 0))
		{
			m_FileDialogNewFolderName[0] = 0;
			m_FileDialogErrString[0] = 0;
			static int s_NewFolderPopupID = 0;
			UiInvokePopupMenu(&s_NewFolderPopupID, 0, Width / 2.0f - 200.0f, Height / 2.0f - 100.0f, 400.0f, 200.0f, PopupNewFolder);
			UI()->SetActiveItem(0);
		}
	}

	if(m_FileDialogStorageType == IStorage::TYPE_SAVE)
	{
		ButtonBar.VSplitLeft(40.0f, 0, &ButtonBar);
		ButtonBar.VSplitLeft(70.0f, &Button, &ButtonBar);
		if(DoButton_Editor(&s_MapInfoButton, "Map details", 0, &Button, 0, 0))
		{
			str_copy(m_Map.m_MapInfo.m_aAuthorTmp, m_Map.m_MapInfo.m_aAuthor, sizeof(m_Map.m_MapInfo.m_aAuthorTmp));
			str_copy(m_Map.m_MapInfo.m_aVersionTmp, m_Map.m_MapInfo.m_aVersion, sizeof(m_Map.m_MapInfo.m_aVersionTmp));
			str_copy(m_Map.m_MapInfo.m_aCreditsTmp, m_Map.m_MapInfo.m_aCredits, sizeof(m_Map.m_MapInfo.m_aCreditsTmp));
			str_copy(m_Map.m_MapInfo.m_aLicenseTmp, m_Map.m_MapInfo.m_aLicense, sizeof(m_Map.m_MapInfo.m_aLicenseTmp));
			static int s_MapInfoPopupID = 0;
			UiInvokePopupMenu(&s_MapInfoPopupID, 0, Width / 2.0f - 200.0f, Height / 2.0f - 100.0f, 400.0f, 200.0f, PopupMapInfo);
			UI()->SetActiveItem(0);
		}
	}
}

void CEditor::FilelistPopulate(int StorageType)
{
	m_FileList.clear();
	if(m_FileDialogStorageType != IStorage::TYPE_SAVE && !str_comp(m_pFileDialogPath, "maps"))
	{
		CFilelistItem Item;
		str_copy(Item.m_aFilename, "downloadedmaps", sizeof(Item.m_aFilename));
		str_copy(Item.m_aName, "downloadedmaps/", sizeof(Item.m_aName));
		Item.m_IsDir = true;
		Item.m_IsLink = true;
		Item.m_StorageType = IStorage::TYPE_SAVE;
		m_FileList.add(Item);
	}
	Storage()->ListDirectory(StorageType, m_pFileDialogPath, EditorListdirCallback, this);
	m_FilesSelectedIndex = m_FileList.size() ? 0 : -1;
	m_PreviewImageIsLoaded = false;
	m_FileDialogActivate = false;

	if(m_FilesSelectedIndex >= 0 && !m_FileList[m_FilesSelectedIndex].m_IsDir)
		str_copy(m_aFileDialogFileName, m_FileList[m_FilesSelectedIndex].m_aFilename, sizeof(m_aFileDialogFileName));
	else
		m_aFileDialogFileName[0] = 0;
}

void CEditor::InvokeFileDialog(int StorageType, int FileType, const char *pTitle, const char *pButtonText,
	const char *pBasePath, const char *pDefaultName,
	void (*pfnFunc)(const char *pFileName, int StorageType, void *pUser), void *pUser)
{
	m_FileDialogStorageType = StorageType;
	m_pFileDialogTitle = pTitle;
	m_pFileDialogButtonText = pButtonText;
	m_pfnFileDialogFunc = pfnFunc;
	m_pFileDialogUser = pUser;
	m_aFileDialogFileName[0] = 0;
	m_aFileDialogSearchText[0] = 0;
	m_aFileDialogCurrentFolder[0] = 0;
	m_aFileDialogCurrentLink[0] = 0;
	m_pFileDialogPath = m_aFileDialogCurrentFolder;
	m_FileDialogFileType = FileType;
	m_FileDialogScrollValue = 0.0f;
	m_PreviewImageIsLoaded = false;
	m_FileDialogOpening = true;

	if(pDefaultName)
		str_copy(m_aFileDialogFileName, pDefaultName, sizeof(m_aFileDialogFileName));
	if(pBasePath)
		str_copy(m_aFileDialogCurrentFolder, pBasePath, sizeof(m_aFileDialogCurrentFolder));

	FilelistPopulate(m_FileDialogStorageType);

	m_FileDialogOpening = true;
	m_Dialog = DIALOG_FILE;
}

void CEditor::RenderModebar(CUIRect View)
{
	CUIRect Button;

	// mode buttons
	{
		View.VSplitLeft(65.0f, &Button, &View);
		Button.HSplitTop(30.0f, 0, &Button);
		static int s_Button = 0;
		const char *pButName = "";

		if(m_Mode == MODE_LAYERS)
			pButName = "Layers";
		else if(m_Mode == MODE_IMAGES)
			pButName = "Images";
		else if(m_Mode == MODE_SOUNDS)
			pButName = "Sounds";

		int MouseButton = DoButton_Tab(&s_Button, pButName, 0, &Button, 0, "Switch between images, sounds and layers management.");
		if(MouseButton == 2 || (Input()->KeyPress(KEY_LEFT) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
		{
			if(m_Mode == MODE_LAYERS)
				m_Mode = MODE_SOUNDS;
			else if(m_Mode == MODE_IMAGES)
				m_Mode = MODE_LAYERS;
			else
				m_Mode = MODE_IMAGES;
		}
		else if(MouseButton == 1 || (Input()->KeyPress(KEY_RIGHT) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0))
		{
			if(m_Mode == MODE_LAYERS)
				m_Mode = MODE_IMAGES;
			else if(m_Mode == MODE_IMAGES)
				m_Mode = MODE_SOUNDS;
			else
				m_Mode = MODE_LAYERS;
		}
	}

	View.VSplitLeft(5.0f, 0, &View);
}

void CEditor::RenderStatusbar(CUIRect View)
{
	CUIRect Button;
	View.VSplitRight(60.0f, &View, &Button);
	static int s_EnvelopeButton = 0;
	int MouseButton = DoButton_Editor(&s_EnvelopeButton, "Envelopes", m_ShowEnvelopeEditor, &Button, 0, "Toggles the envelope editor.");
	if(MouseButton == 2)
		m_ShowEnvelopeEditor = (m_ShowEnvelopeEditor + 3) % 4;
	else if(MouseButton == 1)
		m_ShowEnvelopeEditor = (m_ShowEnvelopeEditor + 1) % 4;

	if(MouseButton)
	{
		m_ShowServerSettingsEditor = false;
	}

	View.VSplitRight(100.0f, &View, &Button);
	Button.VSplitRight(10.0f, &Button, 0);
	static int s_SettingsButton = 0;
	if(DoButton_Editor(&s_SettingsButton, "Server settings", m_ShowServerSettingsEditor, &Button, 0, "Toggles the server settings editor."))
	{
		m_ShowEnvelopeEditor = 0;
		m_ShowServerSettingsEditor ^= 1;
	}

	if(g_Config.m_ClEditorUndo)
	{
		View.VSplitRight(5.0f, &View, &Button);
		View.VSplitRight(60.0f, &View, &Button);
		static int s_UndolistButton = 0;
		if(DoButton_Editor(&s_UndolistButton, "Undolist", m_ShowUndo, &Button, 0, "Toggles the undo list."))
			m_ShowUndo = (m_ShowUndo + 1) % 2;
	}

	if(m_pTooltip)
	{
		char aBuf[512];
		if(ms_pUiGotContext && ms_pUiGotContext == UI()->HotItem())
			str_format(aBuf, sizeof(aBuf), "%s Right click for context menu.", m_pTooltip);
		else
			str_copy(aBuf, m_pTooltip, sizeof(aBuf));

		float FontSize = ScaleFontSize(aBuf, sizeof(aBuf), 10.0f, View.w);
		UI()->DoLabel(&View, aBuf, FontSize, -1, View.w);
	}
}

void CEditor::RenderUndoList(CUIRect View)
{
	CUIRect List, Preview, Scroll, Button;
	View.VSplitMid(&List, &Preview);
	List.VSplitRight(15.0f, &List, &Scroll);
	//int Num = (int)(List.h/17.0f)+1;
	static int ScrollBar = 0;
	Scroll.HMargin(5.0f, &Scroll);
	m_UndoScrollValue = UiDoScrollbarV(&ScrollBar, &Scroll, m_UndoScrollValue);

	float TopY = List.y;
	float Height = List.h;
	UI()->ClipEnable(&List);
	int ClickedIndex = -1;
	int HoveredIndex = -1;
	int ScrollNum = m_lUndoSteps.size() - List.h / 17.0f;
	if(ScrollNum < 0)
		ScrollNum = 0;
	List.y -= m_UndoScrollValue * ScrollNum * 17.0f;
	for(int i = 0; i < m_lUndoSteps.size(); i++)
	{
		List.HSplitTop(17.0f, &Button, &List);
		if(List.y < TopY)
			continue;
		if(List.y - 17.0f > TopY + Height)
			break;
		if(DoButton_Editor(&m_lUndoSteps[i].m_ButtonId, m_lUndoSteps[i].m_aName, 0, &Button, 0, "Undo to this step"))
			ClickedIndex = i;
		if(UI()->HotItem() == &m_lUndoSteps[i].m_ButtonId)
			HoveredIndex = i;
	}
	UI()->ClipDisable();
	if(ClickedIndex != -1)
	{
		char aBuffer[1024];
		str_format(aBuffer, sizeof(aBuffer), "editor/undo_%i", m_lUndoSteps[HoveredIndex].m_FileNum);
		m_Map.Load(m_pStorage, aBuffer, IStorage::TYPE_SAVE);
		m_Map.m_UndoModified = 0;
		m_LastUndoUpdateTime = time_get();
	}
	if(HoveredIndex != -1)
	{
		if(!m_lUndoSteps[HoveredIndex].m_PreviewImageIsLoaded)
		{
			char aBuffer[1024];
			str_format(aBuffer, sizeof(aBuffer), "editor/undo_%i.png", m_lUndoSteps[HoveredIndex].m_FileNum);
			m_lUndoSteps[HoveredIndex].m_PreviewImage = Graphics()->LoadTexture(aBuffer, IStorage::TYPE_SAVE, CImageInfo::FORMAT_RGB, 0);
			m_lUndoSteps[HoveredIndex].m_PreviewImageIsLoaded = true;
		}
		if(m_lUndoSteps[HoveredIndex].m_PreviewImageIsLoaded)
		{
			Graphics()->TextureSet(m_lUndoSteps[HoveredIndex].m_PreviewImage);
			Graphics()->BlendNormal();
			Graphics()->QuadsBegin();
			IGraphics::CQuadItem QuadItem(Preview.x, Preview.y, Preview.w, Preview.h);
			Graphics()->QuadsDrawTL(&QuadItem, 1);
			Graphics()->QuadsEnd();
		}
	}
}

bool CEditor::IsEnvelopeUsed(int EnvelopeIndex) const
{
	for(int i = 0; i < m_Map.m_lGroups.size(); i++)
	{
		for(int j = 0; j < m_Map.m_lGroups[i]->m_lLayers.size(); j++)
		{
			if(m_Map.m_lGroups[i]->m_lLayers[j]->m_Type == LAYERTYPE_QUADS)
			{
				CLayerQuads *pQuadLayer = (CLayerQuads *)m_Map.m_lGroups[i]->m_lLayers[j];
				for(int k = 0; k < pQuadLayer->m_lQuads.size(); k++)
				{
					if(pQuadLayer->m_lQuads[k].m_PosEnv == EnvelopeIndex || pQuadLayer->m_lQuads[k].m_ColorEnv == EnvelopeIndex)
					{
						return true;
					}
				}
			}
			else if(m_Map.m_lGroups[i]->m_lLayers[j]->m_Type == LAYERTYPE_SOUNDS)
			{
				CLayerSounds *pSoundLayer = (CLayerSounds *)m_Map.m_lGroups[i]->m_lLayers[j];
				for(int k = 0; k < pSoundLayer->m_lSources.size(); k++)
				{
					if(pSoundLayer->m_lSources[k].m_PosEnv == EnvelopeIndex || pSoundLayer->m_lSources[k].m_SoundEnv == EnvelopeIndex)
					{
						return true;
					}
				}
			}
			else if(m_Map.m_lGroups[i]->m_lLayers[j]->m_Type == LAYERTYPE_TILES)
			{
				CLayerTiles *pTileLayer = (CLayerTiles *)m_Map.m_lGroups[i]->m_lLayers[j];
				if(pTileLayer->m_ColorEnv == EnvelopeIndex)
					return true;
			}
		}
	}
	return false;
}

void CEditor::RenderEnvelopeEditor(CUIRect View)
{
	if(m_SelectedEnvelope < 0)
		m_SelectedEnvelope = 0;
	if(m_SelectedEnvelope >= m_Map.m_lEnvelopes.size())
		m_SelectedEnvelope = m_Map.m_lEnvelopes.size() - 1;

	CEnvelope *pEnvelope = 0;
	if(m_SelectedEnvelope >= 0 && m_SelectedEnvelope < m_Map.m_lEnvelopes.size())
		pEnvelope = m_Map.m_lEnvelopes[m_SelectedEnvelope];

	CUIRect ToolBar, CurveBar, ColorBar;
	View.HSplitTop(15.0f, &ToolBar, &View);
	View.HSplitTop(15.0f, &CurveBar, &View);
	ToolBar.Margin(2.0f, &ToolBar);
	CurveBar.Margin(2.0f, &CurveBar);

	bool CurrentEnvelopeSwitched = false;

	// do the toolbar
	{
		CUIRect Button;
		CEnvelope *pNewEnv = 0;

		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		static int s_NewSoundButton = 0;
		if(DoButton_Editor(&s_NewSoundButton, "Sound+", 0, &Button, 0, "Creates a new sound envelope"))
		{
			m_Map.m_Modified = true;
			m_Map.m_UndoModified++;
			pNewEnv = m_Map.NewEnvelope(1);
		}

		ToolBar.VSplitRight(5.0f, &ToolBar, &Button);
		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		static int s_New4dButton = 0;
		if(DoButton_Editor(&s_New4dButton, "Color+", 0, &Button, 0, "Creates a new color envelope"))
		{
			m_Map.m_Modified = true;
			m_Map.m_UndoModified++;
			pNewEnv = m_Map.NewEnvelope(4);
		}

		ToolBar.VSplitRight(5.0f, &ToolBar, &Button);
		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		static int s_New2dButton = 0;
		if(DoButton_Editor(&s_New2dButton, "Pos.+", 0, &Button, 0, "Creates a new position envelope"))
		{
			m_Map.m_Modified = true;
			m_Map.m_UndoModified++;
			pNewEnv = m_Map.NewEnvelope(3);
		}

		// Delete button
		if(m_SelectedEnvelope >= 0)
		{
			ToolBar.VSplitRight(10.0f, &ToolBar, &Button);
			ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
			static int s_DelButton = 0;
			if(DoButton_Editor(&s_DelButton, "Delete", 0, &Button, 0, "Delete this envelope"))
			{
				m_Map.m_Modified = true;
				m_Map.m_UndoModified++;
				m_Map.DeleteEnvelope(m_SelectedEnvelope);
				if(m_SelectedEnvelope >= m_Map.m_lEnvelopes.size())
					m_SelectedEnvelope = m_Map.m_lEnvelopes.size() - 1;
				pEnvelope = m_SelectedEnvelope >= 0 ? m_Map.m_lEnvelopes[m_SelectedEnvelope] : 0;
			}
		}

		if(pNewEnv) // add the default points
		{
			if(pNewEnv->m_Channels == 4)
			{
				pNewEnv->AddPoint(0, f2fx(1.0f), f2fx(1.0f), f2fx(1.0f), f2fx(1.0f));
				pNewEnv->AddPoint(1000, f2fx(1.0f), f2fx(1.0f), f2fx(1.0f), f2fx(1.0f));
			}
			else
			{
				pNewEnv->AddPoint(0, 0);
				pNewEnv->AddPoint(1000, 0);
			}
		}

		CUIRect Shifter, Inc, Dec;
		ToolBar.VSplitLeft(60.0f, &Shifter, &ToolBar);
		Shifter.VSplitRight(15.0f, &Shifter, &Inc);
		Shifter.VSplitLeft(15.0f, &Dec, &Shifter);
		char aBuf[512];
		str_format(aBuf, sizeof(aBuf), "%d/%d", m_SelectedEnvelope + 1, m_Map.m_lEnvelopes.size());

		ColorRGBA EnvColor = ColorRGBA(1, 1, 1, 0.5f);
		if(m_Map.m_lEnvelopes.size())
		{
			EnvColor = IsEnvelopeUsed(m_SelectedEnvelope) ?
					   ColorRGBA(1, 0.7f, 0.7f, 0.5f) :
					   ColorRGBA(0.7f, 1, 0.7f, 0.5f);
		}

		RenderTools()->DrawUIRect(&Shifter, EnvColor, 0, 0.0f);
		UI()->DoLabel(&Shifter, aBuf, 10.0f, 0, -1);

		static int s_PrevButton = 0;
		if(DoButton_ButtonDec(&s_PrevButton, 0, 0, &Dec, 0, "Previous Envelope"))
		{
			m_SelectedEnvelope--;
			if(m_SelectedEnvelope < 0)
				m_SelectedEnvelope = m_Map.m_lEnvelopes.size() - 1;
			CurrentEnvelopeSwitched = true;
		}

		static int s_NextButton = 0;
		if(DoButton_ButtonInc(&s_NextButton, 0, 0, &Inc, 0, "Next Envelope"))
		{
			m_SelectedEnvelope++;
			if(m_SelectedEnvelope >= m_Map.m_lEnvelopes.size())
				m_SelectedEnvelope = 0;
			CurrentEnvelopeSwitched = true;
		}

		if(pEnvelope)
		{
			ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);
			ToolBar.VSplitLeft(35.0f, &Button, &ToolBar);
			UI()->DoLabel(&Button, "Name:", 10.0f, -1, -1);

			ToolBar.VSplitLeft(80.0f, &Button, &ToolBar);

			static float s_NameBox = 0;
			if(DoEditBox(&s_NameBox, &Button, pEnvelope->m_aName, sizeof(pEnvelope->m_aName), 10.0f, &s_NameBox))
			{
				m_Map.m_Modified = true;
				m_Map.m_UndoModified++;
			}
		}
	}

	bool ShowColorBar = false;
	if(pEnvelope && pEnvelope->m_Channels == 4)
	{
		ShowColorBar = true;
		View.HSplitTop(20.0f, &ColorBar, &View);
		ColorBar.Margin(2.0f, &ColorBar);
		RenderBackground(ColorBar, m_CheckerTexture, 16.0f, 1.0f);
	}

	RenderBackground(View, m_CheckerTexture, 32.0f, 0.1f);

	if(pEnvelope)
	{
		static array<int> Selection;
		static int sEnvelopeEditorID = 0;
		static int s_ActiveChannels = 0xf;

		ColorRGBA aColors[] = {ColorRGBA(1, 0.2f, 0.2f), ColorRGBA(0.2f, 1, 0.2f), ColorRGBA(0.2f, 0.2f, 1), ColorRGBA(1, 1, 0.2f)};

		if(pEnvelope)
		{
			CUIRect Button;

			ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);

			static const char *s_paNames[4][4] = {
				{"V", "", "", ""},
				{"", "", "", ""},
				{"X", "Y", "R", ""},
				{"R", "G", "B", "A"},
			};

			const char *paDescriptions[4][4] = {
				{"Volume of the envelope", "", "", ""},
				{"", "", "", ""},
				{"X-axis of the envelope", "Y-axis of the envelope", "Rotation of the envelope", ""},
				{"Red value of the envelope", "Green value of the envelope", "Blue value of the envelope", "Alpha value of the envelope"},
			};

			static int s_aChannelButtons[4] = {0};
			int Bit = 1;
			//ui_draw_button_func draw_func;

			for(int i = 0; i < pEnvelope->m_Channels; i++, Bit <<= 1)
			{
				ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);

				/*if(i == 0) draw_func = draw_editor_button_l;
				else if(i == envelope->channels-1) draw_func = draw_editor_button_r;
				else draw_func = draw_editor_button_m;*/
				if(DoButton_Env(&s_aChannelButtons[i], s_paNames[pEnvelope->m_Channels - 1][i], s_ActiveChannels & Bit, &Button, paDescriptions[pEnvelope->m_Channels - 1][i], aColors[i]))
					s_ActiveChannels ^= Bit;
			}

			// sync checkbox
			ToolBar.VSplitLeft(15.0f, &Button, &ToolBar);
			ToolBar.VSplitLeft(12.0f, &Button, &ToolBar);
			static int s_SyncButton;
			if(DoButton_Editor(&s_SyncButton, pEnvelope->m_Synchronized ? "X" : "", 0, &Button, 0, "Synchronize envelope animation to game time (restarts when you touch the start line)"))
				pEnvelope->m_Synchronized = !pEnvelope->m_Synchronized;

			ToolBar.VSplitLeft(4.0f, &Button, &ToolBar);
			ToolBar.VSplitLeft(80.0f, &Button, &ToolBar);
			UI()->DoLabel(&Button, "Synchronized", 10.0f, -1, -1);
		}

		float EndTime = pEnvelope->EndTime();
		if(EndTime < 1)
			EndTime = 1;

		pEnvelope->FindTopBottom(s_ActiveChannels);
		float Top = pEnvelope->m_Top;
		float Bottom = pEnvelope->m_Bottom;

		if(Top < 1)
			Top = 1;
		if(Bottom >= 0)
			Bottom = 0;

		float TimeScale = EndTime / View.w;
		float ValueScale = (Top - Bottom) / View.h;

		if(UI()->MouseInside(&View))
			UI()->SetHotItem(&sEnvelopeEditorID);

		if(UI()->HotItem() == &sEnvelopeEditorID)
		{
			// do stuff
			if(pEnvelope)
			{
				if(UI()->MouseButtonClicked(1))
				{
					// add point
					int Time = (int)(((UI()->MouseX() - View.x) * TimeScale) * 1000.0f);
					//float env_y = (UI()->MouseY()-view.y)/TimeScale;
					float aChannels[4];
					pEnvelope->Eval(Time / 1000.0f, aChannels);
					pEnvelope->AddPoint(Time,
						f2fx(aChannels[0]), f2fx(aChannels[1]),
						f2fx(aChannels[2]), f2fx(aChannels[3]));
					m_Map.m_Modified = true;
					m_Map.m_UndoModified++;
				}

				m_ShowEnvelopePreview = 1;
				m_pTooltip = "Press right mouse button to create a new point";
			}
		}

		// render lines
		{
			UI()->ClipEnable(&View);
			Graphics()->TextureClear();
			Graphics()->LinesBegin();
			for(int c = 0; c < pEnvelope->m_Channels; c++)
			{
				if(s_ActiveChannels & (1 << c))
					Graphics()->SetColor(aColors[c].r, aColors[c].g, aColors[c].b, 1);
				else
					Graphics()->SetColor(aColors[c].r * 0.5f, aColors[c].g * 0.5f, aColors[c].b * 0.5f, 1);

				float PrevX = 0;
				float aResults[4];
				pEnvelope->Eval(0.000001f, aResults);
				float PrevValue = aResults[c];

				int Steps = (int)((View.w / UI()->Screen()->w) * Graphics()->ScreenWidth());
				for(int i = 1; i <= Steps; i++)
				{
					float a = i / (float)Steps;
					pEnvelope->Eval(a * EndTime, aResults);
					float v = aResults[c];
					v = (v - Bottom) / (Top - Bottom);

					IGraphics::CLineItem LineItem(View.x + PrevX * View.w, View.y + View.h - PrevValue * View.h, View.x + a * View.w, View.y + View.h - v * View.h);
					Graphics()->LinesDraw(&LineItem, 1);
					PrevX = a;
					PrevValue = v;
				}
			}
			Graphics()->LinesEnd();
			UI()->ClipDisable();
		}

		// render curve options
		{
			for(int i = 0; i < pEnvelope->m_lPoints.size() - 1; i++)
			{
				float t0 = pEnvelope->m_lPoints[i].m_Time / 1000.0f / EndTime;
				float t1 = pEnvelope->m_lPoints[i + 1].m_Time / 1000.0f / EndTime;

				//dbg_msg("", "%f", end_time);

				CUIRect v;
				v.x = CurveBar.x + (t0 + (t1 - t0) * 0.5f) * CurveBar.w;
				v.y = CurveBar.y;
				v.h = CurveBar.h;
				v.w = CurveBar.h;
				v.x -= v.w / 2;
				void *pID = &pEnvelope->m_lPoints[i].m_Curvetype;
				const char *paTypeName[] = {
					"N", "L", "S", "F", "M"};
				const char *pTypeName = "Invalid";
				if(0 <= pEnvelope->m_lPoints[i].m_Curvetype && pEnvelope->m_lPoints[i].m_Curvetype < (int)(sizeof(paTypeName) / sizeof(const char *)))
					pTypeName = paTypeName[pEnvelope->m_lPoints[i].m_Curvetype];
				if(DoButton_Editor(pID, pTypeName, 0, &v, 0, "Switch curve type"))
					pEnvelope->m_lPoints[i].m_Curvetype = (pEnvelope->m_lPoints[i].m_Curvetype + 1) % NUM_CURVETYPES;
			}
		}

		// render colorbar
		if(ShowColorBar)
		{
			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(int i = 0; i < pEnvelope->m_lPoints.size() - 1; i++)
			{
				float r0 = fx2f(pEnvelope->m_lPoints[i].m_aValues[0]);
				float g0 = fx2f(pEnvelope->m_lPoints[i].m_aValues[1]);
				float b0 = fx2f(pEnvelope->m_lPoints[i].m_aValues[2]);
				float a0 = fx2f(pEnvelope->m_lPoints[i].m_aValues[3]);
				float r1 = fx2f(pEnvelope->m_lPoints[i + 1].m_aValues[0]);
				float g1 = fx2f(pEnvelope->m_lPoints[i + 1].m_aValues[1]);
				float b1 = fx2f(pEnvelope->m_lPoints[i + 1].m_aValues[2]);
				float a1 = fx2f(pEnvelope->m_lPoints[i + 1].m_aValues[3]);

				IGraphics::CColorVertex Array[4] = {IGraphics::CColorVertex(0, r0, g0, b0, a0),
					IGraphics::CColorVertex(1, r1, g1, b1, a1),
					IGraphics::CColorVertex(2, r1, g1, b1, a1),
					IGraphics::CColorVertex(3, r0, g0, b0, a0)};
				Graphics()->SetColorVertex(Array, 4);

				float x0 = pEnvelope->m_lPoints[i].m_Time / 1000.0f / EndTime;
				//				float y0 = (fx2f(envelope->points[i].values[c])-bottom)/(top-bottom);
				float x1 = pEnvelope->m_lPoints[i + 1].m_Time / 1000.0f / EndTime;
				//float y1 = (fx2f(envelope->points[i+1].values[c])-bottom)/(top-bottom);
				CUIRect v;
				v.x = ColorBar.x + x0 * ColorBar.w;
				v.y = ColorBar.y;
				v.w = (x1 - x0) * ColorBar.w;
				v.h = ColorBar.h;

				IGraphics::CQuadItem QuadItem(v.x, v.y, v.w, v.h);
				Graphics()->QuadsDrawTL(&QuadItem, 1);
			}
			Graphics()->QuadsEnd();
		}

		// render handles

		// keep track of last Env
		static void *s_pID = 0;

		// chars for textinput
		static char s_aStrCurTime[32] = "0.000";
		static char s_aStrCurValue[32] = "0.000";

		if(CurrentEnvelopeSwitched)
		{
			s_pID = 0;

			// update displayed text
			str_format(s_aStrCurTime, sizeof(s_aStrCurTime), "0.000");
			str_format(s_aStrCurValue, sizeof(s_aStrCurValue), "0.000");
		}

		{
			int CurrentValue = 0, CurrentTime = 0;

			Graphics()->TextureClear();
			Graphics()->QuadsBegin();
			for(int c = 0; c < pEnvelope->m_Channels; c++)
			{
				if(!(s_ActiveChannels & (1 << c)))
					continue;

				for(int i = 0; i < pEnvelope->m_lPoints.size(); i++)
				{
					float x0 = pEnvelope->m_lPoints[i].m_Time / 1000.0f / EndTime;
					float y0 = (fx2f(pEnvelope->m_lPoints[i].m_aValues[c]) - Bottom) / (Top - Bottom);
					CUIRect Final;
					Final.x = View.x + x0 * View.w;
					Final.y = View.y + View.h - y0 * View.h;
					Final.x -= 2.0f;
					Final.y -= 2.0f;
					Final.w = 4.0f;
					Final.h = 4.0f;

					void *pID = &pEnvelope->m_lPoints[i].m_aValues[c];

					if(UI()->MouseInside(&Final))
						UI()->SetHotItem(pID);

					float ColorMod = 1.0f;

					if(UI()->ActiveItem() == pID)
					{
						if(!UI()->MouseButton(0))
						{
							m_SelectedQuadEnvelope = -1;
							m_SelectedEnvelopePoint = -1;

							UI()->SetActiveItem(0);
						}
						else
						{
							if(Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT))
							{
								if(i != 0)
								{
									if((Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL)))
										pEnvelope->m_lPoints[i].m_Time += (int)((m_MouseDeltaX));
									else
										pEnvelope->m_lPoints[i].m_Time += (int)((m_MouseDeltaX * TimeScale) * 1000.0f);
									if(pEnvelope->m_lPoints[i].m_Time < pEnvelope->m_lPoints[i - 1].m_Time)
										pEnvelope->m_lPoints[i].m_Time = pEnvelope->m_lPoints[i - 1].m_Time + 1;
									if(i + 1 != pEnvelope->m_lPoints.size() && pEnvelope->m_lPoints[i].m_Time > pEnvelope->m_lPoints[i + 1].m_Time)
										pEnvelope->m_lPoints[i].m_Time = pEnvelope->m_lPoints[i + 1].m_Time - 1;
								}
							}
							else
							{
								if((Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL)))
									pEnvelope->m_lPoints[i].m_aValues[c] -= f2fx(m_MouseDeltaY * 0.001f);
								else
									pEnvelope->m_lPoints[i].m_aValues[c] -= f2fx(m_MouseDeltaY * ValueScale);
							}

							if(m_SelectedEnvelopePoint != i)
								m_Map.m_UndoModified++;

							m_SelectedQuadEnvelope = m_SelectedEnvelope;
							m_ShowEnvelopePreview = 1;
							m_SelectedEnvelopePoint = i;
							m_Map.m_Modified = true;
						}

						ColorMod = 100.0f;
						Graphics()->SetColor(1, 1, 1, 1);
					}
					else if(UI()->HotItem() == pID)
					{
						if(UI()->MouseButton(0))
						{
							Selection.clear();
							Selection.add(i);
							UI()->SetActiveItem(pID);
							// track it
							s_pID = pID;
						}

						// remove point
						if(UI()->MouseButtonClicked(1))
						{
							if(s_pID == pID)
							{
								s_pID = 0;

								// update displayed text
								str_format(s_aStrCurTime, sizeof(s_aStrCurTime), "0.000");
								str_format(s_aStrCurValue, sizeof(s_aStrCurValue), "0.000");
							}

							pEnvelope->m_lPoints.remove_index(i);
							m_Map.m_Modified = true;
							m_Map.m_UndoModified++;
						}

						m_ShowEnvelopePreview = 1;
						ColorMod = 100.0f;
						Graphics()->SetColor(1, 0.75f, 0.75f, 1);
						m_pTooltip = "Left mouse to drag. Hold ctrl to be more precise. Hold shift to alter time point as well. Right click to delete.";
					}

					if(pID == s_pID && (Input()->KeyIsPressed(KEY_RETURN) || Input()->KeyIsPressed(KEY_KP_ENTER)))
					{
						if(i != 0)
						{
							pEnvelope->m_lPoints[i].m_Time = str_tofloat(s_aStrCurTime) * 1000.0f;

							if(pEnvelope->m_lPoints[i].m_Time < pEnvelope->m_lPoints[i - 1].m_Time)
								pEnvelope->m_lPoints[i].m_Time = pEnvelope->m_lPoints[i - 1].m_Time + 1;
							if(i + 1 != pEnvelope->m_lPoints.size() && pEnvelope->m_lPoints[i].m_Time > pEnvelope->m_lPoints[i + 1].m_Time)
								pEnvelope->m_lPoints[i].m_Time = pEnvelope->m_lPoints[i + 1].m_Time - 1;
						}
						else
							pEnvelope->m_lPoints[i].m_Time = 0.0f;

						str_format(s_aStrCurTime, sizeof(s_aStrCurTime), "%.3f", pEnvelope->m_lPoints[i].m_Time / 1000.0f);

						pEnvelope->m_lPoints[i].m_aValues[c] = f2fx(str_tofloat(s_aStrCurValue));
					}

					if(UI()->ActiveItem() == pID /* || UI()->HotItem() == pID*/)
					{
						CurrentTime = pEnvelope->m_lPoints[i].m_Time;
						CurrentValue = pEnvelope->m_lPoints[i].m_aValues[c];

						// update displayed text
						str_format(s_aStrCurTime, sizeof(s_aStrCurTime), "%.3f", CurrentTime / 1000.0f);
						str_format(s_aStrCurValue, sizeof(s_aStrCurValue), "%.3f", fx2f(CurrentValue));
					}

					if(m_SelectedQuadEnvelope == m_SelectedEnvelope && m_SelectedEnvelopePoint == i)
						Graphics()->SetColor(1.0f, 1.0f, 1.0f, 1.0f);
					else
						Graphics()->SetColor(aColors[c].r * ColorMod, aColors[c].g * ColorMod, aColors[c].b * ColorMod, 1.0f);
					IGraphics::CQuadItem QuadItem(Final.x, Final.y, Final.w, Final.h);
					Graphics()->QuadsDrawTL(&QuadItem, 1);
				}
			}
			Graphics()->QuadsEnd();

			static float s_ValNumber = 0;
			static float s_TimeNumber = 0;

			CUIRect ToolBar1;
			CUIRect ToolBar2;

			ToolBar.VSplitMid(&ToolBar1, &ToolBar2);
			if(ToolBar.w > ToolBar.h * 21)
			{
				ToolBar1.VMargin(3.0f, &ToolBar1);
				ToolBar2.VMargin(3.0f, &ToolBar2);

				CUIRect Label1;
				CUIRect Label2;

				ToolBar1.VSplitMid(&Label1, &ToolBar1);
				ToolBar2.VSplitMid(&Label2, &ToolBar2);

				UI()->DoLabel(&Label1, "Value:", 10.0f, -1, -1);
				UI()->DoLabel(&Label2, "Time (in s):", 10.0f, -1, -1);
			}

			DoEditBox(&s_ValNumber, &ToolBar1, s_aStrCurValue, sizeof(s_aStrCurValue), 10.0f, &s_ValNumber);
			DoEditBox(&s_TimeNumber, &ToolBar2, s_aStrCurTime, sizeof(s_aStrCurTime), 10.0f, &s_TimeNumber);
		}
	}
}

void CEditor::RenderServerSettingsEditor(CUIRect View, bool ShowServerSettingsEditorLast)
{
	static int s_CommandSelectedIndex = -1;

	CUIRect ToolBar;
	View.HSplitTop(20.0f, &ToolBar, &View);
	ToolBar.Margin(2.0f, &ToolBar);

	// do the toolbar
	{
		CUIRect Button;

		// command line
		ToolBar.VSplitLeft(5.0f, 0, &Button);
		UI()->DoLabel(&Button, "Command:", 12.0f, -1);

		Button.VSplitLeft(70.0f, 0, &Button);
		Button.VSplitLeft(180.0f, &Button, 0);
		static int s_ClearButton = 0;
		DoClearableEditBox(&m_CommandBox, &s_ClearButton, &Button, m_aSettingsCommand, sizeof(m_aSettingsCommand), 12.0f, &m_CommandBox, false, CUI::CORNER_ALL);

		if(!ShowServerSettingsEditorLast) // Just activated
			UI()->SetActiveItem(&m_CommandBox);

		// buttons
		ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
		static int s_AddButton = 0;
		if(DoButton_Editor(&s_AddButton, "Add", 0, &Button, 0, "Add a command to command list.") || ((Input()->KeyPress(KEY_RETURN) || Input()->KeyPress(KEY_KP_ENTER)) && UI()->LastActiveItem() == &m_CommandBox))
		{
			if(m_aSettingsCommand[0] != 0 && str_find(m_aSettingsCommand, " "))
			{
				bool Found = false;
				for(int i = 0; i < m_Map.m_lSettings.size(); i++)
					if(!str_comp(m_Map.m_lSettings[i].m_aCommand, m_aSettingsCommand))
					{
						Found = true;
						break;
					}

				if(!Found)
				{
					CEditorMap::CSetting Setting;
					str_copy(Setting.m_aCommand, m_aSettingsCommand, sizeof(Setting.m_aCommand));
					m_Map.m_lSettings.add(Setting);
					s_CommandSelectedIndex = m_Map.m_lSettings.size() - 1;
				}
			}
			UI()->SetActiveItem(&m_CommandBox);
		}

		if(m_Map.m_lSettings.size() && s_CommandSelectedIndex > -1 && s_CommandSelectedIndex < m_Map.m_lSettings.size())
		{
			ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
			Button.VSplitRight(5.0f, &Button, 0);
			static int s_ModButton = 0;
			if(DoButton_Editor(&s_ModButton, "Mod", 0, &Button, 0, "Modify a command from the command list.") || (Input()->KeyPress(KEY_M) && UI()->LastActiveItem() != &m_CommandBox))
			{
				if(str_comp(m_Map.m_lSettings[s_CommandSelectedIndex].m_aCommand, m_aSettingsCommand) != 0 && m_aSettingsCommand[0] != 0 && str_find(m_aSettingsCommand, " "))
				{
					bool Found = false;
					int i;
					for(i = 0; i < m_Map.m_lSettings.size(); i++)
						if(i != s_CommandSelectedIndex && !str_comp(m_Map.m_lSettings[i].m_aCommand, m_aSettingsCommand))
						{
							Found = true;
							break;
						}
					if(Found)
					{
						m_Map.m_lSettings.remove_index(s_CommandSelectedIndex);
						s_CommandSelectedIndex = i > s_CommandSelectedIndex ? i - 1 : i;
					}
					else
					{
						str_copy(m_Map.m_lSettings[s_CommandSelectedIndex].m_aCommand, m_aSettingsCommand, sizeof(m_Map.m_lSettings[s_CommandSelectedIndex].m_aCommand));
					}
				}
				UI()->SetActiveItem(&m_CommandBox);
			}

			ToolBar.VSplitRight(50.0f, &ToolBar, &Button);
			Button.VSplitRight(5.0f, &Button, 0);
			static int s_DelButton = 0;
			if(DoButton_Editor(&s_DelButton, "Del", 0, &Button, 0, "Delete a command from the command list.") || (Input()->KeyPress(KEY_DELETE) && UI()->LastActiveItem() != &m_CommandBox))
			{
				m_Map.m_lSettings.remove_index(s_CommandSelectedIndex);
				if(s_CommandSelectedIndex >= m_Map.m_lSettings.size())
					s_CommandSelectedIndex = m_Map.m_lSettings.size() - 1;
				if(s_CommandSelectedIndex >= 0)
					str_copy(m_aSettingsCommand, m_Map.m_lSettings[s_CommandSelectedIndex].m_aCommand, sizeof(m_aSettingsCommand));
				UI()->SetActiveItem(&m_CommandBox);
			}

			ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
			Button.VSplitRight(5.0f, &Button, 0);
			static int s_DownButton = 0;
			if(s_CommandSelectedIndex < m_Map.m_lSettings.size() - 1 && DoButton_Editor(&s_DownButton, "▼", 0, &Button, 0, "Move command down"))
			{
				std::swap(m_Map.m_lSettings[s_CommandSelectedIndex], m_Map.m_lSettings[s_CommandSelectedIndex + 1]);
				s_CommandSelectedIndex++;
			}

			ToolBar.VSplitRight(25.0f, &ToolBar, &Button);
			Button.VSplitRight(5.0f, &Button, 0);
			static int s_UpButton = 0;
			if(s_CommandSelectedIndex > 0 && DoButton_Editor(&s_UpButton, "▲", 0, &Button, 0, "Move command up"))
			{
				std::swap(m_Map.m_lSettings[s_CommandSelectedIndex], m_Map.m_lSettings[s_CommandSelectedIndex - 1]);
				s_CommandSelectedIndex--;
			}
		}
	}

	View.HSplitTop(2.0f, 0, &View);
	RenderBackground(View, m_CheckerTexture, 32.0f, 0.1f);

	CUIRect ListBox;
	View.Margin(1.0f, &ListBox);

	float ListHeight = 17.0f * m_Map.m_lSettings.size();
	static int s_ScrollBar = 0;
	static float s_ScrollValue = 0;

	float ScrollDifference = ListHeight - ListBox.h;

	if(ListHeight > ListBox.h) // Do we even need a scrollbar?
	{
		CUIRect Scroll;
		ListBox.VSplitRight(15.0f, &ListBox, &Scroll);
		ListBox.VSplitRight(3.0f, &ListBox, 0); // extra spacing
		Scroll.HMargin(5.0f, &Scroll);
		s_ScrollValue = UiDoScrollbarV(&s_ScrollBar, &Scroll, s_ScrollValue);

		if(UI()->MouseInside(&Scroll) || UI()->MouseInside(&ListBox))
		{
			int ScrollNum = (int)((ListHeight - ListBox.h) / 17.0f) + 1;
			if(ScrollNum > 0)
			{
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
					s_ScrollValue = clamp(s_ScrollValue - 1.0f / ScrollNum, 0.0f, 1.0f);
				if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
					s_ScrollValue = clamp(s_ScrollValue + 1.0f / ScrollNum, 0.0f, 1.0f);
			}
		}
	}

	float ListStartAt = ScrollDifference * s_ScrollValue;
	if(ListStartAt < 0.0f)
		ListStartAt = 0.0f;

	float ListStopAt = ListHeight - ScrollDifference * (1 - s_ScrollValue);
	float ListCur = 0;

	UI()->ClipEnable(&ListBox);
	for(int i = 0; i < m_Map.m_lSettings.size(); i++)
	{
		if(ListCur > ListStopAt)
			break;

		if(ListCur >= ListStartAt)
		{
			CUIRect Button;
			ListBox.HSplitTop(15.0f, &Button, &ListBox);
			ListBox.HSplitTop(2.0f, 0, &ListBox);
			Button.VSplitLeft(5.0f, 0, &Button);

			if(DoButton_MenuItem(&m_Map.m_lSettings[i], m_Map.m_lSettings[i].m_aCommand, s_CommandSelectedIndex == i, &Button, 0, 0))
			{
				s_CommandSelectedIndex = i;
				str_copy(m_aSettingsCommand, m_Map.m_lSettings[i].m_aCommand, sizeof(m_aSettingsCommand));
				UI()->SetActiveItem(&m_CommandBox);
			}
		}
		ListCur += 17.0f;
	}
	UI()->ClipDisable();
}

int CEditor::PopupMenuFile(CEditor *pEditor, CUIRect View, void *pContext)
{
	static int s_NewMapButton = 0;
	static int s_SaveButton = 0;
	static int s_SaveAsButton = 0;
	static int s_SaveCopyButton = 0;
	static int s_OpenButton = 0;
	static int s_OpenCurrentMapButton = 0;
	static int s_AppendButton = 0;
	static int s_ExitButton = 0;

	CUIRect Slot;
	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_NewMapButton, "New", 0, &Slot, 0, "Creates a new map (ctrl+n)"))
	{
		if(pEditor->HasUnsavedData())
		{
			pEditor->m_PopupEventType = POPEVENT_NEW;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->Reset();
			pEditor->m_aFileName[0] = 0;
		}
		return 1;
	}

	View.HSplitTop(10.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_OpenButton, "Load", 0, &Slot, 0, "Opens a map for editing (ctrl+l)"))
	{
		if(pEditor->HasUnsavedData())
		{
			pEditor->m_PopupEventType = POPEVENT_LOAD;
			pEditor->m_PopupEventActivated = true;
		}
		else
			pEditor->InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_MAP, "Load map", "Load", "maps", "", pEditor->CallbackOpenMap, pEditor);
		return 1;
	}

	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_OpenCurrentMapButton, "Load Current Map", 0, &Slot, 0, "Opens the current in game map for editing (ctrl+alt+l)"))
	{
		if(pEditor->HasUnsavedData())
		{
			pEditor->m_PopupEventType = POPEVENT_LOADCURRENT;
			pEditor->m_PopupEventActivated = true;
		}
		else
		{
			pEditor->LoadCurrentMap();
		}
		return 1;
	}

	View.HSplitTop(10.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_AppendButton, "Append", 0, &Slot, 0, "Opens a map and adds everything from that map to the current one (ctrl+a)"))
	{
		pEditor->InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_MAP, "Append map", "Append", "maps", "", pEditor->CallbackAppendMap, pEditor);
		return 1;
	}

	View.HSplitTop(10.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveButton, "Save", 0, &Slot, 0, "Saves the current map (ctrl+s)"))
	{
		if(pEditor->m_aFileName[0] && pEditor->m_ValidSaveFilename)
		{
			str_copy(pEditor->m_aFileSaveName, pEditor->m_aFileName, sizeof(pEditor->m_aFileSaveName));
			pEditor->m_PopupEventType = POPEVENT_SAVE;
			pEditor->m_PopupEventActivated = true;
		}
		else
			pEditor->InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", pEditor->CallbackSaveMap, pEditor);
		return 1;
	}

	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveAsButton, "Save As", 0, &Slot, 0, "Saves the current map under a new name (ctrl+shift+s)"))
	{
		pEditor->InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", pEditor->CallbackSaveMap, pEditor);
		return 1;
	}

	View.HSplitTop(2.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_SaveCopyButton, "Save Copy", 0, &Slot, 0, "Saves a copy of the current map under a new name (ctrl+shift+alt+s)"))
	{
		pEditor->InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", pEditor->CallbackSaveCopyMap, pEditor);
		return 1;
	}

	View.HSplitTop(10.0f, &Slot, &View);
	View.HSplitTop(12.0f, &Slot, &View);
	if(pEditor->DoButton_MenuItem(&s_ExitButton, "Exit", 0, &Slot, 0, "Exits from the editor"))
	{
		if(pEditor->HasUnsavedData())
		{
			pEditor->m_PopupEventType = POPEVENT_EXIT;
			pEditor->m_PopupEventActivated = true;
		}
		else
			g_Config.m_ClEditor = 0;
		return 1;
	}

	return 0;
}

void CEditor::RenderMenubar(CUIRect MenuBar)
{
	static CUIRect s_File /*, view, help*/;

	MenuBar.VSplitLeft(60.0f, &s_File, &MenuBar);
	if(DoButton_Menu(&s_File, "File", 0, &s_File, 0, 0))
		UiInvokePopupMenu(&s_File, 1, s_File.x, s_File.y + s_File.h - 1.0f, 120, 160, PopupMenuFile, this);

	/*
	menubar.VSplitLeft(5.0f, 0, &menubar);
	menubar.VSplitLeft(60.0f, &view, &menubar);
	if(do_editor_button(&view, "View", 0, &view, draw_editor_button_menu, 0, 0))
		(void)0;

	menubar.VSplitLeft(5.0f, 0, &menubar);
	menubar.VSplitLeft(60.0f, &help, &menubar);
	if(do_editor_button(&help, "Help", 0, &help, draw_editor_button_menu, 0, 0))
		(void)0;
		*/

	CUIRect Info, Close;
	MenuBar.VSplitLeft(40.0f, 0, &MenuBar);
	MenuBar.VSplitRight(20.0f, &MenuBar, &Close);
	Close.VSplitLeft(5.0f, 0, &Close);
	MenuBar.VSplitLeft(MenuBar.w * 0.75f, &MenuBar, &Info);
	char aBuf[128];
	str_format(aBuf, sizeof(aBuf), "File: %s", m_aFileName);
	UI()->DoLabel(&MenuBar, aBuf, 10.0f, -1, -1);

	char aTimeStr[6];
	str_timestamp_format(aTimeStr, sizeof(aTimeStr), "%H:%M");

	str_format(aBuf, sizeof(aBuf), "X: %i, Y: %i, Z: %i, A: %.1f, G: %i  %s", (int)UI()->MouseWorldX() / 32, (int)UI()->MouseWorldY() / 32, m_ZoomLevel, m_AnimateSpeed, m_GridFactor, aTimeStr);
	UI()->DoLabel(&Info, aBuf, 10.0f, 1, -1);

	static int s_CloseButton = 0;
	if(DoButton_Editor(&s_CloseButton, "×", 0, &Close, 0, "Exits from the editor", 0) || (m_Dialog == DIALOG_NONE && !UiPopupOpen() && !m_PopupEventActivated && Input()->KeyPress(KEY_ESCAPE)))
		g_Config.m_ClEditor = 0;
}

void CEditor::Render()
{
	// basic start
	Graphics()->Clear(1.0f, 0.0f, 1.0f);
	CUIRect View = *UI()->Screen();
	UI()->MapScreen();

	float Width = View.w;
	float Height = View.h;

	// reset tip
	m_pTooltip = 0;

	if(m_EditBoxActive)
		--m_EditBoxActive;

	// render checker
	RenderBackground(View, m_CheckerTexture, 32.0f, 1.0f);

	CUIRect MenuBar, CModeBar, ToolBar, StatusBar, ExtraEditor, UndoList, ToolBox;
	m_ShowPicker = Input()->KeyIsPressed(KEY_SPACE) != 0 && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0 && UI()->LastActiveItem() != &m_CommandBox && m_lSelectedLayers.size() == 1;

	if(m_GuiActive)
	{
		View.HSplitTop(16.0f, &MenuBar, &View);
		View.HSplitTop(53.0f, &ToolBar, &View);
		View.VSplitLeft(100.0f, &ToolBox, &View);
		View.HSplitBottom(16.0f, &View, &StatusBar);

		if(m_ShowEnvelopeEditor && !m_ShowPicker)
		{
			float Size = 125.0f;
			if(m_ShowEnvelopeEditor == 2)
				Size *= 2.0f;
			else if(m_ShowEnvelopeEditor == 3)
				Size *= 3.0f;
			View.HSplitBottom(Size, &View, &ExtraEditor);
		}
		if(m_ShowUndo && !m_ShowPicker)
		{
			View.HSplitBottom(250.0f, &View, &UndoList);
		}

		if(m_ShowServerSettingsEditor && !m_ShowPicker)
			View.HSplitBottom(250.0f, &View, &ExtraEditor);
	}
	else
	{
		// hack to get keyboard inputs from toolbar even when GUI is not active
		ToolBar.x = -100;
		ToolBar.y = -100;
		ToolBar.w = 50;
		ToolBar.h = 50;
	}

	//	a little hack for now
	if(m_Mode == MODE_LAYERS)
		DoMapEditor(View);

	// do zooming
	if(Input()->KeyPress(KEY_KP_MINUS) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0)
		m_ZoomLevel += 50;
	if(Input()->KeyPress(KEY_KP_PLUS) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0)
		m_ZoomLevel -= 50;
	if(Input()->KeyPress(KEY_KP_MULTIPLY) && m_Dialog == DIALOG_NONE && m_EditBoxActive == 0)
	{
		m_EditorOffsetX = 0;
		m_EditorOffsetY = 0;
		m_ZoomLevel = 100;
	}

	for(int i = KEY_1; i <= KEY_0; i++)
	{
		if(m_Dialog != DIALOG_NONE || m_EditBoxActive != 0)
			break;

		if(Input()->KeyPress(i))
		{
			int Slot = i - KEY_1;
			if((Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL)) && !m_Brush.IsEmpty())
			{
				dbg_msg("editor", "saving current brush to %d", Slot);
				if(m_apSavedBrushes[Slot])
				{
					CLayerGroup *pPrev = m_apSavedBrushes[Slot];
					for(int i = 0; i < pPrev->m_lLayers.size(); i++)
					{
						if(pPrev->m_lLayers[i]->m_BrushRefCount == 1)
							delete pPrev->m_lLayers[i];
						else
							pPrev->m_lLayers[i]->m_BrushRefCount--;
					}
				}
				delete m_apSavedBrushes[Slot];
				m_apSavedBrushes[Slot] = new CLayerGroup(m_Brush);

				for(int i = 0; i < m_apSavedBrushes[Slot]->m_lLayers.size(); i++)
					m_apSavedBrushes[Slot]->m_lLayers[i]->m_BrushRefCount++;
			}
			else if(m_apSavedBrushes[Slot])
			{
				dbg_msg("editor", "loading brush from slot %d", Slot);

				CLayerGroup *pNew = m_apSavedBrushes[Slot];
				for(int i = 0; i < pNew->m_lLayers.size(); i++)
					pNew->m_lLayers[i]->m_BrushRefCount++;

				m_Brush = *pNew;
			}
		}
	}

	float Brightness = 0.25f;

	if(m_GuiActive)
	{
		RenderBackground(MenuBar, m_BackgroundTexture, 128.0f, Brightness * 0);
		MenuBar.Margin(2.0f, &MenuBar);

		RenderBackground(ToolBox, m_BackgroundTexture, 128.0f, Brightness);
		ToolBox.Margin(2.0f, &ToolBox);

		RenderBackground(ToolBar, m_BackgroundTexture, 128.0f, Brightness);
		ToolBar.Margin(2.0f, &ToolBar);
		ToolBar.VSplitLeft(100.0f, &CModeBar, &ToolBar);

		RenderBackground(StatusBar, m_BackgroundTexture, 128.0f, Brightness);
		StatusBar.Margin(2.0f, &StatusBar);
	}

	// show mentions
	if(m_GuiActive && m_Mentions)
	{
		char aBuf[16];
		if(m_Mentions == 1)
		{
			str_copy(aBuf, Localize("1 new mention"), sizeof(aBuf));
		}
		else if(m_Mentions <= 9)
		{
			str_format(aBuf, sizeof(aBuf), Localize("%d new mentions"), m_Mentions);
		}
		else
		{
			str_copy(aBuf, Localize("9+ new mentions"), sizeof(aBuf));
		}

		TextRender()->TextColor(1.0f, 0.0f, 0.0f, 1.0f);
		TextRender()->Text(0, 5.0f, 27.0f, 10.0f, aBuf, -1.0f);
		TextRender()->TextColor(1.0f, 1.0f, 1.0f, 1.0f);
	}

	// do the toolbar
	if(m_Mode == MODE_LAYERS)
		DoToolbar(ToolBar);

	if(m_Dialog == DIALOG_NONE)
	{
		bool CtrlPressed = Input()->KeyIsPressed(KEY_LCTRL) || Input()->KeyIsPressed(KEY_RCTRL);
		bool ShiftPressed = Input()->KeyIsPressed(KEY_LSHIFT) || Input()->KeyIsPressed(KEY_RSHIFT);
		bool AltPressed = Input()->KeyIsPressed(KEY_LALT) || Input()->KeyIsPressed(KEY_RALT);
		// ctrl+n to create new map
		if(Input()->KeyPress(KEY_N) && CtrlPressed)
		{
			if(HasUnsavedData())
			{
				if(!m_PopupEventWasActivated)
				{
					m_PopupEventType = POPEVENT_NEW;
					m_PopupEventActivated = true;
				}
			}
			else
			{
				Reset();
				m_aFileName[0] = 0;
			}
		}
		// ctrl+a to append map
		if(Input()->KeyPress(KEY_A) && CtrlPressed)
		{
			InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_MAP, "Append map", "Append", "maps", "", CallbackAppendMap, this);
		}
		// ctrl+o or ctrl+l to open
		if((Input()->KeyPress(KEY_O) || Input()->KeyPress(KEY_L)) && CtrlPressed)
		{
			if(ShiftPressed)
			{
				if(HasUnsavedData())
				{
					if(!m_PopupEventWasActivated)
					{
						m_PopupEventType = POPEVENT_LOADCURRENT;
						m_PopupEventActivated = true;
					}
				}
				else
				{
					LoadCurrentMap();
				}
			}
			else
			{
				if(HasUnsavedData())
				{
					if(!m_PopupEventWasActivated)
					{
						m_PopupEventType = POPEVENT_LOAD;
						m_PopupEventActivated = true;
					}
				}
				else
				{
					InvokeFileDialog(IStorage::TYPE_ALL, FILETYPE_MAP, "Load map", "Load", "maps", "", CallbackOpenMap, this);
				}
			}
		}

		// ctrl+shift+alt+s to save as
		if(Input()->KeyPress(KEY_S) && CtrlPressed && ShiftPressed && AltPressed)
			InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", CallbackSaveCopyMap, this);
		// ctrl+shift+s to save as
		else if(Input()->KeyPress(KEY_S) && CtrlPressed && ShiftPressed)
			InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", CallbackSaveMap, this);
		// ctrl+s to save
		else if(Input()->KeyPress(KEY_S) && CtrlPressed)
		{
			if(m_aFileName[0] && m_ValidSaveFilename)
			{
				if(!m_PopupEventWasActivated)
				{
					str_copy(m_aFileSaveName, m_aFileName, sizeof(m_aFileSaveName));
					CallbackSaveMap(m_aFileSaveName, IStorage::TYPE_SAVE, this);
				}
			}
			else
				InvokeFileDialog(IStorage::TYPE_SAVE, FILETYPE_MAP, "Save map", "Save", "maps", "", CallbackSaveMap, this);
		}
	}

	if(m_GuiActive)
	{
		if(!m_ShowPicker)
		{
			if(m_ShowEnvelopeEditor || m_ShowServerSettingsEditor)
			{
				RenderBackground(ExtraEditor, m_BackgroundTexture, 128.0f, Brightness);
				ExtraEditor.Margin(2.0f, &ExtraEditor);
			}
			if(m_ShowUndo)
			{
				RenderBackground(UndoList, m_BackgroundTexture, 128.0f, Brightness);
				UndoList.Margin(2.0f, &UndoList);
			}
		}

		if(m_Mode == MODE_LAYERS)
			RenderLayers(ToolBox, View);
		else if(m_Mode == MODE_IMAGES)
			RenderImages(ToolBox, View);
		else if(m_Mode == MODE_SOUNDS)
			RenderSounds(ToolBox, View);
	}

	UI()->MapScreen();

	if(m_GuiActive)
	{
		RenderMenubar(MenuBar);

		RenderModebar(CModeBar);
		if(!m_ShowPicker)
		{
			if(m_ShowEnvelopeEditor)
				RenderEnvelopeEditor(ExtraEditor);
			if(m_ShowUndo)
				RenderUndoList(UndoList);
			static bool s_ShowServerSettingsEditorLast = false;
			if(m_ShowServerSettingsEditor)
			{
				RenderServerSettingsEditor(ExtraEditor, s_ShowServerSettingsEditorLast);
			}
			s_ShowServerSettingsEditorLast = m_ShowServerSettingsEditor;
		}
	}

	if(m_Dialog == DIALOG_FILE)
	{
		static int s_NullUiTarget = 0;
		UI()->SetHotItem(&s_NullUiTarget);
		RenderFileDialog();
	}

	if(m_PopupEventActivated)
	{
		static int s_PopupID = 0;
		UiInvokePopupMenu(&s_PopupID, 0, Width / 2.0f - 200.0f, Height / 2.0f - 100.0f, 400.0f, 200.0f, PopupEvent);
		m_PopupEventActivated = false;
		m_PopupEventWasActivated = true;
	}

	UiDoPopupMenu();

	if(m_Dialog == DIALOG_NONE && !m_MouseInsidePopup && UI()->MouseInside(&View))
	{
		// Determines in which direction to zoom.
		int Zoom = 0;
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_UP))
			Zoom--;
		if(Input()->KeyPress(KEY_MOUSE_WHEEL_DOWN))
			Zoom++;

		if(Zoom != 0)
		{
			float OldLevel = m_ZoomLevel;
			m_ZoomLevel = clamp(m_ZoomLevel + Zoom * 20, 10, 2000);
			if(g_Config.m_EdZoomTarget)
				ZoomMouseTarget((float)m_ZoomLevel / OldLevel);
		}
	}

	m_ZoomLevel = clamp(m_ZoomLevel, 10, 2000);
	m_WorldZoom = m_ZoomLevel / 100.0f;

	if(m_GuiActive)
		RenderStatusbar(StatusBar);

	//
	if(g_Config.m_EdShowkeys)
	{
		UI()->MapScreen();
		CTextCursor Cursor;
		TextRender()->SetCursor(&Cursor, View.x + 10, View.y + View.h - 24 - 10, 24.0f, TEXTFLAG_RENDER);

		int NKeys = 0;
		for(int i = 0; i < KEY_LAST; i++)
		{
			if(Input()->KeyIsPressed(i))
			{
				if(NKeys)
					TextRender()->TextEx(&Cursor, " + ", -1);
				TextRender()->TextEx(&Cursor, Input()->KeyName(i), -1);
				NKeys++;
			}
		}
	}

	if(m_ShowMousePointer)
	{
		// render butt ugly mouse cursor
		float mx = UI()->MouseX();
		float my = UI()->MouseY();
		Graphics()->WrapClamp();
		Graphics()->TextureSet(m_CursorTexture);
		Graphics()->QuadsBegin();
		if(ms_pUiGotContext == UI()->HotItem())
			Graphics()->SetColor(1, 0, 0, 1);
		IGraphics::CQuadItem QuadItem(mx, my, 16.0f, 16.0f);
		Graphics()->QuadsDrawTL(&QuadItem, 1);
		Graphics()->QuadsEnd();
		Graphics()->WrapNormal();
	}

	m_MouseInsidePopup = false;
}

static int UndoStepsListdirCallback(const char *pName, int IsDir, int StorageType, void *pUser)
{
	IStorage *pStorage = (IStorage *)pUser;
	if(str_comp_nocase_num(pName, "undo_", 5) == 0)
	{
		char aBuffer[1024];
		pStorage->GetCompletePath(IStorage::TYPE_SAVE, "editor/", aBuffer, sizeof(aBuffer));
		str_append(aBuffer, pName, sizeof(aBuffer));
		fs_remove(aBuffer);
	}
	return 0;
}

void CEditor::Reset(bool CreateDefault)
{
	m_Map.Clean();

	//delete undo file
	char aBuffer[1024];
	m_pStorage->GetCompletePath(IStorage::TYPE_SAVE, "editor/", aBuffer, sizeof(aBuffer));
	fs_listdir(aBuffer, UndoStepsListdirCallback, 0, m_pStorage);
	m_lUndoSteps.clear();

	mem_zero(m_apSavedBrushes, sizeof m_apSavedBrushes);

	// create default layers
	if(CreateDefault)
		m_Map.CreateDefault(m_EntitiesTexture);

	SelectGameLayer();
	m_lSelectedQuads.clear();
	m_SelectedPoints = 0;
	m_SelectedEnvelope = 0;
	m_SelectedImage = 0;
	m_SelectedSound = 0;
	m_SelectedSource = -1;

	m_WorldOffsetX = 0;
	m_WorldOffsetY = 0;
	m_EditorOffsetX = 0.0f;
	m_EditorOffsetY = 0.0f;

	m_WorldZoom = 1.0f;
	m_ZoomLevel = 200;

	m_MouseDeltaX = 0;
	m_MouseDeltaY = 0;
	m_MouseDeltaWx = 0;
	m_MouseDeltaWy = 0;

	m_Map.m_Modified = false;
	m_Map.m_UndoModified = 0;
	m_LastUndoUpdateTime = time_get();
	m_UndoRunning = false;

	m_ShowEnvelopePreview = 0;
	m_ShiftBy = 1;

	m_Map.m_Modified = false;
	m_Map.m_UndoModified = 0;
	m_LastUndoUpdateTime = time_get();
}

int CEditor::GetLineDistance() const
{
	int LineDistance = 512;

	if(m_ZoomLevel <= 100)
		LineDistance = 16;
	else if(m_ZoomLevel <= 250)
		LineDistance = 32;
	else if(m_ZoomLevel <= 450)
		LineDistance = 64;
	else if(m_ZoomLevel <= 850)
		LineDistance = 128;
	else if(m_ZoomLevel <= 1550)
		LineDistance = 256;

	return LineDistance;
}

void CEditor::ZoomMouseTarget(float ZoomFactor)
{
	// zoom to the current mouse position
	// get absolute mouse position
	float aPoints[4];
	RenderTools()->MapscreenToWorld(
		m_WorldOffsetX, m_WorldOffsetY,
		100.0f, 100.0f, 0.0f, 0.0f, Graphics()->ScreenAspect(), m_WorldZoom, aPoints);

	float WorldWidth = aPoints[2] - aPoints[0];
	float WorldHeight = aPoints[3] - aPoints[1];

	float Mwx = aPoints[0] + WorldWidth * (UI()->MouseX() / UI()->Screen()->w);
	float Mwy = aPoints[1] + WorldHeight * (UI()->MouseY() / UI()->Screen()->h);

	// adjust camera
	m_WorldOffsetX += (Mwx - m_WorldOffsetX) * (1 - ZoomFactor);
	m_WorldOffsetY += (Mwy - m_WorldOffsetY) * (1 - ZoomFactor);
}

void CEditorMap::DeleteEnvelope(int Index)
{
	if(Index < 0 || Index >= m_lEnvelopes.size())
		return;

	m_Modified = true;
	m_UndoModified++;

	// fix links between envelopes and quads
	for(int i = 0; i < m_lGroups.size(); ++i)
		for(int j = 0; j < m_lGroups[i]->m_lLayers.size(); ++j)
			if(m_lGroups[i]->m_lLayers[j]->m_Type == LAYERTYPE_QUADS)
			{
				CLayerQuads *pLayer = static_cast<CLayerQuads *>(m_lGroups[i]->m_lLayers[j]);
				for(int k = 0; k < pLayer->m_lQuads.size(); ++k)
				{
					if(pLayer->m_lQuads[k].m_PosEnv == Index)
						pLayer->m_lQuads[k].m_PosEnv = -1;
					else if(pLayer->m_lQuads[k].m_PosEnv > Index)
						pLayer->m_lQuads[k].m_PosEnv--;
					if(pLayer->m_lQuads[k].m_ColorEnv == Index)
						pLayer->m_lQuads[k].m_ColorEnv = -1;
					else if(pLayer->m_lQuads[k].m_ColorEnv > Index)
						pLayer->m_lQuads[k].m_ColorEnv--;
				}
			}
			else if(m_lGroups[i]->m_lLayers[j]->m_Type == LAYERTYPE_TILES)
			{
				CLayerTiles *pLayer = static_cast<CLayerTiles *>(m_lGroups[i]->m_lLayers[j]);
				if(pLayer->m_ColorEnv == Index)
					pLayer->m_ColorEnv = -1;
				if(pLayer->m_ColorEnv > Index)
					pLayer->m_ColorEnv--;
			}

	m_lEnvelopes.remove_index(Index);
}

void CEditorMap::MakeGameLayer(CLayer *pLayer)
{
	m_pGameLayer = (CLayerGame *)pLayer;
	m_pGameLayer->m_pEditor = m_pEditor;
	m_pGameLayer->m_Texture = m_pEditor->m_EntitiesTexture;
}

void CEditorMap::MakeGameGroup(CLayerGroup *pGroup)
{
	m_pGameGroup = pGroup;
	m_pGameGroup->m_GameGroup = true;
	str_copy(m_pGameGroup->m_aName, "Game", sizeof(m_pGameGroup->m_aName));
}

void CEditorMap::Clean()
{
	for(int i = 0; i < m_lGroups.size(); i++)
	{
		m_lGroups[i]->m_lLayers.delete_all();
	}
	m_lGroups.delete_all();
	m_lEnvelopes.delete_all();
	m_lImages.delete_all();
	m_lSounds.delete_all();

	m_MapInfo.Reset();

	m_lSettings.clear();

	m_pGameLayer = 0x0;
	m_pGameGroup = 0x0;

	m_Modified = false;

	m_pTeleLayer = 0x0;
	m_pSpeedupLayer = 0x0;
	m_pFrontLayer = 0x0;
	m_pSwitchLayer = 0x0;
	m_pTuneLayer = 0x0;
}

void CEditorMap::CreateDefault(IGraphics::CTextureHandle EntitiesTexture)
{
	// add background
	CLayerGroup *pGroup = NewGroup();
	pGroup->m_ParallaxX = 0;
	pGroup->m_ParallaxY = 0;
	CLayerQuads *pLayer = new CLayerQuads;
	pLayer->m_pEditor = m_pEditor;
	CQuad *pQuad = pLayer->NewQuad(0, 0, 1600, 1200);
	pQuad->m_aColors[0].r = pQuad->m_aColors[1].r = 94;
	pQuad->m_aColors[0].g = pQuad->m_aColors[1].g = 132;
	pQuad->m_aColors[0].b = pQuad->m_aColors[1].b = 174;
	pQuad->m_aColors[2].r = pQuad->m_aColors[3].r = 204;
	pQuad->m_aColors[2].g = pQuad->m_aColors[3].g = 232;
	pQuad->m_aColors[2].b = pQuad->m_aColors[3].b = 255;
	pGroup->AddLayer(pLayer);

	// add game layer and reset front, tele, speedup, tune and switch layer pointers
	MakeGameGroup(NewGroup());
	MakeGameLayer(new CLayerGame(50, 50));
	m_pGameGroup->AddLayer(m_pGameLayer);

	m_pFrontLayer = 0x0;
	m_pTeleLayer = 0x0;
	m_pSpeedupLayer = 0x0;
	m_pSwitchLayer = 0x0;
	m_pTuneLayer = 0x0;
}

void CEditor::Init()
{
	m_pInput = Kernel()->RequestInterface<IInput>();
	m_pClient = Kernel()->RequestInterface<IClient>();
	m_pConfig = Kernel()->RequestInterface<IConfigManager>()->Values();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_pGraphics = Kernel()->RequestInterface<IGraphics>();
	m_pTextRender = Kernel()->RequestInterface<ITextRender>();
	m_pStorage = Kernel()->RequestInterface<IStorage>();
	m_pSound = Kernel()->RequestInterface<ISound>();
	CGameClient *pGameClient = (CGameClient *)Kernel()->RequestInterface<IGameClient>();
	m_RenderTools.Init(m_pGraphics, &m_UI, pGameClient);
	m_UI.SetGraphics(m_pGraphics, m_pTextRender);
	m_Map.m_pEditor = this;

	m_UIEx.Init(UI(), Kernel(), RenderTools(), Input()->GetEventsRaw(), Input()->GetEventCountRaw());

	m_CheckerTexture = Graphics()->LoadTexture("editor/checker.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	m_BackgroundTexture = Graphics()->LoadTexture("editor/background.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	m_CursorTexture = Graphics()->LoadTexture("editor/cursor.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, 0);
	int TextureLoadFlag = Graphics()->HasTextureArrays() ? IGraphics::TEXLOAD_TO_2D_ARRAY_TEXTURE : IGraphics::TEXLOAD_TO_3D_TEXTURE;
	m_EntitiesTexture = Graphics()->LoadTexture("editor/entities/DDNet.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);

	m_FrontTexture = Graphics()->LoadTexture("editor/front.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);
	m_TeleTexture = Graphics()->LoadTexture("editor/tele.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);
	m_SpeedupTexture = Graphics()->LoadTexture("editor/speedup.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);
	m_SwitchTexture = Graphics()->LoadTexture("editor/switch.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);
	m_TuneTexture = Graphics()->LoadTexture("editor/tune.png", IStorage::TYPE_ALL, CImageInfo::FORMAT_AUTO, TextureLoadFlag);

	m_TilesetPicker.m_pEditor = this;
	m_TilesetPicker.MakePalette();
	m_TilesetPicker.m_Readonly = true;

	m_QuadsetPicker.m_pEditor = this;
	m_QuadsetPicker.NewQuad(0, 0, 64, 64);
	m_QuadsetPicker.m_Readonly = true;

	m_Brush.m_pMap = &m_Map;

	Reset();
	m_Map.m_Modified = false;
	m_Map.m_UndoModified = 0;
	m_LastUndoUpdateTime = time_get();

	ms_PickerColor = ColorHSVA(1.0f, 0.0f, 0.0f);
}

void CEditor::PlaceBorderTiles()
{
	CLayerTiles *pT = (CLayerTiles *)GetSelectedLayerType(0, LAYERTYPE_TILES);

	for(int i = 0; i < pT->m_Width * 2; ++i)
		pT->m_pTiles[i].m_Index = 1;

	for(int i = 0; i < pT->m_Width * pT->m_Height; ++i)
	{
		if(i % pT->m_Width < 2 || i % pT->m_Width > pT->m_Width - 3)
			pT->m_pTiles[i].m_Index = 1;
	}

	for(int i = (pT->m_Width * (pT->m_Height - 2)); i < pT->m_Width * pT->m_Height; ++i)
		pT->m_pTiles[i].m_Index = 1;
}

void CEditor::CreateUndoStep()
{
	thread_init_and_detach(CreateUndoStepThread, this, "Editor Undo");
}

void CEditor::CreateUndoStepThread(void *pUser)
{
	CEditor *pEditor = (CEditor *)pUser;

	CUndo NewStep;
	str_timestamp(NewStep.m_aName, sizeof(NewStep.m_aName));
	if(pEditor->m_lUndoSteps.size())
		NewStep.m_FileNum = pEditor->m_lUndoSteps[pEditor->m_lUndoSteps.size() - 1].m_FileNum + 1;
	else
		NewStep.m_FileNum = 0;
	NewStep.m_PreviewImageIsLoaded = false;

	char aBuffer[1024];
	str_format(aBuffer, sizeof(aBuffer), "editor/undo_%i.png", NewStep.m_FileNum);
	pEditor->Graphics()->TakeCustomScreenshot(aBuffer);

	str_format(aBuffer, sizeof(aBuffer), "editor/undo_%i", NewStep.m_FileNum);
	pEditor->Save(aBuffer);

	pEditor->m_lUndoSteps.add(NewStep);
	pEditor->m_UndoRunning = false;
}

void CEditor::UpdateAndRender()
{
	static float s_MouseX = 0.0f;
	static float s_MouseY = 0.0f;

	if(m_Animate)
		m_AnimateTime = (time_get() - m_AnimateStart) / (float)time_freq();
	else
		m_AnimateTime = 0;
	ms_pUiGotContext = 0;

	// handle mouse movement
	float mx, my, Mwx, Mwy;
	float rx = 0, ry = 0;
	{
		Input()->MouseRelative(&rx, &ry);
		UI()->ConvertMouseMove(&rx, &ry);

		// TODO: Why do we have to halve this?
		rx /= 2;
		ry /= 2;

		m_MouseDeltaX = rx;
		m_MouseDeltaY = ry;

		if(!m_LockMouse)
		{
			s_MouseX = clamp(s_MouseX + rx, 0.0f, UI()->Screen()->w);
			s_MouseY = clamp(s_MouseY + ry, 0.0f, UI()->Screen()->h);
		}

		// update the ui
		mx = s_MouseX;
		my = s_MouseY;
		Mwx = 0;
		Mwy = 0;

		// fix correct world x and y
		CLayerGroup *g = GetSelectedGroup();
		if(g)
		{
			float aPoints[4];
			g->Mapping(aPoints);

			float WorldWidth = aPoints[2] - aPoints[0];
			float WorldHeight = aPoints[3] - aPoints[1];

			Mwx = aPoints[0] + WorldWidth * (s_MouseX / UI()->Screen()->w);
			Mwy = aPoints[1] + WorldHeight * (s_MouseY / UI()->Screen()->h);
			m_MouseDeltaWx = m_MouseDeltaX * (WorldWidth / UI()->Screen()->w);
			m_MouseDeltaWy = m_MouseDeltaY * (WorldHeight / UI()->Screen()->h);
		}

		int Buttons = 0;
		if(Input()->KeyIsPressed(KEY_MOUSE_1))
			Buttons |= 1;
		if(Input()->KeyIsPressed(KEY_MOUSE_2))
			Buttons |= 2;
		if(Input()->KeyIsPressed(KEY_MOUSE_3))
			Buttons |= 4;

		UI()->Update(mx, my, Mwx, Mwy, Buttons);
	}

	// toggle gui
	if(Input()->KeyPress(KEY_TAB))
		m_GuiActive = !m_GuiActive;

	if(Input()->KeyPress(KEY_F10))
		m_ShowMousePointer = false;

	if(g_Config.m_ClEditorUndo)
	{
		// Screenshot at most every 5 seconds, at least every 60
		if((m_LastUndoUpdateTime + time_freq() * 60 < time_get() && m_Map.m_UndoModified) || (m_LastUndoUpdateTime + time_freq() * 5 < time_get() && m_Map.m_UndoModified >= 10))
		{
			m_Map.m_UndoModified = 0;
			m_LastUndoUpdateTime = time_get();

			if(!m_UndoRunning)
			{
				m_UndoRunning = true;
				CreateUndoStep();
			}
		}
	}
	Render();

	if(Input()->KeyPress(KEY_F10))
	{
		Graphics()->TakeScreenshot(0);
		m_ShowMousePointer = true;
	}

	Input()->Clear();
}

void CEditor::LoadCurrentMap()
{
	Load(m_pClient->GetCurrentMapPath(), IStorage::TYPE_ALL);
	m_ValidSaveFilename = true;

	CGameClient *pGameClient = (CGameClient *)Kernel()->RequestInterface<IGameClient>();
	vec2 Center = pGameClient->m_Camera.m_Center;

	m_WorldOffsetX = Center.x;
	m_WorldOffsetY = Center.y;
}

IEditor *CreateEditor() { return new CEditor; }

// DDRace

void CEditorMap::MakeTeleLayer(CLayer *pLayer)
{
	m_pTeleLayer = (CLayerTele *)pLayer;
	m_pTeleLayer->m_pEditor = m_pEditor;
	m_pTeleLayer->m_Texture = m_pEditor->m_TeleTexture;
}

void CEditorMap::MakeSpeedupLayer(CLayer *pLayer)
{
	m_pSpeedupLayer = (CLayerSpeedup *)pLayer;
	m_pSpeedupLayer->m_pEditor = m_pEditor;
	m_pSpeedupLayer->m_Texture = m_pEditor->m_SpeedupTexture;
}

void CEditorMap::MakeFrontLayer(CLayer *pLayer)
{
	m_pFrontLayer = (CLayerFront *)pLayer;
	m_pFrontLayer->m_pEditor = m_pEditor;
	m_pFrontLayer->m_Texture = m_pEditor->m_FrontTexture;
}

void CEditorMap::MakeSwitchLayer(CLayer *pLayer)
{
	m_pSwitchLayer = (CLayerSwitch *)pLayer;
	m_pSwitchLayer->m_pEditor = m_pEditor;
	m_pSwitchLayer->m_Texture = m_pEditor->m_SwitchTexture;
}

void CEditorMap::MakeTuneLayer(CLayer *pLayer)
{
	m_pTuneLayer = (CLayerTune *)pLayer;
	m_pTuneLayer->m_pEditor = m_pEditor;
	m_pTuneLayer->m_Texture = m_pEditor->m_TuneTexture;
}
