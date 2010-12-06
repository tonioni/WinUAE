
#include <windows.h>
#include <resource>

#include "sysconfig.h"
#include "sysdeps.h"

#if defined (D3D) && defined (GFXFILTER)

#define D3DXFX_LARGEADDRESS_HANDLE
#ifdef D3DXFX_LARGEADDRESS_HANDLE
#define EFFECTCOMPILERFLAGS D3DXFX_LARGEADDRESSAWARE
#else
#define EFFECTCOMPILERFLAGS 0
#endif

#define EFFECT_VERSION 2
#define D3DX9DLL L"d3dx9_43.dll"
#define TWOPASS 1

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
extern int D3DEX, d3ddebug;

#include <d3d9.h>
#include <d3dx9.h>

#include "direct3d.h"

static TCHAR *D3DHEAD = L"-";
static int psEnabled, psActive, psPreProcess, shaderon;

static D3DFORMAT tformat;
static int d3d_enabled, d3d_ex;
static IDirect3D9 *d3d;
static IDirect3D9Ex *d3dex;
static D3DPRESENT_PARAMETERS dpp;
static D3DDISPLAYMODEEX modeex;
static IDirect3DDevice9 *d3ddev;
static IDirect3DDevice9Ex *d3ddevex;
static D3DSURFACE_DESC dsdbb;
static LPDIRECT3DTEXTURE9 texture, sltexture, ledtexture, masktexture, mask2texture, blanktexture;
static int masktexture_w, masktexture_h;
static float mask2texture_w, mask2texture_h, mask2texture_ww, mask2texture_wh;
static float mask2texture_multx, mask2texture_multy, mask2texture_offsetw;
static LPDIRECT3DTEXTURE9 lpWorkTexture1, lpWorkTexture2, lpTempTexture;
LPDIRECT3DTEXTURE9 cursorsurfaced3d;
static LPDIRECT3DVOLUMETEXTURE9 lpHq2xLookupTexture;
static IDirect3DVertexBuffer9 *vertexBuffer;
static ID3DXSprite *sprite;
static HWND d3dhwnd;
static int devicelost;
static int locked, fulllocked;
static int cursor_offset_x, cursor_offset_y;
static float maskmult_x, maskmult_y;
static RECT mask2rect;

static D3DXMATRIXA16 m_matProj, m_matProj2;
static D3DXMATRIXA16 m_matWorld, m_matWorld2;
static D3DXMATRIXA16 m_matView, m_matView2;
static D3DXMATRIXA16 m_matPreProj;
static D3DXMATRIXA16 m_matPreView;
static D3DXMATRIXA16 m_matPreWorld;
static D3DXMATRIXA16 postproj;
static D3DXVECTOR4 maskmult, maskshift;

static int ledwidth, ledheight;
static int max_texture_w, max_texture_h;
static int tin_w, tin_h, tout_w, tout_h, window_h, window_w;
static int t_depth, mult, multx;
static int required_sl_texture_w, required_sl_texture_h;
static int vsync2, guimode;
static int resetcount;
static int cursor_x, cursor_y, cursor_v;

#define D3DFVF_TLVERTEX D3DFVF_XYZ|D3DFVF_DIFFUSE|D3DFVF_TEX1
struct TLVERTEX {
	D3DXVECTOR3 position;       // vertex position
	D3DCOLOR    diffuse;
	D3DXVECTOR2 texcoord;       // texture coords
};

static int ddraw_fs;
static LPDIRECTDRAW7 ddraw;

static void ddraw_fs_hack_free (void)
{
	HRESULT hr;

	if (!ddraw_fs)
		return;
	if (ddraw_fs == 2)
		ddraw->RestoreDisplayMode ();
	hr = ddraw->SetCooperativeLevel (d3dhwnd, DDSCL_NORMAL);
	if (FAILED (hr)) {
		write_log (L"IDirectDraw7_SetCooperativeLevel CLEAR: %s\n", DXError (hr));
	}
	ddraw->Release ();
	ddraw = NULL;
	ddraw_fs = 0;

}

static int ddraw_fs_hack_init (void)
{
	HRESULT hr;
	struct MultiDisplay *md;

	ddraw_fs_hack_free ();
	md = getdisplay (&currprefs);
	if (!md)
		return 0;
	hr = DirectDrawCreateEx (md->primary ? NULL : &md->guid, (LPVOID*)&ddraw, IID_IDirectDraw7, NULL);
	if (FAILED (hr)) {
		write_log (L"DirectDrawCreateEx failed, %s\n", DXError (hr));
		return 0;
	}
	ddraw_fs = 1;
	hr = ddraw->SetCooperativeLevel (d3dhwnd, DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
	if (FAILED (hr)) {
		write_log (L"IDirectDraw7_SetCooperativeLevel SET: %s\n", DXError (hr));
		ddraw_fs_hack_free ();
		return 0;
	}
	hr = ddraw->SetDisplayMode (dpp.BackBufferWidth, dpp.BackBufferHeight, t_depth, dpp.FullScreen_RefreshRateInHz, 0);
	if (FAILED (hr)) {
		write_log (L"IDirectDraw7_SetDisplayMode: %s\n", DXError (hr));
		ddraw_fs_hack_free ();
		return 0;
	}
	ddraw_fs = 2;
	return 1;
}

static TCHAR *D3D_ErrorText (HRESULT error)
{
	return L"";
}
static TCHAR *D3D_ErrorString (HRESULT dival)
{
	static TCHAR dierr[200];
	_stprintf (dierr, L"%08X S=%d F=%04X C=%04X (%d) (%s)",
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
	static TCHAR buffer[1000];
	TCHAR *s = NULL;

	buffer[0] = 0;
	if (Errors)
		s = au ((char*)Errors->GetBufferPointer ());
	if (hr != S_OK)
		_tcscpy (buffer, D3D_ErrorString (hr));
	if (s) {
		if (buffer[0])
			_tcscat (buffer, L" ");
		_tcscat (buffer, s);
	}
	xfree (s);
	return buffer;
}

static LPD3DXEFFECT postEffect;
static D3DXHANDLE postSourceTextureHandle;
static D3DXHANDLE postMaskTextureHandle;
static D3DXHANDLE postTechnique, postTechniquePlain, postTechniqueAlpha;
static D3DXHANDLE postMatrixSource;
static D3DXHANDLE postMaskMult, postMaskShift;
static D3DXHANDLE postFilterMode;

static LPD3DXEFFECT pEffect;
static D3DXEFFECT_DESC EffectDesc;
static float m_scale;
static LPCSTR m_strName;
// Matrix Handles
static D3DXHANDLE m_MatWorldEffectHandle;
static D3DXHANDLE m_MatViewEffectHandle;
static D3DXHANDLE m_MatProjEffectHandle;
static D3DXHANDLE m_MatWorldViewEffectHandle;
static D3DXHANDLE m_MatViewProjEffectHandle;
static D3DXHANDLE m_MatWorldViewProjEffectHandle;
// Texture Handles
static D3DXHANDLE m_SourceDimsEffectHandle;
static D3DXHANDLE m_TexelSizeEffectHandle;
static D3DXHANDLE m_SourceTextureEffectHandle;
static D3DXHANDLE m_WorkingTexture1EffectHandle;
static D3DXHANDLE m_WorkingTexture2EffectHandle;
static D3DXHANDLE m_Hq2xLookupTextureHandle;
// Technique stuff
static D3DXHANDLE m_PreprocessTechnique1EffectHandle;
static D3DXHANDLE m_PreprocessTechnique2EffectHandle;
static D3DXHANDLE m_CombineTechniqueEffectHandle;
enum psEffect_Pass { psEffect_None, psEffect_PreProcess1, psEffect_PreProcess2, psEffect_Combine };

static int postEffect_ParseParameters (LPD3DXEFFECTCOMPILER EffectCompiler, LPD3DXEFFECT effect)
{
	postSourceTextureHandle = effect->GetParameterByName (NULL, "SourceTexture");
	postMaskTextureHandle = effect->GetParameterByName (NULL, "OverlayTexture");
	postTechnique = effect->GetTechniqueByName ("PostTechnique");
	postTechniquePlain = effect->GetTechniqueByName ("PostTechniquePlain");
	postTechniqueAlpha = effect->GetTechniqueByName ("PostTechniqueAlpha");
	postMatrixSource = effect->GetParameterByName (NULL, "mtx");
	postMaskMult = effect->GetParameterByName (NULL, "maskmult");
	postMaskShift = effect->GetParameterByName (NULL, "maskshift");
	postFilterMode = effect->GetParameterByName (NULL, "filtermode");
	if (!postMaskShift || !postMaskMult || !postFilterMode || !postMatrixSource) {
		gui_message (L"Mismatched _winuae.fx! Exiting..");
		abort ();
	}
	return true;
}

static int psEffect_ParseParameters (LPD3DXEFFECTCOMPILER EffectCompiler, LPD3DXEFFECT effect)
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
			write_log (L"GetParameterDescParm(%d) failed: %s\n", D3DHEAD, iParam, D3DX_ErrorString (hr, NULL));
			return 0;
		}
		hr = S_OK;
		if(ParamDesc.Semantic != NULL) {
			if(ParamDesc.Class == D3DXPC_MATRIX_ROWS || ParamDesc.Class == D3DXPC_MATRIX_COLUMNS) {
				if(strcmpi(ParamDesc.Semantic, "world") == 0)
					m_MatWorldEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "view") == 0)
					m_MatViewEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "projection") == 0)
					m_MatProjEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "worldview") == 0)
					m_MatWorldViewEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "viewprojection") == 0)
					m_MatViewProjEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "worldviewprojection") == 0)
					m_MatWorldViewProjEffectHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_VECTOR && ParamDesc.Type == D3DXPT_FLOAT) {
				if(strcmpi(ParamDesc.Semantic, "sourcedims") == 0)
					m_SourceDimsEffectHandle = hParam;
				else if(strcmpi(ParamDesc.Semantic, "texelsize") == 0)
					m_TexelSizeEffectHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_SCALAR && ParamDesc.Type == D3DXPT_FLOAT) {
				if(strcmpi(ParamDesc.Semantic, "SCALING") == 0)
					hr = effect->GetFloat(hParam, &m_scale);
			} else if(ParamDesc.Class == D3DXPC_OBJECT && ParamDesc.Type == D3DXPT_TEXTURE) {
				if(strcmpi(ParamDesc.Semantic, "SOURCETEXTURE") == 0)
					m_SourceTextureEffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE") == 0)
					m_WorkingTexture1EffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE1") == 0)
					m_WorkingTexture2EffectHandle = hParam;
				if(strcmpi(ParamDesc.Semantic, "HQ2XLOOKUPTEXTURE") == 0)
					m_Hq2xLookupTextureHandle = hParam;
			} else if(ParamDesc.Class == D3DXPC_OBJECT && ParamDesc.Type == D3DXPT_STRING) {
				LPCSTR pstrTechnique = NULL;
				if(strcmpi(ParamDesc.Semantic, "COMBINETECHNIQUE") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					m_CombineTechniqueEffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					m_PreprocessTechnique1EffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE1") == 0) {
					hr = effect->GetString(hParam, &pstrTechnique);
					m_PreprocessTechnique2EffectHandle = effect->GetTechniqueByName(pstrTechnique);
				} else if(strcmpi(ParamDesc.Semantic, "NAME") == 0) {
					hr = effect->GetString(hParam, &m_strName);
				}
			}
			if (FAILED (hr)) {
				write_log (L"ParamDesc.Semantic failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, NULL));
				return 0;
			}
		}

		for(iAnnot = 0; iAnnot < ParamDesc.Annotations; iAnnot++) {
			hAnnot = effect->GetAnnotation (hParam, iAnnot);
			hr = effect->GetParameterDesc(hAnnot, &AnnotDesc);
			if (FAILED (hr)) {
				write_log (L"GetParameterDescAnnot(%d) failed: %s\n", D3DHEAD, iAnnot, D3DX_ErrorString (hr, NULL));
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
				write_log (L"GetString/GetInt(%d) failed: %s\n", D3DHEAD, iAnnot, D3DX_ErrorString (hr, NULL));
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
							if(SUCCEEDED(hr = D3DXCreateVolumeTexture(d3ddev,
								Width, Height, Depth, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pVolumeTex))) {
									if(SUCCEEDED(hr = D3DXFillVolumeTextureTX(pVolumeTex, ppTextureShader))) {
										pTex = pVolumeTex;
									}
							}
						} else if(strcmpi(pstrTextureType, "cube") == 0) {
							LPDIRECT3DCUBETEXTURE9 pCubeTex = NULL;
							if(SUCCEEDED(hr = D3DXCreateCubeTexture(d3ddev,
								Width, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pCubeTex))) {
									if(SUCCEEDED(hr = D3DXFillCubeTextureTX(pCubeTex, ppTextureShader))) {
										pTex = pCubeTex;
									}
							}
						}
					} else {
						LPDIRECT3DTEXTURE9 p2DTex = NULL;
						if(SUCCEEDED(hr = D3DXCreateTexture(d3ddev, Width, Height,
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
				write_log (L"%s: Could not compile texture shader: %s\n", D3DHEAD, D3DX_ErrorString (hr, lpErrors));
				if (lpErrors)
					lpErrors->Release ();
				return 0;
			}
		}
	}
	return 1;
}

static int psEffect_hasPreProcess (void) { return m_PreprocessTechnique1EffectHandle != 0; }
static int psEffect_hasPreProcess2 (void) { return m_PreprocessTechnique2EffectHandle != 0; }

int D3D_goodenough (void)
{
	static int d3d_good;
	LPDIRECT3D9 d3dx;
	D3DCAPS9 d3dCaps;

	if (d3d_good > 0)
		return 1;
	if (d3d_good < 0)
		return 0;
	d3d_good = -1;
	d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
	if (d3dx != NULL) {
		if (SUCCEEDED (d3dx->GetDeviceCaps (D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
			if (d3dCaps.PixelShaderVersion >= D3DPS_VERSION(1, 0)) {
				if (((d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) == D3DPTEXTURECAPS_NONPOW2CONDITIONAL) || !(d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2)) {
					if (!(d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY) && (d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)) {
						d3d_good = 1;
						shaderon = 1;
					}
				}
			}
		}
		d3dx->Release ();
	}
	return d3d_good > 0 ? 1 : 0;
}

int D3D_canshaders (void)
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
					write_log (L"Direct3D: Pixel shader 2.0+ support detected, shader filters enabled.\n");
					d3d_yesno = 1;
				}
			}
			d3dx->Release ();
		}
	}
	return d3d_yesno > 0 ? 1 : 0;
}

static const char *fx10 = {

"// 2 (version)\n"
"//\n"
"// WinUAE Direct3D post processing shader\n"
"//\n"
"// by Toni Wilen 2010\n"
"\n"
"uniform extern float4x4 mtx;\n"
"uniform extern float2 maskmult;\n"
"uniform extern float2 maskshift;\n"
"uniform extern int filtermode;\n"
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

"// 2 (version)\n"
"//\n"
"// WinUAE Direct3D post processing shader\n"
"//\n"
"// by Toni Wilen 2010\n"
"\n"
"uniform extern float4x4 mtx;\n"
"uniform extern float2 maskmult;\n"
"uniform extern float2 maskshift;\n"
"uniform extern int filtermode;\n"
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

static LPD3DXEFFECT psEffect_LoadEffect (const TCHAR *shaderfile, int full)
{
	int ret = 0;
	LPD3DXEFFECTCOMPILER EffectCompiler = NULL;
	LPD3DXBUFFER Errors = NULL;
	LPD3DXBUFFER BufferEffect = NULL;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];
	LPD3DXEFFECT effect = NULL;
	static int first;
	DWORD compileflags = psEnabled ? 0 : D3DXSHADER_USE_LEGACY_D3DX9_31_DLL;
	int canusefile = 0, existsfile = 0;

	compileflags |= EFFECTCOMPILERFLAGS;
	get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), L"filtershaders\\direct3d");
	_tcscat (tmp, shaderfile);
	if (!full) {
		struct zfile *z = zfile_fopen (tmp, L"r", 0);
		if (z) {
			existsfile = 1;
			zfile_fgets (tmp2, sizeof tmp2 / sizeof (TCHAR), z);
			zfile_fclose (z);
			int ver = _tstol (tmp2 + 2);
			if (ver == EFFECT_VERSION) {
				canusefile = 1;
			} else {
				write_log (L"'%s' mismatched version (%d != %d)\n", tmp, ver, EFFECT_VERSION);
			}
		}
		hr = E_FAIL;
		if (canusefile) {
			write_log (L"%s: Attempting to load '%s'\n", D3DHEAD, tmp);
			hr = D3DXCreateEffectCompilerFromFile (tmp, NULL, NULL, compileflags, &EffectCompiler, &Errors);
			if (FAILED (hr))
				write_log (L"%s: D3DXCreateEffectCompilerFromFile failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		}
		if (FAILED (hr)) {
			const char *str = psEnabled ? fx20 : fx10;
			int len = strlen (str);
			if (!existsfile || canusefile == 0) {
				struct zfile *z = zfile_fopen (tmp, L"w", 0);
				if (z) {
					zfile_fwrite ((void*)str, len, 1, z);
					zfile_fclose (z);
				}
			}
			hr = D3DXCreateEffectCompiler (str, len, NULL, NULL, compileflags, &EffectCompiler, &Errors);
			if (FAILED (hr)) {
				write_log (L"%s: D3DXCreateEffectCompilerFromResource failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
				goto end;
			}
		}
	} else {
		write_log (L"%s: Attempting to load '%s'\n", D3DHEAD, tmp);
		hr = D3DXCreateEffectCompilerFromFile (tmp, NULL, NULL, compileflags, &EffectCompiler, &Errors);
		if (FAILED (hr)) {
			write_log (L"%s: D3DXCreateEffectCompilerFromFile failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
			goto end;
		}
	}

	if (Errors) {
		write_log (L"%s: '%s' warning: %s\n", D3DHEAD, shaderfile, D3DX_ErrorString (hr, Errors));
		Errors->Release();
		Errors = NULL;
	}

	hr = EffectCompiler->CompileEffect (0, &BufferEffect, &Errors);
	if (FAILED (hr)) {
		write_log (L"%s: CompileEffect failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	void *bp = BufferEffect->GetBufferPointer ();
	int bplen = BufferEffect->GetBufferSize ();
	hr = D3DXCreateEffect (d3ddev,
		bp, bplen,
		NULL, NULL,
		EFFECTCOMPILERFLAGS,
		NULL, &effect, &Errors);
	if (FAILED (hr)) {
		write_log (L"%s: D3DXCreateEffect failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	hr = effect->GetDesc (&EffectDesc);
	if (FAILED (hr)) {
		write_log (L"%s: effect->GetDesc() failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	if (full) {
		if (!psEffect_ParseParameters (EffectCompiler, effect))
			goto end;
	} else {
		if (!postEffect_ParseParameters (EffectCompiler, effect))
			goto end;
	}
	ret = 1;
end:
	if (Errors)
		Errors->Release ();
	if (BufferEffect)
		BufferEffect->Release ();
	if (EffectCompiler)
		EffectCompiler->Release ();

	if (full) {
		psActive = FALSE;
		psPreProcess = FALSE;
		if (ret) {
			psActive = TRUE;
			if (psEffect_hasPreProcess ())
				psPreProcess = TRUE;
		}
	}
	if (ret)
		write_log (L"%s: pixelshader filter '%s' enabled\n", D3DHEAD, tmp);
	else
		write_log (L"%s: pixelshader filter '%s' failed to initialize\n", D3DHEAD, tmp);
	return effect;
}

static int psEffect_SetMatrices (D3DXMATRIXA16 *matProj, D3DXMATRIXA16 *matView, D3DXMATRIXA16 *matWorld)
{
	HRESULT hr;

	if (m_MatWorldEffectHandle) {
		hr = pEffect->SetMatrix (m_MatWorldEffectHandle, matWorld);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matWorld %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatViewEffectHandle) {
		hr = pEffect->SetMatrix (m_MatViewEffectHandle, matView);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matView %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatProjEffectHandle) {
		hr = pEffect->SetMatrix (m_MatProjEffectHandle, matProj);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matProj %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatWorldViewEffectHandle) {
		D3DXMATRIXA16 matWorldView;
		D3DXMatrixMultiply (&matWorldView, matWorld, matView);
		hr = pEffect->SetMatrix (m_MatWorldViewEffectHandle, &matWorldView);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matWorldView %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatViewProjEffectHandle) {
		D3DXMATRIXA16 matViewProj;
		D3DXMatrixMultiply (&matViewProj, matView, matProj);
		hr = pEffect->SetMatrix (m_MatViewProjEffectHandle, &matViewProj);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matViewProj %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatWorldViewProjEffectHandle) {
		D3DXMATRIXA16 tmp, matWorldViewProj;
		D3DXMatrixMultiply (&tmp, matWorld, matView);
		D3DXMatrixMultiply (&matWorldViewProj, &tmp, matProj);
		hr = pEffect->SetMatrix (m_MatWorldViewProjEffectHandle, &matWorldViewProj);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matWorldViewProj %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	return 1;
}

static int psEffect_SetTextures (LPDIRECT3DTEXTURE9 lpSource, LPDIRECT3DTEXTURE9 lpWorking1,
	LPDIRECT3DTEXTURE9 lpWorking2, LPDIRECT3DVOLUMETEXTURE9 lpHq2xLookupTexture)
{
	HRESULT hr;
	D3DXVECTOR4 fDims, fTexelSize;

	if (!m_SourceTextureEffectHandle) {
		write_log (L"%s: Texture with SOURCETEXTURE semantic not found\n", D3DHEAD);
		return 0;
	}
	hr = pEffect->SetTexture (m_SourceTextureEffectHandle, lpSource);
	if (FAILED (hr)) {
		write_log (L"%s: SetTextures:lpSource %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	if (m_WorkingTexture1EffectHandle) {
		hr = pEffect->SetTexture (m_WorkingTexture1EffectHandle, lpWorking1);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:lpWorking1 %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_WorkingTexture2EffectHandle) {
		hr = pEffect->SetTexture (m_WorkingTexture2EffectHandle, lpWorking2);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:lpWorking2 %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_Hq2xLookupTextureHandle) {
		hr = pEffect->SetTexture (m_Hq2xLookupTextureHandle, lpHq2xLookupTexture);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:lpHq2xLookupTexture %s\n", D3DHEAD, D3D_ErrorString (hr));
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
	fTexelSize.x = 1 / fDims.x;
	fTexelSize.y = 1 / fDims.y;
	if (m_SourceDimsEffectHandle) {
		hr = pEffect->SetVector (m_SourceDimsEffectHandle, &fDims);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:SetVector:Source %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_TexelSizeEffectHandle) {
		hr = pEffect->SetVector (m_TexelSizeEffectHandle, &fTexelSize);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:SetVector:Texel %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}

	return 1;
}

static int psEffect_Begin (LPD3DXEFFECT effect, enum psEffect_Pass pass, UINT *pPasses)
{
	HRESULT hr;
	switch (pass)
	{
	case psEffect_PreProcess1:
		hr = effect->SetTechnique (m_PreprocessTechnique1EffectHandle);
		break;
	case psEffect_PreProcess2:
		hr = effect->SetTechnique (m_PreprocessTechnique2EffectHandle);
		break;
	case psEffect_Combine:
		hr = effect->SetTechnique (m_CombineTechniqueEffectHandle);
		break;
	default:
		hr = S_OK;
		break;
	}
	if (FAILED (hr)) {
		write_log (L"%s: SetTechnique: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	hr = effect->Begin (pPasses, 0);
	if (FAILED (hr)) {
		write_log (L"%s: Begin: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}

static int psEffect_BeginPass (LPD3DXEFFECT effect, UINT Pass)
{
	HRESULT hr;

	hr = effect->BeginPass (Pass);
	if (FAILED (hr)) {
		write_log (L"%s: BeginPass: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_EndPass (LPD3DXEFFECT effect)
{
	HRESULT hr;

	hr = effect->EndPass ();
	if (FAILED (hr)) {
		write_log (L"%s: EndPass: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_End (LPD3DXEFFECT effect)
{
	HRESULT hr;

	hr = effect->End ();
	if (FAILED (hr)) {
		write_log (L"%s: End: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}

static LPDIRECT3DTEXTURE9 createtext (int w, int h, D3DFORMAT format)
{
	LPDIRECT3DTEXTURE9 t;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	hr = d3ddev->CreateTexture (w, h, 1, D3DUSAGE_DYNAMIC, format, D3DPOOL_DEFAULT, &t, NULL);
	if (FAILED (hr))
		write_log (L"%s: CreateTexture() D3DUSAGE_DYNAMIC failed: %s (%d*%d %08x)\n",
			D3DHEAD, D3D_ErrorString (hr), w, h, format);
	if (FAILED (hr)) {
		hr = d3ddev->CreateTexture (w, h, 1, 0, format, D3DPOOL_DEFAULT, &t, NULL);
	}
	if (FAILED (hr)) {
		write_log (L"%s: CreateTexture() failed: %s (%d*%d %08x)\n",
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

static int createtexture (int iw, int ih, int ow, int oh, int win_w, int win_h)
{
	HRESULT hr;

	texture = createtext (iw, ih, tformat);
	if (!texture)
		return 0;
	write_log (L"%s: %d*%d texture allocated, bits per pixel %d\n", D3DHEAD, iw, ih, t_depth);
	int w, h;
	if (ow > win_w * multx && oh > win_h * multx) {
		w = ow;
		h = oh;
	} else {
		w = win_w * multx;
		h = win_h * multx;
	}
	if (FAILED (hr = d3ddev->CreateTexture (w, h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpTempTexture, NULL))) {
		write_log (L"%s: Failed to create working texture1: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	write_log (L"%s: %d*%d working texture allocated, bits per pixel %d\n", D3DHEAD, w, h, t_depth);
	if (psActive) {
		D3DLOCKED_BOX lockedBox;
		if (FAILED (hr = d3ddev->CreateTexture (iw, ih, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture1, NULL))) {
			write_log (L"%s: Failed to create temp texture: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		if (FAILED (hr = d3ddev->CreateTexture (iw, ih, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture2, NULL))) {
			write_log (L"%s: Failed to create working texture2: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		if (FAILED (hr = d3ddev->CreateVolumeTexture (256, 16, 256, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpHq2xLookupTexture, NULL))) {
			write_log (L"%s: Failed to create volume texture: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		if (FAILED (hr = lpHq2xLookupTexture->LockBox (0, &lockedBox, NULL, 0))) {
			write_log (L"%s: Failed to lock box of volume texture: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		BuildHq2xLookupTexture (w, h, iw, ih,  (unsigned char*)lockedBox.pBits);
		lpHq2xLookupTexture->UnlockBox (0);

	}
	return 1;
}

static void updateleds (void)
{
	D3DLOCKED_RECT locked;
	HRESULT hr;
	static uae_u32 rc[256], gc[256], bc[256], a[256];
	static int done;
	int i, y;

	if (!done) {
		for (i = 0; i < 256; i++) {
			rc[i] = i << 16;
			gc[i] = i << 8;
			bc[i] = i << 0;
			a[i] = i << 24;
		}
		done = 1;
	}
	hr = ledtexture->LockRect (0, &locked, NULL, D3DLOCK_DISCARD);
	if (FAILED (hr)) {
		write_log (L"%d: SL LockRect failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
		uae_u8 *buf = (uae_u8*)locked.pBits + y * locked.Pitch;
		memset (buf, 0, ledwidth * 4);
		draw_status_line_single (buf, 32 / 8, y, ledwidth, rc, gc, bc, a);
	}
	ledtexture->UnlockRect (0);
}

static int createledtexture (void)
{
	ledwidth = window_w;
	ledheight = TD_TOTAL_HEIGHT;
	ledtexture = createtext (ledwidth, ledheight, D3DFMT_A8R8G8B8);
	if (!ledtexture)
		return 0;
	return 1;
}

static int createsltexture (void)
{
	if (masktexture)
		return 0;
	sltexture = createtext (required_sl_texture_w, required_sl_texture_h, t_depth < 32 ? D3DFMT_A4R4G4B4 : D3DFMT_A8R8G8B8);
	if (!sltexture)
		return 0;
	write_log (L"%s: SL %d*%d texture allocated\n", D3DHEAD, required_sl_texture_w, required_sl_texture_h);
	maskmult_x = 1.0;
	maskmult_y = 1.0;
	return 1;
}

static void createscanlines (int force)
{
	HRESULT hr;
	D3DLOCKED_RECT locked;
	static int osl1, osl2, osl3;
	int sl4, sl42;
	int l1, l2;
	int x, y, yy;
	uae_u8 *sld, *p;
	int bpp;

	if (osl1 == currprefs.gfx_filter_scanlines && osl3 == currprefs.gfx_filter_scanlinelevel && osl2 == currprefs.gfx_filter_scanlineratio && !force)
		return;
	bpp = t_depth < 32 ? 2 : 4;
	osl1 = currprefs.gfx_filter_scanlines;
	osl3 = currprefs.gfx_filter_scanlinelevel;
	osl2 = currprefs.gfx_filter_scanlineratio;
	sl4 = currprefs.gfx_filter_scanlines * 16 / 100;
	sl42 = currprefs.gfx_filter_scanlinelevel * 16 / 100;
	if (sl4 > 15)
		sl4 = 15;
	if (sl42 > 15)
		sl42 = 15;
	l1 = (currprefs.gfx_filter_scanlineratio >> 0) & 15;
	l2 = (currprefs.gfx_filter_scanlineratio >> 4) & 15;

	if (l1 + l2 <= 0)
		return;

	if (!sltexture) {
		if (osl1 == 0 && osl3 == 0)
			return;
		if (!createsltexture ())
			return;
	}

	hr = sltexture->LockRect (0, &locked, NULL, 0);
	if (FAILED (hr)) {
		write_log (L"%s: SL LockRect failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	sld = (uae_u8*)locked.pBits;
	for (y = 0; y < required_sl_texture_h; y++)
		memset (sld + y * locked.Pitch, 0, required_sl_texture_w * bpp);
	for (y = 1; y < required_sl_texture_h; y += l1 + l2) {
		for (yy = 0; yy < l2 && y + yy < required_sl_texture_h; yy++) {
			for (x = 0; x < required_sl_texture_w; x++) {
				uae_u8 sll = sl42;
				p = &sld[(y + yy) * locked.Pitch + (x * bpp)];
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
	sltexture->UnlockRect (0);
}

static int findedge (D3DLOCKED_RECT *lock, int w, int h, int dx, int dy)
{
	int x = w / 2;
	int y = h / 2;
	
	if (dx != 0)
		x = dx < 0 ? 0 : w - 1;
	if (dy != 0)
		y = dy < 0 ? 0 : h - 1;
	
	for (;;) {
		uae_u32 *p = (uae_u32*)((uae_u8*)lock->pBits + y * lock->Pitch + x * 4);
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

static int createmask2texture (const TCHAR *filename)
{
	struct zfile *zf;
	int size;
	uae_u8 *buf;
	LPDIRECT3DTEXTURE9 tx;
	HRESULT hr;
	D3DXIMAGE_INFO dinfo;
	TCHAR tmp[MAX_DPATH];

	if (filename[0] == 0 || WIN32GFX_IsPicassoScreen ())
		return 0;

	zf = NULL;
	for (int i = 0; i < 2; i++) {
		if (i == 0) {
			get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), L"overlays");
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
			_stprintf (s, L"_%dx%d%s", window_w, window_h, tmp2);
			zf = zfile_fopen (tmp3, L"rb", ZFD_NORMAL);
			if (zf)
				break;
			float aspect = (float)window_w / window_h;
			int ax = -1, ay = -1;
			if (abs (aspect - 16.0 / 10.0) <= 0.1)
				ax = 16, ay = 10;
			if (abs (aspect - 16.0 / 9.0) <= 0.1)
				ax = 16, ay = 9;
			if (abs (aspect - 4.0 / 3.0) <= 0.1)
				ax = 4, ay = 3;
			if (ax > 0 && ay > 0) {
				_stprintf (s, L"_%dx%d%s", ax, ay, tmp2);
				zf = zfile_fopen (tmp3, L"rb", ZFD_NORMAL);
				if (zf)
					break;
			}
		}
		zf = zfile_fopen (tmp, L"rb", ZFD_NORMAL);
		if (zf)
			break;
	}
	if (!zf) {
		write_log (L"%s: couldn't open overlay '%s'\n", D3DHEAD, filename);
		return 0;
	}
	size = zfile_size (zf);
	buf = xmalloc (uae_u8, size);
	zfile_fread (buf, size, 1, zf);
	zfile_fclose (zf);
	hr = D3DXCreateTextureFromFileInMemoryEx (d3ddev, buf, size,
		 D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
		 D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, &dinfo, NULL, &tx);
	xfree (buf);
	if (FAILED (hr)) {
		write_log (L"%s: overlay texture load failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		goto end;
	}
	mask2texture_w = dinfo.Width;
	mask2texture_h = dinfo.Height;
	mask2texture = tx;
	mask2rect.left = 0;
	mask2rect.top = 0;
	mask2rect.right = mask2texture_w;
	mask2rect.bottom = mask2texture_h;

	D3DLOCKED_RECT lock;
	if (SUCCEEDED (hr = mask2texture->LockRect (0, &lock, NULL, 0))) {
		mask2rect.left = findedge (&lock, mask2texture_w, mask2texture_h, -1, 0);
		mask2rect.right = findedge (&lock, mask2texture_w, mask2texture_h, 1, 0);
		mask2rect.top = findedge (&lock, mask2texture_w, mask2texture_h, 0, -1);
		mask2rect.bottom = findedge (&lock, mask2texture_w, mask2texture_h, 0, 1);
		mask2texture->UnlockRect (0);
	}
	if (mask2rect.left >= mask2texture_w / 2 || mask2rect.top >= mask2texture_h / 2 ||
		mask2rect.right <= mask2texture_w / 2 || mask2rect.bottom <= mask2texture_h / 2) {
		mask2rect.left = 0;
		mask2rect.top = 0;
		mask2rect.right = mask2texture_w;
		mask2rect.bottom = mask2texture_h;
	}
	mask2texture_multx = (float)window_w / mask2texture_w;
	mask2texture_multy = (float)window_h / mask2texture_h;
	mask2texture_offsetw = 0;

	if (isfullscreen () > 0) {
		struct MultiDisplay *md = getdisplay (&currprefs);
		float deskw = md->rect.right - md->rect.left;
		float deskh = md->rect.bottom - md->rect.top;
		//deskw = 800; deskh = 600;
		float dstratio = deskw / deskh;
		float srcratio = mask2texture_w / mask2texture_h;
		mask2texture_multx *= srcratio / dstratio;
	} else {
		mask2texture_multx = mask2texture_multy;
	}

	mask2texture_wh = window_h;
	mask2texture_ww = mask2texture_w * mask2texture_multx; 

	mask2texture_offsetw = (window_w - mask2texture_ww) / 2;

	blanktexture = createtext (mask2texture_offsetw + 1, window_h, D3DFMT_X8R8G8B8);

	write_log (L"%s: overlay '%s' %.0f*%.0f (%d*%d - %d*%d)\n",
		D3DHEAD, tmp, mask2texture_w, mask2texture_h,
		mask2rect.left, mask2rect.top, mask2rect.right, mask2rect.bottom);

	return 1;
end:
	if (tx)
		tx->Release ();
	return 0;
}

static int createmasktexture (const TCHAR *filename)
{
	int ww = window_w;
	int hh = window_h;
	struct zfile *zf;
	int size;
	uae_u8 *buf;
	D3DSURFACE_DESC maskdesc, txdesc;
	LPDIRECT3DTEXTURE9 tx;
	HRESULT hr;
	D3DLOCKED_RECT lock, slock;
	D3DXIMAGE_INFO dinfo;
	TCHAR tmp[MAX_DPATH];

	if (filename[0] == 0)
		return 0;
	tx = NULL;
	get_plugin_path (tmp, sizeof tmp / sizeof (TCHAR), L"masks");
	_tcscat (tmp, filename);
	zf = zfile_fopen (tmp, L"rb", ZFD_NORMAL);
	if (!zf) {
		zf = zfile_fopen (filename, L"rb", ZFD_NORMAL);
		if (!zf) {
			write_log (L"%s: couldn't open mask '%s'\n", D3DHEAD, filename);
			return 0;
		}
	}
	size = zfile_size (zf);
	buf = xmalloc (uae_u8, size);
	zfile_fread (buf, size, 1, zf);
	zfile_fclose (zf);
	hr = D3DXCreateTextureFromFileInMemoryEx (d3ddev, buf, size,
		 D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, 1, D3DUSAGE_DYNAMIC, D3DFMT_X8R8G8B8,
		 D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, &dinfo, NULL, &tx);
	xfree (buf);
	if (FAILED (hr)) {
		write_log (L"%s: temp mask texture load failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		goto end;
	}
	hr = tx->GetLevelDesc (0, &txdesc);
	if (FAILED (hr)) {
		write_log (L"%s: mask image texture GetLevelDesc() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		goto end;
	}
	masktexture_w = dinfo.Width;
	masktexture_h = dinfo.Height;
	if (txdesc.Width == masktexture_w && txdesc.Height == masktexture_h && psEnabled) {
		// texture size == image size, no need to tile it (Wrap sampler does the rest)
		masktexture = tx;
		tx = NULL;
	} else {
		masktexture = createtext (ww, hh, D3DFMT_X8R8G8B8);
		if (FAILED (hr)) {
			write_log (L"%s: mask texture creation failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
			goto end;
		}
		hr = masktexture->GetLevelDesc (0, &maskdesc);
		if (FAILED (hr)) {
			write_log (L"%s: mask texture GetLevelDesc() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
			goto end;
		}
		if (SUCCEEDED (hr = masktexture->LockRect (0, &lock, NULL, 0))) {
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
			masktexture->UnlockRect (0);
		}
		tx->Release ();
		masktexture_w = maskdesc.Width;
		masktexture_h = maskdesc.Height;
	}
	write_log (L"%s: mask %d*%d (%d*%d) ('%s') texture allocated\n", D3DHEAD, masktexture_w, masktexture_h, txdesc.Width, txdesc.Height, filename);
	maskmult_x = (float)ww / masktexture_w;
	maskmult_y = (float)hh / masktexture_h;

	return 1;
end:
	if (masktexture)
		masktexture->Release ();
	masktexture = NULL;
	if (tx)
		tx->Release ();
	return 0;
}

static void setupscenecoords (void)
{
	RECT sr, dr, zr;
	float w, h;
	float dw, dh;
	static RECT sr2, dr2, zr2;

	//write_log (L"%dx%d %dx%d %dx%d\n", tin_w, tin_h, tin_w, tin_h, window_w, window_h);

	float mw = mask2rect.right - mask2rect.left;
	float mh = mask2rect.bottom - mask2rect.top;

	getfilterrect2 (&dr, &sr, &zr, window_w, window_h, tin_w / mult, tin_h / mult, mult, tin_w, tin_h);

	if (memcmp (&sr, &sr2, sizeof RECT) || memcmp (&dr, &dr2, sizeof RECT) || memcmp (&zr, &zr2, sizeof RECT)) {
		write_log (L"POS (%d %d %d %d) - (%d %d %d %d)[%d,%d] (%d %d)\n",
			dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom,
			sr.right - sr.left, sr.bottom - sr.top,
			zr.left, zr.top);
		sr2 = sr;
		dr2 = dr;
		zr2 = zr;
	}

	dw = dr.right - dr.left;
	dh = dr.bottom - dr.top;
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;

	MatrixOrthoOffCenterLH (&m_matProj, 0, w, 0, h, 0.0f, 1.0f);

	float tx, ty;
	float sw, sh;

	if (mask2texture) {

		float winw = mw * mask2texture_multx;
		float winh = mh * mask2texture_multy;
		dw *= winw / window_w;
		dh *= winh / window_h;
		tx = -0.5f + dw * tin_w / winw / 2;
		ty = +0.5f + dh * tin_h / winh / 2;

		float xshift = -zr.left - sr.left;
		float yshift = +zr.top  + sr.top - (tin_h - h);

		sw = dw * tin_w / window_w;
		sh = dh * tin_h / window_h;

		tx += xshift;
		ty += yshift;


	} else {

		tx = -0.5f + dw * tin_w / window_w / 2;
		ty = +0.5f + dh * tin_h / window_h / 2;

		tx += - zr.left - sr.left; // - (tin_w - 2 * zr.left - w),
		ty += + zr.top + sr.top - (tin_h - h);
	
		sw = dw * tin_w / window_w;
		sh = dh * tin_h / window_h;
	}

	MatrixTranslation (&m_matView, tx, ty, 1.0f);

	MatrixScaling (&m_matWorld, sw, sh, 1.0f);

	cursor_offset_x = -zr.left;
	cursor_offset_y = -zr.top;

	//write_log (L"%.1fx%.1f %.1fx%.1f %.1fx%.1f\n", dw, dh, w, h, sw, sh);

	// ratio between Amiga texture and overlay mask texture
	float sw2 = dw * tin_w / window_w;
	float sh2 = dh * tin_h / window_h;

	maskmult.x = sw2 * maskmult_x / w;
	maskmult.y = sh2 * maskmult_y / h;

	maskshift.x = 1.0f / maskmult_x;
	maskshift.y = 1.0f / maskmult_y;

	D3DXMATRIXA16 tmpmatrix;
	D3DXMatrixMultiply (&tmpmatrix, &m_matWorld, &m_matView);
	D3DXMatrixMultiply (&postproj, &tmpmatrix, &m_matProj);
}

uae_u8 *getfilterbuffer3d (int *widthp, int *heightp, int *pitch, int *depth)
{
	RECT dr, sr, zr;
	uae_u8 *p;
	int w, h;

	*depth = t_depth;
	getfilterrect2 (&dr, &sr, &zr, window_w, window_h, tin_w / mult, tin_h / mult, mult, tin_w, tin_h);
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;
	p = gfxvidinfo.bufmem;
	if (pitch)
		*pitch = gfxvidinfo.rowbytes;
	p += (zr.top - h / 2) * gfxvidinfo.rowbytes + (zr.left - w / 2) * t_depth / 8;
	*widthp = w;
	*heightp = h;
	return p;
}

static void createvertex (void)
{
	HRESULT hr;
	struct TLVERTEX *vertices;
	float sizex, sizey;

	sizex = 1.0f;
	sizey = 1.0f;
	if (FAILED (hr = vertexBuffer->Lock (0, 0, (void**)&vertices, 0))) {
		write_log (L"%s: Vertexbuffer lock failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
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
	hr = vertexBuffer->Unlock ();
}

static void settransform (void)
{
	// Projection is (0,0,0) -> (1,1,1)
	MatrixOrthoOffCenterLH (&m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	MatrixTranslation (&m_matPreView, -0.5f / tin_w, 0.5f / tin_h, 0.0f);
	// Identity for world
	D3DXMatrixIdentity (&m_matPreWorld);
	psEffect_SetMatrices (&m_matProj, &m_matView, &m_matWorld);

	MatrixOrthoOffCenterLH (&m_matProj2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	MatrixTranslation (&m_matView2, 0.5f - 0.5f / window_w, 0.5f + 0.5f / window_h, 0.0f);
	D3DXMatrixIdentity (&m_matWorld2);
}

static void invalidatedeviceobjects (void)
{
	if (texture) {
		texture->Release ();
		texture = NULL;
	}
	if (sprite) {
		sprite->Release ();
		sprite = NULL;
	}
	if (ledtexture) {
		ledtexture->Release ();
		ledtexture = NULL;
	}
	if (sltexture) {
		sltexture->Release ();
		sltexture = NULL;
	}
	if (masktexture) {
		masktexture->Release ();
		masktexture = NULL;
	}
	if (mask2texture) {
		mask2texture->Release ();
		mask2texture = NULL;
	}
	if (blanktexture) {
		blanktexture->Release ();
		blanktexture = NULL;
	}
	if (lpTempTexture) {
		lpTempTexture->Release ();
		lpTempTexture = NULL;
	}
	if (lpWorkTexture1) {
		lpWorkTexture1->Release ();
		lpWorkTexture1 = NULL;
	}
	if (lpWorkTexture2) {
		lpWorkTexture2->Release ();
		lpWorkTexture2 = NULL;
	}
	if (lpHq2xLookupTexture) {
		lpHq2xLookupTexture->Release ();
		lpHq2xLookupTexture = NULL;
	}
	if (cursorsurfaced3d) {
		cursorsurfaced3d->Release ();
		cursorsurfaced3d = NULL;
	}
	if (pEffect) {
		pEffect->Release ();
		pEffect = NULL;
	}
	if (postEffect) {
		postEffect->Release ();
		postEffect = NULL;
	}
	if (d3ddev)
		d3ddev->SetStreamSource (0, NULL, 0, 0);
	if (vertexBuffer) {
		vertexBuffer->Release ();
		vertexBuffer = NULL;
	}
	m_MatWorldEffectHandle = NULL;
	m_MatViewEffectHandle = NULL;
	m_MatProjEffectHandle = NULL;
	m_MatWorldViewEffectHandle = NULL;
	m_MatViewProjEffectHandle = NULL;
	m_MatWorldViewProjEffectHandle = NULL;
	m_SourceDimsEffectHandle = NULL;
	m_TexelSizeEffectHandle = NULL;
	m_SourceTextureEffectHandle = NULL;
	m_WorkingTexture1EffectHandle = NULL;
	m_WorkingTexture2EffectHandle = NULL;
	m_Hq2xLookupTextureHandle = NULL;
	m_PreprocessTechnique1EffectHandle = NULL;
	m_PreprocessTechnique2EffectHandle = NULL;
	m_CombineTechniqueEffectHandle = NULL;
	locked = 0;
	maskshift.x = maskshift.y = maskshift.z = maskshift.w = 0;
	maskmult.x = maskmult.y = maskmult.z = maskmult.w = 0;
}

static int restoredeviceobjects (void)
{
	int vbsize;
	int wasshader = shaderon;
	HRESULT hr;

	invalidatedeviceobjects ();
	while (shaderon > 0) {
		postEffect = psEffect_LoadEffect (psEnabled ? L"_winuae.fx" : L"_winuae_old.fx", false);
		if (!postEffect) {
			shaderon = 0;
			break;
		}
		if (currprefs.gfx_filtershader[0]) {
			if (!(pEffect = psEffect_LoadEffect (currprefs.gfx_filtershader, true))) {
				currprefs.gfx_filtershader[0] = changed_prefs.gfx_filtershader[0] = 0;
				break;
			}
		}
		if (currprefs.gfx_filter_scanlines > 0) {
			createsltexture ();
			createscanlines (1);
		}
		createmasktexture (currprefs.gfx_filtermask);
		break;
	}
	if (wasshader && !shaderon)
		write_log (L"Falling back to non-shader mode\n");

	createmask2texture (currprefs.gfx_filteroverlay);

	if (!createtexture (tin_w, tin_h, tout_w, tout_h, window_w, window_h))
		return 0;
	createledtexture ();

	hr = D3DXCreateSprite (d3ddev, &sprite);
	if (FAILED (hr)) {
		write_log (L"%s: D3DXSprite failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
	}

	int curw = CURSORMAXWIDTH, curh = CURSORMAXHEIGHT;
	cursorsurfaced3d = createtext (curw, curh, D3DFMT_A8R8G8B8);
	cursor_v = 0;

	vbsize = sizeof (struct TLVERTEX) * 8;
	if (FAILED (hr = d3ddev->SetFVF (D3DFVF_TLVERTEX)))
		write_log (L"%s: SetFVF failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
	if (FAILED (hr = d3ddev->CreateVertexBuffer (vbsize, D3DUSAGE_WRITEONLY,
		D3DFVF_TLVERTEX, D3DPOOL_DEFAULT, &vertexBuffer, NULL))) {
			write_log (L"%s: failed to create vertex buffer: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
	}
	createvertex ();
	if (FAILED (hr = d3ddev->SetStreamSource (0, vertexBuffer, 0, sizeof (struct TLVERTEX))))
		write_log (L"%s: SetStreamSource failed: %s\n", D3DHEAD, D3D_ErrorString (hr));

	hr = d3ddev->SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
	hr = d3ddev->SetRenderState (D3DRS_LIGHTING, FALSE);

	setupscenecoords ();
	settransform ();

	return 1;
}

static void D3D_free2 (void)
{
	invalidatedeviceobjects ();
	if (d3ddev) {
		d3ddev->Release ();
		d3ddev = NULL;
	}
	if (d3d) {
		d3d->Release ();
		d3d = NULL;
	}
	d3d_enabled = 0;
	psPreProcess = 0;
	psActive = 0;
	resetcount = 0;
	devicelost = 0;
	changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen & ~STATUSLINE_TARGET;
}

void D3D_free (void)
{
	D3D_free2 ();
	ddraw_fs_hack_free ();
}

const TCHAR *D3D_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth, int mmult)
{
	HRESULT ret, hr;
	static TCHAR errmsg[100] = { 0 };
	D3DDISPLAYMODE mode;
	D3DCAPS9 d3dCaps;
	int adapter;
	DWORD flags;
	HINSTANCE d3dDLL, d3dx;
	typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);
	LPDIRECT3DCREATE9EX d3dexp = NULL;

	D3D_free2 ();
	d3d_enabled = 0;
	if (!currprefs.gfx_api) {
		_tcscpy (errmsg, L"D3D: not enabled");
		return errmsg;
	}

	d3dx = LoadLibrary (D3DX9DLL);
	if (d3dx == NULL) {
		_tcscpy (errmsg, L"Direct3D: Newer DirectX Runtime required.\n\nhttp://go.microsoft.com/fwlink/?linkid=56513");
		if (isfullscreen () <= 0)
			ShellExecute(NULL, L"open", L"http://go.microsoft.com/fwlink/?linkid=56513", NULL, NULL, SW_SHOWNORMAL);
		return errmsg;
	}
	FreeLibrary (d3dx);

	D3D_goodenough ();
	D3D_canshaders ();

	d3d_ex = FALSE;
	d3dDLL = LoadLibrary (L"D3D9.DLL");
	if (d3dDLL == NULL) {
		_tcscpy (errmsg, L"Direct3D: DirectX 9 or newer required");
		return errmsg;
	} else {
		d3dexp  = (LPDIRECT3DCREATE9EX)GetProcAddress (d3dDLL, "Direct3DCreate9Ex");
		if (d3dexp)
			d3d_ex = TRUE;
	}
	FreeLibrary (d3dDLL);
	hr = -1;
	if (d3d_ex && D3DEX) {
		hr = d3dexp (D3D_SDK_VERSION, &d3dex);
		if (FAILED (hr))
			write_log (L"Direct3D: failed to create D3DEx object: %s\n", D3D_ErrorString (hr));
		d3d = (IDirect3D9*)d3dex;
	}
	if (FAILED (hr)) {
		d3d_ex = 0;
		d3dex = NULL;
		d3d = Direct3DCreate9 (D3D_SDK_VERSION);
		if (d3d == NULL) {
			D3D_free ();
			_tcscpy (errmsg, L"Direct3D: failed to create D3D object");
			return errmsg;
		}
	}
	if (d3d_ex)
		D3DHEAD = L"D3D9Ex";
	else
		D3DHEAD = L"D3D9";

	adapter = currprefs.gfx_display - 1;
	if (adapter < 0)
		adapter = 0;
	if (adapter >= d3d->GetAdapterCount ())
		adapter = 0;

	modeex.Size = sizeof modeex;
	if (d3dex && D3DEX) {
		LUID luid;
		hr = d3dex->GetAdapterLUID (adapter, &luid);
		hr = d3dex->GetAdapterDisplayModeEx (adapter, &modeex, NULL);
	}
	if (FAILED (hr = d3d->GetAdapterDisplayMode (adapter, &mode)))
		write_log (L"%s: GetAdapterDisplayMode failed %s\n", D3DHEAD, D3D_ErrorString (hr));
	if (FAILED (hr = d3d->GetDeviceCaps (adapter, D3DDEVTYPE_HAL, &d3dCaps)))
		write_log (L"%s: GetDeviceCaps failed %s\n", D3DHEAD, D3D_ErrorString (hr));

	memset (&dpp, 0, sizeof (dpp));
	dpp.Windowed = isfullscreen() <= 0;
	dpp.BackBufferFormat = mode.Format;
	dpp.BackBufferCount = currprefs.gfx_backbuffers;
	dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	dpp.BackBufferWidth = w_w;
	dpp.BackBufferHeight = w_h;
	dpp.PresentationInterval = dpp.Windowed || currprefs.gfx_backbuffers == 0 ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_DEFAULT;

	modeex.Width = w_w;
	modeex.Height = w_h;
	modeex.RefreshRate = 0;
	modeex.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
	modeex.Format = mode.Format;

	vsync2 = 0;
	if (isfullscreen() > 0) {
		dpp.FullScreen_RefreshRateInHz = currprefs.gfx_refreshrate > 0 ? currprefs.gfx_refreshrate : 0;
		modeex.RefreshRate = dpp.FullScreen_RefreshRateInHz;
		if (currprefs.gfx_avsync) {
			dpp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
			if (getvsyncrate (dpp.FullScreen_RefreshRateInHz) != dpp.FullScreen_RefreshRateInHz) {
				if (d3dCaps.PresentationIntervals & D3DPRESENT_INTERVAL_TWO)
					dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
				else
					vsync2 = 1;
			}
		}
	}

	d3dhwnd = ahwnd;
	t_depth = depth;

	flags = D3DCREATE_FPU_PRESERVE;
	// Check if hardware vertex processing is available
	if(d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) {
		flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_PUREDEVICE;
	} else {
		flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	}
	if (d3d_ex && D3DEX) {
		ret = d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, dpp.Windowed ? NULL : &modeex, &d3ddevex);
		d3ddev = d3ddevex;
	} else {
		ret = d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &d3ddev);
	}
	if (FAILED (ret) && (flags & D3DCREATE_PUREDEVICE)) {
		flags &= ~D3DCREATE_PUREDEVICE;
		if (d3d_ex && D3DEX) {
			ret = d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, dpp.Windowed ? NULL : &modeex, &d3ddevex);
			d3ddev = d3ddevex;
		} else {
			ret = d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &d3ddev);
		}
		if (FAILED (ret) && (flags & D3DCREATE_HARDWARE_VERTEXPROCESSING)) {
			flags &= ~D3DCREATE_HARDWARE_VERTEXPROCESSING;
			flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;
			if (d3d_ex && D3DEX) {
				ret = d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, dpp.Windowed ? NULL : &modeex, &d3ddevex);
				d3ddev = d3ddevex;
			} else {
				ret = d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &d3ddev);
			}
		}
	}

	if (FAILED (ret)) {
		_stprintf (errmsg, L"%s failed, %s\n", d3d_ex && D3DEX ? L"CreateDeviceEx" : L"CreateDevice", D3D_ErrorString (ret));
		if (ret == D3DERR_INVALIDCALL && dpp.Windowed == 0 && dpp.FullScreen_RefreshRateInHz && !ddraw_fs) {
			write_log (L"%s\n", errmsg);
			write_log (L"%s: Retrying fullscreen with DirectDraw\n", D3DHEAD);
			if (ddraw_fs_hack_init ()) {
				const TCHAR *err2 = D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth, mult);
				if (err2)
					ddraw_fs_hack_free ();
				return err2;
			}
		}
		if (d3d_ex && D3DEX) {
			write_log (L"%s\n", errmsg);
			D3DEX = 0;
			return D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth, mult);
		}
		D3D_free ();
		return errmsg;
	}

	if (d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2, 0))
		psEnabled = TRUE;
	else
		psEnabled = FALSE;

	max_texture_w = d3dCaps.MaxTextureWidth;
	max_texture_h = d3dCaps.MaxTextureHeight;

	write_log (L"%s: %08X ", D3DHEAD, flags);
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
		write_log (L"SQUAREONLY ");
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2)
		write_log (L"POW2 ");
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL)
		write_log (L"NPOTCONDITIONAL ");
	if (d3dCaps.TextureCaps & D3DPTEXTURECAPS_ALPHA)
		write_log (L"ALPHA ");
	if (d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)
		write_log (L"DYNAMIC ");
	write_log (L"\n");

	write_log (L"%s: PS=%d.%d VS=%d.%d %d*%d*%d%s%s %d-bit\n",
		D3DHEAD,
		(d3dCaps.PixelShaderVersion >> 8) & 0xff, d3dCaps.PixelShaderVersion & 0xff,
		(d3dCaps.VertexShaderVersion >> 8) & 0xff, d3dCaps.VertexShaderVersion & 0xff,
		max_texture_w, max_texture_h,
		dpp.Windowed ? 0 : dpp.FullScreen_RefreshRateInHz,
		dpp.Windowed ? L"" : L" FS",
		currprefs.gfx_avsync ? L" VSYNC" : L"",
		t_depth
		);

	if ((d3dCaps.PixelShaderVersion < D3DPS_VERSION(2,0) || !psEnabled || max_texture_w < 2048 || max_texture_h < 2048 || !shaderon) && d3d_ex) {
		D3DEX = 0;
		write_log (L"Disabling D3D9Ex\n");
		return D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth, mult);
	}
	if (!shaderon)
		write_log (L"Using non-shader version\n");

	multx = mmult;
	mult = S2X_getmult ();
	tin_w = t_w * mult;
	tin_h = t_h * mult;
	tout_w = tin_w * multx;
	tout_h = tin_h * multx;

	if (max_texture_w < w_w  || max_texture_h < w_h) {
		_stprintf (errmsg, L"%s: %d * %d or bigger texture support required\nYour card's maximum texture size is only %d * %d",
			D3DHEAD, t_w, t_h, max_texture_w, max_texture_h);
		return errmsg;
	}
	while (multx > 1 && (w_w * multx > max_texture_w || w_h * multx > max_texture_h))
		multx--;

	required_sl_texture_w = w_w;
	required_sl_texture_h = w_h;
	if (currprefs.gfx_filter_scanlines > 0 && (max_texture_w < w_w || max_texture_h < w_h)) {
		gui_message (L"%s: %d * %d or bigger texture support required for scanlines (max is only %d * %d)\n",
			D3DHEAD, L"Scanlines disabled.",
			required_sl_texture_w, required_sl_texture_h, max_texture_w, max_texture_h);
		changed_prefs.gfx_filter_scanlines = currprefs.gfx_filter_scanlines = 0;
	}

	switch (depth)
	{
		case 32:
		default:
			tformat = D3DFMT_X8R8G8B8;
		break;
		case 15:
			tformat = D3DFMT_X1R5G5B5;
		break;
		case 16:
			tformat = D3DFMT_R5G6B5;
		break;
	}
	window_w = w_w;
	window_h = w_h;

	changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen | STATUSLINE_TARGET;

	if (!restoredeviceobjects ()) {
		D3D_free ();
		_stprintf (errmsg, L"%s: initialization failed.", D3DHEAD);
		return errmsg;
	}

	d3d_enabled = 1;

	return 0;
}

static int isd3d (void)
{
	if (devicelost || !d3ddev || !d3d_enabled)
		return 0;
	return 1;
}

static HRESULT reset (void)
{
	if (d3dex)
		return d3ddevex->ResetEx (&dpp, dpp.Windowed ? NULL : &modeex);
	else
		return d3ddev->Reset (&dpp);
}

int D3D_needreset (void)
{
	HRESULT hr;

	if (!devicelost)
		return -1;
	if (d3dex)
		hr = d3ddevex->CheckDeviceState (d3dhwnd);
	else
		hr = d3ddev->TestCooperativeLevel ();
	if (hr == S_PRESENT_OCCLUDED)
		return 0;
	if (hr == D3DERR_DEVICENOTRESET) {
		write_log (L"%s: DEVICENOTRESET\n", D3DHEAD);
		devicelost = 2;
		invalidatedeviceobjects ();
		hr = reset ();
		if (FAILED (hr)) {
			write_log (L"%s: Reset failed %s\n", D3DHEAD, D3D_ErrorString (hr));
			resetcount++;
			if (resetcount > 2 || hr == D3DERR_DEVICEHUNG) {
				changed_prefs.gfx_api = 0;
				write_log (L"%s: Too many failed resets, disabling Direct3D mode\n", D3DHEAD);
			}
			return 1;
		}
		devicelost = 0;
		write_log (L"%s: Reset succeeded\n", D3DHEAD);
		restoredeviceobjects ();
		return -1;
	} else if (hr == D3DERR_DEVICELOST) {
		invalidatedeviceobjects ();
		return 0;
	} else if (SUCCEEDED (hr)) {
		devicelost = 0;
		invalidatedeviceobjects ();
		hr = reset ();
		if (FAILED (hr))
			write_log (L"%s: Reset failed %s\n", D3DHEAD, D3D_ErrorString (hr));
		restoredeviceobjects ();
		return -1;
	}
	write_log (L"%s: TestCooperativeLevel %s\n", D3DHEAD, D3D_ErrorString (hr));
	return 0;
}

void D3D_clear (void)
{
	int i;
	HRESULT hr;

	if (!isd3d ())
		return;
	for (i = 0; i < 2; i++) {
		hr = d3ddev->Clear (0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, d3ddebug ? 0x80 : 0x00), 0, 0);
		hr = d3ddev->Present (NULL, NULL, NULL, NULL);
	}
}

static void D3D_render22 (void)
{
	HRESULT hr;
	LPDIRECT3DTEXTURE9 srctex = texture;
	UINT uPasses, uPass;

	if (!isd3d ())
		return;

	hr = d3ddev->Clear (0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, d3ddebug ? 0x80 : 0, 0), 0, 0);

	if (FAILED (hr = d3ddev->BeginScene ())) {
		write_log (L"%s: BeginScene: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	if (shaderon > 0 && postEffect) {
		if (psActive) {
			LPDIRECT3DSURFACE9 lpRenderTarget;
			LPDIRECT3DSURFACE9 lpNewRenderTarget;
			LPDIRECT3DTEXTURE9 lpWorkTexture;

			settransform ();
			if (!psEffect_SetTextures (texture, lpWorkTexture1, lpWorkTexture2, lpHq2xLookupTexture))
				return;
			if (psPreProcess) {
				if (!psEffect_SetMatrices (&m_matPreProj, &m_matPreView, &m_matPreWorld))
					return;

				if (FAILED (hr = d3ddev->GetRenderTarget (0, &lpRenderTarget)))
					write_log (L"%s: GetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
				lpWorkTexture = lpWorkTexture1;
				lpNewRenderTarget = NULL;
	pass2:
				if (FAILED (hr = lpWorkTexture->GetSurfaceLevel (0, &lpNewRenderTarget)))
					write_log (L"%s: GetSurfaceLevel: %s\n", D3DHEAD, D3D_ErrorString (hr));
				if (FAILED (hr = d3ddev->SetRenderTarget (0, lpNewRenderTarget)))
					write_log (L"%s: SetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));

				uPasses = 0;
				if (psEffect_Begin (pEffect, (lpWorkTexture == lpWorkTexture1) ? psEffect_PreProcess1 : psEffect_PreProcess2, &uPasses)) {
					for (uPass = 0; uPass < uPasses; uPass++) {
						if (psEffect_BeginPass (pEffect, uPass)) {
							if (FAILED (hr = d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 4, 2))) {
								write_log (L"%s: Effect DrawPrimitive failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
							}
							psEffect_EndPass (pEffect);
						}
					}
					psEffect_End (pEffect);
				}
				if (FAILED (hr = d3ddev->SetRenderTarget (0, lpRenderTarget)))
					write_log (L"%s: Effect RenderTarget reset failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
				lpNewRenderTarget->Release ();
				lpNewRenderTarget = NULL;
				if (psEffect_hasPreProcess2 () && lpWorkTexture == lpWorkTexture1) {
					lpWorkTexture = lpWorkTexture2;
					goto pass2;
				}
				lpRenderTarget->Release ();
				lpRenderTarget = NULL;
			}
			psEffect_SetMatrices (&m_matProj2, &m_matView2, &m_matWorld2);

#if TWOPASS
			if (FAILED (hr = d3ddev->GetRenderTarget (0, &lpRenderTarget)))
				write_log (L"%s: GetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = lpTempTexture->GetSurfaceLevel (0, &lpNewRenderTarget)))
				write_log (L"%s: GetSurfaceLevel: %s\n", D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = d3ddev->SetRenderTarget (0, lpNewRenderTarget)))
				write_log (L"%s: SetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
#endif
			hr = d3ddev->Clear (0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, d3ddebug ? 0x80 : 0), 0, 0);

			uPasses = 0;
			if (psEffect_Begin (pEffect, psEffect_Combine, &uPasses)) {
				for (uPass = 0; uPass < uPasses; uPass++) {
					if (!psEffect_BeginPass (pEffect, uPass))
						return;
					if (FAILED (hr = d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2)))
						write_log (L"%s: Effect2 DrawPrimitive failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
					psEffect_EndPass (pEffect);
				}
				psEffect_End (pEffect);
			}
#if TWOPASS
			if (FAILED (hr = d3ddev->SetRenderTarget (0, lpRenderTarget)))
				write_log (L"%s: SetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
			lpNewRenderTarget->Release ();
			lpRenderTarget->Release ();
#endif
			srctex = lpTempTexture;

		}

	}
#if TWOPASS
	if (shaderon > 0 && postEffect) {

		if (masktexture) {
			if (FAILED (hr = postEffect->SetTechnique (postTechnique)))
				write_log (L"%s: SetTechnique(postTechnique) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = postEffect->SetTexture (postMaskTextureHandle, masktexture)))
				write_log (L"%s: SetTexture(masktexture) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		} else if (sltexture) {
			if (FAILED (hr = postEffect->SetTechnique (postTechniqueAlpha)))
				write_log (L"%s: SetTechnique(postTechniqueAlpha) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
			if (FAILED (hr = postEffect->SetTexture (postMaskTextureHandle, sltexture)))
				write_log (L"%s: SetTexture(sltexture) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		} else {
			if (FAILED (hr = postEffect->SetTechnique (postTechniquePlain)))
				write_log (L"%s: SetTechnique(postTechniquePlain) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		}
		hr = postEffect->SetInt (postFilterMode, currprefs.gfx_filter_bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT);

		if (FAILED (hr = postEffect->SetTexture (postSourceTextureHandle, srctex)))
			write_log (L"%s: SetTexture(srctex) failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		setupscenecoords ();
		hr = postEffect->SetMatrix (postMatrixSource, &postproj);
		hr = postEffect->SetVector (postMaskMult, &maskmult);
		hr = postEffect->SetVector (postMaskShift, &maskshift);

		uPasses = 0;
		if (psEffect_Begin (postEffect, psEffect_None, &uPasses)) {
			for (uPass = 0; uPass < uPasses; uPass++) {
				if (psEffect_BeginPass (postEffect, uPass)) {
					if (FAILED (hr = d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2)))
						write_log (L"%s: Post DrawPrimitive failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
					psEffect_EndPass (postEffect);
				}
			}
			psEffect_End (postEffect);
		}

	} else {

		// non-shader version
		setupscenecoords ();
		hr = d3ddev->SetTransform (D3DTS_PROJECTION, &m_matProj);
		hr = d3ddev->SetTransform (D3DTS_VIEW, &m_matView);
		hr = d3ddev->SetTransform (D3DTS_WORLD, &m_matWorld);
		hr = d3ddev->SetTexture (0, srctex);
		hr = d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2);
		int bl = currprefs.gfx_filter_bilinear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
		hr = d3ddev->SetSamplerState (0, D3DSAMP_MINFILTER, bl);
		hr = d3ddev->SetSamplerState (0, D3DSAMP_MAGFILTER, bl);

		if (sprite && sltexture) {
			D3DXVECTOR3 v;
			sprite->Begin (D3DXSPRITE_ALPHABLEND);
			v.x = v.y = v.z = 0;
			sprite->Draw (sltexture, NULL, NULL, &v, 0xffffffff);
			sprite->End ();
		}
	}

	if (sprite && ((ledtexture) || (mask2texture) || (cursorsurfaced3d && cursor_v))) {
		D3DXVECTOR3 v;
		sprite->Begin (D3DXSPRITE_ALPHABLEND);
		if (cursorsurfaced3d && cursor_v) {
			D3DXMATRIXA16 t;

			MatrixScaling (&t, ((float)(window_w) / (tout_w + 2 * cursor_offset_x)), ((float)(window_h) / (tout_h + 2 * cursor_offset_y)), 0);
			v.x = cursor_x + cursor_offset_x;
			v.y = cursor_y + cursor_offset_y;
			v.z = 0;
			sprite->SetTransform (&t);
			sprite->Draw (cursorsurfaced3d, NULL, NULL, &v, 0xffffffff);
			MatrixScaling (&t, 1, 1, 0);
			sprite->Flush ();
			sprite->SetTransform (&t);
		}
		if (mask2texture) {
			D3DXMATRIXA16 t;
			RECT r;
			float srcw = mask2texture_w;
			float srch = mask2texture_h;
			float aspectsrc = srcw / srch;
			float aspectdst = (float)window_w / window_h;
			float w, h;

			w = mask2texture_multx;
			h = mask2texture_multy;
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
			if (currprefs.gfx_filteroverlay_pos.x == -1)
				v.x = (window_w - (mask2texture_w * w)) / 2;
			else if (currprefs.gfx_filteroverlay_pos.x > -24000)
				v.x = currprefs.gfx_filteroverlay_pos.x;
			else
				v.x = (window_w - (mask2texture_w * w)) / 2 + (-currprefs.gfx_filteroverlay_pos.x - 30100) * window_w / 100.0;

			v.y = 0;
			if (currprefs.gfx_filteroverlay_pos.y == -1)
				v.y = (window_h - (mask2texture_h * h)) / 2;
			else if (currprefs.gfx_filteroverlay_pos.y > -24000)
				v.y = currprefs.gfx_filteroverlay_pos.y;
			else
				v.y = (window_h - (mask2texture_h * h)) / 2 + (-currprefs.gfx_filteroverlay_pos.y - 30100) * window_h / 100.0;

			v.x /= w;
			v.y /= h;
			v.x = v.y = 0;
			v.z = 0;
			v.x += mask2texture_offsetw / w;

			r.left = 0;
			r.top = 0;
			r.right = mask2texture_w;
			r.bottom = mask2texture_h;

			sprite->SetTransform (&t);
			sprite->Draw (mask2texture, &r, NULL, &v, 0xffffffff);
			sprite->Flush ();
			MatrixScaling (&t, 1, 1, 0);
			sprite->SetTransform (&t);

			if (mask2texture_offsetw > 0) {
				v.x = 0;
				v.y = 0;
				r.left = 0;
				r.top = 0;
				r.right = mask2texture_offsetw + 1;
				r.bottom = window_h;
				sprite->Draw (blanktexture, &r, NULL, &v, 0xffffffff);
				if (window_w > mask2texture_offsetw + mask2texture_ww) {
					v.x = mask2texture_offsetw + mask2texture_ww;
					v.y = 0;
					r.left = 0;
					r.top = 0;
					r.right = window_w - (mask2texture_offsetw + mask2texture_ww) + 1;
					r.bottom = window_h;
					sprite->Draw (blanktexture, &r, NULL, &v, 0xffffffff);
				}
			}

		}
		if (ledtexture && (((currprefs.leds_on_screen & STATUSLINE_RTG) && WIN32GFX_IsPicassoScreen ()) || ((currprefs.leds_on_screen & STATUSLINE_CHIPSET) && !WIN32GFX_IsPicassoScreen ()))) {
			v.x = 0;
			v.y = window_h - TD_TOTAL_HEIGHT;
			v.z = 0;
			sprite->Draw (ledtexture, NULL, NULL, &v, 0xffffffff);
		}
		sprite->End ();
	}
#endif

	hr = d3ddev->EndScene ();
	if (FAILED (hr))
		write_log (L"%s: EndScene() %s\n", D3DHEAD, D3D_ErrorString (hr));
	hr = d3ddev->Present (NULL, NULL, NULL, NULL);
	if (FAILED (hr)) {
		write_log (L"%s: Present() %s\n", D3DHEAD, D3D_ErrorString (hr));
		if (hr == D3DERR_DEVICELOST) {
			devicelost = 1;
		}
	}
}

static void D3D_render2 (void)
{   
	D3D_render22 ();
}

void D3D_setcursor (int x, int y, int visible)
{
	cursor_x = x;
	cursor_y = y;
	cursor_v = visible;
}

void D3D_unlocktexture (void)
{
	HRESULT hr;

	if (!isd3d ())
		return;
	if (currprefs.leds_on_screen & (STATUSLINE_CHIPSET | STATUSLINE_RTG))
		updateleds ();

	if (locked)
		hr = texture->UnlockRect (0);
	locked = 0;
	fulllocked = 0;
}

void D3D_flushtexture (int miny, int maxy)
{
	if (fulllocked)
		return;
	if (miny >= 0 && maxy >= 0) {
		RECT r;
		maxy++;
		r.left = 0;
		r.right = tin_w;
		r.top = miny <= 0 ? 0 : miny;
		r.bottom = maxy <= tin_h ? maxy : tin_h;
		if (r.top <= r.bottom) {
			HRESULT hr = texture->AddDirtyRect (&r);
			if (FAILED (hr))
				write_log (L"%s: AddDirtyRect(): %s\n", D3DHEAD, D3D_ErrorString (hr));
			//write_log (L"%d %d\n", r.top, r.bottom);
		}
	}
}

uae_u8 *D3D_locktexture (int *pitch, int fullupdate)
{
	D3DLOCKED_RECT lock;
	HRESULT hr;

	if (D3D_needreset () > 0)
		return NULL;
	if (!isd3d ())
		return NULL;

	lock.pBits = NULL;
	lock.Pitch = 0;
	hr = texture->LockRect (0, &lock, NULL, fullupdate ? D3DLOCK_DISCARD : D3DLOCK_NO_DIRTY_UPDATE);
	if (FAILED (hr)) {
		write_log (L"%s: LockRect failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return NULL;
	}
	if (lock.pBits == NULL || lock.Pitch == 0) {
		write_log (L"%s: LockRect returned NULL texture\n", D3DHEAD);
		D3D_unlocktexture ();
		return NULL;
	}
	locked = 1;
	fulllocked = fullupdate;
	*pitch = lock.Pitch;
	return (uae_u8*)lock.pBits;
}

void D3D_flip (void)
{
	static int frameskip;
	if (!isd3d ())
		return;
	if (currprefs.turbo_emulation && isfullscreen () > 0 && frameskip-- > 0)
		return;
	frameskip = 50;
	D3D_render2 ();
	if (vsync2 && !currprefs.turbo_emulation)
		D3D_render2 ();
}

void D3D_refresh (void)
{
	if (!isd3d ())
		return;
	D3D_render2 ();
	D3D_render2 ();
	createscanlines (0);
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

void D3D_guimode (int guion)
{
	HRESULT hr;
	if (!isd3d ())
		return;
	hr = d3ddev->SetDialogBoxMode (guion);
	if (FAILED (hr))
		write_log (L"%s: SetDialogBoxMode %s\n", D3DHEAD, D3D_ErrorString (hr));
	guimode = guion;
}

HDC D3D_getDC (HDC hdc)
{
	static LPDIRECT3DSURFACE9 bb;
	HRESULT hr;

	if (!isd3d ())
		return 0;
	if (!hdc) {
		hr = d3ddev->GetBackBuffer (0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
		if (FAILED (hr)) {
			write_log (L"%s: GetBackBuffer() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		hr = bb->GetDC (&hdc);
		if (SUCCEEDED (hr))
			return hdc;
		write_log (L"%s: GetDC() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
	}
	if (hdc)
		bb->ReleaseDC (hdc);
	bb->Release ();
	bb = NULL;
	return 0;
}

int D3D_isenabled (void)
{
	return d3d_enabled;
}

#endif
