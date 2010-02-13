#undef CINTERFACE

#include <windows.h>
#include "sysconfig.h"
#include "sysdeps.h"

#if defined (D3D) && defined (GFXFILTER)

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
extern int D3DEX, d3ddebug;

#include <d3d9.h>
#include <d3dx9.h>

#include "direct3d.h"

static TCHAR *D3DHEAD = L"-";
static int tex_pow2, tex_square, tex_dynamic;
static int psEnabled, psActive, psPreProcess;

static D3DFORMAT tformat;
static int d3d_enabled, d3d_ex;
static IDirect3D9 *d3d;
static IDirect3D9Ex *d3dex;
static D3DPRESENT_PARAMETERS dpp;
static D3DDISPLAYMODEEX modeex;
static IDirect3DDevice9 *d3ddev;
static IDirect3DDevice9Ex *d3ddevex;
static D3DSURFACE_DESC dsdbb;
static LPDIRECT3DTEXTURE9 texture, sltexture, ledtexture, masktexture;
static LPDIRECT3DTEXTURE9 lpWorkTexture1, lpWorkTexture2;
static LPDIRECT3DVOLUMETEXTURE9 lpHq2xLookupTexture;
static IDirect3DVertexBuffer9 *vertexBuffer;
static ID3DXSprite *sprite;
static HWND d3dhwnd;
static int devicelost;

static D3DXMATRIX m_matProj, m_matProj2;
static D3DXMATRIX m_matWorld, m_matWorld2;
static D3DXMATRIX m_matView, m_matView2;
static D3DXMATRIX m_matPreProj;
static D3DXMATRIX m_matPreView;
static D3DXMATRIX m_matPreWorld;

static int ledwidth, ledheight;
static int twidth, theight, max_texture_w, max_texture_h;
static int tin_w, tin_h, window_h, window_w;
static int t_depth, mult;
static int required_sl_texture_w, required_sl_texture_h;
static int vsync2, guimode;
static int needclear;
static int resetcount;

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
		IDirectDraw7_RestoreDisplayMode (ddraw);
	hr = IDirectDraw7_SetCooperativeLevel (ddraw, d3dhwnd, DDSCL_NORMAL);
	if (FAILED (hr)) {
		write_log (L"IDirectDraw7_SetCooperativeLevel CLEAR: %s\n", DXError (hr));
	}
	IDirectDraw7_Release (ddraw);
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
	hr = IDirectDraw7_SetCooperativeLevel (ddraw, d3dhwnd, DDSCL_ALLOWREBOOT | DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
	if (FAILED (hr)) {
		write_log (L"IDirectDraw7_SetCooperativeLevel SET: %s\n", DXError (hr));
		ddraw_fs_hack_free ();
		return 0;
	}
	hr = IDirectDraw7_SetDisplayMode (ddraw, dpp.BackBufferWidth, dpp.BackBufferHeight, t_depth, dpp.FullScreen_RefreshRateInHz, 0);
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

static D3DXMATRIX* MatrixOrthoOffCenterLH (D3DXMATRIX *pOut, float l, float r, float b, float t, float zn, float zf)
{
	pOut->_11=2.0f/r; pOut->_12=0.0f;   pOut->_13=0.0f;  pOut->_14=0.0f;
	pOut->_21=0.0f;   pOut->_22=2.0f/t; pOut->_23=0.0f;  pOut->_24=0.0f;
	pOut->_31=0.0f;   pOut->_32=0.0f;   pOut->_33=1.0f;  pOut->_34=0.0f;
	pOut->_41=-1.0f;  pOut->_42=-1.0f;  pOut->_43=0.0f;  pOut->_44=1.0f;
	return pOut;
}

static D3DXMATRIX* MatrixScaling (D3DXMATRIX *pOut, float sx, float sy, float sz)
{
	pOut->_11=sx;     pOut->_12=0.0f;   pOut->_13=0.0f;  pOut->_14=0.0f;
	pOut->_21=0.0f;   pOut->_22=sy;     pOut->_23=0.0f;  pOut->_24=0.0f;
	pOut->_31=0.0f;   pOut->_32=0.0f;   pOut->_33=sz;    pOut->_34=0.0f;
	pOut->_41=0.0f;   pOut->_42=0.0f;   pOut->_43=0.0f;  pOut->_44=1.0f;
	return pOut;
}

static D3DXMATRIX* MatrixTranslation (D3DXMATRIX *pOut, float tx, float ty, float tz)
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

	if (Errors)
		s = (TCHAR*)Errors->GetBufferPointer ();
	_tcscpy (buffer, D3D_ErrorString (hr));
	if (s) {
		_tcscat (buffer, L" ");
		_tcscat (buffer, s);
	}
	return buffer;
}

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
enum psEffect_Pass { psEffect_PreProcess1, psEffect_PreProcess2, psEffect_Combine };

static int psEffect_ParseParameters (LPD3DXEFFECTCOMPILER EffectCompiler)
{
	HRESULT hr = S_OK;
	// Look at parameters for semantics and annotations that we know how to interpret
	D3DXPARAMETER_DESC ParamDesc;
	D3DXPARAMETER_DESC AnnotDesc;
	D3DXHANDLE hParam;
	D3DXHANDLE hAnnot;
	LPDIRECT3DBASETEXTURE9 pTex = NULL;
	UINT iParam, iAnnot;

	if(pEffect == NULL)
		return 0;

	for(iParam = 0; iParam < EffectDesc.Parameters; iParam++) {
		LPCSTR pstrName = NULL;
		LPCSTR pstrFunction = NULL;
		LPCSTR pstrTarget = NULL;
		LPCSTR pstrTextureType = NULL;
		INT Width = D3DX_DEFAULT;
		INT Height= D3DX_DEFAULT;
		INT Depth = D3DX_DEFAULT;

		hParam = pEffect->GetParameter (NULL, iParam);
		pEffect->GetParameterDesc (hParam, &ParamDesc);

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
					pEffect->GetFloat(hParam, &m_scale);
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
					pEffect->GetString(hParam, &pstrTechnique);
					m_CombineTechniqueEffectHandle = pEffect->GetTechniqueByName(pstrTechnique);
				}
				else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE") == 0) {
					pEffect->GetString(hParam, &pstrTechnique);
					m_PreprocessTechnique1EffectHandle = pEffect->GetTechniqueByName(pstrTechnique);
				}
				else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE1") == 0) {
					pEffect->GetString(hParam, &pstrTechnique);
					m_PreprocessTechnique2EffectHandle = pEffect->GetTechniqueByName(pstrTechnique);
				}
				else if(strcmpi(ParamDesc.Semantic, "NAME") == 0)
					pEffect->GetString(hParam, &m_strName);
			}
		}

		for(iAnnot = 0; iAnnot < ParamDesc.Annotations; iAnnot++) {
			hAnnot = pEffect->GetAnnotation (hParam, iAnnot);
			pEffect->GetParameterDesc(hAnnot, &AnnotDesc);
			if(strcmpi(AnnotDesc.Name, "name") == 0)
				pEffect->GetString(hAnnot, &pstrName);
			else if(strcmpi(AnnotDesc.Name, "function") == 0)
				pEffect->GetString(hAnnot, &pstrFunction);
			else if(strcmpi(AnnotDesc.Name, "target") == 0)
				pEffect->GetString(hAnnot, &pstrTarget);
			else if(strcmpi(AnnotDesc.Name, "width") == 0)
				pEffect->GetInt(hAnnot, &Width);
			else if(strcmpi(AnnotDesc.Name, "height") == 0)
				pEffect->GetInt(hAnnot, &Height);
			else if(strcmpi(AnnotDesc.Name, "depth") == 0)
				pEffect->GetInt(hAnnot, &Depth);
			else if(strcmpi(AnnotDesc.Name, "type") == 0)
				pEffect->GetString(hAnnot, &pstrTextureType);
		}

		if(pstrFunction != NULL) {
			LPD3DXBUFFER pTextureShader = NULL;
			LPD3DXBUFFER lpErrors = 0;

			if(pstrTarget == NULL || strcmp(pstrTarget,"tx_1_1"))
				pstrTarget = "tx_1_0";

			if(SUCCEEDED(hr = EffectCompiler->CompileShader(
				pstrFunction, pstrTarget, 0, &pTextureShader, &lpErrors, NULL))) {
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
					pEffect->SetTexture(pEffect->GetParameter(NULL, iParam), pTex);
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

static int d3d_yesno = 0;

int D3D_goodenough (void)
{
	static int d3d_good;
	LPDIRECT3D9 d3dx;
	D3DCAPS9 d3dCaps;

	if (d3d_yesno > 0 || d3d_good > 0)
		return 1;
	if (d3d_good < 0)
		return 0;
	d3d_good = -1;
	d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
	if (d3dx != NULL) {
		if (SUCCEEDED (IDirect3D9_GetDeviceCaps (d3dx, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
			if(d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2,0))
				d3d_good = 1;
		}
		IDirect3D9_Release (d3dx);
	}
	return d3d_good > 0 ? 1 : 0;
}

int D3D_canshaders (void)
{
	HMODULE h;
	LPDIRECT3D9 d3dx;
	D3DCAPS9 d3dCaps;

	if (d3d_yesno < 0)
		return 0;
	if (d3d_yesno > 0)
		return 1;
	d3d_yesno = -1;
	h = LoadLibrary (L"d3dx9_42.dll");
	if (h != NULL) {
		FreeLibrary (h);
		d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
		if (d3dx != NULL) {
			if (SUCCEEDED (IDirect3D9_GetDeviceCaps (d3dx, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
				if(d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
					write_log (L"Direct3D: Pixel shader 2.0+ support detected, shader filters enabled.\n");
					d3d_yesno = 1;
				}
			}
			IDirect3D9_Release (d3dx);
		}
	}
	return d3d_yesno > 0 ? 1 : 0;
}

static int psEffect_LoadEffect (const TCHAR *shaderfile)
{
	int ret = 0;
	LPD3DXEFFECTCOMPILER EffectCompiler = NULL;
	LPD3DXBUFFER Errors = NULL;
	LPD3DXBUFFER BufferEffect = NULL;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH];
	static int first;

	if (!D3D_canshaders ()) {
		if (!first)
			gui_message (L"Installed DirectX is too old\nD3D shaders disabled.");
		first = 1;
		return 0;
	}
	_stprintf (tmp, L"%s%sfiltershaders\\direct3d\\%s", start_path_exe, WIN32_PLUGINDIR, shaderfile);
	hr = D3DXCreateEffectCompilerFromFile (tmp, NULL, NULL, 0, &EffectCompiler, &Errors);
	if (FAILED (hr)) {
		write_log (L"%s: D3DXCreateEffectCompilerFromFile failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	hr = EffectCompiler->CompileEffect (0, &BufferEffect, &Errors);
	if (FAILED (hr)) {
		write_log (L"%s: CompileEffect failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	hr = D3DXCreateEffect (d3ddev,
		BufferEffect->GetBufferPointer (),
		BufferEffect->GetBufferSize (),
		NULL, NULL,
		0,
		NULL, &pEffect, &Errors);
	if (FAILED (hr)) {
		write_log (L"%s: D3DXCreateEffect failed: %s\n", D3DHEAD, D3DX_ErrorString (hr, Errors));
		goto end;
	}
	pEffect->GetDesc (&EffectDesc);
	if (!psEffect_ParseParameters (EffectCompiler))
		goto end;
	ret = 1;
end:
	if (Errors)
		Errors->Release ();
	if (BufferEffect)
		BufferEffect->Release ();
	if (EffectCompiler)
		EffectCompiler->Release ();

	psActive = FALSE;
	psPreProcess = FALSE;
	if (ret) {
		psActive = TRUE;
		if (psEffect_hasPreProcess ())
			psPreProcess = TRUE;
		write_log (L"%s: pixelshader filter '%s' enabled, preproc=%d\n", D3DHEAD, tmp, psPreProcess);
	} else {
		write_log (L"%s: pixelshader filter '%s' failed to initialize\n", D3DHEAD, tmp);
	}
	return ret;
}

static int psEffect_SetMatrices (D3DXMATRIX *matProj, D3DXMATRIX *matView, D3DXMATRIX *matWorld)
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
		D3DXMATRIX matWorldView;
		D3DXMatrixMultiply (&matWorldView, matWorld, matView);
		hr = pEffect->SetMatrix (m_MatWorldViewEffectHandle, &matWorldView);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matWorldView %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatViewProjEffectHandle) {
		D3DXMATRIX matViewProj;
		D3DXMatrixMultiply (&matViewProj, matView, matProj);
		hr = pEffect->SetMatrix (m_MatViewProjEffectHandle, &matViewProj);
		if (FAILED (hr)) {
			write_log (L"%s: Create:SetMatrix:matViewProj %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if (m_MatWorldViewProjEffectHandle) {
		D3DXMATRIX tmp, matWorldViewProj;
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
	hr = pEffect->SetTexture (m_SourceTextureEffectHandle, (LPDIRECT3DBASETEXTURE9)lpSource);
	if (FAILED (hr)) {
		write_log (L"%s: SetTextures:lpSource %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	if(m_WorkingTexture1EffectHandle) {
		hr = pEffect->SetTexture (m_WorkingTexture1EffectHandle, (LPDIRECT3DBASETEXTURE9)lpWorking1);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:lpWorking1 %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if(m_WorkingTexture2EffectHandle) {
		hr = pEffect->SetTexture (m_WorkingTexture2EffectHandle, (LPDIRECT3DBASETEXTURE9)lpWorking2);
		if (FAILED (hr)) {
			write_log (L"%s: SetTextures:lpWorking2 %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
	}
	if(m_Hq2xLookupTextureHandle) {
		hr = pEffect->SetTexture (m_Hq2xLookupTextureHandle, (LPDIRECT3DBASETEXTURE9)lpHq2xLookupTexture);
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
static int psEffect_Begin (enum psEffect_Pass pass, UINT *pPasses)
{
	HRESULT hr;
	switch (pass) {
	case psEffect_PreProcess1:
		hr = pEffect->SetTechnique (m_PreprocessTechnique1EffectHandle);
		break;
	case psEffect_PreProcess2:
		hr = pEffect->SetTechnique (m_PreprocessTechnique2EffectHandle);
		break;
	case psEffect_Combine:
		hr = pEffect->SetTechnique (m_CombineTechniqueEffectHandle);
		break;
	}
	if(FAILED(hr)) {
		write_log (L"%s: SetTechnique: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	hr = pEffect->Begin (pPasses, D3DXFX_DONOTSAVESTATE|D3DXFX_DONOTSAVESHADERSTATE);
	if(FAILED(hr)) {
		write_log (L"%s: Begin: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_BeginPass (UINT Pass)
{
	HRESULT hr;

	hr = pEffect->BeginPass (Pass);
	if (FAILED (hr)) {
		write_log (L"%s: BeginPass: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_EndPass (void)
{
	HRESULT hr;

	hr = pEffect->EndPass ();
	if (FAILED (hr)) {
		write_log (L"%s: EndPass: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}
static int psEffect_End (void)
{
	HRESULT hr;

	hr = pEffect->End ();
	if (FAILED (hr)) {
		write_log (L"%s: End: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return 0;
	}
	return 1;
}

static LPDIRECT3DTEXTURE9 createtext (int *ww, int *hh, D3DFORMAT format)
{
	LPDIRECT3DTEXTURE9 t;
	D3DLOCKED_RECT locked;
	HRESULT hr;
	int w, h;

	w = *ww;
	h = *hh;
	if (tex_pow2) {
		if (w < 256)
			w = 256;
		else if (w < 512)
			w = 512;
		else if (w < 1024)
			w = 1024;
		else if (w < 2048)
			w = 2048;
		else if (w < 4096)
			w = 4096;
		else
			w = 8192;
		if (h < 256)
			h = 256;
		else if (h < 512)
			h = 512;
		else if (h < 1024)
			h = 1024;
		else if (h < 2048)
			h = 2048;
		else if (h < 4096)
			h = 4096;
		else
			h = 8192;
	}
	if (tex_square) {
		if (w > h)
			h = w;
		else
			w = h;
	}

	if (tex_dynamic) {
		hr = IDirect3DDevice9_CreateTexture (d3ddev, w, h, 1, D3DUSAGE_DYNAMIC, format,
			D3DPOOL_DEFAULT, &t, NULL);
		if (FAILED (hr))
			write_log (L"%s: CreateTexture() D3DUSAGE_DYNAMIC failed: %s (%d*%d %08x)\n",
			D3DHEAD,
			D3D_ErrorString (hr), w, h, format);
	}
	if (!tex_dynamic || (tex_dynamic && FAILED (hr))) {
		hr = IDirect3DDevice9_CreateTexture (d3ddev, w, h, 1, 0, format,
			D3DPOOL_DEFAULT, &t, NULL);
	}
	if (FAILED (hr)) {
		write_log (L"%s: CreateTexture() failed: %s (%d*%d %08x)\n",
			D3DHEAD, D3D_ErrorString (hr), w, h, format);
		return 0;
	}
	*ww = w;
	*hh = h;
	hr = IDirect3DTexture9_LockRect (t, 0, &locked, NULL, 0);
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
		IDirect3DTexture9_UnlockRect (t, 0);
	}
	return t;
}

static int createtexture (int w, int h)
{
	HRESULT hr;
	int ww = w;
	int hh = h;

	texture = createtext (&ww, &hh, tformat);
	if (!texture)
		return 0;
	twidth = ww;
	theight = hh;
	write_log (L"%s: %d*%d texture allocated, bits per pixel %d\n", D3DHEAD, ww, hh, t_depth);
	if (psActive) {
		D3DLOCKED_BOX lockedBox;
		if (FAILED (hr = d3ddev->CreateTexture (ww, hh, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture1, NULL))) {
				write_log (L"%s: Failed to create working texture1: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return 0;
		}
		if (FAILED (hr = d3ddev->CreateTexture (ww, hh, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture2, NULL))) {
				write_log (L"%s: Failed to create working texture2: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return 0;
		}
		if (FAILED (hr = d3ddev->CreateVolumeTexture (256, 16, 256, 1,
			D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpHq2xLookupTexture, NULL))) {
				write_log (L"%s: Failed to create volume texture: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return 0;
		}
		if (FAILED (hr = lpHq2xLookupTexture->LockBox (0, &lockedBox, NULL, 0))) {
			write_log (L"%s: Failed to lock box of volume texture: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
		}
		//BuildHq2xLookupTexture(tin_w / mult, tin_w / mult, window_w, window_h, (unsigned char*)lockedBox.pBits);
		BuildHq2xLookupTexture(window_w, window_h, tin_w / mult, tin_w / mult,  (unsigned char*)lockedBox.pBits);
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
	hr = IDirect3DTexture9_LockRect (ledtexture, 0, &locked, NULL, 0);
	if (FAILED (hr)) {
		write_log (L"%d: SL LockRect failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	for (y = 0; y < TD_TOTAL_HEIGHT; y++) {
		uae_u8 *buf = (uae_u8*)locked.pBits + y * locked.Pitch;
		draw_status_line_single (buf, 32 / 8, y, ledwidth, rc, gc, bc, a);
	}
	IDirect3DTexture9_UnlockRect (ledtexture, 0);
}

static int createledtexture (void)
{
	ledwidth = window_w;
	ledheight = TD_TOTAL_HEIGHT;
	ledtexture = createtext (&ledwidth, &ledheight, D3DFMT_A8R8G8B8);
	if (!ledtexture)
		return 0;
	return 1;
}

static int createsltexture (void)
{
	int ww = required_sl_texture_w;
	int hh = required_sl_texture_h;

	sltexture = createtext (&ww, &hh, t_depth < 32 ? D3DFMT_A4R4G4B4 : D3DFMT_A8R8G8B8);
	if (!sltexture)
		return 0;
	required_sl_texture_w = ww;
	required_sl_texture_h = hh;
	write_log (L"%s: SL %d*%d texture allocated\n", D3DHEAD, ww, hh);
	return 1;
}

static int createmasktexture (TCHAR *filename)
{
	int ww = tin_w;
	int hh = tin_h;
	struct zfile *zf;
	int size;
	uae_u8 *buf;
	D3DSURFACE_DESC tmpdesc, maskdesc;
	LPDIRECT3DTEXTURE9 tx;
	HRESULT hr;
	D3DLOCKED_RECT lock, slock;
	TCHAR tmp[MAX_DPATH];

	tx = NULL;
	_stprintf (tmp, L"%s%soverlays\\%s", start_path_exe, WIN32_PLUGINDIR, filename);
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
		 D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT_NONPOW2, D3DX_DEFAULT, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8,
		 D3DPOOL_DEFAULT, D3DX_FILTER_NONE, D3DX_FILTER_NONE, 0, NULL, NULL,
		 &tx);
	xfree (buf);
	if (FAILED (hr)) {
		write_log (L"%s: temp mask texture load failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		goto end;
	}
	hr = tx->GetLevelDesc (0, &tmpdesc);
	if (FAILED (hr)) {
		write_log (L"%s: temp mask texture GetLevelDesc() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		goto end;
	}
	masktexture = createtext (&ww, &hh, D3DFMT_X8R8G8B8);
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
					if (sx >= tmpdesc.Width)
						sx = 0;
				}
				sy++;
				if (sy >= tmpdesc.Height)
					sy = 0;
			}
			tx->UnlockRect (0);
		}
		masktexture->UnlockRect (0);
	}
	tx->Release ();
	write_log (L"%s: mask %d*%d ('%s') texture allocated\n", D3DHEAD, ww, hh, filename);

	return 1;
end:
	if (masktexture)
		masktexture->Release ();
	masktexture = NULL;
	if (tx)
		tx->Release ();
	return 0;
}


static void setupscenescaled (void)
{
	HRESULT hr;
	int v;

	// Set up the texture
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_COLOROP,   D3DTOP_MODULATE);
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_ALPHAOP,   D3DTOP_MODULATE);
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	hr = IDirect3DDevice9_SetTextureStageState (d3ddev, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	switch (currprefs.gfx_filter_bilinear)
	{
	case 0:
		v = D3DTEXF_POINT;
		break;
	case 1:
	default:
		v = D3DTEXF_LINEAR;
		break;
	}
	hr = IDirect3DDevice9_SetSamplerState (d3ddev, 0, D3DSAMP_MINFILTER, v);
	hr = IDirect3DDevice9_SetSamplerState (d3ddev, 0, D3DSAMP_MAGFILTER, v);
	hr = IDirect3DDevice9_SetSamplerState (d3ddev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
	hr = IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_ALPHABLENDENABLE, FALSE);
}

static void setupscenecoordssl (void)
{
	float w, h;

	w = window_w;
	h = window_h;

	MatrixOrthoOffCenterLH (&m_matProj, 0.0f, (float)w, 0.0f, (float)h, 0.0f, 1.0f);
	MatrixTranslation (&m_matView,
		(float)w / 2 - 0.5,
		(float)h / 2 + 0.5,
		0.0f);
	MatrixScaling (&m_matWorld, w, h, 1.0f);
}

static void setupscenecoords (void)
{
	RECT sr, dr, zr;
	float w, h;
	float dw, dh;
	static RECT sro, dro, zro;

	//write_log (L"%dx%d %dx%d %dx%d\n", twidth, theight, tin_w, tin_h, window_w, window_h);

	getfilterrect2 (&dr, &sr, &zr, window_w, window_h, tin_w / mult, tin_h / mult, mult, tin_w, tin_h);
	//write_log (L"(%d %d %d %d) - (%d %d %d %d) (%d %d)\n",
	//	dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);

	dw = dr.right - dr.left;
	dh = dr.bottom - dr.top;
	w = sr.right - sr.left;
	h = sr.bottom - sr.top;
	//write_log (L"%.1fx%.1f %.1fx%.1f\n", dw, dh, w, h);

	MatrixOrthoOffCenterLH (&m_matProj, 0, w, 0, h, 0.0f, 1.0f);

	MatrixTranslation (&m_matView,
		-0.5f + dw * tin_w / window_w / 2 - zr.left - sr.left, // - (tin_w - 2 * zr.left - w),
		+0.5f + dh * tin_h / window_h / 2 - zr.top - (tin_h - 2 * zr.top - h) + sr.top, // <- ???
		0);

	MatrixScaling (&m_matWorld,
		dw * tin_w / window_w,
		dh * tin_h / window_h,
		1.0f);

	if (memcmp (&sr, &sro, sizeof (RECT)) || memcmp (&dr, &dro, sizeof (RECT)) || memcmp (&zr, &zro, sizeof (RECT))) {
		needclear = 1;
		sro = sr;
		dro = dr;
		zro = zr;
	}
}

uae_u8 *getfilterbuffer3d (int *widthp, int *heightp, int *pitch, int *depth)
{
	RECT dr, sr, zr;
	uae_u8 *p;
	int w, h;

	*depth = t_depth;
	getfilterrect2 (&dr, &sr, &zr, window_w, window_h, tin_w, tin_h, mult, tin_w, tin_h);
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

	sizex = 1.0;
	if (twidth > tin_w)
		sizex = (float)tin_w / twidth;
	sizey = 1.0;
	if (theight > tin_h)
		sizey = (float)tin_h / theight;

	hr = IDirect3DVertexBuffer9_Lock (vertexBuffer, 0, 0, (void**)&vertices, 0);
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
	// fullscreen vertices
	vertices[4].position.x = -0.5f; vertices[0].position.y = -0.5f;
	vertices[4].diffuse  = 0xFFFFFFFF;
	vertices[4].texcoord.x = 0.0f; vertices[0].texcoord.y = 1.0f;
	vertices[5].position.x = -0.5f; vertices[1].position.y = 0.5f;
	vertices[5].diffuse  = 0xFFFFFFFF;
	vertices[5].texcoord.x = 0.0f; vertices[1].texcoord.y = 0.0f;
	vertices[6].position.x = 0.5f; vertices[2].position.y = -0.5f;
	vertices[6].diffuse  = 0xFFFFFFFF;
	vertices[6].texcoord.x = 1.0f; vertices[2].texcoord.y = 1.0f;
	vertices[7].position.x = 0.5f; vertices[3].position.y = 0.5f;
	vertices[7].diffuse  = 0xFFFFFFFF;
	vertices[7].texcoord.x = 1.0f; vertices[3].texcoord.y = 0.0f;
	// Additional vertices required for some PS effects
	if (psPreProcess) {
		vertices[8].position.x = 0.0f; vertices[4].position.y = 0.0f;
		vertices[8].diffuse  = 0xFFFFFF00;
		vertices[8].texcoord.x = 0.0f; vertices[4].texcoord.y = 1.0f;
		vertices[9].position.x = 0.0f; vertices[5].position.y = 1.0f;
		vertices[9].diffuse  = 0xFFFFFF00;
		vertices[9].texcoord.x = 0.0f; vertices[5].texcoord.y = 0.0f;
		vertices[10].position.x = 1.0f; vertices[6].position.y = 0.0f;
		vertices[10].diffuse  = 0xFFFFFF00;
		vertices[10].texcoord.x = 1.0f; vertices[6].texcoord.y = 1.0f;
		vertices[11].position.x = 1.0f; vertices[7].position.y = 1.0f;
		vertices[11].diffuse  = 0xFFFFFF00;
		vertices[11].texcoord.x = 1.0f; vertices[7].texcoord.y = 0.0f;
	}
	hr = IDirect3DVertexBuffer9_Unlock (vertexBuffer);
}

static void settransformsl (void)
{
	HRESULT hr;

	// Disable Shaders
	hr = IDirect3DDevice9_SetVertexShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetPixelShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_PROJECTION, &m_matProj);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_VIEW, &m_matView);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_WORLD, &m_matWorld);
}

static void settransformfs (void)
{
	HRESULT hr;

	// Disable Shaders
	hr = IDirect3DDevice9_SetVertexShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetPixelShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_PROJECTION, &m_matProj2);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_VIEW, &m_matView2);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_WORLD, &m_matWorld2);
}

static void settransform (void)
{
	if (!psActive) {
		settransformsl ();
	} else {
		if (psPreProcess) {
			// Projection is (0,0,0) -> (1,1,1)
			MatrixOrthoOffCenterLH (&m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
			// Align texels with pixels
			MatrixTranslation (&m_matPreView, -0.5f / twidth, 0.5f / theight, 0.0f);
			// Identity for world
			D3DXMatrixIdentity (&m_matPreWorld);
		} else {
			psEffect_SetMatrices (&m_matProj, &m_matView, &m_matWorld);
		}
	}
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

	if (!sltexture)
		return;
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
	hr = IDirect3DTexture9_LockRect (sltexture, 0, &locked, NULL, 0);
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
	IDirect3DTexture9_UnlockRect (sltexture, 0);
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
	if (pEffect) {
		pEffect->Release ();
		pEffect = NULL;
	}
	if (d3ddev)
		IDirect3DDevice9_SetStreamSource (d3ddev, 0, NULL, 0, 0);
	if (vertexBuffer) {
		IDirect3DVertexBuffer9_Release (vertexBuffer);
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
}

static int restoredeviceobjects (void)
{
	int vbsize;
	HRESULT hr;

	invalidatedeviceobjects ();
	if (currprefs.gfx_filtershader[0]) {
		if (!psEnabled || !psEffect_LoadEffect (currprefs.gfx_filtershader))
			currprefs.gfx_filtershader[0] = changed_prefs.gfx_filtershader[0] = 0;
	}
	if (!createtexture (tin_w, tin_h))
		return 0;
	if (currprefs.gfx_filter_scanlines > 0)
		createsltexture ();
	createledtexture ();
	if (currprefs.gfx_filtermask[0])
		createmasktexture (currprefs.gfx_filtermask);

	vbsize = sizeof (struct TLVERTEX) * 8;
	if (psPreProcess)
		vbsize = sizeof (struct TLVERTEX) * 12;
	hr = d3ddev->SetFVF (D3DFVF_TLVERTEX);
	if (FAILED (d3ddev->CreateVertexBuffer (vbsize, D3DUSAGE_WRITEONLY,
		D3DFVF_TLVERTEX, D3DPOOL_DEFAULT, &vertexBuffer, NULL))) {
			write_log (L"%s: failed to create vertex buffer: %s\n", D3DHEAD, D3D_ErrorString (hr));
			return 0;
	}
	createvertex ();
	hr = d3ddev->SetStreamSource (0, vertexBuffer, 0, sizeof (struct TLVERTEX));

	// turn off culling
	hr = d3ddev->SetRenderState (D3DRS_CULLMODE, D3DCULL_NONE);
	// turn off lighting
	hr = d3ddev->SetRenderState (D3DRS_LIGHTING, FALSE);
	// turn of zbuffer
	hr = d3ddev->SetRenderState (D3DRS_ZENABLE, FALSE);

	setupscenescaled ();
	setupscenecoords ();
	settransform ();

	return 1;
}

static void D3D_free2 (void)
{
	D3D_clear ();
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

const TCHAR *D3D_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth)
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
	D3D_canshaders ();
	d3d_enabled = 0;
	if (!currprefs.gfx_api) {
		_tcscpy (errmsg, L"D3D: not enabled");
		return errmsg;
	}

	d3dx = LoadLibrary (L"d3dx9_42.dll");
	if (d3dx == NULL) {
		_tcscpy (errmsg, L"Direct3D: August 2009 or newer DirectX Runtime required.\n\nhttp://go.microsoft.com/fwlink/?linkid=56513");
		if (isfullscreen () <= 0)
			ShellExecute(NULL, L"open", L"http://go.microsoft.com/fwlink/?linkid=56513", NULL, NULL, SW_SHOWNORMAL);
		return errmsg;
	}
	FreeLibrary (d3dx);

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
		write_log (L"%s: IDirect3D9_GetAdapterDisplayMode failed %s\n", D3DHEAD, D3D_ErrorString (hr));
	if (FAILED (hr = d3d->GetDeviceCaps (adapter, D3DDEVTYPE_HAL, &d3dCaps)))
		write_log (L"%s: IDirect3D9_GetDeviceCaps failed %s\n", D3DHEAD, D3D_ErrorString (hr));

	memset (&dpp, 0, sizeof (dpp));
	dpp.Windowed = isfullscreen() <= 0;
	dpp.BackBufferFormat = mode.Format;
	dpp.BackBufferCount = 1;
	dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
	dpp.BackBufferWidth = w_w;
	dpp.BackBufferHeight = w_h;
	dpp.PresentationInterval = dpp.Windowed ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_DEFAULT;

	modeex.Width = w_w;
	modeex.Height = w_h;
	modeex.RefreshRate = 0;
	modeex.ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
	modeex.Format = mode.Format;

	vsync2 = 0;
	if (isfullscreen() > 0) {
		dpp.FullScreen_RefreshRateInHz = currprefs.gfx_refreshrate > 0 ? currprefs.gfx_refreshrate : 0;
		modeex.RefreshRate = dpp.FullScreen_RefreshRateInHz;
		if (currprefs.gfx_avsync > 0) {
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

	// Check if hardware vertex processing is available
	if(d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
		flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
	else
		flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
	flags |= D3DCREATE_FPU_PRESERVE;

	if (d3d_ex && D3DEX) {
		ret = d3dex->CreateDeviceEx (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, dpp.Windowed ? NULL : &modeex, &d3ddevex);
		d3ddev = (LPDIRECT3DDEVICE9)d3ddevex;
	} else {
		ret = d3d->CreateDevice (adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &d3ddev);
	}
	if (FAILED (ret)) {
		_stprintf (errmsg, L"%s failed, %s\n", d3d_ex && D3DEX ? L"CreateDeviceEx" : L"CreateDevice", D3D_ErrorString (ret));
		if (ret == D3DERR_INVALIDCALL && dpp.Windowed == 0 && dpp.FullScreen_RefreshRateInHz && !ddraw_fs) {
			write_log (L"%s\n", errmsg);
			write_log (L"%s: Retrying fullscreen with DirectDraw\n", D3DHEAD);
			if (ddraw_fs_hack_init ()) {
				const TCHAR *err2 = D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth);
				if (err2)
					ddraw_fs_hack_free ();
				return err2;
			}
		}
		if (d3d_ex && D3DEX) {
			write_log (L"%s\n", errmsg);
			D3DEX = 0;
			return D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth);
		}
		D3D_free ();
		return errmsg;
	}

	if(d3dCaps.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY)
		tex_square = TRUE;
	if(d3dCaps.TextureCaps & D3DPTEXTURECAPS_POW2) {
		if(d3dCaps.TextureCaps & D3DPTEXTURECAPS_NONPOW2CONDITIONAL) {
			tex_pow2 = FALSE;
		} else {
			tex_pow2 = TRUE;
		}
	} else {
		tex_pow2 = FALSE;
	}
	if(d3dCaps.Caps2 & D3DCAPS2_DYNAMICTEXTURES)
		tex_dynamic = TRUE;

	if(d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
		if((d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && tex_dynamic && !tex_pow2 && !tex_square) {
			psEnabled = TRUE;
		} else {
			psEnabled = FALSE;
		}
	}else {
		psEnabled = FALSE;
	}

	max_texture_w = d3dCaps.MaxTextureWidth;
	max_texture_h = d3dCaps.MaxTextureHeight;

	write_log (L"%s: PS=%d.%d VS=%d.%d Square=%d, Pow2=%d, Dyn=%d, %d*%d*%d%s\n",
		D3DHEAD,
		(d3dCaps.PixelShaderVersion >> 8) & 0xff, d3dCaps.PixelShaderVersion & 0xff,
		(d3dCaps.VertexShaderVersion >> 8) & 0xff, d3dCaps.VertexShaderVersion & 0xff,
		tex_square, tex_pow2, tex_dynamic,
		max_texture_w, max_texture_h,
		dpp.Windowed ? 0 : dpp.FullScreen_RefreshRateInHz, currprefs.gfx_avsync ? L" VSYNC" : L""
		);

	if ((d3dCaps.PixelShaderVersion < 3 || d3dCaps.VertexShaderVersion < 3 || tex_pow2 || tex_square || !tex_dynamic || max_texture_w < 4096 || max_texture_h < 4096) && D3DEX) {
		D3DEX = 0;
		write_log (L"Disabling D3D9Ex\n");
		return D3D_init (ahwnd, w_w, w_h, t_w, t_h, depth);
	}


	mult = S2X_getmult ();
	t_w *= mult;
	t_h *= mult;

	if (max_texture_w < t_w || max_texture_h < t_h) {
		_stprintf (errmsg, L"%s: %d * %d or bigger texture support required\nYour card's maximum texture size is only %d * %d",
			D3DHEAD, t_w, t_h, max_texture_w, max_texture_h);
		return errmsg;
	}

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
		if (currprefs.gfx_filter_scanlines)
			tformat = D3DFMT_A8R8G8B8;
		else
			tformat = D3DFMT_X8R8G8B8;
		break;
	case 15:
	case 16:
		if (currprefs.gfx_filter_scanlines)
			tformat = D3DFMT_A1R5G5B5;
		else
			tformat = D3DFMT_X1R5G5B5;
		break;
	}
	window_w = w_w;
	window_h = w_h;
	tin_w = t_w;
	tin_h = t_h;
	if (!restoredeviceobjects ()) {
		D3D_free ();
		_stprintf (errmsg, L"%s: texture creation failed", D3DHEAD);
		return errmsg;
	}

	changed_prefs.leds_on_screen = currprefs.leds_on_screen = currprefs.leds_on_screen | STATUSLINE_TARGET;

	hr = D3DXCreateSprite (d3ddev, &sprite);
	if (FAILED (hr)) {
		write_log (L"%s: LED D3DXSprite failed: %s\n", D3D_ErrorString (hr), D3DHEAD);
	}

	MatrixOrthoOffCenterLH (&m_matProj2, 0, window_w, 0, window_h, 0.0f, 1.0f);
	MatrixTranslation (&m_matView2, -0.5f + window_w / 2, 0.5f + window_h / 2, 0);
	MatrixScaling (&m_matWorld2, window_w, window_h, 1.0f);

	createscanlines (1);
	d3d_enabled = 1;
	return 0;
}

static int isd3d (void)
{
	if (devicelost || !d3ddev || !d3d_enabled)
		return 0;
	return 1;
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
		if (d3dex)
			hr = d3ddevex->ResetEx (&dpp, dpp.Windowed ? NULL : &modeex);
		else
			hr = d3ddev->Reset (&dpp);
		if (FAILED (hr)) {
			write_log (L"%s: Reset failed %s\n", D3DHEAD, D3D_ErrorString (hr));
			resetcount++;
			if (resetcount > 2 || hr == D3DERR_DEVICEHUNG) {
				changed_prefs.gfx_api = 0;
				write_log (L"%s: Too many failed resets, disabling Direct3D mode\n", D3DHEAD);
			}
			return 1;
		}
		write_log (L"%s: Reset succeeded\n", D3DHEAD);
		restoredeviceobjects ();
		return 1;
	} else if (hr == D3DERR_DEVICELOST) {
		invalidatedeviceobjects ();
		Sleep (500);
	} else if (SUCCEEDED (hr)) {
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

static void D3D_render22 (int clear)
{
	HRESULT hr;

	if (!isd3d ())
		return;

	setupscenecoords ();
	settransform ();
	if (needclear || clear) {
		hr = d3ddev->Clear (0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, d3ddebug ? 0x80 : 0x00, 0), 0, 0);
		if (FAILED (hr))
			write_log (L"%s: Clear() failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		needclear = 0;
	}
	if (FAILED (hr = d3ddev->BeginScene ())) {
		write_log (L"%s: BeginScene: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return;
	}
	setupscenescaled ();
	if (window_h > tin_h || window_w > tin_w) {
		D3DRECT r[4];
		int num;
		num = 0;
		if (window_w > tin_w) {
			r[num].x1 = 0;
			r[num].y1 = 0;
			r[num].x2 = (window_w - tin_w) / 2;
			r[num].y2 = window_h;
			num++;
			r[num].x1 = window_w - (window_w - tin_w) / 2;
			r[num].y1 = 0;
			r[num].x2 = window_w;
			r[num].y2 = window_h;
			num++;
		}
		if (window_h > tin_h) {
			r[num].x1 = 0;
			r[num].y1 = 0;
			r[num].x2 = window_w;
			r[num].y2 = (window_h - tin_h) / 2;
			num++;
			r[num].x1 = 0;
			r[num].y1 = window_h - (window_h - tin_h) / 2;
			r[num].x2 = window_w;
			r[num].y2 = window_h;
			num++;
		}
		d3ddev->Clear (num, r, D3DCLEAR_TARGET, D3DCOLOR_XRGB(d3ddebug ? 0x80 : 0x00, 0, 0), 0, 0);
	}
	if (psActive) {
		UINT uPasses, uPass;
		LPDIRECT3DSURFACE9 lpRenderTarget;
		LPDIRECT3DSURFACE9 lpNewRenderTarget;
		LPDIRECT3DTEXTURE9 lpWorkTexture;

		if (!psEffect_SetTextures (texture, lpWorkTexture1, lpWorkTexture2, lpHq2xLookupTexture))
			return;
		if (psPreProcess) {
			if (!psEffect_SetMatrices (&m_matPreProj, &m_matPreView, &m_matPreWorld))
				return;
			if (FAILED (hr = d3ddev->GetRenderTarget (0, &lpRenderTarget))) {
				write_log (L"%s: GetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return;
			}
			lpWorkTexture = lpWorkTexture1;
pass2:
			if (FAILED (hr = lpWorkTexture->GetSurfaceLevel (0, &lpNewRenderTarget))) {
				write_log (L"%s: GetSurfaceLevel: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return;
			}
			if (FAILED (hr = d3ddev->SetRenderTarget (0, lpNewRenderTarget))) {
				write_log (L"%s: SetRenderTarget: %s\n", D3DHEAD, D3D_ErrorString (hr));
				return;
			}
			uPasses = 0;
			if (!psEffect_Begin ((lpWorkTexture == lpWorkTexture1) ? psEffect_PreProcess1 : psEffect_PreProcess2, &uPasses))
				return;
			for (uPass = 0; uPass < uPasses; uPass++) {
				if (!psEffect_BeginPass (uPass))
					return;
				d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 8, 2);
				psEffect_EndPass ();
			}
			if (!psEffect_End ())
				return;
			if (psEffect_hasPreProcess2 () && lpWorkTexture == lpWorkTexture1) {
				lpWorkTexture = lpWorkTexture2;
				goto pass2;
			}
			if (lpRenderTarget) {
				d3ddev->SetRenderTarget (0, lpRenderTarget);
				lpRenderTarget->Release ();
			}
			if (!psEffect_SetMatrices (&m_matProj, &m_matView, &m_matWorld))
				return;
		}
		uPasses = 0;
		if (!psEffect_Begin (psEffect_Combine, &uPasses))
			return;
		for (uPass = 0; uPass < uPasses; uPass++) {
			if (!psEffect_BeginPass (uPass))
				return;
			d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2);
			psEffect_EndPass ();
		}
		if (!psEffect_End ())
			return;

	} else {

		if (masktexture) {
			hr = d3ddev->SetTexture (0, masktexture);
			hr = d3ddev->SetTextureStageState (0, D3DTSS_TEXCOORDINDEX, 0);
			hr = d3ddev->SetTextureStageState (0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
			hr = d3ddev->SetTextureStageState (0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			hr = d3ddev->SetTextureStageState (0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);

			hr = d3ddev->SetTexture (1, texture);
			hr = d3ddev->SetTextureStageState (1, D3DTSS_TEXCOORDINDEX, 0);
			hr = d3ddev->SetTextureStageState (1, D3DTSS_COLOROP, D3DTOP_MODULATE);
			hr = d3ddev->SetTextureStageState (1, D3DTSS_COLORARG1, D3DTA_TEXTURE);
			hr = d3ddev->SetTextureStageState (1, D3DTSS_COLORARG2, D3DTA_CURRENT);
		} else {
			hr = d3ddev->SetTexture (0, texture);
		}
		hr = d3ddev->DrawPrimitive (D3DPT_TRIANGLESTRIP, 0, 2);
	}

	if (sprite && (sltexture || ledtexture)) {
		D3DXVECTOR3 v;
		sprite->Begin (D3DXSPRITE_ALPHABLEND);
		if (sltexture) {
			v.x = v.y = v.z = 0;
			sprite->Draw (sltexture, NULL, NULL, &v, 0xffffffff);
		}
		if (ledtexture) {
			v.x = 0;
			v.y = window_h - TD_TOTAL_HEIGHT;
			v.z = 0;
			sprite->Draw (ledtexture, NULL, NULL, &v, 0xffffffff);
		}
		sprite->End ();
	}

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

static void D3D_render2 (int clear)
{   
	int fpuv;

	fpux_save (&fpuv);
	D3D_render22 (clear);
	fpux_restore (&fpuv);
}

void D3D_unlocktexture (void)
{
	HRESULT hr;

	if (!isd3d ())
		return;
	if (currprefs.leds_on_screen & STATUSLINE_CHIPSET)
		updateleds ();

	hr = texture->UnlockRect (0);

	D3D_render2 (0);
	if (vsync2 && !currprefs.turbo_emulation)
		D3D_render2 (0);
}

uae_u8 *D3D_locktexture (int *pitch)
{
	static int frameskip;
	D3DLOCKED_RECT locked;
	HRESULT hr;

	if (!isd3d ())
		return NULL;
	if (currprefs.turbo_emulation && isfullscreen () > 0 && frameskip-- > 0)
		return NULL;
	frameskip = 50;

	locked.pBits = NULL;
	locked.Pitch = 0;
	hr = texture->LockRect (0, &locked, NULL, 0);
	if (FAILED (hr)) {
		write_log (L"%s: LockRect failed: %s\n", D3DHEAD, D3D_ErrorString (hr));
		return NULL;
	}
	if (locked.pBits == NULL || locked.Pitch == 0) {
		write_log (L"%s: LockRect returned NULL texture\n", D3DHEAD);
		D3D_unlocktexture ();
		return NULL;
	}
	*pitch = locked.Pitch;
	return (uae_u8*)locked.pBits;
}

void D3D_refresh (void)
{
	if (!isd3d ())
		return;
	createscanlines (1);
	D3D_render2 (1);
	D3D_render2 (1);
}

void D3D_getpixelformat (int depth,int *rb, int *gb, int *bb, int *rs, int *gs, int *bs, int *ab, int *as, int *a)
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
		*a = 255;
		break;
	case 15:
	case 16:
		*rb = 5;
		*gb = 5;
		*bb = 5;
		*ab = 1;
		*rs = 10;
		*gs = 5;
		*bs = 0;
		*as = 15;
		*a = 1;
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
		return 0;
	}
	bb->ReleaseDC (hdc);
	bb->Release ();
	return 0;
}

int D3D_isenabled (void)
{
	return d3d_enabled;
}

#endif
