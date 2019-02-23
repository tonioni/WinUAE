
#include <windows.h>
#include "resource.h"

#include "sysconfig.h"
#include "sysdeps.h"

#if defined (D3D) && defined (GFXFILTER)

#define D3DXFX_LARGEADDRESS_HANDLE
#ifdef D3DXFX_LARGEADDRESS_HANDLE
#define EFFECTCOMPILERFLAGS D3DXFX_LARGEADDRESSAWARE
#else
#define EFFECTCOMPILERFLAGS 0
#endif

#define EFFECT_VERSION 3
#define D3DX9DLL _T("d3dx9_43.dll")
#define TWOPASS 1
#define SHADER 1

#include "options.h"
#include "xwin.h"
#include "custom.h"
#include "drawing.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "gfxfilter.h"
#include "statusline.h"
#include "hq2x_d3d.h"
#include "zfile.h"
#include "uae.h"
#include "gui.h"
#include "threaddep\thread.h"

extern int D3DEX, shaderon, d3ddebug;
int forcedframelatency = -1;

#include <d3d9.h>
#include <d3dx9.h>

#include "direct3d.h"

static TCHAR *D3DHEAD = _T("-");

static bool showoverlay = true;
static int slicecnt;
static int clearcnt;
static bool debugcolors;
static bool noclear;
static bool cannoclear;

int fakemodewaitms;

static int leds[LED_MAX];

static const TCHAR *overlayleds[] = {
	_T("power"),
	_T("df0"),
	_T("df1"),
	_T("df2"),
	_T("df3"),
	_T("hd"),
	_T("cd"),
	_T("md"),
	_T("net"),
	NULL
};
static const int ledtypes[] = {
	LED_POWER,
	LED_DF0,
	LED_DF1,
	LED_DF2,
	LED_DF3,
	LED_HD,
	LED_CD,
	LED_MD,
	LED_NET
}; 

#define MAX_PASSES 2

#define SHADERTYPE_BEFORE 1
#define SHADERTYPE_AFTER 2
#define SHADERTYPE_MIDDLE 3
#define SHADERTYPE_MASK_BEFORE 4
#define SHADERTYPE_MASK_AFTER 5
#define SHADERTYPE_MASK_MIDDLE 6
#define SHADERTYPE_POST 10

struct shaderdata
{
	int type;
	int psPreProcess;
	int worktex_width;
	int worktex_height;
	int targettex_width;
	int targettex_height;
	LPDIRECT3DTEXTURE9 lpWorkTexture1;
	LPDIRECT3DTEXTURE9 lpWorkTexture2;
	LPDIRECT3DTEXTURE9 lpTempTexture;
	LPDIRECT3DVOLUMETEXTURE9 lpHq2xLookupTexture;
	LPD3DXEFFECT pEffect;
	// Technique stuff
	D3DXHANDLE m_PreprocessTechnique1EffectHandle;
	D3DXHANDLE m_PreprocessTechnique2EffectHandle;
	D3DXHANDLE m_CombineTechniqueEffectHandle;
	// Matrix Handles
	D3DXHANDLE m_MatWorldEffectHandle;
	D3DXHANDLE m_MatViewEffectHandle;
	D3DXHANDLE m_MatProjEffectHandle;
	D3DXHANDLE m_MatWorldViewEffectHandle;
	D3DXHANDLE m_MatViewProjEffectHandle;
	D3DXHANDLE m_MatWorldViewProjEffectHandle;
	// Texture Handles
	D3DXHANDLE m_SourceDimsEffectHandle;
	D3DXHANDLE m_InputDimsEffectHandle;
	D3DXHANDLE m_TargetDimsEffectHandle;
	D3DXHANDLE m_TexelSizeEffectHandle;
	D3DXHANDLE m_ScaleEffectHandle;
	D3DXHANDLE m_SourceTextureEffectHandle;
	D3DXHANDLE m_WorkingTexture1EffectHandle;
	D3DXHANDLE m_WorkingTexture2EffectHandle;
	D3DXHANDLE m_Hq2xLookupTextureHandle;
	// Masks
	LPDIRECT3DTEXTURE9 masktexture;
	int masktexture_w, masktexture_h;
	// Stuff
	D3DXHANDLE framecounterHandle;
};

struct d3d9overlay
{
	struct d3d9overlay *next;
	int id;
	int x, y;
	LPDIRECT3DTEXTURE9 tex;
};

#define MAX_SHADERS (2 * MAX_FILTERSHADERS + 2)
#define SHADER_POST 0

struct d3dstruct
{
	int psEnabled, psActive;
	struct shaderdata shaders[MAX_SHADERS];
	LPDIRECT3DTEXTURE9 lpPostTempTexture;
	IDirect3DSurface9 *screenshotsurface;
	D3DFORMAT tformat;
	int d3d_enabled, d3d_ex;
	IDirect3D9 *d3d;
	IDirect3D9Ex *d3dex;
	IDirect3DSwapChain9 *d3dswapchain;
	D3DPRESENT_PARAMETERS dpp;
	D3DDISPLAYMODEEX modeex;
	IDirect3DDevice9 *d3ddev;
	IDirect3DDevice9Ex *d3ddevex;
	D3DSURFACE_DESC dsdbb;
	LPDIRECT3DTEXTURE9 texture, sltexture, ledtexture, blanktexture;
	LPDIRECT3DTEXTURE9 mask2texture, mask2textureleds[9], mask2textureled_power_dim;
	int mask2textureledoffsets[9 * 2];
	IDirect3DQuery9 *query;
	float mask2texture_w, mask2texture_h, mask2texture_ww, mask2texture_wh;
	float mask2texture_wwx, mask2texture_hhx, mask2texture_minusx, mask2texture_minusy;
	float mask2texture_multx, mask2texture_multy, mask2texture_offsetw;
	LPDIRECT3DTEXTURE9 cursorsurfaced3d;
	struct d3d9overlay *extoverlays;
	IDirect3DVertexBuffer9 *vertexBuffer;
	ID3DXSprite *sprite;
	HWND d3dhwnd;
	int devicelost;
	int locked, fulllocked;
	int cursor_offset_x, cursor_offset_y, cursor_offset2_x, cursor_offset2_y;
	float maskmult_x, maskmult_y;
	RECT mask2rect;
	bool wasstilldrawing_broken;
	bool renderdisabled;
	HANDLE filenotificationhandle;
	int frames_since_init;

	volatile bool fakemode;
	uae_u8 *fakebitmap;
	uae_thread_id fakemodetid;

	D3DXMATRIXA16 m_matProj, m_matProj2, m_matProj_out;
	D3DXMATRIXA16 m_matWorld, m_matWorld2, m_matWorld_out;
	D3DXMATRIXA16 m_matView, m_matView2, m_matView_out;
	D3DXMATRIXA16 m_matPreProj;
	D3DXMATRIXA16 m_matPreView;
	D3DXMATRIXA16 m_matPreWorld;
	D3DXMATRIXA16 postproj;
	D3DXVECTOR4 maskmult, maskshift;
	D3DXVECTOR4 fakesize;

	int ledwidth, ledheight;
	int max_texture_w, max_texture_h;
	int tin_w, tin_h, tout_w, tout_h, window_h, window_w;
	int t_depth, dmult, dmultxh, dmultxv;
	int required_sl_texture_w, required_sl_texture_h;
	int vsync2, guimode, maxscanline, variablerefresh;
	int resetcount;
	double cursor_x, cursor_y;
	bool cursor_v, cursor_scale;
	int statusbar_vx, statusbar_hx;

	struct gfx_filterdata *filterd3d;
	int filterd3didx;
	int scanline_osl1, scanline_osl2, scanline_osl3, scanline_osl4;

	D3DXHANDLE postSourceTextureHandle;
	D3DXHANDLE postMaskTextureHandle;
	D3DXHANDLE postTechnique, postTechniquePlain, postTechniqueAlpha;
	D3DXHANDLE postMatrixSource;
	D3DXHANDLE postMaskMult, postMaskShift;
	D3DXHANDLE postFilterMode;
	D3DXHANDLE postTexelSize;
	D3DXHANDLE postFramecounterHandle;

	float m_scale;
	LPCSTR m_strName;

	int ddraw_fs;
	int ddraw_fs_attempt;
	LPDIRECTDRAW7 ddraw;
};

static struct d3dstruct d3ddata[MAX_AMIGAMONITORS];

#define NUMVERTICES 8
#define D3DFVF_TLVERTEX D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1
struct TLVERTEX {
	D3DXVECTOR3 position;       // vertex position
	D3DCOLOR    diffuse;
	D3DXVECTOR2 texcoord;       // texture coords
};


static void ddraw_fs_hack_free (struct d3dstruct *d3d)
{
	HRESULT hr;

	if (!d3d->ddraw_fs)
		return;
	if (d3d->ddraw_fs == 2)
		d3d->ddraw->RestoreDisplayMode ();
	hr = d3d->ddraw->SetCooperativeLevel (d3d->d3dhwnd, DDSCL_NORMAL);
	if (FAILED (hr)) {
		write_log (_T("IDirectDraw7_SetCooperativeLevel CLEAR: %s\n"), DXError (hr));
	}
	d3d->ddraw->Release ();
	d3d->ddraw = NULL;
	d3d->ddraw_fs = 0;
}

static int ddraw_fs_hack_init (struct d3dstruct *d3d)
{
	HRESULT hr;
	struct MultiDisplay *md;

	ddraw_fs_hack_free(d3d);
	DirectDraw_get_GUIDs();
	md = getdisplay(&currprefs, 0);
	if (!md)
		return 0;
	hr = DirectDrawCreateEx(md->primary ? NULL : &md->ddguid, (LPVOID*)&d3d->ddraw, IID_IDirectDraw7, NULL);
	if (FAILED (hr)) {
		write_log (_T("DirectDrawCreateEx failed, %s\n"), DXError (hr));
		return 0;
	}
	d3d->ddraw_fs = 1;
	hr = d3d->ddraw->SetCooperativeLevel(d3d->d3dhwnd, DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
	if (FAILED (hr)) {
		write_log (_T("IDirectDraw7_SetCooperativeLevel SET: %s\n"), DXError (hr));
		ddraw_fs_hack_free (d3d);
		return 0;
	}
	hr = d3d->ddraw->SetDisplayMode(d3d->dpp.BackBufferWidth, d3d->dpp.BackBufferHeight, d3d->t_depth, d3d->dpp.FullScreen_RefreshRateInHz, 0);
	if (FAILED (hr)) {
		write_log (_T("1:IDirectDraw7_SetDisplayMode: %s\n"), DXError (hr));
		if (d3d->dpp.FullScreen_RefreshRateInHz && isvsync_chipset () < 0) {
			hr = d3d->ddraw->SetDisplayMode(d3d->dpp.BackBufferWidth, d3d->dpp.BackBufferHeight, d3d->t_depth, 0, 0);
			if (FAILED (hr))
				write_log (_T("2:IDirectDraw7_SetDisplayMode: %s\n"), DXError (hr));
		}
		if (FAILED (hr)) {
			write_log (_T("IDirectDraw7_SetDisplayMode: %s\n"), DXError (hr));
			ddraw_fs_hack_free(d3d);
			return 0;
		}
	}
	d3d->ddraw_fs = 2;
	return 1;
}

static TCHAR *D3D_ErrorText (HRESULT error)
{
	return _T("");
}
static TCHAR *D3D_ErrorString (HRESULT dival)
{
	static TCHAR dierr[200];
	_stprintf (dierr, _T("%08X S=%d F=%04X C=%04X (%d) (%s)"),
		dival, (dival & 0x80000000) ? 1 : 0,
		HRESULT_FACILITY(dival),
		HRESULT_CODE(dival),
		HRESULT_CODE(dival),
		D3D_ErrorText (dival));
	return dierr;
}

static D3DXMATRIX* MatrixOrthoOffCenterLH (D3DXMATRIXA16 *pOut, float l, float r, float b, float t, float zn, float zf)
{
	pOut->_11=2.0f/r; pOut->_12=0.0f;   pOut->_13=0.0f;  pOut->_14=0.0f;
	pOut->_21=0.0f;   pOut->_22=2.0f/t; pOut->_23=0.0f;  pOut->_24=0.0f;
	pOut->_31=0.0f;   pOut->_32=0.0f;   pOut->_33=1.0f;  pOut->_34=0.0f;
	pOut->_41=-1.0f;  pOut->_42=-1.0f;  pOut->_43=0.0f;  pOut->_44=1.0f;
	return pOut;
}

static D3DXMATRIX* MatrixScaling (D3DXMATRIXA16 *pOut, float sx, float sy, float sz)
{
	pOut->_11=sx;     pOut->_12=0.0f;   pOut->_13=0.0f;  pOut->_14=0.0f;
	pOut->_21=0.0f;   pOut->_22=sy;     pOut->_23=0.0f;  pOut->_24=0.0f;
	pOut->_31=0.0f;   pOut->_32=0.0f;   pOut->_33=sz;    pOut->_34=0.0f;
	pOut->_41=0.0f;   pOut->_42=0.0f;   pOut->_43=0.0f;  pOut->_44=1.0f;
	return pOut;
}

static D3DXMATRIX* MatrixTranslation (D3DXMATRIXA16 *pOut, float tx, float ty, float tz)
{
	pOut->_11=1.0f;   pOut->_12=0.0f;   pOut->_13=0.0f;  pOut->_14=0.0f;
	pOut->_21=0.0f;   pOut->_22=1.0f;   pOut->_23=0.0f;  pOut->_24=0.0f;
	pOut->_31=0.0f;   pOut->_32=0.0f;   pOut->_33=1.0f;  pOut->_34=0.0f;
	pOut->_41=tx;     pOut->_42=ty;     pOut->_43=tz;    pOut->_44=1.0f;
	return pOut;
}

static TCHAR *D3DX_ErrorString (HRESULT hr, LPD3DXBUFFER Errors)
{
	static TCHAR *buffer;
	static int buffersize;
	TCHAR *s = NULL;
	int size = 0;

	if (Errors)
		s = au ((char*)Errors->GetBufferPointer ());
	size = (s == NULL ? 0 : _tcslen (s)) + 1000;
	if (size + 1000 > buffersize) {
		xfree (buffer);
		buffer = xmalloc (TCHAR, size);
		buffersize = size;
	}
	buffer[0] = 0;
	if (hr != S_OK)
		_tcscpy (buffer, D3D_ErrorString (hr));
	if (s) {
		if (buffer[0])
			_tcscat (buffer, _T(" "));
		_tcscat (buffer, s);
	}
	xfree (s);
	return buffer;
}

static int isd3d (struct d3dstruct *d3d)
{
	if (d3d->fakemode || d3d->devicelost || !d3d->d3ddev || !d3d->d3d_enabled || d3d->renderdisabled)
		return 0;
	return 1;
}
static void waitfakemode (struct d3dstruct *d3d)
{
	while (d3d->fakemode) {
		sleep_millis (10);
	}
}


enum psEffect_Pass { psEffect_None, psEffect_PreProcess1, psEffect_PreProcess2, psEffect_Combine };

static int postEffect_ParseParameters (struct d3dstruct *d3d, LPD3DXEFFECTCOMPILER EffectCompiler, LPD3DXEFFECT effect, struct shaderdata *s)
{
	d3d->postSourceTextureHandle = effect->GetParameterByName (NULL, "SourceTexture");
	d3d->postMaskTextureHandle = effect->GetParameterByName (NULL, "OverlayTexture");
	d3d->postTechnique = effect->GetTechniqueByName ("PostTechnique");
	d3d->postTechniquePlain = effect->GetTechniqueByName ("PostTechniquePlain");
	d3d->postTechniqueAlpha = effect->GetTechniqueByName ("PostTechniqueAlpha");
	d3d->postMatrixSource = effect->GetParameterByName (NULL, "mtx");
	d3d->postMaskMult = effect->GetParameterByName (NULL, "maskmult");
	d3d->postMaskShift = effect->GetParameterByName (NULL, "maskshift");
	d3d->postFilterMode = effect->GetParameterByName (NULL, "filtermode");
	d3d->postTexelSize = effect->GetParameterByName (NULL, "texelsize");
	d3d->postFramecounterHandle = effect->GetParameterByName (NULL, "framecounter");

	if (!d3d->postMaskShift || !d3d->postMaskMult || !d3d->postFilterMode || !d3d->postMatrixSource || !d3d->postTexelSize) {
		gui_message (_T("Mismatched _winuae.fx! Exiting.."));
		abort ();
	}
	return true;
}

static void postEffect_freeParameters(struct d3dstruct *d3d)
{
	d3d->postSourceTextureHandle = NULL;
	d3d->postMaskTextureHandle = NULL;
	d3d->postTechnique = NULL;
	d3d->postTechniquePlain = NULL;
	d3d->postTechniqueAlpha = NULL;
	d3d->postMatrixSource = NULL;
	d3d->postMaskMult = NULL;
	d3d->postMaskShift = NULL;
	d3d->postFilterMode = NULL;
	d3d->postTexelSize = NULL;
	d3d->postFramecounterHandle = NULL;
}

static int psEffect_ParseParameters (struct d3dstruct *d3d, LPD3DXEFFECTCOMPILER EffectCompiler, LPD3DXEFFECT effect, D3DXEFFECT_DESC EffectDesc, struct shaderdata *s)
{
	HRESULT hr = S_OK;
	// Look at parameters for semantics and annotations that we know how to interpret
	D3DXPARAMETER_DESC ParamDesc;
	D3DXPARAMETER_DESC AnnotDesc;
	D3DXHANDLE hParam;
	D3DXHANDLE hAnnot;
	LPDIRECT3DBASETEXTURE9 pTex = NULL;
	UINT iParam, iAnnot;


	if(effect == NULL)
		return 0;

	for(iParam = 0; iParam < EffectDesc.Parameters; iParam++) {
		LPCSTR pstrName = NULL;
		LPCSTR pstrFunction = NULL;
		D3DXHANDLE pstrFunctionHandle = NULL;
		LPCSTR pstrTarget = NULL;
		LPCSTR pstrTextureType = NULL;
		INT Width = D3DX_DEFAULT;
		INT Height= D3DX_DEFAULT;
		INT Depth = D3DX_DEFAULT;

		hParam = effect->GetParameter (NULL, iParam);
		hr = effect->GetParameterDesc (hParam, &ParamDesc);
		if (FAILED (hr)) {
			write_log (_T("GetParameterDescParm(%d) failed: %s\n"), D3DHEAD, iParam, D3DX_ErrorString (hr, NULL));
			return 0;
		}
		s->framecounterHandle = effect->GetParameterByName (NULL, "framecounter");
		hr = S_OK;
		if(ParamDesc.Semantic != NULL) {
			if(ParamDesc.Class == D3DXPC_MATRIX_ROWS || ParamDesc.Class == D3DXPC_MATRIX_COLUMNS) {
				if(strcmpi(ParamDesc.Semantic, "world") == 0)
					s->m_MatWorldEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "view") == 0)
					s->m_MatViewEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "projection") == 0)
					s->m_MatProjEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "worldview") == 0)
					s->m_MatWorldViewEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "viewprojection") == 0)
					s->m_MatViewProjEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "worldviewprojection") == 0)
					s->m_MatWorldViewProjEffectHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_VECTOR && ParamDesc.Type == D3DXPT_FLOAT) {
				if (strcmpi(ParamDesc.Semantic, "sourcedims") == 0)
					s->m_SourceDimsEffectHandle = hParam;
				if (strcmpi(ParamDesc.Semantic, "inputdims") == 0)
					s->m_InputDimsEffectHandle = hParam;
				if (strcmpi(ParamDesc.Semantic, "targetdims") == 0)
					s->m_TargetDimsEffectHandle = hParam;
				else if (strcmpi(ParamDesc.Semantic, "texelsize") == 0)
					s->m_TexelSizeEffectHandle = hParam;
				else if (strcmpi(ParamDesc.Semantic, "sourcescale") == 0)
					s->m_ScaleEffectHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_SCALAR && ParamDesc.Type == D3DXPT_FLOAT) {
				if(strcmpi(ParamDesc.Semantic, "SCALING") == 0)
					hr = effect->GetFloat(hParam, &d3d->m_scale);
			} else if(ParamDesc.Class == D3DXPC_OBJECT && ParamDesc.Type == D3DXPT_TEXTURE) {
				if(strcmpi(ParamDesc.Semantic, "SOURCETEXTURE") == 0)
					s->m_SourceTextureEffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE") == 0)
					s->m_WorkingTexture1EffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE1") == 0)
					s->m_WorkingTexture2EffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "HQ2XLOOKUPTEXTURE") == 0)
					s->m_Hq2xLookupTextureHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_OBJECT && ParamDesc.Type == D3DXPT_STRING) {
				LPCSTR pstrTechnique = NULL;
				if(strcmpi(ParamDesc.Semantic, "COMBINETECHNIQUE") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					s->m_CombineTechniqueEffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					s->m_PreprocessTechnique1EffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE1") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					s->m_PreprocessTechnique2EffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "NAME") == 0) {
					hr = effect->GetString(hParam, &d3d->m_strName);
				}
			}
			if (FAILED (hr)) {
				write_log (_T("ParamDesc.Semantic failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, NULL));
				return 0;
			}
		}

		for(iAnnot = 0; iAnnot < ParamDesc.Annotations; iAnnot++) {
			hAnnot = effect->GetAnnotation (hParam, iAnnot);
			hr = effect->GetParameterDesc(hAnnot, &AnnotDesc);
			if (FAILED (hr)) {
				write_log (_T("GetParameterDescAnnot(%d) failed: %s\n"), D3DHEAD, iAnnot, D3DX_ErrorString (hr, NULL));
				return 0;
			}
			hr = S_OK;
			if(strcmpi(AnnotDesc.Name, "name") == 0) {
				hr = effect->GetString(hAnnot, &pstrName);
			} else if(strcmpi(AnnotDesc.Name, "function") == 0) {
				hr = effect->GetString(hAnnot, &pstrFunction);
				pstrFunctionHandle = effect->GetFunctionByName(pstrFunction);
			} else if(strcmpi(AnnotDesc.Name, "target") == 0) {
				hr = effect->GetString(hAnnot, &pstrTarget);
			} else if(strcmpi(AnnotDesc.Name, "width") == 0) {
				hr = effect->GetInt(hAnnot, &Width);
			} else if(strcmpi(AnnotDesc.Name, "height") == 0) {
				hr = effect->GetInt(hAnnot, &Height);
			} else if(strcmpi(AnnotDesc.Name, "depth") == 0) {
				hr = effect->GetInt(hAnnot, &Depth);
			} else if(strcmpi(AnnotDesc.Name, "type") == 0) {
				hr = effect->GetString(hAnnot, &pstrTextureType);
			}
			if (FAILED (hr)) {
				write_log (_T("GetString/GetInt(%d) failed: %s\n"), D3DHEAD, iAnnot, D3DX_ErrorString (hr, NULL));
				return 0;
			}
		}

		if(pstrFunctionHandle != NULL) {
			LPD3DXBUFFER pTextureShader = NULL;
			LPD3DXBUFFER lpErrors = 0;

			if(pstrTarget == NULL || strcmp(pstrTarget,"tx_1_1"))
				pstrTarget = "tx_1_0";

			if(SUCCEEDED(hr = EffectCompiler->CompileShader(
				pstrFunctionHandle, pstrTarget, D3DXSHADER_SKIPVALIDATION|D3DXSHADER_DEBUG, &pTextureShader, &lpErrors, NULL))) {
					LPD3DXTEXTURESHADER ppTextureShader;
					if (lpErrors)
						lpErrors->Release ();

					if(Width == D3DX_DEFAULT)
						Width = 64;
					if(Height == D3DX_DEFAULT)
						Height = 64;
					if(Depth == D3DX_DEFAULT)
						Depth = 64;

					D3DXCreateTextureShader((DWORD *)pTextureShader->GetBufferPointer(), &ppTextureShader);

					if(pstrTextureType != NULL) {
						if(strcmpi(pstrTextureType, "volume") == 0) {
							LPDIRECT3DVOLUMETEXTURE9 pVolumeTex = NULL;
							if(SUCCEEDED(hr = D3DXCreateVolumeTexture(d3d->d3ddev,
								Width, Height, Depth, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pVolumeTex))) {
									if(SUCCEEDED(hr = D3DXFillVolumeTextureTX(pVolumeTex, ppTextureShader))) {
										pTex = pVolumeTex;
									}
							}
						} else if(strcmpi(pstrTextureType, "cube") == 0) {
							LPDIRECT3DCUBETEXTURE9 pCubeTex = NULL;
							if(SUCCEEDED(hr = D3DXCreateCubeTexture(d3d->d3ddev,
								Width, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pCubeTex))) {
									if(SUCCEEDED(hr = D3DXFillCubeTextureTX(pCubeTex, ppTextureShader))) {
										pTex = pCubeTex;
									}
							}
						}
					} else {
						LPDIRECT3DTEXTURE9 p2DTex = NULL;
						if(SUCCEEDED(hr = D3DXCreateTexture(d3d->d3ddev, Width, Height,
							D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &p2DTex))) {
								if(SUCCEEDED(hr = D3DXFillTextureTX(p2DTex, ppTextureShader))) {
									pTex = p2DTex;
								}
						}
					}
					effect->SetTexture(effect->GetParameter(NULL, iParam), pTex);
					if (pTex)
						pTex->Release ();
					if (pTextureShader)
						pTextureShader->Release ();
					if (ppTextureShader)
						ppTextureShader->Release ();
			} else {
				write_log (_T("%s: Could not compile texture shader: %s\n"), D3DHEAD, D3DX_ErrorString (hr, lpErrors));
				if (lpErrors)
					lpErrors->Release ();
				return 0;
			}
		}
	}

	if (!s->m_CombineTechniqueEffectHandle && EffectDesc.Techniques > 0) {
		s->m_CombineTechniqueEffectHandle = effect->GetTechnique(0);
	}

	return 1;
}

static int psEffect_hasPreProcess (struct shaderdata *s) { return s->m_PreprocessTechnique1EffectHandle != 0; }
static int psEffect_hasPreProcess2 (struct shaderdata *s) { return s->m_PreprocessTechnique2EffectHandle != 0; }

int xD3D_goodenough (void)
{
	static int d3d_good;
	LPDIRECT3D9 d3dx;
	D3DCAPS9 d3dCaps;

	if (d3d_good > 0)
		return d3d_good;
	if (d3d_good < 0)
		return 0;
	d3d_good = -1;
	d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
	if (d3dx != NULL) {
		if (SUCCEEDED (d3dx->GetDeviceCaps (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
			if (((d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == D3DPTEXTURECAPS_NONPOW2CONDITIONAL) || !(d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2)) {
				if (!(d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) && (d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)) {
					d3d_good = 1;
					if (d3dCaps.PixelShaderVersion >= D3DPS_VERSION(1, 0)) {
						d3d_good = 2;
						if (shaderon < 0)
							shaderon = 1;
					}
				}
			}
		}
		d3dx->Release ();
	}
#if SHADER == 0
	shaderon = 0;
#endif
	if (shaderon < 0)
		shaderon = 0;
	return d3d_good > 0 ? d3d_good : 0;
}

int xD3D_canshaders (void)
{
	static int d3d_yesno = 0;
	HMODULE h;
	LPDIRECT3D9 d3dx;
	D3DCAPS9 d3dCaps;

	if (d3d_yesno < 0)
		return 0;
	if (d3d_yesno > 0)
		return 1;
	d3d_yesno = -1;
	h = LoadLibrary (D3DX9DLL);
	if (h != NULL) {
		FreeLibrary (h);
		d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
		if (d3dx != NULL) {
			if (SUCCEEDED (d3dx->GetDeviceCaps (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
				if (d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
					write_log (_T("Direct3D: Pixel shader 2.0+ support detected, shader filters enabled.\n"));
					d3d_yesno = 1;
				}
			}
			d3dx->Release ();
		}
	}
	return d3d_yesno > 0 ? 1 : 0;
}

static const char *fx10 = {

"// 3 (version)\n"
"//\n"
"// WinUAE Direct3D post processing shader\n"
"//\n"
"// by Toni Wilen 2012\n"
"\n"
"uniform extern float4x4 mtx;\n"
"uniform extern float2 maskmult;\n"
"uniform extern float2 maskshift;\n"
"uniform extern int filtermode;\n"
"uniform extern float2 texelsize;\n"
"\n"
"// final possibly filtered Amiga output\n"
"texture SourceTexture : SOURCETEXTURE;\n"
"\n"
"sampler	SourceSampler = sampler_state {\n"
"	Texture	  = (SourceTexture);\n"
"	MinFilter = POINT;\n"
"	MagFilter = POINT;\n"
"	MipFilter = NONE;\n"
"	AddressU  = Clamp;\n"
"	AddressV  = Clamp;\n"
"};\n"
"\n"
"\n"
"texture OverlayTexture : OVERLAYTEXTURE;\n"
"\n"
"sampler	OverlaySampler = sampler_state {\n"
"	Texture	  = (OverlayTexture);\n"
"	MinFilter = POINT;\n"
"	MagFilter = POINT;\n"
"	MipFilter = NONE;\n"
"	AddressU  = Wrap;\n"
"	AddressV  = Wrap;\n"
"};\n"
"\n"
"struct VS_OUTPUT_POST\n"
"{\n"
"	float4 Position		: POSITION;\n"
"	float2 CentreUV		: TEXCOORD0;\n"
"	float2 Selector		: TEXCOORD1;\n"
"};\n"
"\n"
"VS_OUTPUT_POST VS_Post(float3 pos : POSITION, float2 TexCoord : TEXCOORD0)\n"
"{\n"
"	VS_OUTPUT_POST Out = (VS_OUTPUT_POST)0;\n"
"\n"
"	Out.Position = mul(float4(pos, 1.0f), mtx);\n"
"	Out.CentreUV = TexCoord;\n"
"	Out.Selector = TexCoord * maskmult + maskshift;\n"
"	return Out;\n"
"}\n"
"\n"
"float4 PS_Post(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	float4 o = tex2D(OverlaySampler, inp.Selector);\n"
"	return s * o;\n"
"}\n"
"\n"
"float4 PS_PostAlpha(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	float4 o = tex2D(OverlaySampler, inp.Selector);\n"
"	return s * (1 - o.a) + (o * o.a);\n"
"}\n"
"\n"
"float4 PS_PostPlain(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	return s;\n"
"}\n"
"\n"
"// source and overlay texture\n"
"technique PostTechnique\n"
"{\n"
"    pass P0\n"
"    {\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_1_0 PS_Post();\n"
"    }  \n"
"}\n"
"\n"
"// source and scanline texture with alpha\n"
"technique PostTechniqueAlpha\n"
"{\n"
"	pass P0\n"
"	{\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_1_0 PS_PostAlpha();\n"
"    } \n"
"}\n"
"\n"
"// only source texture\n"
"technique PostTechniquePlain\n"
"{\n"
"	pass P0\n"
"	{\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_1_0 PS_PostPlain();\n"
"    }\n"
"}\n"
};

static const char *fx20 = {

"// 3 (version)\n"
"//\n"
"// WinUAE Direct3D post processing shader\n"
"//\n"
"// by Toni Wilen 2012\n"
"\n"
"uniform extern float4x4 mtx;\n"
"uniform extern float2 maskmult;\n"
"uniform extern float2 maskshift;\n"
"uniform extern int filtermode;\n"
"uniform extern float2 texelsize;\n"
"\n"
"// final possibly filtered Amiga output\n"
"texture SourceTexture : SOURCETEXTURE;\n"
"\n"
"sampler	SourceSampler = sampler_state {\n"
"	Texture	  = (SourceTexture);\n"
"	MinFilter = filtermode;\n"
"	MagFilter = filtermode;\n"
"	MipFilter = NONE;\n"
"	AddressU  = Clamp;\n"
"	AddressV  = Clamp;\n"
"};\n"
"\n"
"\n"
"texture OverlayTexture : OVERLAYTEXTURE;\n"
"\n"
"sampler	OverlaySampler = sampler_state {\n"
"	Texture	  = (OverlayTexture);\n"
"	MinFilter = POINT;\n"
"	MagFilter = POINT;\n"
"	MipFilter = NONE;\n"
"	AddressU  = Wrap;\n"
"	AddressV  = Wrap;\n"
"};\n"
"\n"
"struct VS_OUTPUT_POST\n"
"{\n"
"	float4 Position		: POSITION;\n"
"	float2 CentreUV		: TEXCOORD0;\n"
"	float2 Selector		: TEXCOORD1;\n"
"};\n"
"\n"
"VS_OUTPUT_POST VS_Post(float3 pos : POSITION, float2 TexCoord : TEXCOORD0)\n"
"{\n"
"	VS_OUTPUT_POST Out = (VS_OUTPUT_POST)0;\n"
"\n"
"	Out.Position = mul(float4(pos, 1.0f), mtx);\n"
"	Out.CentreUV = TexCoord;\n"
"	Out.Selector = TexCoord * maskmult + maskshift;\n"
"	return Out;\n"
"}\n"
"\n"
"float4 PS_Post(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	float4 o = tex2D(OverlaySampler, inp.Selector);\n"
"	return s * o;\n"
"}\n"
"\n"
"float4 PS_PostAlpha(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	float4 o = tex2D(OverlaySampler, inp.Selector);\n"
"	return s * (1 - o.a) + (o * o.a);\n"
"}\n"
"\n"
"float4 PS_PostPlain(in VS_OUTPUT_POST inp) : COLOR\n"
"{\n"
"	float4 s = tex2D(SourceSampler, inp.CentreUV);\n"
"	return s;\n"
"}\n"
"\n"
"// source and overlay texture\n"
"technique PostTechnique\n"
"{\n"
"    pass P0\n"
"    {\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_2_0 PS_Post();\n"
"    }  \n"
"}\n"
"\n"
"// source and scanline texture with alpha\n"
"technique PostTechniqueAlpha\n"
"{\n"
"	pass P0\n"
"	{\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_2_0 PS_PostAlpha();\n"
"    } \n"
"}\n"
"\n"
"// only source texture\n"
"technique PostTechniquePlain\n"
"{\n"
"	pass P0\n"
"	{\n"
"		VertexShader = compile vs_1_0 VS_Post();\n"
"		PixelShader  = compile ps_2_0 PS_PostPlain();\n"
"    }\n"
"}\n"
};	

static bool psEffect_LoadEffect (struct d3dstruct *d3d, const TCHAR *shaderfile, int full, struct shaderdata *s, int num)
{
	int ret = 0;
	LPD3DXEFFECTCOMPILER EffectCompiler = NULL;
	LPD3DXBUFFER Errors = NULL;
	LPD3DXBUFFER BufferEffect = NULL;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH], tmp3[MAX_DPATH];
	LPD3DXEFFECT effect = NULL;
	static int first;
	DWORD compileflags = d3d->psEnabled ? 0 : D3DXSHADER_USE_LEGACY_D3DX9_31_DLL;
	int canusefile = 0, existsfile = 0;
	bool plugin_path;
	D3DXEFFECT_DESC EffectDesc;

	compileflags |= EFFECTCOMPILERFLAGS;
	plugin_path = get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), _T("filtershaders\\direct3d"));
	_tcscpy (tmp3, tmp);
	_tcscat (tmp, shaderfile);
	if (!full) {
		struct zfile *z = zfile_fopen (tmp, _T("r"));
		if (z) {
			existsfile = 1;
			zfile_fgets (tmp2, sizeof tmp2 / sizeof (TCHAR), z);
			zfile_fclose (z);
			int ver = _tstol (tmp2 + 2);
			if (ver == EFFECT_VERSION) {
				canusefile = 1;
			} else {
				write_log (_T("'%s' mismatched version (%d != %d)\n"), tmp, ver, EFFECT_VERSION);
			}
		}
		hr = E_FAIL;
		if (canusefile) {
			write_log (_T("%s: Attempting to load '%s'\n"), D3DHEAD, tmp);
			hr = D3DXCreateEffectCompilerFromFile (tmp, NULL, NULL, compileflags, &EffectCompiler, &Errors);
			if (FAILED (hr))
				write_log (_T("%s: D3DXCreateEffectCompilerFromFile failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
		}
		if (FAILED (hr)) {
			const char *str = d3d->psEnabled ? fx20 : fx10;
			int len = strlen (str);
			if (!existsfile && plugin_path) {
				struct zfile *z = zfile_fopen (tmp, _T("w"));
				if (z) {
					zfile_fwrite ((void*)str, len, 1, z);
					zfile_fclose (z);
				}
			}
			hr = D3DXCreateEffectCompiler (str, len, NULL, NULL, compileflags, &EffectCompiler, &Errors);
			if (FAILED (hr)) {
				write_log (_T("%s: D3DXCreateEffectCompiler failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
				goto end;
			}
		}
	} else {
		write_log (_T("%s: Attempting to load '%s'\n"), D3DHEAD, tmp);
		hr = D3DXCreateEffectCompilerFromFile (tmp, NULL, NULL, compileflags, &EffectCompiler, &Errors);
		if (FAILED (hr)) {
			write_log (_T("%s: D3DXCreateEffectCompilerFromFile failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
			goto end;
		}
	}

	if (Errors) {
		write_log (_T("%s: '%s' warning: %s\n"), D3DHEAD, shaderfile, D3DX_ErrorString (hr, Errors));
		Errors->Release();
		Errors = NULL;
	}

	hr = EffectCompiler->CompileEffect (0, &BufferEffect, &Errors);
	if (FAILED (hr)) {
		write_log (_T("%s: CompileEffect failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	void *bp = BufferEffect->GetBufferPointer ();
	int bplen = BufferEffect->GetBufferSize ();
	hr = D3DXCreateEffect (d3d->d3ddev,
		bp, bplen,
		NULL, NULL,
		EFFECTCOMPILERFLAGS,
		NULL, &effect, &Errors);
	if (FAILED (hr)) {
		write_log (_T("%s: D3DXCreateEffect failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	hr = effect->GetDesc (&EffectDesc);
	if (FAILED (hr)) {
		write_log (_T("%s: effect->GetDesc() failed: %s\n"), D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	if (full) {
		if (!psEffect_ParseParameters (d3d, EffectCompiler, effect, EffectDesc, s))
			goto end;
	} else {
		if (!postEffect_ParseParameters (d3d, EffectCompiler, effect, s))
			goto end;
	}
	ret = 1;
	d3d->frames_since_init = 0;
	if (plugin_path && d3d->filenotificationhandle == NULL)
		d3d->filenotificationhandle = FindFirstChangeNotification (tmp3, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

end:
	if (Errors)
		Errors->Release ();
	if (BufferEffect)
		BufferEffect->Release ();
	if (EffectCompiler)
		EffectCompiler->Release ();

	if (full) {
		s->psPreProcess = FALSE;
		if (ret) {
			d3d->psActive = TRUE;
			if (psEffect_hasPreProcess (s))
				s->psPreProcess = TRUE;
		}
	}

	if (ret)
		write_log (_T("%s: pixelshader filter '%s':%d enabled\n"), D3DHEAD, tmp, num);
	else
		write_log (_T("%s: pixelshader filter '%s':%d failed to initialize\n"), D3DHEAD, tmp, num);
	s->pEffect = effect;
	return effect != NULL;
}

static int psEffect_SetMatrices (D3DXMATRIXA16 *matProj, D3DXMATRIXA16 *matView, D3DXMATRIXA16 *matWorld, struct shaderdata *s)
{
	HRESULT hr;

	if (s->m_MatWorldEffectHandle) {
		hr = s->pEffect->SetMatrix (s->m_MatWorldEffectHandle, matWorld);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matWorld %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_MatViewEffectHandle) {
		hr = s->pEffect->SetMatrix (s->m_MatViewEffectHandle, matView);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matView %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_MatProjEffectHandle) {
		hr = s->pEffect->SetMatrix (s->m_MatProjEffectHandle, matProj);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matProj %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_MatWorldViewEffectHandle) {
		D3DXMATRIXA16 matWorldView;
		D3DXMatrixMultiply (&matWorldView, matWorld, matView);
		hr = s->pEffect->SetMatrix (s->m_MatWorldViewEffectHandle, &matWorldView);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matWorldView %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_MatViewProjEffectHandle) {
		D3DXMATRIXA16 matViewProj;
		D3DXMatrixMultiply (&matViewProj, matView, matProj);
		hr = s->pEffect->SetMatrix (s->m_MatViewProjEffectHandle, &matViewProj);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matViewProj %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_MatWorldViewProjEffectHandle) {
		D3DXMATRIXA16 tmp, matWorldViewProj;
		D3DXMatrixMultiply (&tmp, matWorld, matView);
		D3DXMatrixMultiply (&matWorldViewProj, &tmp, matProj);
		hr = s->pEffect->SetMatrix (s->m_MatWorldViewProjEffectHandle, &matWorldViewProj);
		if (FAILED (hr)) {
			write_log (_T("%s: Create:SetMatrix:matWorldViewProj %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	return 1;
}

static int psEffect_SetTextures (LPDIRECT3DTEXTURE9 lpSource, struct shaderdata *s)
{
	HRESULT hr;
	D3DXVECTOR4 fDims, fTexelSize;

	if (!s->m_SourceTextureEffectHandle) {
		write_log (_T("%s: Texture with SOURCETEXTURE semantic not found\n"), D3DHEAD);
		return 0;
	}
	hr = s->pEffect->SetTexture (s->m_SourceTextureEffectHandle, lpSource);
	if (FAILED (hr)) {
		write_log (_T("%s: SetTextures:lpSource %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	if (s->m_WorkingTexture1EffectHandle) {
		hr = s->pEffect->SetTexture (s->m_WorkingTexture1EffectHandle, s->lpWorkTexture1);
		if (FAILED (hr)) {
			write_log (_T("%s: SetTextures:lpWorking1 %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_WorkingTexture2EffectHandle) {
		hr = s->pEffect->SetTexture (s->m_WorkingTexture2EffectHandle, s->lpWorkTexture2);
		if (FAILED (hr)) {
			write_log (_T("%s: SetTextures:lpWorking2 %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_Hq2xLookupTextureHandle) {
		hr = s->pEffect->SetTexture (s->m_Hq2xLookupTextureHandle, s->lpHq2xLookupTexture);
		if (FAILED (hr)) {
			write_log (_T("%s: SetTextures:lpHq2xLookupTexture %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	fDims.x = 256; fDims.y = 256; fDims.z  = 1; fDims.w = 1;
	fTexelSize.x = 1; fTexelSize.y = 1; fTexelSize.z = 1; fTexelSize.w = 1; 
	if (lpSource) {
		D3DSURFACE_DESC Desc;
		lpSource->GetLevelDesc (0, &Desc);
		fDims.x = (FLOAT) Desc.Width;
		fDims.y = (FLOAT) Desc.Height;
	}

	fTexelSize.x = 1.0f / fDims.x;
	fTexelSize.y = 1.0f / fDims.y;

	if (s->m_SourceDimsEffectHandle) {
		hr = s->pEffect->SetVector(s->m_SourceDimsEffectHandle, &fDims);
		if (FAILED(hr)) {
			write_log(_T("%s: SetTextures:SetVector:Source %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return 0;
		}
	}
	if (s->m_InputDimsEffectHandle) {
		hr = s->pEffect->SetVector(s->m_InputDimsEffectHandle, &fDims);
		if (FAILED(hr)) {
			write_log(_T("%s: SetTextures:SetVector:Input %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return 0;
		}
	}
	if (s->m_TargetDimsEffectHandle) {
		D3DXVECTOR4 fDimsTarget;
		fDimsTarget.x = s->targettex_width;
		fDimsTarget.y = s->targettex_height;
		fDimsTarget.z = 1;
		fDimsTarget.w = 1;
		hr = s->pEffect->SetVector(s->m_TargetDimsEffectHandle, &fDimsTarget);
		if (FAILED(hr)) {
			write_log(_T("%s: SetTextures:SetVector:Target %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return 0;
		}
	}
	if (s->m_TexelSizeEffectHandle) {
		hr = s->pEffect->SetVector (s->m_TexelSizeEffectHandle, &fTexelSize);
		if (FAILED (hr)) {
			write_log (_T("%s: SetTextures:SetVector:Texel %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (s->m_ScaleEffectHandle) {
		D3DXVECTOR4 fScale;
		fScale.x = 1 << currprefs.gfx_resolution;
		fScale.y = 1 << currprefs.gfx_vresolution;
		fScale.w = fScale.z = 1;
		hr = s->pEffect->SetVector(s->m_ScaleEffectHandle, &fScale);
		if (FAILED(hr)) {
			write_log(_T("%s: SetTextures:SetVector:Scale %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return 0;
		}
	}
	if (s->framecounterHandle)
		s->pEffect->SetFloat(s->framecounterHandle, timeframes);

	return 1;
}

static int psEffect_Begin (enum psEffect_Pass pass, UINT *pPasses, struct shaderdata *s)
{
	HRESULT hr;
	LPD3DXEFFECT effect = s->pEffect;
	switch (pass)
	{
	case psEffect_PreProcess1:
		hr = effect->SetTechnique (s->m_PreprocessTechnique1EffectHandle);
		break;
	case psEffect_PreProcess2:
		hr = effect->SetTechnique (s->m_PreprocessTechnique2EffectHandle);
		break;
	case psEffect_Combine:
		hr = effect->SetTechnique (s->m_CombineTechniqueEffectHandle);
		break;
	default:
		hr = S_OK;
		break;
	}
	if (FAILED (hr)) {
		write_log (_T("%s: SetTechnique: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	hr = effect->Begin (pPasses, 0);
	if (FAILED (hr)) {
		write_log (_T("%s: Begin: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}

static int psEffect_BeginPass (LPD3DXEFFECT effect, UINT Pass)
{
	HRESULT hr;

	hr = effect->BeginPass (Pass);
	if (FAILED (hr)) {
		write_log (_T("%s: BeginPass: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_EndPass (LPD3DXEFFECT effect)
{
	HRESULT hr;

	hr = effect->EndPass ();
	if (FAILED (hr)) {
		write_log (_T("%s: EndPass: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_End (LPD3DXEFFECT effect)
{
	HRESULT hr;

	hr = effect->End ();
	if (FAILED (hr)) {
		write_log (_T("%s: End: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}

static LPDIRECT3DTEXTURE9 createtext (struct d3dstruct *d3d, int w, int h, D3DFORMAT format)
{
	LPDIRECT3DTEXTURE9 t;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	hr = d3d->d3ddev->CreateTexture (w, h, 1, D3DUSAGE_DYNAMIC, format, D3DPOOL_DEFAULT, &t, NULL);
	if (FAILED (hr))
		write_log (_T("%s: CreateTexture() D3DUSAGE_DYNAMIC failed: %s (%d*%d %08x)\n"),
			D3DHEAD, D3D_ErrorString (hr), w, h, format);
	if (FAILED (hr)) {
		hr = d3d->d3ddev->CreateTexture (w, h, 1, 0, format, D3DPOOL_DEFAULT, &t, NULL);
	}
	if (FAILED (hr)) {
		write_log (_T("%s: CreateTexture() failed: %s (%d*%d %08x)\n"),
			D3DHEAD, D3D_ErrorString (hr), w, h, format);
		return 0;
	}
	hr = t->LockRect (0, &locked, NULL, 0);
	if (SUCCEEDED (hr)) {
		int y;
		int wb;
		wb = w * 4;
		if (wb > locked.Pitch)
			wb = w * 2;
		if (wb > locked.Pitch)
			wb = w * 1;
		for (y = 0; y < h; y++)
			memset ((uae_u8*)locked.pBits + y * locked.Pitch, 0, wb);
		t->UnlockRect (0);
	}
	return t;
}

static int allocextratextures (struct d3dstruct *d3d, struct shaderdata *s, int w, int h)
{
	HRESULT hr;
	if (FAILED (hr = d3d->d3ddev->CreateTexture (w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s->lpWorkTexture1, NULL))) {
		write_log (_T("%s: Failed to create working texture1: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), s - &d3d->shaders[0]);
		return 0;
	}
	if (FAILED (hr = d3d->d3ddev->CreateTexture (w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &s->lpWorkTexture2, NULL))) {
		write_log (_T("%s: Failed to create working texture2: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), s - &d3d->shaders[0]);
		return 0;
	}
	write_log (_T("%s: %d*%d working texture:%d\n"), D3DHEAD, w, h, s - &d3d->shaders[0]);
	return 1;
}

static int createamigatexture (struct d3dstruct *d3d, int w, int h)
{
	HRESULT hr;

	d3d->texture = createtext (d3d, w, h, d3d->tformat);
	if (!d3d->texture)
		return 0;
	write_log (_T("%s: %d*%d main texture, depth %d\n"), D3DHEAD, w, h, d3d->t_depth);
	if (d3d->psActive) {
		for (int i = 0; i < MAX_SHADERS; i++) {
			int w2, h2;
			int type = d3d->shaders[i].type;
			if (type == SHADERTYPE_BEFORE) {
				w2 = d3d->shaders[i].worktex_width;
				h2 = d3d->shaders[i].worktex_height;
				if (!allocextratextures (d3d, &d3d->shaders[i], w, h))
					return 0;
			} else if (type == SHADERTYPE_MIDDLE) {
				w2 = d3d->shaders[i].worktex_width;
				h2 = d3d->shaders[i].worktex_height;
			} else {
				w2 = d3d->window_w;
				h2 = d3d->window_h;
			}
			if (type == SHADERTYPE_BEFORE || type == SHADERTYPE_AFTER || type == SHADERTYPE_MIDDLE) {
				D3DLOCKED_BOX lockedBox;
				if (FAILED (hr = d3d->d3ddev->CreateVolumeTexture (256, 16, 256, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d->shaders[i].lpHq2xLookupTexture, NULL))) {
					write_log (_T("%s: Failed to create volume texture: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), i);
					return 0;
				}
				if (FAILED (hr = d3d->shaders[i].lpHq2xLookupTexture->LockBox (0, &lockedBox, NULL, 0))) {
					write_log (_T("%s: Failed to lock box of volume texture: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), i);
					return 0;
				}
				write_log (_T("HQ2X texture (%dx%d) (%dx%d):%d\n"), w2, h2, w, h, i);
				BuildHq2xLookupTexture (w2, h2, w, h,  (unsigned char*)lockedBox.pBits);
				d3d->shaders[i].lpHq2xLookupTexture->UnlockBox (0);
			}
		}
	}
	return 1;
}

static int createtexture (struct d3dstruct *d3d, int ow, int oh, int win_w, int win_h)
{
	HRESULT hr;
	bool haveafter = false;

	int zw, zh;

	if (ow > win_w * d3d->dmultxh && oh > win_h * d3d->dmultxv) {
		zw = ow;
		zh = oh;
	} else {
		zw = win_w * d3d->dmultxh;
		zh = win_h * d3d->dmultxv;
	}
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].type == SHADERTYPE_BEFORE || d3d->shaders[i].type == SHADERTYPE_AFTER || d3d->shaders[i].type == SHADERTYPE_MIDDLE) {
			int w2, h2, w, h;
			if (d3d->shaders[i].type == SHADERTYPE_AFTER) {
				w2 = zw; h2 = zh;
				w = zw; h = zh;
				haveafter = true;
				if (!allocextratextures (d3d, &d3d->shaders[i], d3d->window_w, d3d->window_h))
					return 0;
			} else if (d3d->shaders[i].type == SHADERTYPE_MIDDLE) {
				// worktex_width = 800
				// extratex = amiga res
				w2 = zw; h2 = zh;
				w = zw; h = zh;
				if (!allocextratextures (d3d, &d3d->shaders[i], ow, oh))
					return 0;
			} else {
				w2 = ow;
				h2 = oh;
				w = ow;
				h = oh;
			}
			d3d->shaders[i].targettex_width = w2;
			d3d->shaders[i].targettex_height = h2;
			if (FAILED (hr = d3d->d3ddev->CreateTexture (w2, h2, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d->shaders[i].lpTempTexture, NULL))) {
				write_log (_T("%s: Failed to create working texture1: %s:%d:%d\n"), D3DHEAD, D3D_ErrorString (hr), i, d3d->shaders[i].type);
				return 0;
			}
			write_log (_T("%s: %d*%d temp texture:%d:%d\n"), D3DHEAD, w2, h2, i, d3d->shaders[i].type);
			d3d->shaders[i].worktex_width = w;
			d3d->shaders[i].worktex_height = h;
		}
	}
	if (haveafter) {
		if (FAILED (hr = d3d->d3ddev->CreateTexture (d3d->window_w, d3d->window_h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &d3d->lpPostTempTexture, NULL))) {
			write_log (_T("%s: Failed to create temp texture: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		write_log (_T("%s: %d*%d after texture\n"), D3DHEAD, d3d->window_w, d3d->window_h);
	}
	return 1;
}

static void updateleds (struct d3dstruct *d3d)
{
	D3DLOCKED_RECT locked;
	HRESULT hr;
	static uae_u32 rc[256], gc[256], bc[256], a[256];
	static int done;

	if (!done) {
		for (int i = 0; i < 256; i++) {
			rc[i] = i << 16;
			gc[i] = i << 8;
			bc[i] = i << 0;
			a[i] = i << 24;
		}
		done = 1;
	}

	if (d3d != &d3ddata[0])
		return;

	hr = d3d->ledtexture->LockRect (0, &locked, NULL, D3DLOCK_DISCARD);
	if (FAILED (hr)) {
		write_log (_T("%d: SL LockRect failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return;
	}

	for (int y = 0; y < TD_TOTAL_HEIGHT * d3d->statusbar_vx; y++) {
		uae_u8 *buf = (uae_u8*)locked.pBits + y * locked.Pitch;
		statusline_single_erase(d3d - d3ddata, buf, 32 / 8, y, d3d->ledwidth * d3d->statusbar_hx);
	}
	statusline_render(d3d - d3ddata, (uae_u8*)locked.pBits, 32 / 8, locked.Pitch, d3d->ledwidth, d3d->ledheight, rc, gc, bc, a);

	int y = 0;
	for (int yy = 0; yy < d3d->statusbar_vx * TD_TOTAL_HEIGHT; yy++) {
		uae_u8 *buf = (uae_u8*)locked.pBits + yy * locked.Pitch;
		draw_status_line_single(d3d - d3ddata, buf, 32 / 8, y, d3d->ledwidth, rc, gc, bc, a);
		if ((yy % d3d->statusbar_vx) == 0)
			y++;
	}

	d3d->ledtexture->UnlockRect (0);
}

static int createledtexture (struct d3dstruct *d3d)
{
	d3d->ledwidth = d3d->window_w;
	d3d->ledheight = TD_TOTAL_HEIGHT;
	if (d3d->statusbar_hx < 1)
		d3d->statusbar_hx = 1;
	if (d3d->statusbar_vx < 1)
		d3d->statusbar_vx = 1;
	d3d->ledtexture = createtext (d3d, d3d->ledwidth * d3d->statusbar_hx, d3d->ledheight * d3d->statusbar_vx, D3DFMT_A8R8G8B8);
	if (!d3d->ledtexture)
		return 0;
	return 1;
}

static int createsltexture (struct d3dstruct *d3d)
{
	d3d->sltexture = createtext (d3d, d3d->required_sl_texture_w, d3d->required_sl_texture_h, d3d->t_depth < 32 ? D3DFMT_A4R4G4B4 : D3DFMT_A8R8G8B8);
	if (!d3d->sltexture)
		return 0;
	write_log (_T("%s: SL %d*%d texture allocated\n"), D3DHEAD, d3d->required_sl_texture_w, d3d->required_sl_texture_h);
	d3d->maskmult_x = 1.0f;
	d3d->maskmult_y = 1.0f;
	return 1;
}

static void createscanlines (struct d3dstruct *d3d, int force)
{
	HRESULT hr;
	D3DLOCKED_RECT locked;
	int sl4, sl42;
	int l1, l2;
	int x, y, yy;
	uae_u8 *sld, *p;
	int bpp;

	if (d3d->scanline_osl1 == d3d->filterd3d->gfx_filter_scanlines &&
		d3d->scanline_osl3 == d3d->filterd3d->gfx_filter_scanlinelevel &&
		d3d->scanline_osl2 == d3d->filterd3d->gfx_filter_scanlineratio &&
		d3d->scanline_osl4 == d3d->filterd3d->gfx_filter_scanlineoffset &&
		!force)
		return;
	bpp = d3d->t_depth < 32 ? 2 : 4;
	d3d->scanline_osl1 = d3d->filterd3d->gfx_filter_scanlines;
	d3d->scanline_osl3 = d3d->filterd3d->gfx_filter_scanlinelevel;
	d3d->scanline_osl2 = d3d->filterd3d->gfx_filter_scanlineratio;
	d3d->scanline_osl4 = d3d->filterd3d->gfx_filter_scanlineoffset;
	sl4 = d3d->filterd3d->gfx_filter_scanlines * 16 / 100;
	sl42 = d3d->filterd3d->gfx_filter_scanlinelevel * 16 / 100;
	if (sl4 > 15)
		sl4 = 15;
	if (sl42 > 15)
		sl42 = 15;
	l1 = (d3d->filterd3d->gfx_filter_scanlineratio >> 0) & 15;
	l2 = (d3d->filterd3d->gfx_filter_scanlineratio >> 4) & 15;

	if (l1 + l2 <= 0)
		return;

	if (!d3d->sltexture) {
		if (d3d->scanline_osl1 == 0 && d3d->scanline_osl3 == 0)
			return;
		if (!createsltexture (d3d))
			return;
	}

	hr = d3d->sltexture->LockRect (0, &locked, NULL, 0);
	if (FAILED (hr)) {
		write_log (_T("%s: SL LockRect failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	sld = (uae_u8*)locked.pBits;
	for (y = 0; y < d3d->required_sl_texture_h; y++) {
		memset(sld + y * locked.Pitch, 0, d3d->required_sl_texture_w * bpp);
	}
	for (y = 0; y < d3d->required_sl_texture_h; y += l1 + l2) {
		int y2 = y + (d3d->filterd3d->gfx_filter_scanlineoffset % (l1 + 1));
		for (yy = 0; yy < l2 && y2 + yy < d3d->required_sl_texture_h; yy++) {
			for (x = 0; x < d3d->required_sl_texture_w; x++) {
				uae_u8 sll = sl42;
				p = &sld[(y2 + yy) * locked.Pitch + (x * bpp)];
				if (bpp < 4) {
					/* 16-bit, A4R4G4B4 */
					p[1] = (sl4 << 4) | (sll << 0);
					p[0] = (sll << 4) | (sll << 0);
				} else {
					/* 32-bit, A8R8G8B8 */
					uae_u8 sll4 = sl4 | (sl4 << 4);
					uae_u8 sll2 = sll | (sll << 4);
					p[0] = sll2;
					p[1] = sll2;
					p[2] = sll2;
					p[3] = sll4;
				}
			}
		}
	}
	d3d->sltexture->UnlockRect (0);
}

#include "png.h"

struct uae_image
{
	uae_u8 *data;
	int width, height, pitch;
};

struct png_cb
{
	uae_u8 *ptr;
	int size;
};

static void __cdecl readcallback(png_structp png_ptr, png_bytep out, png_size_t count)
{
	png_voidp io_ptr = png_get_io_ptr(png_ptr);

	if (!io_ptr)
		return;
	struct png_cb *cb = (struct png_cb*)io_ptr;
	if (count > cb->size)
		count = cb->size;
	memcpy(out, cb->ptr, count);
	cb->ptr += count;
	cb->size -= count;
}

static bool load_png_image(struct zfile *zf, struct uae_image *img)
{
	extern unsigned char test_card_png[];
	extern unsigned int test_card_png_len;
	uae_u8 *b = test_card_png;
	uae_u8 *bfree = NULL;
	png_structp png_ptr;
	png_infop info_ptr;
	png_uint_32 width, height;
	int depth, color_type;
	struct png_cb cb;
	png_bytepp row_pp;
	png_size_t cols;
	bool ok = false;

	memset(img, 0, sizeof(struct uae_image));
	int size;
	uae_u8 *bb = zfile_getdata(zf, 0, -1, &size);
	if (!bb)
		goto end;
	b = bb;
	bfree = bb;

	if (!png_check_sig(b, 8))
		goto end;

	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
	if (!png_ptr)
		goto end;
	info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, 0, 0);
		goto end;
	}
	cb.ptr = b;
	cb.size = size;
	png_set_read_fn(png_ptr, &cb, readcallback);

	png_read_info(png_ptr, info_ptr);

	png_get_IHDR(png_ptr, info_ptr, &width, &height, &depth, &color_type, 0, 0, 0);

	if (color_type == PNG_COLOR_TYPE_PALETTE)
		png_set_expand(png_ptr);
	if (color_type == PNG_COLOR_TYPE_GRAY && depth < 8)
		png_set_expand(png_ptr);

	if (depth > 8)
		png_set_strip_16(png_ptr);
	if (depth < 8)
		png_set_packing(png_ptr);
	if (!(color_type & PNG_COLOR_MASK_ALPHA))
		png_set_add_alpha(png_ptr, 0xff, PNG_FILLER_AFTER);

	png_set_bgr(png_ptr);

	cols = png_get_rowbytes(png_ptr, info_ptr);

	img->pitch = width * 4;
	img->width = width;
	img->height = height;

	row_pp = new png_bytep[height];

	img->data = xcalloc(uae_u8, width * height * 4);

	for (int i = 0; i < height; i++) {
		row_pp[i] = (png_bytep)&img->data[i * img->pitch];
	}

	png_read_image(png_ptr, row_pp);
	png_read_end(png_ptr, info_ptr);

	png_destroy_read_struct(&png_ptr, &info_ptr, 0);

	delete[] row_pp;

	ok = true;
end:
	xfree(bfree);

	return ok;
}

static void free_uae_image(struct uae_image *img)
{
	if (!img)
		return;
	xfree(img->data);
	img->data = NULL;
}

static int findedge(struct uae_image *img, int w, int h, int dx, int dy)
{
	int x = w / 2;
	int y = h / 2;

	if (dx != 0)
		x = dx < 0 ? 0 : w - 1;
	if (dy != 0)
		y = dy < 0 ? 0 : h - 1;

	for (;;) {
		uae_u32 *p = (uae_u32*)(img->data + y * img->pitch + x * 4);
		int alpha = (*p) >> 24;
		if (alpha != 255)
			break;
		x -= dx;
		y -= dy;
		if (x <= 0 || y <= 0)
			break;
		if (x >= w - 1 || y >= h - 1)
			break;
	}
	if (dx)
		return x;
	return y;
}

static void narrowimg(struct uae_image *img, int *xop, int *yop, const TCHAR *name)
{
	int x1, x2, y1, y2;

	for (y1 = 0; y1 < img->height; y1++) {
		bool tline = true;
		for (int x = 0; x < img->width; x++) {
			uae_u8 *p = img->data + y1 * img->pitch + x * 4;
			if (p[3] != 0x00)
				tline = false;
		}
		if (!tline)
			break;
	}

	for (y2 = img->height - 1; y2 >= y1; y2--) {
		bool tline = true;
		for (int x = 0; x < img->width; x++) {
			uae_u8 *p = img->data + y2 * img->pitch + x * 4;
			if (p[3] != 0x00)
				tline = false;
		}
		if (!tline)
			break;
	}

	for (x1 = 0; x1 < img->width; x1++) {
		bool tline = true;
		for (int y = y1; y <= y2; y++) {
			uae_u8 *p = img->data + y * img->pitch + x1 * 4;
			if (p[3] != 0x00)
				tline = false;
		}
		if (!tline)
			break;
	}

	for (x2 = img->width - 1; x2 >= x1; x2--) {
		bool tline = true;
		for (int y = y1; y <= y2; y++) {
			uae_u8 *p = img->data + y * img->pitch + x2 * 4;
			if (p[3] != 0x00)
				tline = false;
		}
		if (!tline)
			break;
	}

	int w = x2 - x1 + 1;
	int h = y2 - y1 + 1;
	int pitch = w * 4;

	*xop = x1;
	*yop = y1;

	uae_u8 *d = xcalloc(uae_u8, img->width * img->height * 4);
	for (int y = 0; y < h; y++) {
		uae_u8 *dp = d + y * pitch;
		uae_u8 *sp = img->data + (y + y1) * img->pitch + x1 * 4;
		memcpy(dp, sp, w * 4);
	}

	write_log(_T("Overlay LED: '%s' %d*%d -> %d*%d (%d*%d - %d*%d)\n"), name, img->width, img->height, w, h, x1, y1, x2, y2);

	xfree(img->data);
	img->width = w;
	img->height = h;
	img->pitch = pitch;
	img->data = d;
}

static uae_u8 dimming(uae_u8 v)
{
	return v * currprefs.power_led_dim / 100;
}

static int createmask2texture (struct d3dstruct *d3d, const TCHAR *filename)
{
	struct AmigaMonitor *mon = &AMonitors[d3d - d3ddata];
	struct zfile *zf;
	LPDIRECT3DTEXTURE9 tx;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH];
	TCHAR filepath[MAX_DPATH];
	D3DLOCKED_RECT locked;

	if (d3d->mask2texture)
		d3d->mask2texture->Release();
	d3d->mask2texture = NULL;
	for (int i = 0; overlayleds[i]; i++) {
		if (d3d->mask2textureleds[i])
			d3d->mask2textureleds[i]->Release();
		d3d->mask2textureleds[i] = NULL;
	}
	if (d3d->mask2textureled_power_dim)
		d3d->mask2textureled_power_dim->Release();
	d3d->mask2textureled_power_dim = NULL;

	if (filename[0] == 0 || WIN32GFX_IsPicassoScreen(mon))
		return 0;

	zf = NULL;
	for (int i = 0; i < 2; i++) {
		if (i == 0) {
			get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), _T("overlays"));
			_tcscat (tmp, filename);
		} else {
			_tcscpy (tmp, filename);
		}
		TCHAR tmp2[MAX_DPATH], tmp3[MAX_DPATH];
		_tcscpy (tmp3, tmp);
		TCHAR *s = _tcsrchr (tmp3, '.');
		if (s) {
			TCHAR *s2 = s;
			while (s2 > tmp3) {
				TCHAR v = *s2;
				if (v == '_') {
					s = s2;
					break;
				}
				if (v == 'X' || v == 'x') {
					s2--;
					continue;
				}
				if (!_istdigit (v))
					break;
				s2--;
			}
			_tcscpy (tmp2, s);
			_stprintf (s, _T("_%dx%d%s"), d3d->window_w, d3d->window_h, tmp2);
			_tcscpy(filepath, tmp3);
			zf = zfile_fopen (tmp3, _T("rb"), ZFD_NORMAL);
			if (zf)
				break;
			float aspect = (float)d3d->window_w / d3d->window_h;
			int ax = -1, ay = -1;
			if (abs (aspect - 16.0 / 10.0) <= 0.1)
				ax = 16, ay = 10;
			if (abs (aspect - 16.0 / 9.0) <= 0.1)
				ax = 16, ay = 9;
			if (abs (aspect - 4.0 / 3.0) <= 0.1)
				ax = 4, ay = 3;
			if (ax > 0 && ay > 0) {
				_stprintf (s, _T("_%dx%d%s"), ax, ay, tmp2);
				_tcscpy(filepath, tmp3);
				zf = zfile_fopen (tmp3, _T("rb"), ZFD_NORMAL);
				if (zf)
					break;
			}
		}
		_tcscpy(filepath, tmp);
		zf = zfile_fopen (tmp, _T("rb"), ZFD_NORMAL);
		if (zf)
			break;
	}
	if (!zf) {
		write_log (_T("%s: couldn't open overlay '%s'\n"), D3DHEAD, filename);
		return 0;
	}
	struct uae_image img;
	if (!load_png_image(zf, &img)) {
		write_log(_T("Overlay texture '%s' load failed.\n"), filename);
		goto end;
	}

	tx = createtext(d3d, img.width, img.height, D3DFMT_A8R8G8B8);
	if (!tx) {
		write_log(_T("%s: overlay texture load failed.\n"), D3DHEAD);
		goto end;
	}

	d3d->mask2texture_w = img.width;
	d3d->mask2texture_h = img.height;
	d3d->mask2texture = tx;

	hr = tx->LockRect(0, &locked, NULL, 0);
	if (FAILED(hr)) {
		write_log(_T("%s: Overlay LockRect failed: %s\n"), D3DHEAD, D3D_ErrorString(hr));
		goto end;
	}
	for (int i = 0; i < img.height; i++) {
		memcpy((uae_u8*)locked.pBits + i * locked.Pitch, img.data + i * img.pitch, img.width * 4);
	}
	tx->UnlockRect(0);

	d3d->mask2rect.left = findedge(&img, d3d->mask2texture_w, d3d->mask2texture_h, -1, 0);
	d3d->mask2rect.right = findedge(&img, d3d->mask2texture_w, d3d->mask2texture_h, 1, 0);
	d3d->mask2rect.top = findedge(&img, d3d->mask2texture_w, d3d->mask2texture_h, 0, -1);
	d3d->mask2rect.bottom = findedge(&img, d3d->mask2texture_w, d3d->mask2texture_h, 0, 1);

	if (d3d->mask2rect.left >= d3d->mask2texture_w / 2 || d3d->mask2rect.top >= d3d->mask2texture_h / 2 ||
		d3d->mask2rect.right <= d3d->mask2texture_w / 2 || d3d->mask2rect.bottom <= d3d->mask2texture_h / 2) {
		d3d->mask2rect.left = 0;
		d3d->mask2rect.top = 0;
		d3d->mask2rect.right = d3d->mask2texture_w;
		d3d->mask2rect.bottom = d3d->mask2texture_h;
	}
	d3d->mask2texture_multx = (float)d3d->window_w / d3d->mask2texture_w;
	d3d->mask2texture_multy = (float)d3d->window_h / d3d->mask2texture_h;
	d3d->mask2texture_offsetw = 0;

	if (isfullscreen () > 0) {
		struct MultiDisplay *md = getdisplay(&currprefs, mon->monitor_id);
		float deskw = md->rect.right - md->rect.left;
		float deskh = md->rect.bottom - md->rect.top;
		//deskw = 800; deskh = 600;
		float dstratio = deskw / deskh;
		float srcratio = d3d->mask2texture_w / d3d->mask2texture_h;
		d3d->mask2texture_multx *= srcratio / dstratio;
	} else {
		d3d->mask2texture_multx = d3d->mask2texture_multy;
	}

	d3d->mask2texture_wh = d3d->window_h;
	d3d->mask2texture_ww = d3d->mask2texture_w * d3d->mask2texture_multx; 

	d3d->mask2texture_offsetw = (d3d->window_w - d3d->mask2texture_ww) / 2;

	if (d3d->mask2texture_offsetw > 0)
		d3d->blanktexture = createtext (d3d, d3d->mask2texture_offsetw + 1, d3d->window_h, D3DFMT_X8R8G8B8);

	float xmult = d3d->mask2texture_multx;
	float ymult = d3d->mask2texture_multy;

	d3d->mask2rect.left *= xmult;
	d3d->mask2rect.right *= xmult;
	d3d->mask2rect.top *= ymult;
	d3d->mask2rect.bottom *= ymult;
	d3d->mask2texture_wwx = d3d->mask2texture_w * xmult;
	if (d3d->mask2texture_wwx > d3d->window_w)
		d3d->mask2texture_wwx = d3d->window_w;
	if (d3d->mask2texture_wwx < d3d->mask2rect.right - d3d->mask2rect.left)
		d3d->mask2texture_wwx = d3d->mask2rect.right - d3d->mask2rect.left;
	if (d3d->mask2texture_wwx > d3d->mask2texture_ww)
		d3d->mask2texture_wwx = d3d->mask2texture_ww;

	d3d->mask2texture_minusx = - ((d3d->window_w - d3d->mask2rect.right) + d3d->mask2rect.left);
	if (d3d->mask2texture_offsetw > 0)
		d3d->mask2texture_minusx += d3d->mask2texture_offsetw * xmult;
	

	d3d->mask2texture_minusy = -(d3d->window_h - (d3d->mask2rect.bottom - d3d->mask2rect.top));

	d3d->mask2texture_hhx = d3d->mask2texture_h * ymult;

	write_log (_T("%s: overlay '%s' %.0f*%.0f (%d*%d - %d*%d) (%d*%d)\n"),
		D3DHEAD, tmp, d3d->mask2texture_w, d3d->mask2texture_h,
		d3d->mask2rect.left, d3d->mask2rect.top, d3d->mask2rect.right, d3d->mask2rect.bottom,
		d3d->mask2rect.right - d3d->mask2rect.left, d3d->mask2rect.bottom - d3d->mask2rect.top);

	for (int i = 0; overlayleds[i]; i++) {
		if (!overlayleds[i][0])
			continue;
		TCHAR tmp1[MAX_DPATH];
		_tcscpy(tmp1, filepath);
		_tcscpy(tmp1 + _tcslen(tmp1) - 4, _T("_"));
		_tcscpy(tmp1 + _tcslen(tmp1), overlayleds[i]);
		_tcscat(tmp1, _T("_led"));
		_tcscat(tmp1, filepath + _tcslen(filepath) - 4);
		zf = zfile_fopen(tmp1, _T("rb"), ZFD_NORMAL);
		if (zf) {
			struct uae_image ledimg;
			if (load_png_image(zf, &ledimg)) {
				if (ledimg.width == img.width && ledimg.height == img.height) {
					narrowimg(&ledimg, &d3d->mask2textureledoffsets[i * 2 + 0], &d3d->mask2textureledoffsets[i * 2 + 1], tmp1);
					d3d->mask2textureleds[i] = createtext(d3d, ledimg.width, ledimg.height, D3DFMT_A8R8G8B8);
					if (d3d->mask2textureleds[i]) {
						hr = d3d->mask2textureleds[i]->LockRect(0, &locked, NULL, 0);
						if (SUCCEEDED(hr)) {
							for (int j = 0; j < ledimg.height; j++) {
								memcpy((uae_u8*)locked.pBits + j * locked.Pitch, ledimg.data + j * ledimg.pitch, ledimg.width * 4);
							}
							d3d->mask2textureleds[i]->UnlockRect(0);
						}
					}
					if (ledtypes[i] == LED_POWER) {
						d3d->mask2textureled_power_dim = createtext(d3d, ledimg.width, ledimg.height, D3DFMT_A8R8G8B8);
						if (d3d->mask2textureled_power_dim) {
							hr = d3d->mask2textureled_power_dim->LockRect(0, &locked, NULL, 0);
							if (SUCCEEDED(hr)) {
								for (int j = 0; j < ledimg.height; j++) {
									uae_u8 *pd = (uae_u8*)locked.pBits + j * locked.Pitch;
									uae_u8 *ps = ledimg.data + j * ledimg.pitch;
									for (int k = 0; k < ledimg.width; k++) {
										pd[0] = dimming(ps[0]);
										pd[1] = dimming(ps[1]);
										pd[2] = dimming(ps[2]);
										pd[3] = ps[3];
										pd += 4;
										ps += 4;
									}
								}
								d3d->mask2textureled_power_dim->UnlockRect(0);
							}
						}
					}
				} else {
					write_log(_T("Overlay led '%s' size mismatch.\n"), tmp1);
				}
				free_uae_image(&ledimg);
			} else {
				write_log(_T("Overlay led '%s' load failed.\n"), tmp1);
			}
			zfile_fclose(zf);
		}
	}

	free_uae_image(&img);

	return 1;
end:
	free_uae_image(&img);
	if (tx)
		tx->Release ();
	if (d3d->blanktexture)
		d3d->blanktexture->Release();
	d3d->blanktexture = NULL;
	return 0;
}

static int createmasktexture (struct d3dstruct *d3d, const TCHAR *filename, struct shaderdata *sd)
{
	struct zfile *zf;
	int size;
	uae_u8 *buf;
	D3DSURFACE_DESC maskdesc, txdesc;
	LPDIRECT3DTEXTURE9 tx;
	HRESULT hr;
	D3DLOCKED_RECT lock, slock;
	D3DXIMAGE_INFO dinfo;
	TCHAR tmp[MAX_DPATH];
	int maskwidth, maskheight;
	int idx = sd - &d3d->shaders[0];

	if (filename[0] == 0)
		return 0;
	tx = NULL;
	get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), _T("masks"));
	_tcscat (tmp, filename);
	zf = zfile_fopen (tmp, _T("rb"), ZFD_NORMAL);
	if (!zf) {
		zf = zfile_fopen (filename, _T("rb"), ZFD_NORMAL);
		if (!zf) {
			write_log (_T("%s: couldn't open mask '%s':%d\n"), D3DHEAD, filename, idx);
			return 0;
		}
	}
	size = zfile_size (zf);
	buf = xmalloc (uae_u8, size);
	zfile_fread (buf, size, 1, zf);
	zfile_fclose (zf);
	hr = D3DXCreateTextureFromFileInMemoryEx (d3d->d3ddev, buf, size,
		 D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8,
		 D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, &dinfo, NULL, &tx);
	xfree (buf);
	if (FAILED (hr)) {
		write_log (_T("%s: temp mask texture load failed: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), idx);
		goto end;
	}
	hr = tx->GetLevelDesc (0, &txdesc);
	if (FAILED (hr)) {
		write_log (_T("%s: mask image texture GetLevelDesc() failed: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), idx);
		goto end;
	}
	sd->masktexture_w = dinfo.Width;
	sd->masktexture_h = dinfo.Height;
#if 0
	if (txdesc.Width == masktexture_w && txdesc.Height == masktexture_h && psEnabled) {
		// texture size == image size, no need to tile it (Wrap sampler does the rest)
		if (masktexture_w < window_w || masktexture_h < window_h) {
			maskwidth = window_w;
			maskheight = window_h;
		} else {
			masktexture = tx;
			tx = NULL;
		}
	} else {
#endif
		// both must be divisible by mask size
		maskwidth = ((d3d->window_w + sd->masktexture_w - 1) / sd->masktexture_w) * sd->masktexture_w;
		maskheight = ((d3d->window_h + sd->masktexture_h - 1) / sd->masktexture_h) * sd->masktexture_h;
#if 0
	}
#endif
	if (tx) {
		sd->masktexture = createtext (d3d, maskwidth, maskheight, D3DFMT_X8R8G8B8);
		if (FAILED (hr)) {
			write_log (_T("%s: mask texture creation failed: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), idx);
			goto end;
		}
		hr = sd->masktexture->GetLevelDesc (0, &maskdesc);
		if (FAILED (hr)) {
			write_log (_T("%s: mask texture GetLevelDesc() failed: %s:%d\n"), D3DHEAD, D3D_ErrorString (hr), idx);
			goto end;
		}
		if (SUCCEEDED (hr = sd->masktexture->LockRect (0, &lock, NULL, 0))) {
			if (SUCCEEDED (hr = tx->LockRect (0, &slock, NULL, 0))) {
				int x, y, sx, sy;
				uae_u32 *sptr, *ptr;
				sy = 0;
				for (y = 0; y < maskdesc.Height; y++) {
					sx = 0;
					for (x = 0; x < maskdesc.Width; x++) {
						uae_u32 v;
						sptr = (uae_u32*)((uae_u8*)slock.pBits + sy * slock.Pitch + sx * 4);
						ptr = (uae_u32*)((uae_u8*)lock.pBits + y * lock.Pitch + x * 4);
						v = *sptr;
	//					v &= 0x00FFFFFF;
	//					v |= 0x80000000;
						*ptr = v;
						sx++;
						if (sx >= dinfo.Width)
							sx = 0;
					}
					sy++;
					if (sy >= dinfo.Height)
						sy = 0;
				}
				tx->UnlockRect (0);
			}
			sd->masktexture->UnlockRect (0);
		}
		tx->Release ();
		sd->masktexture_w = maskdesc.Width;
		sd->masktexture_h = maskdesc.Height;
	}
	write_log (_T("%s: mask %d*%d (%d*%d) %d*%d ('%s':%d) texture allocated\n"), D3DHEAD, sd->masktexture_w, sd->masktexture_h, txdesc.Width, txdesc.Height, maskdesc.Width, maskdesc.Height, filename, idx);
	d3d->maskmult_x = (float)d3d->window_w / sd->masktexture_w;
	d3d->maskmult_y = (float)d3d->window_h / sd->masktexture_h;

	return 1;
end:
	if (sd->masktexture)
		sd->masktexture->Release ();
	sd->masktexture = NULL;
	if (tx)
		tx->Release ();
	return 0;
}

static bool xD3D_getscalerect(int monid, float *mx, float *my, float *sx, float *sy)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;

	if (!d3d->mask2texture)
		return false;

	float mw = d3d->mask2rect.right - d3d->mask2rect.left;
	float mh = d3d->mask2rect.bottom - d3d->mask2rect.top;

	float mxt = (float)mw / vidinfo->outbuffer->inwidth2;
	float myt = (float)mh / vidinfo->outbuffer->inheight2;

	*mx = d3d->mask2texture_minusx / mxt;
	*my = d3d->mask2texture_minusy / myt;

	*sx = -((d3d->mask2texture_ww - d3d->mask2rect.right) - (d3d->mask2rect.left)) / 2;
	*sy = -((d3d->mask2texture_wh - d3d->mask2rect.bottom) - (d3d->mask2rect.top)) / 2;

	*sx /= mxt;
	*sy /= myt;

	return true;
}

static void setupscenecoords (struct d3dstruct *d3d, bool normalrender)
{
	int monid = d3d - d3ddata;
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
	RECT sr, dr, zr;
	float w, h;
	float dw, dh;
	static RECT sr2[MAX_AMIGAMONITORS], dr2[MAX_AMIGAMONITORS], zr2[MAX_AMIGAMONITORS];

	if (!normalrender)
		return;

	//write_log (_T("%dx%d %dx%d %dx%d\n"), tin_w, tin_h, tin_w, tin_h, window_w, window_h);

	getfilterrect2 (monid, &dr, &sr, &zr, d3d->window_w, d3d->window_h, d3d->tin_w / d3d->dmult, d3d->tin_h / d3d->dmult, d3d->dmult, d3d->tin_w, d3d->tin_h);

	if (memcmp (&sr, &sr2[monid], sizeof RECT) || memcmp (&dr, &dr2[monid], sizeof RECT) || memcmp (&zr, &zr2[monid], sizeof RECT)) {
		write_log (_T("POS (%d %d %d %d) - (%d %d %d %d)[%d,%d] (%d %d)\n"),
			dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom,
			sr.right - sr.left, sr.bottom - sr.top,
			zr.left, zr.top);
		sr2[monid] = sr;
		dr2[monid] = dr;
		zr2[monid] = zr;
	}

	dw = dr.right - dr.left;
	dh = dr.bottom - dr.top;
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;

	d3d->fakesize.x = w;
	d3d->fakesize.y = h;
	d3d->fakesize.w = 1;
	d3d->fakesize.z = 1;

	MatrixOrthoOffCenterLH (&d3d->m_matProj_out, 0, w + 0.05f, 0, h + 0.05f, 0.0f, 1.0f);

	float tx, ty;
	float sw, sh;

	if (0 && d3d->mask2texture) {

		float mw = d3d->mask2rect.right - d3d->mask2rect.left;
		float mh = d3d->mask2rect.bottom - d3d->mask2rect.top;

		tx = -0.5f + dw * d3d->tin_w / mw / 2;
		ty = +0.5f + dh * d3d->tin_h / mh / 2;

		float xshift = -zr.left;
		float yshift = -zr.top;

		sw = dw * d3d->tin_w / vidinfo->outbuffer->inwidth2;
		sw *= mw / d3d->window_w;

		tx = -0.5f + d3d->window_w / 2;

		sh = dh * d3d->tin_h / vidinfo->outbuffer->inheight2;
		sh *= mh / d3d->window_h;

		ty = +0.5f + d3d->window_h / 2;

		tx += xshift;
		ty += yshift;

	} else {

		tx = -0.5f + dw * d3d->tin_w / d3d->window_w / 2;
		ty = +0.5f + dh * d3d->tin_h / d3d->window_h / 2;

		float xshift = - zr.left - sr.left; // - (tin_w - 2 * zr.left - w),
		float yshift = + zr.top + sr.top - (d3d->tin_h - h);
	
		sw = dw * d3d->tin_w / d3d->window_w;
		sh = dh * d3d->tin_h / d3d->window_h;

		//sw -= 0.5f;
		//sh += 0.5f;

		tx += xshift;
		ty += yshift;

	}

	MatrixTranslation (&d3d->m_matView_out, tx, ty, 1.0f);

	MatrixScaling (&d3d->m_matWorld_out, sw + 0.5f / sw, sh + 0.5f / sh, 1.0f);

	d3d->cursor_offset_x = -zr.left;
	d3d->cursor_offset_y = -zr.top;

	//write_log (_T("%.1fx%.1f %.1fx%.1f %.1fx%.1f\n"), dw, dh, w, h, sw, sh);

	// ratio between Amiga texture and overlay mask texture
	float sw2 = dw * d3d->tin_w / d3d->window_w;
	float sh2 = dh * d3d->tin_h / d3d->window_h;

	//sw2 -= 0.5f;
	//sh2 += 0.5f;

	d3d->maskmult.x = sw2 * d3d->maskmult_x / w;
	d3d->maskmult.y = sh2 * d3d->maskmult_y / h;

	d3d->maskshift.x = 1.0f / d3d->maskmult_x;
	d3d->maskshift.y = 1.0f / d3d->maskmult_y;

	D3DXMATRIXA16 tmpmatrix;
	D3DXMatrixMultiply (&tmpmatrix, &d3d->m_matWorld_out, &d3d->m_matView_out);
	D3DXMatrixMultiply (&d3d->postproj, &tmpmatrix, &d3d->m_matProj_out);
}

#if 0
uae_u8 *getfilterbuffer3d(struct vidbuffer *vb, int *widthp, int *heightp, int *pitch, int *depth)
{
	struct d3dstruct *d3d = &d3ddata[0];
	RECT dr, sr, zr;
	uae_u8 *p;
	int w, h;

	*depth = d3d->t_depth;
	getfilterrect2 (&dr, &sr, &zr, d3d->window_w, d3d->window_h, d3d->tin_w / d3d->dmult, d3d->tin_h / d3d->dmult, d3d->dmult, d3d->tin_w, d3d->tin_h);
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;
	p = vb->bufmem;
	if (pitch)
		*pitch = vb->rowbytes;
	p += (zr.top - h / 2) * vb->rowbytes + (zr.left - w / 2) * d3d->t_depth / 8;
	*widthp = w;
	*heightp = h;
	return p;
}
#endif

static void createvertex (struct d3dstruct *d3d)
{
	HRESULT hr;
	struct TLVERTEX *vertices;
	float sizex, sizey;

	sizex = 1.0f;
	sizey = 1.0f;
	if (FAILED (hr = d3d->vertexBuffer->Lock (0, 0, (void**)&vertices, 0))) {
		write_log (_T("%s: Vertexbuffer lock failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	memset (vertices, 0, sizeof (struct TLVERTEX) * NUMVERTICES);
	//Setup vertices
	vertices[0].position.x = -0.5f; vertices[0].position.y = -0.5f;
	vertices[0].diffuse  = 0xFFFFFFFF;
	vertices[0].texcoord.x = 0.0f; vertices[0].texcoord.y = sizey;
	vertices[1].position.x = -0.5f; vertices[1].position.y = 0.5f;
	vertices[1].diffuse  = 0xFFFFFFFF;
	vertices[1].texcoord.x = 0.0f; vertices[1].texcoord.y = 0.0f;
	vertices[2].position.x = 0.5f; vertices[2].position.y = -0.5f;
	vertices[2].diffuse  = 0xFFFFFFFF;
	vertices[2].texcoord.x = sizex; vertices[2].texcoord.y = sizey;
	vertices[3].position.x = 0.5f; vertices[3].position.y = 0.5f;
	vertices[3].diffuse  = 0xFFFFFFFF;
	vertices[3].texcoord.x = sizex; vertices[3].texcoord.y = 0.0f;
	// Additional vertices required for some PS effects
	vertices[4].position.x = 0.0f; vertices[4].position.y = 0.0f;
	vertices[4].diffuse  = 0xFFFFFF00;
	vertices[4].texcoord.x = 0.0f; vertices[4].texcoord.y = 1.0f;
	vertices[5].position.x = 0.0f; vertices[5].position.y = 1.0f;
	vertices[5].diffuse  = 0xFFFFFF00;
	vertices[5].texcoord.x = 0.0f; vertices[5].texcoord.y = 0.0f;
	vertices[6].position.x = 1.0f; vertices[6].position.y = 0.0f;
	vertices[6].diffuse  = 0xFFFFFF00;
	vertices[6].texcoord.x = 1.0f; vertices[6].texcoord.y = 1.0f;
	vertices[7].position.x = 1.0f; vertices[7].position.y = 1.0f;
	vertices[7].diffuse  = 0xFFFFFF00;
	vertices[7].texcoord.x = 1.0f; vertices[7].texcoord.y = 0.0f;
	if (FAILED(hr = d3d->vertexBuffer->Unlock ()))
		write_log (_T("%s: Vertexbuffer unlock failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
}

static void settransform_pre (struct d3dstruct *d3d, struct shaderdata *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	MatrixOrthoOffCenterLH (&d3d->m_matProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	MatrixTranslation (&d3d->m_matView, -0.5f / d3d->tout_w, 0.5f / d3d->tout_h, 0.0f);
	// Identity for world
	D3DXMatrixIdentity (&d3d->m_matWorld);
}

static void settransform (struct d3dstruct *d3d, struct shaderdata *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	MatrixOrthoOffCenterLH (&d3d->m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	MatrixTranslation (&d3d->m_matPreView, -0.5f / d3d->tout_w, 0.5f / d3d->tout_h, 0.0f);
	// Identity for world
	D3DXMatrixIdentity (&d3d->m_matPreWorld);

	if (s)
		psEffect_SetMatrices (&d3d->m_matProj, &d3d->m_matView, &d3d->m_matWorld, s);

	MatrixOrthoOffCenterLH (&d3d->m_matProj2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	MatrixTranslation (&d3d->m_matView2, 0.5f - 0.5f / d3d->tout_w, 0.5f + 0.5f / d3d->tout_h, 0.0f);

	D3DXMatrixIdentity (&d3d->m_matWorld2);
}

static void settransform2 (struct d3dstruct *d3d, struct shaderdata *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	MatrixOrthoOffCenterLH (&d3d->m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	MatrixTranslation (&d3d->m_matPreView, -0.5f / d3d->window_w, 0.5f / d3d->window_h, 0.0f);
	// Identity for world
	D3DXMatrixIdentity (&d3d->m_matPreWorld);

	MatrixOrthoOffCenterLH (&d3d->m_matProj2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	MatrixTranslation (&d3d->m_matView2, 0.5f - 0.5f / d3d->window_w, 0.5f + 0.5f / d3d->window_h, 0.0f);
	D3DXMatrixIdentity (&d3d->m_matWorld2);
}

static void freetextures (struct d3dstruct *d3d)
{
	if (d3d->texture) {
		d3d->texture->Release ();
		d3d->texture = NULL;
	}
	for (int i = 0; i < MAX_SHADERS; i++) {
		struct shaderdata *s = &d3d->shaders[i];
		if (s->lpTempTexture) {
			s->lpTempTexture->Release ();
			s->lpTempTexture = NULL;
		}
		if (s->lpWorkTexture1) {
			s->lpWorkTexture1->Release ();
			s->lpWorkTexture1 = NULL;
		}
		if (s->lpWorkTexture2) {
			s->lpWorkTexture2->Release ();
			s->lpWorkTexture2 = NULL;
		}
		if (s->lpHq2xLookupTexture) {
			s->lpHq2xLookupTexture->Release ();
			s->lpHq2xLookupTexture = NULL;
		}
	}
	if (d3d->lpPostTempTexture) {
		d3d->lpPostTempTexture->Release();
		d3d->lpPostTempTexture = NULL;
	}
}

static void getswapchain (struct d3dstruct *d3d)
{
	if (!d3d->d3dswapchain) {
		HRESULT hr = d3d->d3ddev->GetSwapChain (0, &d3d->d3dswapchain);
		if (FAILED (hr)) {
			write_log (_T("%s: GetSwapChain() failed, %s\n"), D3DHEAD, D3D_ErrorString (hr));
		}
	}
}

static void invalidatedeviceobjects (struct d3dstruct *d3d)
{
	if (d3d->filenotificationhandle != NULL)
		FindCloseChangeNotification  (d3d->filenotificationhandle);
	d3d->filenotificationhandle = NULL;
	freetextures (d3d);
	if (d3d->query) {
		d3d->query->Release();
		d3d->query = NULL;
	}
	if (d3d->sprite) {
		d3d->sprite->Release ();
		d3d->sprite = NULL;
	}
	if (d3d->ledtexture) {
		d3d->ledtexture->Release ();
		d3d->ledtexture = NULL;
	}
	if (d3d->sltexture) {
		d3d->sltexture->Release ();
		d3d->sltexture = NULL;
	}
	if (d3d->mask2texture) {
		d3d->mask2texture->Release ();
		d3d->mask2texture = NULL;
	}
	for (int i = 0; overlayleds[i]; i++) {
		if (d3d->mask2textureleds[i])
			d3d->mask2textureleds[i]->Release();
		d3d->mask2textureleds[i] = NULL;
	}
	if (d3d->mask2textureled_power_dim) {
		d3d->mask2textureled_power_dim->Release();
		d3d->mask2textureled_power_dim = NULL;
	}
	if (d3d->blanktexture) {
		d3d->blanktexture->Release ();
		d3d->blanktexture = NULL;
	}
	if (d3d->cursorsurfaced3d) {
		d3d->cursorsurfaced3d->Release ();
		d3d->cursorsurfaced3d = NULL;
	}
	struct d3d9overlay *ov = d3d->extoverlays;
	while (ov) {
		struct d3d9overlay *next = ov->next;
		if (ov->tex)
			ov->tex->Release();
		xfree(ov);
		ov = next;
	}
	d3d->extoverlays = NULL;
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].pEffect) {
			d3d->shaders[i].pEffect->Release ();
			d3d->shaders[i].pEffect = NULL;
		}
		if (d3d->shaders[i].masktexture) {
			d3d->shaders[i].masktexture->Release ();
			d3d->shaders[i].masktexture = NULL;
		}
		memset (&d3d->shaders[i], 0, sizeof (struct shaderdata));
	}
	postEffect_freeParameters(d3d);
	if (d3d->d3ddev)
		d3d->d3ddev->SetStreamSource (0, NULL, 0, 0);
	if (d3d->vertexBuffer) {
		d3d->vertexBuffer->Release ();
		d3d->vertexBuffer = NULL;
	}
	if (d3d->d3dswapchain)  {
		d3d->d3dswapchain->Release ();
		d3d->d3dswapchain = NULL;
	}
	d3d->locked = 0;
	d3d->maskshift.x = d3d->maskshift.y = d3d->maskshift.z = d3d->maskshift.w = 0;
	d3d->maskmult.x = d3d->maskmult.y = d3d->maskmult.z = d3d->maskmult.w = 0;
}

static struct shaderdata *allocshaderslot (struct d3dstruct *d3d, int type)
{
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].type == 0) {
			d3d->shaders[i].type = type;
			return &d3d->shaders[i];
		}
	}
	return NULL;
}

static int restoredeviceobjects (struct d3dstruct *d3d)
{
	int vbsize;
	int wasshader = shaderon;
	HRESULT hr;

	invalidatedeviceobjects (d3d);
	getswapchain (d3d);

	while (shaderon > 0) {
		d3d->shaders[SHADER_POST].type = SHADERTYPE_POST;
		if (!psEffect_LoadEffect (d3d, d3d->psEnabled ? _T("_winuae.fx") : _T("_winuae_old.fx"), false, &d3d->shaders[SHADER_POST], -1)) {
			shaderon = 0;
			break;
		}
		for (int i = 0; i < MAX_FILTERSHADERS; i++) {
			if (d3d->filterd3d->gfx_filtershader[i][0]) {
				struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_BEFORE);
				if (!psEffect_LoadEffect (d3d, d3d->filterd3d->gfx_filtershader[i], true, s, i)) {
					d3d->filterd3d->gfx_filtershader[i][0] = changed_prefs.gf[d3d->filterd3didx].gfx_filtershader[i][0] = 0;
					break;
				}
			}
			if (d3d->filterd3d->gfx_filtermask[i][0]) {
				struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_MASK_BEFORE);
				createmasktexture (d3d, d3d->filterd3d->gfx_filtermask[i], s);
			}
		}
		if (d3d->filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS][0]) {
			struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_MIDDLE);
			if (!psEffect_LoadEffect (d3d, d3d->filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS], true, s, 2 * MAX_FILTERSHADERS)) {
				d3d->filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS][0] = changed_prefs.gf[d3d->filterd3didx].gfx_filtershader[2 * MAX_FILTERSHADERS][0] = 0;
			}
		}
		if (d3d->filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS][0]) {
			struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_MASK_MIDDLE);
			createmasktexture (d3d, d3d->filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS], s);
		}
		for (int i = 0; i < MAX_FILTERSHADERS; i++) {
			if (d3d->filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS][0]) {
				struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_AFTER);
				if (!psEffect_LoadEffect (d3d, d3d->filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS], true, s, i + MAX_FILTERSHADERS)) {
					d3d->filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS][0] = changed_prefs.gf[d3d->filterd3didx].gfx_filtershader[i + MAX_FILTERSHADERS][0] = 0;
					break;
				}
			}
			if (d3d->filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS][0]) {
				struct shaderdata *s = allocshaderslot (d3d, SHADERTYPE_MASK_AFTER);
				createmasktexture (d3d, d3d->filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS], s);
			}
		}
		break;
	}
	if (d3d->filterd3d->gfx_filter_scanlines > 0) {
		createsltexture(d3d);
		createscanlines(d3d, 1);
	}
	if (wasshader && !shaderon)
		write_log (_T("Falling back to non-shader mode\n"));

	createmask2texture (d3d, d3d->filterd3d->gfx_filteroverlay);

	createledtexture (d3d);

	hr = D3DXCreateSprite (d3d->d3ddev, &d3d->sprite);
	if (FAILED (hr)) {
		write_log (_T("%s: D3DXSprite failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
	}

	int curw = CURSORMAXWIDTH, curh = CURSORMAXHEIGHT;
	d3d->cursorsurfaced3d = createtext (d3d, curw, curh, D3DFMT_A8R8G8B8);
	d3d->cursor_v = false;
	d3d->cursor_scale = false;

	vbsize = sizeof (struct TLVERTEX) * NUMVERTICES;
	if (FAILED (hr = d3d->d3ddev->CreateVertexBuffer (vbsize, D3DUSAGE_WRITEONLY,
		D3DFVF_TLVERTEX, D3DPOOL_DEFAULT, &d3d->vertexBuffer, NULL))) {
			write_log (_T("%s: failed to create vertex buffer: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
	}
	createvertex (d3d);
	if (FAILED (hr = d3d->d3ddev->SetFVF (D3DFVF_TLVERTEX)))
		write_log (_T("%s: SetFVF failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
	if (FAILED (hr = d3d->d3ddev->SetStreamSource (0, d3d->vertexBuffer, 0, sizeof (struct TLVERTEX))))
		write_log (_T("%s: SetStreamSource failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));

	hr = d3d->d3ddev->SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
	hr = d3d->d3ddev->SetRenderState (D3DRS_LIGHTING, FALSE);

	settransform (d3d, NULL);

	return 1;
}

static void D3D_free2 (struct d3dstruct *d3d)
{
	invalidatedeviceobjects (d3d);
	if (d3d->screenshotsurface)
		d3d->screenshotsurface->Release();
	d3d->screenshotsurface = NULL;
	if (d3d->d3ddev) {
		d3d->d3ddev->Release ();
		d3d->d3ddev = NULL;
	}
	if (d3d->d3d) {
		d3d->d3d->Release ();
		d3d->d3d = NULL;
	}
	d3d->d3d_enabled = 0;
	d3d->psActive = FALSE;
	d3d->resetcount = 0;
	d3d->devicelost = 0;
	d3d->renderdisabled = false;
	for (int i = 0; i < LED_MAX; i++) {
		leds[i] = 0;
	}
	changed_prefs.leds_on_screen &= ~STATUSLINE_TARGET;
	currprefs.leds_on_screen &= ~STATUSLINE_TARGET;
}

void xD3D_free (int monid, bool immediate)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	if (!fakemodewaitms || immediate) {
		waitfakemode (d3d);
		D3D_free2 (d3d);
		ddraw_fs_hack_free (d3d);
		return;
	}
}

#define VBLANKDEBUG 0

static bool xD3D_getvblankpos (int *vpos)
{
	struct d3dstruct *d3d = &d3ddata[0];
	HRESULT hr;
	D3DRASTER_STATUS rt;
#if VBLANKDEBUG
	static UINT lastline;
	static BOOL lastinvblank;
#endif
	*vpos = -2;
	if (!isd3d (d3d))
		return false;
	if (d3d->d3dswapchain)
		hr = d3d->d3dswapchain->GetRasterStatus (&rt);
	else
		hr = d3d->d3ddev->GetRasterStatus (0, &rt);
	if (FAILED (hr)) {
		write_log (_T("%s: GetRasterStatus %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return false;
	}
	if (rt.ScanLine > d3d->maxscanline)
		d3d->maxscanline = rt.ScanLine;
	*vpos = rt.ScanLine;
#if VBLANKDEBUG
	if (lastline != rt.ScanLine || lastinvblank != rt.InVBlank) {
		write_log(_T("%d:%d "), rt.InVBlank ? 1 : 0, rt.ScanLine);
		lastline = rt.ScanLine;
		lastinvblank = rt.InVBlank;
	}
#endif
	if (rt.InVBlank != 0)
		*vpos = -1;
	return true;
}

static void xD3D_vblank_reset (double freq)
{
	struct d3dstruct *d3d = &d3ddata[0];
	if (!isd3d (d3d))
		return;
}

static int getd3dadapter (IDirect3D9 *id3d)
{
	struct MultiDisplay *md = getdisplay(&currprefs, 0);
	int num = id3d->GetAdapterCount ();
	HMONITOR winmon;
	POINT pt;

	pt.x = (md->rect.right - md->rect.left) / 2 + md->rect.left;
	pt.y = (md->rect.bottom - md->rect.top) / 2 + md->rect.top;
	winmon = MonitorFromPoint (pt, MONITOR_DEFAULTTONEAREST);
	for (int i = 0; i < num; i++) {
		HMONITOR d3dmon = id3d->GetAdapterMonitor (i);
		if (d3dmon == winmon)
			return i;
	}
	return D3DADAPTER_DEFAULT;
}

static const TCHAR *D3D_init2 (struct d3dstruct *d3d, HWND ahwnd, int w_w, int w_h, int depth, int *freq, int mmulth, int mmultv)
{
	int monid = d3d - d3ddata;
	struct amigadisplay *ad = &adisplays[monid];
	HRESULT ret, hr;
	static TCHAR errmsg[300] = { 0 };
	D3DDISPLAYMODE mode = { 0 };
	D3DCAPS9 d3dCaps;
	int adapter;
	DWORD flags;
	HINSTANCE d3dDLL, d3dx;
	typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);
	LPDIRECT3DCREATE9EX d3dexp = NULL;
	int vsync = isvsync ();
	struct apmode *apm = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	struct apmode ap;
	D3DADAPTER_IDENTIFIER9 did;

	d3d->filterd3didx = ad->picasso_on;
	d3d->filterd3d = &currprefs.gf[d3d->filterd3didx];

	D3D_free2 (d3d);
	if (!currprefs.gfx_api) {
		_tcscpy (errmsg, _T("D3D: not enabled"));
		return errmsg;
	}

	xfree (d3d->fakebitmap);
	d3d->fakebitmap = xmalloc (uae_u8, w_w * depth);

	d3dx = LoadLibrary (D3DX9DLL);
	if (d3dx == NULL) {
		static bool warned;
		if (!warned) {
			if (os_vista)
				_tcscpy(errmsg, _T("Direct3D: Optional DirectX9 components are not installed.\n")
					_T("\nhttps://www.microsoft.com/en-us/download/details.aspx?id=8109"));
			else
				_tcscpy (errmsg, _T("Direct3D: Newer DirectX Runtime required or optional DirectX9 components are not installed.\n")
					_T("\nhttps://www.microsoft.com/en-us/download/details.aspx?id=8109"));
			warned = true;
		}
		return errmsg;
	}
	FreeLibrary (d3dx);

	D3D_goodenough ();
	D3D_canshaders ();

	d3d->d3d_ex = FALSE;
	d3dDLL = LoadLibrary (_T("D3D9.DLL"));
	if (d3dDLL == NULL) {
		_tcscpy (errmsg, _T("Direct3D: DirectX 9 or newer required"));
		return errmsg;
	} else {
		d3dexp  = (LPDIRECT3DCREATE9EX)GetProcAddress (d3dDLL, "Direct3DCreate9Ex");
		if (d3dexp)
			d3d->d3d_ex = TRUE;
	}
	FreeLibrary (d3dDLL);
	hr = -1;
	if (d3d->d3d_ex && D3DEX) {
		hr = d3dexp (D3D_SDK_VERSION, &d3d->d3dex);
		if (FAILED (hr))
			write_log (_T("Direct3D: failed to create D3DEx object: %s\n"), D3D_ErrorString (hr));
		d3d->d3d = (IDirect3D9*)d3d->d3dex;
	}
	if (FAILED (hr)) {
		d3d->d3d_ex = 0;
		d3d->d3dex = NULL;
		d3d->d3d = Direct3DCreate9 (D3D_SDK_VERSION);
		if (d3d->d3d == NULL) {
			D3D_free(monid, true);
			_tcscpy (errmsg, _T("Direct3D: failed to create D3D object"));
			return errmsg;
		}
	}
	if (d3d->d3d_ex)
		D3DHEAD = _T("D3D9Ex");
	else
		D3DHEAD = _T("D3D9");

	memcpy(&ap, apm, sizeof ap);

	if (os_dwm_enabled && isfullscreen() <= 0 && apm->gfx_backbuffers > 1 && !apm->gfx_vsync) {
		write_log(_T("Switch from triple buffer to double buffer (%d).\n"), apm->gfx_vflip);
		ap.gfx_vflip = 0;
		ap.gfx_backbuffers = 1;
	}

	adapter = getd3dadapter (d3d->d3d);

	d3d->modeex.Size = sizeof d3d->modeex;
	if (d3d->d3dex && D3DEX) {
		LUID luid;
		hr = d3d->d3dex->GetAdapterLUID (adapter, &luid);
		hr = d3d->d3dex->GetAdapterDisplayModeEx (adapter, &d3d->modeex, NULL);
	}
	if (FAILED (hr = d3d->d3d->GetAdapterDisplayMode (adapter, &mode)))
		write_log (_T("%s: GetAdapterDisplayMode failed %s\n"), D3DHEAD, D3D_ErrorString (hr));
	if (FAILED (hr = d3d->d3d->GetDeviceCaps (adapter, D3DDEVTYPE_HAL, &d3dCaps)))
		write_log (_T("%s: GetDeviceCaps failed %s\n"), D3DHEAD, D3D_ErrorString (hr));
	if (SUCCEEDED (hr = d3d->d3d->GetAdapterIdentifier (adapter, 0, &did))) {
		TCHAR *s = au (did.Description);
		write_log (_T("Device name: '%s' %llx.%x\n"), s, did.DriverVersion, did.Revision);
		xfree (s);
	}

	d3d->variablerefresh = false;
	cannoclear = ap.gfx_vsyncmode != 0;

	memset (&d3d->dpp, 0, sizeof (d3d->dpp));
	d3d->dpp.Windowed = isfullscreen () <= 0;
	d3d->dpp.BackBufferFormat = mode.Format;
	d3d->dpp.BackBufferCount = ap.gfx_backbuffers;
	d3d->dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3d->dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	d3d->dpp.BackBufferWidth = w_w;
	d3d->dpp.BackBufferHeight = w_h;
	d3d->dpp.PresentationInterval = d3d->variablerefresh ? D3DPRESENT_INTERVAL_DEFAULT : ((!ap.gfx_vflip || monid) ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE);

	d3d->modeex.Width = w_w;
	d3d->modeex.Height = w_h;
	d3d->modeex.RefreshRate = 0;
	d3d->modeex.ScanLineOrdering = ap.gfx_interlaced ? D3DSCANLINEORDERING_INTERLACED : D3DSCANLINEORDERING_PROGRESSIVE;
	d3d->modeex.Format = mode.Format;

	d3d->vsync2 = 0;
	int hzmult = 0;
	if (isfullscreen () > 0) {
		d3d->dpp.FullScreen_RefreshRateInHz = getrefreshrate(monid, d3d->modeex.Width, d3d->modeex.Height);
		d3d->modeex.RefreshRate = d3d->dpp.FullScreen_RefreshRateInHz;
		if (vsync > 0) {
			d3d->dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			getvsyncrate(monid, d3d->dpp.FullScreen_RefreshRateInHz, &hzmult);
			if (hzmult < 0) {
				if (!ap.gfx_strobo) {
					if (d3dCaps.PresentationIntervals & D3DPRESENT_INTERVAL_TWO)
						d3d->dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
				}  else {
					d3d->vsync2 = -2;
				}
			} else if (hzmult > 0) {
				d3d->vsync2 = 1;
			}
		}
		*freq = d3d->modeex.RefreshRate;
	} else {
		if (mode.RefreshRate > 0) {
			if (vsync > 0) {
				d3d->dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
				getvsyncrate(monid, mode.RefreshRate, &hzmult);
				if (hzmult < 0) {
					if (!ap.gfx_strobo) {
						if ((d3dCaps.PresentationIntervals & D3DPRESENT_INTERVAL_TWO) && isfullscreen() > 0)
							d3d->dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
					} else {
						d3d->vsync2 = -2;
					}
				} else if (hzmult > 0) {
					d3d->vsync2 = 1;
				}
			}
			*freq = mode.RefreshRate;
		}
	}

	if (vsync < 0) {
		d3d->vsync2 = 0;
		getvsyncrate(monid, isfullscreen() > 0 ? d3d->dpp.FullScreen_RefreshRateInHz : mode.RefreshRate, &hzmult);
		if (hzmult > 0) {
			d3d->vsync2 = 1;
		} else if (hzmult < 0) {
			if (ap.gfx_strobo) {
				d3d->vsync2 = -2;
			} else if (ap.gfx_vflip) {
				if ((d3dCaps.PresentationIntervals & D3DPRESENT_INTERVAL_TWO) && isfullscreen() > 0)
					d3d->dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
				else
					d3d->vsync2 = -1;
			}
		}
	}

	d3d->d3dhwnd = ahwnd;
	d3d->t_depth = depth;

	flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED;
	// Check if hardware vertex processing is available
	if(d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
		flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
	} else {
		flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	}
	if (d3d->d3d_ex && D3DEX) {
		ret = d3d->d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, d3d->dpp.Windowed ? NULL : &d3d->modeex, &d3d->d3ddevex);
		d3d->d3ddev = d3d->d3ddevex;
	} else {
		ret = d3d->d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, &d3d->d3ddev);
	}
	if (FAILED (ret) && (flags & D3DCREATE_PUREDEVICE)) {
		flags &= ~D3DCREATE_PUREDEVICE;
		if (d3d->d3d_ex && D3DEX) {
			ret = d3d->d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, d3d->dpp.Windowed ? NULL : &d3d->modeex, &d3d->d3ddevex);
			d3d->d3ddev = d3d->d3ddevex;
		} else {
			ret = d3d->d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, &d3d->d3ddev);
		}
		if (FAILED (ret) && (flags & D3DCREATE_HARDWARE_VERTEXPROCESSING)) {
			flags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
			flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
			if (d3d->d3d_ex && D3DEX) {
				ret = d3d->d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, d3d->dpp.Windowed ? NULL : &d3d->modeex, &d3d->d3ddevex);
				d3d->d3ddev = d3d->d3ddevex;
			} else {
				ret = d3d->d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3d->d3dhwnd, flags, &d3d->dpp, &d3d->d3ddev);
			}
		}
	}

	if (FAILED (ret)) {
		_stprintf (errmsg, _T("%s failed, %s\n"), d3d->d3d_ex && D3DEX ? _T("CreateDeviceEx") : _T("CreateDevice"), D3D_ErrorString (ret));
		if (ret == D3DERR_INVALIDCALL && d3d->dpp.Windowed == 0 && d3d->dpp.FullScreen_RefreshRateInHz && !d3d->ddraw_fs) {
			write_log (_T("%s\n"), errmsg);
			write_log (_T("%s: Retrying fullscreen with DirectDraw\n"), D3DHEAD);
			if (ddraw_fs_hack_init (d3d)) {
				const TCHAR *err2 = D3D_init (ahwnd, monid, w_w, w_h, depth, freq, mmulth, mmultv);
				if (err2)
					ddraw_fs_hack_free (d3d);
				return err2;
			}
		}
		if (d3d->d3d_ex && D3DEX) {
			write_log (_T("%s\n"), errmsg);
			D3DEX = 0;
			return D3D_init(ahwnd, monid, w_w, w_h, depth, freq, mmulth, mmultv);
		}
		D3D_free(monid, true);
		return errmsg;
	}

	if (d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
		d3d->psEnabled = TRUE;
	else
		d3d->psEnabled = FALSE;

	d3d->max_texture_w = d3dCaps.MaxTextureWidth;
	d3d->max_texture_h = d3dCaps.MaxTextureHeight;

	write_log (_T("%s: %08X %08X %08X %08X"), D3DHEAD, flags, d3dCaps.Caps, d3dCaps.Caps2, d3dCaps.Caps3);
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
		write_log (_T(" SQUAREONLY"));
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2)
		write_log (_T(" POW2"));
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL)
		write_log (_T(" NPOTCONDITIONAL"));
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_ALPHA)
		write_log (_T(" ALPHA"));
	if (d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)
		write_log (_T(" DYNAMIC"));
	if (d3dCaps.Caps & D3DCAPS_READ_SCANLINE)
		write_log (_T(" SCANLINE"));
	
	write_log (_T("\n"));

	write_log (_T("%s: PS=%d.%d VS=%d.%d %d*%d*%d%s%s VS=%d B=%d%s %d-bit %d (%dx%d)\n"),
		D3DHEAD,
		(d3dCaps.PixelShaderVersion >> 8) & 0xff, d3dCaps.PixelShaderVersion & 0xff,
		(d3dCaps.VertexShaderVersion >> 8) & 0xff, d3dCaps.VertexShaderVersion & 0xff,
		d3d->modeex.Width, d3d->modeex.Height,
		d3d->dpp.FullScreen_RefreshRateInHz,
		ap.gfx_interlaced ? _T("i") : _T("p"),
		d3d->dpp.Windowed ? _T("") : _T(" FS"),
		vsync, ap.gfx_backbuffers,
		ap.gfx_vflip < 0 ? _T("WE") : (ap.gfx_vflip > 0 ? _T("WS") :  _T("I")), 
		d3d->t_depth, adapter,
		d3d->max_texture_w, d3d->max_texture_h
	);

#if 0
	if ((d3dCaps.PixelShaderVersion < D3DPS_VERSION(2,0) || !d3d->psEnabled || d3d->max_texture_w < 2048 || d3d->max_texture_h < 2048 || (!shaderon && SHADER > 0)) && d3d->d3d_ex) {
		D3DEX = 0;
		write_log (_T("Disabling D3D9Ex\n"));
		if (d3d->d3ddev) {
			d3d->d3ddev->Release ();
			d3d->d3ddev = NULL;
		}
		if (d3d->d3d) {
			d3d->d3d->Release ();
			d3d->d3d = NULL;
		}
		d3d->d3ddevex = NULL;
		return D3D_init (ahwnd, w_w, w_h, depth, freq, mmult);
	}
#endif

	if (!shaderon)
		write_log (_T("Using non-shader version\n"));

	d3d->dmultxh = mmulth;
	d3d->dmultxv = mmultv;
	d3d->dmult = S2X_getmult(d3d - d3ddata);

	d3d->window_w = w_w;
	d3d->window_h = w_h;

	if (d3d->max_texture_w < w_w  || d3d->max_texture_h < w_h) {
		_stprintf (errmsg, _T("%s: %d * %d or bigger texture support required\nYour card's maximum texture size is only %d * %d"),
			D3DHEAD, w_w, w_h, d3d->max_texture_w, d3d->max_texture_h);
		return errmsg;
	}
	while (d3d->dmultxh > 1 && w_w * d3d->dmultxh > d3d->max_texture_w)
		d3d->dmultxh--;
	while (d3d->dmultxv > 1 && w_h * d3d->dmultxv > d3d->max_texture_h)
		d3d->dmultxv--;

	d3d->required_sl_texture_w = w_w;
	d3d->required_sl_texture_h = w_h;
	if (d3d->filterd3d->gfx_filter_scanlines > 0 && (d3d->max_texture_w < w_w || d3d->max_texture_h < w_h)) {
		gui_message (_T("%s: %d * %d or bigger texture support required for scanlines (max is only %d * %d)\n"),
			D3DHEAD, _T("Scanlines disabled."),
			d3d->required_sl_texture_w, d3d->required_sl_texture_h, d3d->max_texture_w, d3d->max_texture_h);
		changed_prefs.gf[d3d->filterd3didx].gfx_filter_scanlines = d3d->filterd3d->gfx_filter_scanlines = 0;
	}

	switch (depth)
	{
		case 32:
		default:
			d3d->tformat = D3DFMT_X8R8G8B8;
		break;
		case 15:
			d3d->tformat = D3DFMT_X1R5G5B5;
		break;
		case 16:
			d3d->tformat = D3DFMT_R5G6B5;
		break;
	}

	changed_prefs.leds_on_screen |= STATUSLINE_TARGET;
	currprefs.leds_on_screen |= STATUSLINE_TARGET;

	if (!restoredeviceobjects (d3d)) {
		D3D_free(monid, true);
		_stprintf (errmsg, _T("%s: initialization failed."), D3DHEAD);
		return errmsg;
	}
	d3d->maxscanline = 0;
	d3d->d3d_enabled = 1;
	d3d->wasstilldrawing_broken = true;

	if ((vsync < 0 || d3d->variablerefresh) && ap.gfx_vflip == 0) {
		hr = d3d->d3ddev->CreateQuery(D3DQUERYTYPE_EVENT, &d3d->query);
		if (FAILED (hr))
			write_log (_T("%s: CreateQuery(D3DQUERYTYPE_EVENT) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
	}
	if (d3d->d3ddevex) {
		UINT v = 12345;
		hr = d3d->d3ddevex->GetMaximumFrameLatency (&v);
		if (FAILED (hr)) {
			write_log (_T("%s: GetMaximumFrameLatency() failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			v = 1;
		}
		hr = S_OK;
		if (forcedframelatency >= 0) {
			hr = d3d->d3ddevex->SetMaximumFrameLatency(forcedframelatency);
		} else if (ap.gfx_vsyncmode) {
			hr = d3d->d3ddevex->SetMaximumFrameLatency(1);
		} else if (d3d->dpp.PresentationInterval == D3DPRESENT_INTERVAL_IMMEDIATE && (v > 1 || !vsync)) {
			hr = d3d->d3ddevex->SetMaximumFrameLatency((vsync || d3d->variablerefresh) ? (hzmult < 0 && !ap.gfx_strobo && !d3d->variablerefresh ? 2 : 1) : 0);
		}
		if (FAILED (hr))
			write_log (_T("%s: SetMaximumFrameLatency() failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
	}

	hr = d3d->d3ddev->CreateOffscreenPlainSurface(w_w, w_h, d3d->tformat, D3DPOOL_SYSTEMMEM, &d3d->screenshotsurface, NULL);
	if (FAILED(hr)) {
		write_log(_T("%s: CreateOffscreenPlainSurface RT failed: %s\n"), D3DHEAD, D3D_ErrorString(hr));
	}

	return 0;
}

struct d3d_initargs
{
	HWND hwnd;
	int w;
	int h;
	int depth;
	int mmulth, mmultv;
	int *freq;
};
static struct d3d_initargs d3dargs;

static void *D3D_init_start (void *p)
{
	struct d3dstruct *d3d = &d3ddata[0];
	struct timeval tv1, tv2;

	gettimeofday (&tv1, NULL);
	sleep_millis (1000);
	write_log (_T("Threaded D3D_init() start (free)\n"));
	D3D_free2 (d3d);
	sleep_millis (1000);
	write_log (_T("Threaded D3D_init() start (init)\n"));
	const TCHAR *t = D3D_init2 (d3d, d3dargs.hwnd, d3dargs.w, d3dargs.h,d3dargs.depth, d3dargs.freq, d3dargs.mmulth, d3dargs.mmultv);
	if (t) {
		gui_message (_T("Threaded D3D_init() returned error '%s'\n"), t);
	}
	write_log (_T("Threaded D3D_init() returned\n"));
	for (;;) {
		uae_u64 us1, us2, diff;
		gettimeofday (&tv2, NULL);
		us1 = (uae_u64)tv1.tv_sec * 1000000 + tv1.tv_usec;
		us2 = (uae_u64)tv2.tv_sec * 1000000 + tv2.tv_usec;
		diff = us2 - us1;
		if (diff >= fakemodewaitms * 1000)
			break;
		sleep_millis (10);
	}
	write_log (_T("Threaded D3D_init() finished\n"));
	d3d->frames_since_init = 0;
	d3d->fakemode = false;
	return NULL;
}

static const TCHAR *xD3D_init (HWND ahwnd, int monid, int w_w, int w_h, int depth, int *freq, int mmulth, int mmultv)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	if (!fakemodewaitms)
		return D3D_init2 (d3d, ahwnd, w_w, w_h, depth, freq, mmulth, mmultv);
	d3d->fakemode = true;
	d3dargs.hwnd = ahwnd;
	d3dargs.w = w_w;
	d3dargs.h = w_h;
	d3dargs.depth = depth;
	d3dargs.mmulth = mmulth;
	d3dargs.mmultv = mmultv;
	d3dargs.freq = freq;
	uae_start_thread_fast (D3D_init_start, NULL, &d3d->fakemodetid);
	return NULL;
}

static bool alloctextures (struct d3dstruct *d3d)
{
	if (!createtexture (d3d, d3d->tout_w, d3d->tout_h, d3d->window_w, d3d->window_h))
		return false;
	if (!createamigatexture (d3d, d3d->tin_w, d3d->tin_h))
		return false;
	return true;
}

static bool xD3D_alloctexture (int monid, int w, int h)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	d3d->tin_w = w * d3d->dmult;
	d3d->tin_h = h * d3d->dmult;

	d3d->tout_w = d3d->tin_w * d3d->dmultxh;
	d3d->tout_h = d3d->tin_h * d3d->dmultxv;

	if (d3d->fakemode)
		return false;

	changed_prefs.leds_on_screen |= STATUSLINE_TARGET;
	currprefs.leds_on_screen |= STATUSLINE_TARGET;

	freetextures (d3d);
	return alloctextures (d3d);
}


static HRESULT reset (void)
{
	struct d3dstruct *d3d = &d3ddata[0];
	HRESULT hr;
	bool oldrender = d3d->renderdisabled;
	d3d->renderdisabled = true;
	if (d3d->d3dex)
		hr = d3d->d3ddevex->ResetEx (&d3d->dpp, d3d->dpp.Windowed ? NULL : &d3d->modeex);
	else
		hr = d3d->d3ddev->Reset (&d3d->dpp);
	d3d->renderdisabled = oldrender;
	return hr;
}

static int D3D_needreset (struct d3dstruct *d3d)
{
	HRESULT hr;
	bool do_dd = false;

	if (!d3d->devicelost)
		return -1;
	if (d3d->d3dex)
		hr = d3d->d3ddevex->CheckDeviceState (d3d->d3dhwnd);
	else
		hr = d3d->d3ddev->TestCooperativeLevel ();
	if (hr == S_PRESENT_OCCLUDED) {
		// no need to draw anything
		return 1;
	}
	if (hr == D3DERR_DEVICELOST) {
		d3d->renderdisabled = true;
		// lost but can't be reset yet
		return 1;
	}
	if (hr == D3DERR_DEVICENOTRESET) {
		// lost and can be reset
		write_log (_T("%s: DEVICENOTRESET\n"), D3DHEAD);
		d3d->devicelost = 2;
		invalidatedeviceobjects (d3d);
		freetextures (d3d);
		hr = reset ();
		if (FAILED (hr)) {
			write_log (_T("%s: Reset failed %s\n"), D3DHEAD, D3D_ErrorString (hr));
			d3d->resetcount++;
			if (d3d->resetcount > 2 || hr == D3DERR_DEVICEHUNG) {
				changed_prefs.gfx_api = 0;
				write_log (_T("%s: Too many failed resets, disabling Direct3D mode\n"), D3DHEAD);
			}
			return 1;
		}
		d3d->devicelost = 0;
		write_log (_T("%s: Reset succeeded\n"), D3DHEAD);
		d3d->renderdisabled = false;
		restoredeviceobjects (d3d);
		alloctextures (d3d);
		return -1;
	} else if (hr == S_PRESENT_MODE_CHANGED) {
		write_log (_T("%s: S_PRESENT_MODE_CHANGED (%d,%d)\n"), D3DHEAD, d3d->ddraw_fs, d3d->ddraw_fs_attempt);
#if 0
		if (!d3d->ddraw_fs) {
			d3d->ddraw_fs_attempt++;
			if (d3d->ddraw_fs_attempt >= 5) {
				do_dd = true;
			}
		}
#endif
	}
	if (SUCCEEDED (hr)) {
		d3d->devicelost = 0;
		invalidatedeviceobjects (d3d);
		if (do_dd) {
			write_log (_T("%s: S_PRESENT_MODE_CHANGED, Retrying fullscreen with DirectDraw\n"), D3DHEAD);
			ddraw_fs_hack_init (d3d);
		}
		hr = reset ();
		if (FAILED (hr))
			write_log (_T("%s: Reset failed %s\n"), D3DHEAD, D3D_ErrorString (hr));
		restoredeviceobjects (d3d);
		return -1;
	}
	write_log (_T("%s: TestCooperativeLevel %s\n"), D3DHEAD, D3D_ErrorString (hr));
	return 0;
}

static void D3D_showframe2 (struct d3dstruct *d3d, bool dowait)
{
	HRESULT hr;

	if (!isd3d (d3d))
		return;
	for (;;) {
		if (d3d->d3dswapchain)
			hr = d3d->d3dswapchain->Present (NULL, NULL, NULL, NULL, dowait ? 0 : D3DPRESENT_DONOTWAIT);
		else
			hr = d3d->d3ddev->Present (NULL, NULL, NULL, NULL);
		if (hr == D3DERR_WASSTILLDRAWING) {
			d3d->wasstilldrawing_broken = false;
			if (!dowait)
				return;
			sleep_millis (1);
			continue;
		} else if (hr == S_PRESENT_OCCLUDED) {
			d3d->renderdisabled = true;
		} else if (hr == S_PRESENT_MODE_CHANGED) {
			// In most cases mode actually didn't change but
			// D3D is just being stupid and not accepting
			// all modes that DirectDraw does accept,
			// for example interlaced or EDS_RAWMODE modes!
			//devicelost = 1;
			; //write_log (_T("S_PRESENT_MODE_CHANGED\n"));
		} else if (FAILED (hr)) {
			write_log (_T("%s: Present() %s\n"), D3DHEAD, D3D_ErrorString (hr));
			if (hr == D3DERR_DEVICELOST || hr == S_PRESENT_MODE_CHANGED) {
				d3d->devicelost = 1;
				d3d->renderdisabled = true;
				write_log (_T("%s: mode changed or fullscreen focus lost\n"), D3DHEAD);
			}
		} else {
			d3d->ddraw_fs_attempt = 0;
		}
		return;
	}
}

static void xD3D_restore(int monid, bool checkonly)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	if (checkonly)
		return;
	d3d->renderdisabled = false;
}

static void xD3D_clear (int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	int i;
	HRESULT hr;

	if (!isd3d (d3d))
		return;
	for (i = 0; i < 2; i++) {
		hr = d3d->d3ddev->Clear (0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, d3ddebug ? 0x80 : 0x00), 0, 0);
		D3D_showframe2 (d3d, true);		
	}
}

static LPDIRECT3DTEXTURE9 processshader(struct d3dstruct *d3d, LPDIRECT3DTEXTURE9 srctex, struct shaderdata *s, bool rendertarget)
{
	HRESULT hr;
	UINT uPasses, uPass;
	LPDIRECT3DSURFACE9 lpRenderTarget;
	LPDIRECT3DSURFACE9 lpNewRenderTarget;
	LPDIRECT3DTEXTURE9 lpWorkTexture;

	if (!psEffect_SetTextures (srctex, s))
		return NULL;
	if (s->psPreProcess) {
		if (!psEffect_SetMatrices (&d3d->m_matPreProj, &d3d->m_matPreView, &d3d->m_matPreWorld, s))
			return NULL;

		if (FAILED (hr = d3d->d3ddev->GetRenderTarget (0, &lpRenderTarget)))
			write_log (_T("%s: GetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		lpWorkTexture = s->lpWorkTexture1;
		lpNewRenderTarget = NULL;
pass2:
		if (FAILED (hr = lpWorkTexture->GetSurfaceLevel (0, &lpNewRenderTarget)))
			write_log (_T("%s: GetSurfaceLevel: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpNewRenderTarget)))
			write_log (_T("%s: SetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));

		uPasses = 0;
		if (psEffect_Begin ((lpWorkTexture == s->lpWorkTexture1) ? psEffect_PreProcess1 : psEffect_PreProcess2, &uPasses, s)) {
			for (uPass = 0; uPass < uPasses; uPass++) {
				if (psEffect_BeginPass (s->pEffect, uPass)) {
					if (FAILED (hr = d3d->d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 4, 2))) {
						write_log (_T("%s: Effect DrawPrimitive failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
					}
					psEffect_EndPass (s->pEffect);
				}
			}
			psEffect_End (s->pEffect);
		}
		if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpRenderTarget)))
			write_log (_T("%s: Effect RenderTarget reset failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		lpNewRenderTarget->Release ();
		lpNewRenderTarget = NULL;
		if (psEffect_hasPreProcess2 (s) && lpWorkTexture == s->lpWorkTexture1) {
			lpWorkTexture = s->lpWorkTexture2;
			goto pass2;
		}
		lpRenderTarget->Release ();
		lpRenderTarget = NULL;
	}
	psEffect_SetMatrices (&d3d->m_matProj2, &d3d->m_matView2, &d3d->m_matWorld2, s);

	if (rendertarget) {
#if TWOPASS
		if (FAILED (hr = d3d->d3ddev->GetRenderTarget (0, &lpRenderTarget)))
			write_log (_T("%s: GetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		if (FAILED (hr = s->lpTempTexture->GetSurfaceLevel (0, &lpNewRenderTarget)))
			write_log (_T("%s: GetSurfaceLevel: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpNewRenderTarget)))
			write_log (_T("%s: SetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
#endif
	}

	uPasses = 0;
	if (psEffect_Begin (psEffect_Combine, &uPasses, s)) {
		for (uPass = 0; uPass < uPasses; uPass++) {
			if (!psEffect_BeginPass (s->pEffect, uPass))
				return NULL;
			if (FAILED (hr = d3d->d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2)))
				write_log (_T("%s: Effect2 DrawPrimitive failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			psEffect_EndPass (s->pEffect);
		}
		psEffect_End (s->pEffect);
	}
	if (rendertarget) {
#if TWOPASS
		if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpRenderTarget)))
			write_log (_T("%s: SetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		lpNewRenderTarget->Release ();
		lpRenderTarget->Release ();
#endif
	}
	return s->lpTempTexture;
}

static void xD3D_led(int led, int on, int brightness)
{
	struct d3dstruct *d3d = &d3ddata[0];
	leds[led] = on;
}

static int xD3D_debug(int monid, int mode)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	int old = debugcolors ? 1 : 0;
	debugcolors = (mode & 1) != 0;
	noclear = debugcolors ? false : true;
	clearcnt = 0;
	return old;
}

static void clearrt(struct d3dstruct *d3d)
{
	HRESULT hr;
	uae_u8 color[4] = { 0, 0, 0, 0 };

	if (noclear && cannoclear) {
		if (clearcnt > 3)
			return;
		clearcnt++;
	}

	if (!noclear && debugcolors && slicecnt > 0) {
		int cnt = slicecnt - 1;
		int v = cnt % 3;
		if (cnt / 3 == 1)
			color[(v + 1) % 3] = 80;
		color[v] = 80;
	}

	hr = d3d->d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(color[0], d3ddebug ? 0x80 : color[1], color[2]), 0, 0);
}

static void D3D_render2(struct d3dstruct *d3d, int mode)
{
	struct AmigaMonitor *mon = &AMonitors[d3d - d3ddata];
	HRESULT hr;
	LPDIRECT3DTEXTURE9 srctex = d3d->texture;
	UINT uPasses, uPass;

	if (!isd3d (d3d) || !d3d->texture)
		return;

	bool normalrender = mode < 0 || (mode & 1);

	if (mode > 0 && (mode & 2))
		slicecnt = 0;
	else if (mode < 0)
		slicecnt = slicecnt == 2 ? 0 : slicecnt;

	clearrt(d3d);

	slicecnt++;

	if (FAILED (hr = d3d->d3ddev->BeginScene ())) {
		write_log (_T("%s: BeginScene: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	if (shaderon > 0 && d3d->shaders[SHADER_POST].pEffect) {
		for (int i = 0; i < MAX_SHADERS; i++) {
			struct shaderdata *s = &d3d->shaders[i];
			if (s->type == SHADERTYPE_BEFORE)
				settransform_pre (d3d, s);
			if (s->type == SHADERTYPE_MIDDLE) {
				d3d->m_matProj = d3d->m_matProj_out;
				d3d->m_matView = d3d->m_matView_out;
				d3d->m_matWorld = d3d->m_matWorld_out;
			}
			if (s->type == SHADERTYPE_BEFORE || s->type == SHADERTYPE_MIDDLE) {
				settransform (d3d, s);
				srctex = processshader (d3d, srctex, s, true);
				if (!srctex)
					return;
			}
		}
	}

	d3d->m_matProj = d3d->m_matProj_out;
	d3d->m_matView = d3d->m_matView_out;
	d3d->m_matWorld = d3d->m_matWorld_out;

#if TWOPASS
	if (shaderon > 0 && d3d->shaders[SHADER_POST].pEffect) {
		LPDIRECT3DSURFACE9 lpRenderTarget;
		LPDIRECT3DSURFACE9 lpNewRenderTarget;
		struct shaderdata *s = &d3d->shaders[SHADER_POST];
		LPD3DXEFFECT postEffect = s->pEffect;
		int after = -1;
		LPDIRECT3DTEXTURE9 masktexture = NULL;
		D3DSURFACE_DESC Desc;
		D3DXVECTOR4 texelsize;

		for (int i = 0; i < MAX_SHADERS; i++) {
			struct shaderdata *s = &d3d->shaders[i];
			if (s->type == SHADERTYPE_AFTER)
				after = i;
			if (s->type == SHADERTYPE_MASK_MIDDLE && s->masktexture)
				masktexture = s->masktexture;
		}

		setupscenecoords(d3d, normalrender);
		hr = d3d->d3ddev->SetTransform (D3DTS_PROJECTION, &d3d->m_matProj);
		hr = d3d->d3ddev->SetTransform (D3DTS_VIEW, &d3d->m_matView);
		hr = d3d->d3ddev->SetTransform (D3DTS_WORLD, &d3d->m_matWorld);

		hr = postEffect->SetMatrix (d3d->postMatrixSource, &d3d->postproj);
		hr = postEffect->SetVector (d3d->postMaskMult, &d3d->maskmult);
		hr = postEffect->SetVector (d3d->postMaskShift, &d3d->maskshift);

		srctex->GetLevelDesc (0, &Desc);
		texelsize.x = 1.0f / Desc.Width;
		texelsize.y = 1.0f / Desc.Height;
		texelsize.z = 1; texelsize.w = 1;
		hr = postEffect->SetVector (d3d->postTexelSize, &texelsize);
		if (d3d->postFramecounterHandle)
			postEffect->SetFloat(d3d->postFramecounterHandle, timeframes);

		if (masktexture) {
			if (FAILED (hr = postEffect->SetTechnique (d3d->postTechnique)))
				write_log (_T("%s: SetTechnique(postTechnique) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = postEffect->SetTexture (d3d->postMaskTextureHandle, masktexture)))
				write_log (_T("%s: SetTexture(masktexture) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		} else if (d3d->sltexture) {
			if (FAILED (hr = postEffect->SetTechnique (d3d->postTechniqueAlpha)))
				write_log (_T("%s: SetTechnique(postTechniqueAlpha) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = postEffect->SetTexture (d3d->postMaskTextureHandle, d3d->sltexture)))
				write_log (_T("%s: SetTexture(sltexture) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		} else {
			if (FAILED (hr = postEffect->SetTechnique (d3d->postTechniquePlain)))
				write_log (_T("%s: SetTechnique(postTechniquePlain) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		}
		hr = postEffect->SetInt (d3d->postFilterMode, d3d->filterd3d->gfx_filter_bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT);

		if (FAILED (hr = postEffect->SetTexture (d3d->postSourceTextureHandle, srctex)))
			write_log (_T("%s: SetTexture(srctex) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));

		if (s->m_SourceDimsEffectHandle) {
			D3DXVECTOR4 fDimsSource;
			fDimsSource.x = (FLOAT)Desc.Width;
			fDimsSource.y = (FLOAT)Desc.Height;
			fDimsSource.z  = 1; fDimsSource.w = 1;
			hr = postEffect->SetVector(s->m_SourceDimsEffectHandle, &fDimsSource);
			if (FAILED(hr)) {
				write_log(_T("%s: SetTextures:SetVector:Source %s\n"), D3DHEAD, D3D_ErrorString(hr));
			}
		}
		if (s->m_TargetDimsEffectHandle) {
			D3DXVECTOR4 fDimsTarget;
			fDimsTarget.x = s->targettex_width;
			fDimsTarget.y = s->targettex_height;
			fDimsTarget.z = 1; fDimsTarget.w = 1;
			hr = postEffect->SetVector(s->m_TargetDimsEffectHandle, &fDimsTarget);
			if (FAILED(hr)) {
				write_log(_T("%s: SetTextures:SetVector:Target %s\n"), D3DHEAD, D3D_ErrorString(hr));
			}
		}

		if (after >= 0) {
			if (FAILED (hr = d3d->d3ddev->GetRenderTarget (0, &lpRenderTarget)))
				write_log (_T("%s: GetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = d3d->lpPostTempTexture->GetSurfaceLevel (0, &lpNewRenderTarget)))
				write_log (_T("%s: GetSurfaceLevel: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpNewRenderTarget)))
				write_log (_T("%s: SetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		}

		uPasses = 0;
		if (psEffect_Begin (psEffect_None, &uPasses, s)) {
			for (uPass = 0; uPass < uPasses; uPass++) {
				if (psEffect_BeginPass (postEffect, uPass)) {
					if (FAILED (hr = d3d->d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2)))
						write_log (_T("%s: Post DrawPrimitive failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
					psEffect_EndPass (postEffect);
				}
			}
			psEffect_End (postEffect);
		}

		if (after >= 0) {
			if (FAILED (hr = d3d->d3ddev->SetRenderTarget (0, lpRenderTarget)))
				write_log (_T("%s: SetRenderTarget: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			lpNewRenderTarget->Release ();
			lpRenderTarget->Release ();

			srctex = d3d->lpPostTempTexture;
			for (int i = 0; i < MAX_SHADERS; i++) {
				struct shaderdata *s = &d3d->shaders[i];
				if (s->type == SHADERTYPE_AFTER) {
					settransform2 (d3d, s);
					srctex = processshader (d3d, srctex, s, i != after);
					if (!srctex)
						return;
				}
			}
		}

	} else {

		// non-shader version
		setupscenecoords (d3d, normalrender);
		hr = d3d->d3ddev->SetTransform (D3DTS_PROJECTION, &d3d->m_matProj);
		hr = d3d->d3ddev->SetTransform (D3DTS_VIEW, &d3d->m_matView);
		hr = d3d->d3ddev->SetTransform (D3DTS_WORLD, &d3d->m_matWorld);
		hr = d3d->d3ddev->SetTexture (0, srctex);
		hr = d3d->d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2);
		int bl = d3d->filterd3d->gfx_filter_bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
		hr = d3d->d3ddev->SetSamplerState(0, D3DSAMP_MINFILTER, bl);
		hr = d3d->d3ddev->SetSamplerState(0, D3DSAMP_MAGFILTER, bl);
		hr = d3d->d3ddev->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
		hr = d3d->d3ddev->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

		if (d3d->sprite && d3d->sltexture) {
			D3DXVECTOR3 v;
			d3d->sprite->Begin(D3DXSPRITE_ALPHABLEND);
			v.x = v.y = v.z = 0;
			d3d->sprite->Draw(d3d->sltexture, NULL, NULL, &v, 0xffffffff);
			d3d->sprite->End();
		}
	}

	if (d3d->sprite && ((d3d->ledtexture) || (d3d->mask2texture) || (d3d->cursorsurfaced3d && d3d->cursor_v))) {
		D3DXVECTOR3 v;
		d3d->sprite->Begin(D3DXSPRITE_ALPHABLEND);
		if (d3d->cursorsurfaced3d && d3d->cursor_v) {
			D3DXMATRIXA16 t;

			if (d3d->cursor_scale)
				MatrixScaling(&t, ((float)(d3d->window_w) / (d3d->tout_w + 2 * d3d->cursor_offset2_x)), ((float)(d3d->window_h) / (d3d->tout_h + 2 * d3d->cursor_offset2_y)), 0);
			else
				MatrixScaling(&t, 1.0f, 1.0f, 0);
			v.x = d3d->cursor_x + d3d->cursor_offset2_x;
			v.y = d3d->cursor_y + d3d->cursor_offset2_y;
			v.z = 0;
			d3d->sprite->SetTransform(&t);
			d3d->sprite->Draw(d3d->cursorsurfaced3d, NULL, NULL, &v, 0xffffffff);
			MatrixScaling(&t, 1, 1, 0);
			d3d->sprite->Flush();
			d3d->sprite->SetTransform(&t);
		}
		if (d3d->mask2texture) {
			D3DXMATRIXA16 t;
			RECT r;
			float srcw = d3d->mask2texture_w;
			float srch = d3d->mask2texture_h;
			float aspectsrc = srcw / srch;
			float aspectdst = (float)d3d->window_w / d3d->window_h;
			float w, h;

			w = d3d->mask2texture_multx;
			h = d3d->mask2texture_multy;
#if 0
			if (currprefs.gfx_filteroverlay_pos.width > 0)
				w = (float)currprefs.gfx_filteroverlay_pos.width / srcw;
			else if (currprefs.gfx_filteroverlay_pos.width == -1)
				w = 1.0;
			else if (currprefs.gfx_filteroverlay_pos.width <= -24000)
				w = w * (-currprefs.gfx_filteroverlay_pos.width - 30000) / 100.0;

			if (currprefs.gfx_filteroverlay_pos.height > 0)
				h = (float)currprefs.gfx_filteroverlay_pos.height / srch;
			else if (currprefs.gfx_filteroverlay_pos.height == -1)
				h = 1;
			else if (currprefs.gfx_filteroverlay_pos.height <= -24000)
				h = h * (-currprefs.gfx_filteroverlay_pos.height - 30000) / 100.0;
#endif
			MatrixScaling (&t, w, h, 0);

			v.x = 0;
			if (d3d->filterd3d->gfx_filteroverlay_pos.x == -1)
				v.x = (d3d->window_w - (d3d->mask2texture_w * w)) / 2;
			else if (d3d->filterd3d->gfx_filteroverlay_pos.x > -24000)
				v.x = d3d->filterd3d->gfx_filteroverlay_pos.x;
			else
				v.x = (d3d->window_w - (d3d->mask2texture_w * w)) / 2 + (-d3d->filterd3d->gfx_filteroverlay_pos.x - 30100) * d3d->window_w / 100.0;

			v.y = 0;
			if (d3d->filterd3d->gfx_filteroverlay_pos.y == -1)
				v.y = (d3d->window_h - (d3d->mask2texture_h * h)) / 2;
			else if (d3d->filterd3d->gfx_filteroverlay_pos.y > -24000)
				v.y = d3d->filterd3d->gfx_filteroverlay_pos.y;
			else
				v.y = (d3d->window_h - (d3d->mask2texture_h * h)) / 2 + (-d3d->filterd3d->gfx_filteroverlay_pos.y - 30100) * d3d->window_h / 100.0;

			v.x /= w;
			v.y /= h;
			v.x = v.y = 0;
			v.z = 0;
			v.x += d3d->mask2texture_offsetw / w;

			r.left = 0;
			r.top = 0;
			r.right = d3d->mask2texture_w;
			r.bottom = d3d->mask2texture_h;
			if (showoverlay) {
				d3d->sprite->SetTransform(&t);
				d3d->sprite->Draw(d3d->mask2texture, &r, NULL, &v, 0xffffffff);
				d3d->sprite->Flush();
				for (int i = 0; overlayleds[i]; i++) {
					bool led = leds[ledtypes[i]] != 0;
					if (led || (ledtypes[i] == LED_POWER && currprefs.power_led_dim)) {
						LPDIRECT3DTEXTURE9 spr = d3d->mask2textureleds[i];
						if (!led && ledtypes[i] == LED_POWER && currprefs.power_led_dim)
							spr = d3d->mask2textureled_power_dim;
						if (spr) {
							v.x = d3d->mask2texture_offsetw / w + d3d->mask2textureledoffsets[i * 2 + 0];
							v.y = d3d->mask2textureledoffsets[i * 2 + 1];
							v.z = 0;
							d3d->sprite->Draw(spr, NULL, NULL, &v, 0xffffffff);
							d3d->sprite->Flush();
						}
					}
				}
			}

			MatrixScaling(&t, 1, 1, 0);
			d3d->sprite->SetTransform(&t);

			if (d3d->mask2texture_offsetw > 0) {
				v.x = 0;
				v.y = 0;
				r.left = 0;
				r.top = 0;
				r.right = d3d->mask2texture_offsetw + 1;
				r.bottom = d3d->window_h;
				d3d->sprite->Draw (d3d->blanktexture, &r, NULL, &v, 0xffffffff);
				if (d3d->window_w > d3d->mask2texture_offsetw + d3d->mask2texture_ww) {
					v.x = d3d->mask2texture_offsetw + d3d->mask2texture_ww;
					v.y = 0;
					r.left = 0;
					r.top = 0;
					r.right = d3d->window_w - (d3d->mask2texture_offsetw + d3d->mask2texture_ww) + 1;
					r.bottom = d3d->window_h;
					d3d->sprite->Draw(d3d->blanktexture, &r, NULL, &v, 0xffffffff);
				}
			}

		}
		if (d3d->ledtexture && (((currprefs.leds_on_screen & STATUSLINE_RTG) && WIN32GFX_IsPicassoScreen(mon)) || ((currprefs.leds_on_screen & STATUSLINE_CHIPSET) && !WIN32GFX_IsPicassoScreen(mon)))) {
			int slx, sly;
			statusline_getpos(d3d - d3ddata, &slx, &sly, d3d->window_w, d3d->window_h, d3d->statusbar_hx, d3d->statusbar_vx);
			v.x = slx;
			v.y = sly;
			v.z = 0;
			d3d->sprite->Draw(d3d->ledtexture, NULL, NULL, &v, 0xffffffff);
		}
		struct d3d9overlay *ov = d3d->extoverlays;
		while (ov) {
			if (ov->tex) {
				v.x = ov->x;
				v.y = ov->y;
				v.z = 0;
				d3d->sprite->Draw(ov->tex, NULL, NULL, &v, 0xffffffff);
			}
			ov = ov->next;
		}
		d3d->sprite->End();
	}
#endif

	hr = d3d->d3ddev->EndScene();
	if (FAILED (hr))
		write_log (_T("%s: EndScene() %s\n"), D3DHEAD, D3D_ErrorString (hr));
}

static bool xD3D_setcursor(int monid, int x, int y, int width, int height, bool visible, bool noscale)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	if (width < 0 || height < 0)
		return true;

	if (width && height) {
		d3d->cursor_offset2_x = d3d->cursor_offset_x * d3d->window_w / width;
		d3d->cursor_offset2_y = d3d->cursor_offset_y * d3d->window_h / height;
		d3d->cursor_x = x * d3d->window_w / width;
		d3d->cursor_y = y * d3d->window_h / height;
	} else {
		d3d->cursor_x = d3d->cursor_y = 0;
		d3d->cursor_offset2_x = d3d->cursor_offset2_y = 0;
	}
	d3d->cursor_scale = !noscale;
	d3d->cursor_v = visible;
	return true;
}

static void xD3D_flushtexture(int monid, int miny, int maxy)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	if (d3d->fakemode || d3d->fulllocked || !d3d->texture || d3d->renderdisabled)
		return;
	if (miny >= 0 && maxy >= 0) {
		RECT r;
		maxy++;
		r.left = 0;
		r.right = d3d->tin_w;
		r.top = miny <= 0 ? 0 : miny;
		r.bottom = maxy <= d3d->tin_h ? maxy : d3d->tin_h;
		if (r.top <= r.bottom) {
			HRESULT hr = d3d->texture->AddDirtyRect (&r);
			if (FAILED (hr))
				write_log (_T("%s: AddDirtyRect(): %s\n"), D3DHEAD, D3D_ErrorString (hr));
			//write_log (_T("%d %d\n"), r.top, r.bottom);
		}
	}
}

static void xD3D_unlocktexture(int monid, int y_start, int y_end)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	HRESULT hr;

	if (!isd3d(d3d) || !d3d->texture)
		return;

	if (d3d->locked) {
		if (currprefs.leds_on_screen & (STATUSLINE_CHIPSET | STATUSLINE_RTG))
			updateleds(d3d);
		hr = d3d->texture->UnlockRect(0);
		if (y_start >= 0)
			xD3D_flushtexture(monid, y_start, y_end);
	}
	d3d->locked = 0;
	d3d->fulllocked = 0;
}

static uae_u8 *xD3D_locktexture (int monid, int *pitch, int *height, bool fullupdate)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	D3DLOCKED_RECT lock;
	HRESULT hr;

	if (d3d->fakemode) {
		*pitch = 0;
		return d3d->fakebitmap;
	}

	if (D3D_needreset (d3d) > 0) {
		return NULL;
	}
	if (!isd3d (d3d) || !d3d->texture)
		return NULL;

	if (d3d->locked) {
		write_log (_T("%s: texture already locked!\n"), D3DHEAD);
		return NULL;
	}

	lock.pBits = NULL;
	lock.Pitch = 0;
	hr = d3d->texture->LockRect (0, &lock, NULL, fullupdate ? D3DLOCK_DISCARD : D3DLOCK_NO_DIRTY_UPDATE);
	if (FAILED (hr)) {
		write_log (_T("%s: LockRect failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
		return NULL;
	}
	d3d->locked = 1;
	if (lock.pBits == NULL || lock.Pitch == 0) {
		write_log (_T("%s: LockRect returned NULL texture\n"), D3DHEAD);
		D3D_unlocktexture(monid, -1, -1);
		return NULL;
	}
	d3d->fulllocked = fullupdate;
	*pitch = lock.Pitch;
	if (height)
		*height = d3d->tin_h;
	return (uae_u8*)lock.pBits;
}

static void flushgpu (struct d3dstruct *d3d, bool wait)
{
	if (currprefs.turbo_emulation)
		return;
	if (d3d->query) {
		HRESULT hr = d3d->query->Issue (D3DISSUE_END);
		if (SUCCEEDED (hr)) {
			while (d3d->query->GetData (NULL, 0, D3DGETDATA_FLUSH) == S_FALSE) {
				if (!wait)
					return;
			}
		} else {
			static int reported;
			if (reported < 10) {
				reported++;
				write_log (_T("%s: query->Issue (D3DISSUE_END) failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			}
		}
	}
}

static bool xD3D_renderframe(int monid, int mode, bool immediate)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	static int vsync2_cnt;

	d3d->frames_since_init++;

	if (d3d->fakemode)
		return true;

	if (!isd3d (d3d) || !d3d->texture)
		return false;

	if (d3d->filenotificationhandle != NULL) {
		bool notify = false;
		while (WaitForSingleObject (d3d->filenotificationhandle, 0) == WAIT_OBJECT_0) {
			if (FindNextChangeNotification (d3d->filenotificationhandle)) {
				if (d3d->frames_since_init > 50)
					notify = true;
			}
		}
		if (notify) {
			d3d->devicelost = 2;
			write_log (_T("%s: Shader file modification notification\n"), D3DHEAD);
		}
	}

	if (d3d->vsync2 > 0) {
		vsync2_cnt ^= 1;
		if (vsync2_cnt == 0)
			return true;
	}

	D3D_render2 (d3d, mode);
	flushgpu (d3d, immediate);

	return true;
}

static void xD3D_showframe (int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	if (!isd3d (d3d))
		return;
	if (currprefs.turbo_emulation) {
		if ((!(d3d->dpp.PresentationInterval & D3DPRESENT_INTERVAL_IMMEDIATE) || d3d->variablerefresh) && d3d->wasstilldrawing_broken) {
			static int frameskip;
			if (currprefs.turbo_emulation && frameskip-- > 0)
				return;
			frameskip = 10;
		}
		D3D_showframe2 (d3d, false);
	} else {
		D3D_showframe2 (d3d, true);
		if (d3d->vsync2 == -1 && !currprefs.turbo_emulation) {
			D3D_showframe2 (d3d ,true);
		}
		flushgpu(d3d, true);
	}
}

static void xD3D_showframe_special (int monid, int mode)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	HRESULT hr;
	if (!isd3d (d3d))
		return;
	if (currprefs.turbo_emulation)
		return;
	if (pause_emulation)
		return;
	if (mode == 2) {
		hr = d3d->d3ddev->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, d3ddebug ? 0x80 : 0, 0), 0, 0);
	}
	if (mode == 1) {
		D3D_showframe2(d3d, true);
	}
	flushgpu(d3d, true);
}

static void xD3D_refresh (int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];

	if (!isd3d (d3d))
		return;
	createscanlines(d3d, 0);
	for (int i = 0; i < 3; i++) {
		D3D_render2(d3d, true);
		D3D_showframe2(d3d, true);
	}
}

void D3D_getpixelformat (int depth, int *rb, int *gb, int *bb, int *rs, int *gs, int *bs, int *ab, int *as, int *a)
{
	switch (depth)
	{
	case 32:
		*rb = 8;
		*gb = 8;
		*bb = 8;
		*ab = 8;
		*rs = 16;
		*gs = 8;
		*bs = 0;
		*as = 24;
		*a = 0;
		break;
	case 15:
		*rb = 5;
		*gb = 5;
		*bb = 5;
		*ab = 1;
		*rs = 10;
		*gs = 5;
		*bs = 0;
		*as = 15;
		*a = 0;
	break;
	case 16:
		*rb = 5;
		*gb = 6;
		*bb = 5;
		*ab = 0;
		*rs = 11;
		*gs = 5;
		*bs = 0;
		*as = 0;
		*a = 0;
		break;
	}
}

static float xD3D_getrefreshrate(int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	HRESULT hr;
	D3DDISPLAYMODE dmode;

	waitfakemode (d3d);
	if (!isd3d (d3d))
		return -1;
	hr = d3d->d3ddev->GetDisplayMode (0, &dmode);
	if (FAILED (hr))
		return -1;
	return dmode.RefreshRate;
}

static void xD3D_guimode(int monid, int guion)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	HRESULT hr;

	if (guion != 0 && guion != 1)
		return;

	waitfakemode (d3d);
	if (!isd3d (d3d))
		return;
	D3D_render2(d3d, true);
	D3D_showframe2(d3d, true);
	hr = d3d->d3ddev->SetDialogBoxMode (guion ? TRUE : FALSE);
	if (FAILED (hr))
		write_log (_T("%s: SetDialogBoxMode %s\n"), D3DHEAD, D3D_ErrorString (hr));
	d3d->guimode = guion;
}

LPDIRECT3DSURFACE9 D3D_capture(int monid, int *w, int *h, int *bits, bool rendertarget)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	HRESULT hr;
	LPDIRECT3DSURFACE9 ret = NULL;

	waitfakemode(d3d);
	if (!isd3d(d3d))
		return NULL;
	if (rendertarget) {
		LPDIRECT3DSURFACE9 rt;
		hr = d3d->d3ddev->GetRenderTarget(0, &rt);
		if (FAILED(hr)) {
			write_log(_T("%s: D3D_capture() GetRenderTarget() failed: %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return NULL;
		}
		hr = d3d->d3ddev->GetRenderTargetData(rt, d3d->screenshotsurface);
		rt->Release();
		if (FAILED(hr)) {
			write_log(_T("%s: D3D_capture() GetRenderTargetData() failed: %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return NULL;
		}
		ret = d3d->screenshotsurface;
	} else {
		hr = d3d->texture->GetSurfaceLevel(0, &ret);
		if (FAILED(hr)) {
			write_log(_T("%s: D3D_capture() GetSurfaceLevel() failed: %s\n"), D3DHEAD, D3D_ErrorString(hr));
			return NULL;
		}
	}
	*w = d3d->window_w;
	*h = d3d->window_h;
	*bits = d3d->t_depth;
	return ret;
}

static HDC xD3D_getDC(int monid, HDC hdc)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	static LPDIRECT3DSURFACE9 bb;
	HRESULT hr;

	waitfakemode (d3d);
	if (!isd3d (d3d))
		return 0;
	if (!hdc) {
		hr = d3d->d3ddev->GetBackBuffer (0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
		if (FAILED (hr)) {
			write_log (_T("%s: GetBackBuffer() failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		hr = bb->GetDC (&hdc);
		if (SUCCEEDED (hr))
			return hdc;
		write_log (_T("%s: GetDC() failed: %s\n"), D3DHEAD, D3D_ErrorString (hr));
	}
	if (bb) {
		if (hdc)
			bb->ReleaseDC (hdc);
		bb->Release ();
		bb = NULL;
	}
	return 0;
}

static int xD3D_isenabled(int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	return d3d->d3d_enabled ? 1 : 0;
}

static uae_u8 *xD3D_setcursorsurface(int monid, int *pitch)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	if (pitch) {
		D3DLOCKED_RECT locked;
		if (!d3d->cursorsurfaced3d)
			return NULL;
		HRESULT hr = d3d->cursorsurfaced3d->LockRect(0, &locked, NULL, 0);
		if (FAILED(hr))
			return NULL;
		*pitch = locked.Pitch;
		return (uae_u8*)locked.pBits;
	} else {
		d3d->cursorsurfaced3d->UnlockRect(0);
		return NULL;
	}
}

static bool xD3D_getscanline(int *scanline, bool *invblank)
{
	struct d3dstruct *d3d = &d3ddata[0];
	HRESULT hr;
	D3DRASTER_STATUS rt;

	if (!isd3d(d3d))
		return false;
	if (d3d->d3dswapchain)
		hr = d3d->d3dswapchain->GetRasterStatus(&rt);
	else
		hr = d3d->d3ddev->GetRasterStatus(0, &rt);
	if (FAILED(hr))
		return false;
	*scanline = rt.ScanLine;
	*invblank = rt.InVBlank != FALSE;
	return true;
}

static bool xD3D_run(int monid)
{
	struct d3dstruct *d3d = &d3ddata[monid];
	return false;
}

static bool xD3D_extoverlay(struct extoverlay *ext)
{
	struct d3dstruct *d3d = &d3ddata[0];
	struct d3d9overlay *ov, *ovprev, *ov2;
	LPDIRECT3DTEXTURE9 s;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	s = NULL;
	ov = d3d->extoverlays;
	ovprev = NULL;
	while (ov) {
		if (ov->id == ext->idx) {
			s = ov->tex;
			break;
		}
		ovprev = ov;
		ov = ov->next;
	}

	write_log(_T("extoverlay %d: x=%d y=%d %d*%d data=%p ovl=%p\n"), ext->idx, ext->xpos, ext->ypos, ext->width, ext->height, ext->data, ov);

	if (!s && (ext->width <= 0 || ext->height <= 0))
		return false;

	if (!ext->data && s && (ext->width == 0 || ext->height == 0)) {
		ov->x = ext->xpos;
		ov->y = ext->ypos;
		return true;
	}

	if (ov && s) {
		if (ovprev) {
			ovprev->next = ov->next;
		} else {
			d3d->extoverlays = ov->next;
		}
		s->Release();
		xfree(ov);
		if (ext->width <= 0 || ext->height <= 0)
			return true;
	}

	if (ext->width <= 0 || ext->height <= 0)
		return false;

	ov = xcalloc(d3d9overlay, 1);
	s = createtext(d3d, ext->width, ext->height, D3DFMT_A8R8G8B8);
	if (!s) {
		xfree(ov);
		return false;
	}

	ov->tex = s;
	ov->id = ext->idx;

	ov2 = d3d->extoverlays;
	ovprev = NULL;
	for (;;) {
		if (ov2 == NULL || ov2->id >= ov->id) {
			if (ov2 == d3d->extoverlays) {
				d3d->extoverlays = ov;
				ov->next = ov2;
			} else {
				ov->next = ovprev->next;
				ovprev->next = ov;
			}
			break;
		}
		ovprev = ov2;
		ov2 = ov2->next;
	}

	ov->x = ext->xpos;
	ov->y = ext->ypos;

	hr = s->LockRect(0, &locked, NULL, 0);
	if (SUCCEEDED(hr)) {
		for (int y = 0; y < ext->height; y++) {
			memcpy((uae_u8*)locked.pBits + y * locked.Pitch, ext->data + y * ext->width * 4, ext->width * 4);
		}
		s->UnlockRect(0);
	}

	return true;
}

void d3d9_select(void)
{
	D3D_free = xD3D_free;
	D3D_init = xD3D_init;
	D3D_alloctexture = xD3D_alloctexture;
	D3D_refresh = xD3D_refresh;
	D3D_renderframe = xD3D_renderframe;
	D3D_showframe = xD3D_showframe;
	D3D_showframe_special = xD3D_showframe_special;
	D3D_locktexture = xD3D_locktexture;
	D3D_unlocktexture = xD3D_unlocktexture;
	D3D_flushtexture = xD3D_flushtexture;
	D3D_guimode = xD3D_guimode;
	D3D_getDC = xD3D_getDC;
	D3D_isenabled = xD3D_isenabled;
	D3D_clear = xD3D_clear;
	D3D_canshaders = xD3D_canshaders;
	D3D_goodenough = xD3D_goodenough;
	D3D_setcursor = xD3D_setcursor;
	D3D_setcursorsurface = xD3D_setcursorsurface;
	D3D_getrefreshrate = xD3D_getrefreshrate;
	D3D_restore = xD3D_restore;
	D3D_resize = NULL;
	D3D_change = NULL;
	D3D_getscalerect = xD3D_getscalerect;
	D3D_run = xD3D_run;
	D3D_debug = xD3D_debug;
	D3D_led = xD3D_led;
	D3D_getscanline = xD3D_getscanline;
	D3D_extoverlay = xD3D_extoverlay;
}

#endif
