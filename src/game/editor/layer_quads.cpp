/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <base/math.h>

#include <engine/console.h>
#include <engine/graphics.h>

#include "editor.h"
#include <game/client/render.h>
#include <game/generated/client_data.h>
#include <game/localization.h>

CLayerQuads::CLayerQuads()
{
	m_Type = LAYERTYPE_QUADS;
	m_aName[0] = '\0';
	m_Image = -1;
}

CLayerQuads::~CLayerQuads()
{
}

void CLayerQuads::Render(bool QuadPicker)
{
	Graphics()->TextureClear();
	if(m_Image >= 0 && m_Image < m_pEditor->m_Map.m_lImages.size())
		Graphics()->TextureSet(m_pEditor->m_Map.m_lImages[m_Image]->m_Texture);

	Graphics()->BlendNone();
	m_pEditor->RenderTools()->ForceRenderQuads(m_lQuads.base_ptr(), m_lQuads.size(), LAYERRENDERFLAG_OPAQUE, m_pEditor->EnvelopeEval, m_pEditor);
	Graphics()->BlendNormal();
	m_pEditor->RenderTools()->ForceRenderQuads(m_lQuads.base_ptr(), m_lQuads.size(), LAYERRENDERFLAG_TRANSPARENT, m_pEditor->EnvelopeEval, m_pEditor);
}

CQuad *CLayerQuads::NewQuad(int x, int y, int Width, int Height)
{
	m_pEditor->m_Map.m_Modified = true;

	CQuad *q = &m_lQuads[m_lQuads.add(CQuad())];

	q->m_PosEnv = -1;
	q->m_ColorEnv = -1;
	q->m_PosEnvOffset = 0;
	q->m_ColorEnvOffset = 0;

	Width /= 2;
	Height /= 2;
	q->m_aPoints[0].x = i2fx(x - Width);
	q->m_aPoints[0].y = i2fx(y - Height);
	q->m_aPoints[1].x = i2fx(x + Width);
	q->m_aPoints[1].y = i2fx(y - Height);
	q->m_aPoints[2].x = i2fx(x - Width);
	q->m_aPoints[2].y = i2fx(y + Height);
	q->m_aPoints[3].x = i2fx(x + Width);
	q->m_aPoints[3].y = i2fx(y + Height);

	q->m_aPoints[4].x = i2fx(x); // pivot
	q->m_aPoints[4].y = i2fx(y);

	q->m_aTexcoords[0].x = i2fx(0);
	q->m_aTexcoords[0].y = i2fx(0);

	q->m_aTexcoords[1].x = i2fx(1);
	q->m_aTexcoords[1].y = i2fx(0);

	q->m_aTexcoords[2].x = i2fx(0);
	q->m_aTexcoords[2].y = i2fx(1);

	q->m_aTexcoords[3].x = i2fx(1);
	q->m_aTexcoords[3].y = i2fx(1);

	q->m_aColors[0].r = 255;
	q->m_aColors[0].g = 255;
	q->m_aColors[0].b = 255;
	q->m_aColors[0].a = 255;
	q->m_aColors[1].r = 255;
	q->m_aColors[1].g = 255;
	q->m_aColors[1].b = 255;
	q->m_aColors[1].a = 255;
	q->m_aColors[2].r = 255;
	q->m_aColors[2].g = 255;
	q->m_aColors[2].b = 255;
	q->m_aColors[2].a = 255;
	q->m_aColors[3].r = 255;
	q->m_aColors[3].g = 255;
	q->m_aColors[3].b = 255;
	q->m_aColors[3].a = 255;

	return q;
}

void CLayerQuads::BrushSelecting(CUIRect Rect)
{
	// draw selection rectangle
	IGraphics::CLineItem Array[4] = {
		IGraphics::CLineItem(Rect.x, Rect.y, Rect.x + Rect.w, Rect.y),
		IGraphics::CLineItem(Rect.x + Rect.w, Rect.y, Rect.x + Rect.w, Rect.y + Rect.h),
		IGraphics::CLineItem(Rect.x + Rect.w, Rect.y + Rect.h, Rect.x, Rect.y + Rect.h),
		IGraphics::CLineItem(Rect.x, Rect.y + Rect.h, Rect.x, Rect.y)};
	Graphics()->TextureClear();
	Graphics()->LinesBegin();
	Graphics()->LinesDraw(Array, 4);
	Graphics()->LinesEnd();
}

int CLayerQuads::BrushGrab(CLayerGroup *pBrush, CUIRect Rect)
{
	// create new layers
	CLayerQuads *pGrabbed = new CLayerQuads();
	pGrabbed->m_pEditor = m_pEditor;
	pGrabbed->m_Image = m_Image;
	pBrush->AddLayer(pGrabbed);

	//dbg_msg("", "%f %f %f %f", rect.x, rect.y, rect.w, rect.h);
	for(int i = 0; i < m_lQuads.size(); i++)
	{
		CQuad *q = &m_lQuads[i];
		float px = fx2f(q->m_aPoints[4].x);
		float py = fx2f(q->m_aPoints[4].y);

		if(px > Rect.x && px < Rect.x + Rect.w && py > Rect.y && py < Rect.y + Rect.h)
		{
			CQuad n;
			n = *q;

			for(auto &Point : n.m_aPoints)
			{
				Point.x -= f2fx(Rect.x);
				Point.y -= f2fx(Rect.y);
			}

			pGrabbed->m_lQuads.add(n);
		}
	}

	return pGrabbed->m_lQuads.size() ? 1 : 0;
}

void CLayerQuads::BrushPlace(CLayer *pBrush, float wx, float wy)
{
	CLayerQuads *l = (CLayerQuads *)pBrush;
	for(int i = 0; i < l->m_lQuads.size(); i++)
	{
		CQuad n = l->m_lQuads[i];

		for(auto &Point : n.m_aPoints)
		{
			Point.x += f2fx(wx);
			Point.y += f2fx(wy);
		}

		m_lQuads.add(n);
	}
	m_pEditor->m_Map.m_Modified = true;
}

void Swap(CPoint &a, CPoint &b)
{
	CPoint Tmp;
	Tmp.x = a.x;
	Tmp.y = a.y;

	a.x = b.x;
	a.y = b.y;

	b.x = Tmp.x;
	b.y = Tmp.y;
}

void CLayerQuads::BrushFlipX()
{
	for(int i = 0; i < m_lQuads.size(); i++)
	{
		CQuad *q = &m_lQuads[i];

		Swap(q->m_aPoints[0], q->m_aPoints[1]);
		Swap(q->m_aPoints[2], q->m_aPoints[3]);
	}
	m_pEditor->m_Map.m_Modified = true;
}

void CLayerQuads::BrushFlipY()
{
	for(int i = 0; i < m_lQuads.size(); i++)
	{
		CQuad *q = &m_lQuads[i];

		Swap(q->m_aPoints[0], q->m_aPoints[2]);
		Swap(q->m_aPoints[1], q->m_aPoints[3]);
	}
	m_pEditor->m_Map.m_Modified = true;
}

void Rotate(vec2 *pCenter, vec2 *pPoint, float Rotation)
{
	float x = pPoint->x - pCenter->x;
	float y = pPoint->y - pCenter->y;
	pPoint->x = x * cosf(Rotation) - y * sinf(Rotation) + pCenter->x;
	pPoint->y = x * sinf(Rotation) + y * cosf(Rotation) + pCenter->y;
}

void CLayerQuads::BrushRotate(float Amount)
{
	vec2 Center;
	GetSize(&Center.x, &Center.y);
	Center.x /= 2;
	Center.y /= 2;

	for(int i = 0; i < m_lQuads.size(); i++)
	{
		CQuad *q = &m_lQuads[i];

		for(auto &Point : q->m_aPoints)
		{
			vec2 Pos(fx2f(Point.x), fx2f(Point.y));
			Rotate(&Center, &Pos, Amount);
			Point.x = f2fx(Pos.x);
			Point.y = f2fx(Pos.y);
		}
	}
}

void CLayerQuads::GetSize(float *w, float *h)
{
	*w = 0;
	*h = 0;

	for(int i = 0; i < m_lQuads.size(); i++)
	{
		for(auto &Point : m_lQuads[i].m_aPoints)
		{
			*w = maximum(*w, fx2f(Point.x));
			*h = maximum(*h, fx2f(Point.y));
		}
	}
}

int CLayerQuads::RenderProperties(CUIRect *pToolBox)
{
	// layer props
	enum
	{
		PROP_IMAGE = 0,
		NUM_PROPS,
	};

	CProperty aProps[] = {
		{"Image", m_Image, PROPTYPE_IMAGE, -1, 0},
		{0},
	};

	static int s_aIds[NUM_PROPS] = {0};
	int NewVal = 0;
	int Prop = m_pEditor->DoProperties(pToolBox, aProps, s_aIds, &NewVal);
	if(Prop != -1)
		m_pEditor->m_Map.m_Modified = true;

	if(Prop == PROP_IMAGE)
	{
		if(NewVal >= 0)
			m_Image = NewVal % m_pEditor->m_Map.m_lImages.size();
		else
			m_Image = -1;
	}

	return 0;
}

void CLayerQuads::ModifyImageIndex(INDEX_MODIFY_FUNC Func)
{
	Func(&m_Image);
}

void CLayerQuads::ModifyEnvelopeIndex(INDEX_MODIFY_FUNC Func)
{
	for(int i = 0; i < m_lQuads.size(); i++)
	{
		Func(&m_lQuads[i].m_PosEnv);
		Func(&m_lQuads[i].m_ColorEnv);
	}
}
