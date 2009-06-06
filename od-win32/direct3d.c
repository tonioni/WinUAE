#include <windows.h>
#include "sysconfig.h"
#include "sysdeps.h"

#if defined (OPENGL) && defined (GFXFILTER)

#include "options.h"
#include "xwin.h"
#include "custom.h"
#include "drawing.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "gfxfilter.h"

#include <d3d9.h>
#include <d3dx9.h>

#include "direct3d.h"
#include "hq2x_d3d.h"

#define D3DEX 0

static int tex_pow2, tex_square, tex_dynamic;
static int psEnabled, psActive, psPreProcess;

static int tformat;
static int d3d_enabled, d3d_ex, scanlines_ok;
static LPDIRECT3D9 d3d;
static LPDIRECT3D9EX d3dex;
static D3DPRESENT_PARAMETERS dpp;
static LPDIRECT3DDEVICE9 d3ddev;
static LPDIRECT3DDEVICE9EX d3ddevex;
static D3DSURFACE_DESC dsdbb;
static LPDIRECT3DTEXTURE9 texture, sltexture;
static LPDIRECT3DTEXTURE9 lpWorkTexture1, lpWorkTexture2;
static LPDIRECT3DVOLUMETEXTURE9 lpHq2xLookupTexture;
static IDirect3DVertexBuffer9 *vertexBuffer;
static HWND d3dhwnd;

static D3DXMATRIX m_matProj;
static D3DXMATRIX m_matWorld;
static D3DXMATRIX m_matView;
static D3DXMATRIX m_matPreProj;
static D3DXMATRIX m_matPreView;
static D3DXMATRIX m_matPreWorld;

static int twidth, theight, max_texture_w, max_texture_h;
static int tin_w, tin_h, window_h, window_w;
static int t_depth;
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
	s = Errors->lpVtbl->GetBufferPointer (Errors);
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

        hParam = pEffect->lpVtbl->GetParameter (pEffect, NULL, iParam);
        pEffect->lpVtbl->GetParameterDesc (pEffect, hParam, &ParamDesc);

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
		    pEffect->lpVtbl->GetFloat(pEffect, hParam, &m_scale);
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
		    pEffect->lpVtbl->GetString(pEffect, hParam, &pstrTechnique);
		    m_CombineTechniqueEffectHandle = pEffect->lpVtbl->GetTechniqueByName(pEffect, pstrTechnique);
		}
		else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE") == 0) {
		    pEffect->lpVtbl->GetString(pEffect, hParam, &pstrTechnique);
		    m_PreprocessTechnique1EffectHandle = pEffect->lpVtbl->GetTechniqueByName(pEffect, pstrTechnique);
		}
		else if(strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE1") == 0) {
		    pEffect->lpVtbl->GetString(pEffect, hParam, &pstrTechnique);
		    m_PreprocessTechnique2EffectHandle = pEffect->lpVtbl->GetTechniqueByName(pEffect, pstrTechnique);
		}
		else if(strcmpi(ParamDesc.Semantic, "NAME") == 0)
		    pEffect->lpVtbl->GetString(pEffect, hParam, &m_strName);
	    }
	}

	for(iAnnot = 0; iAnnot < ParamDesc.Annotations; iAnnot++) {
            hAnnot = pEffect->lpVtbl->GetAnnotation (pEffect, hParam, iAnnot);
            pEffect->lpVtbl->GetParameterDesc(pEffect, hAnnot, &AnnotDesc);
            if(strcmpi(AnnotDesc.Name, "name") == 0)
                pEffect->lpVtbl->GetString(pEffect, hAnnot, &pstrName);
            else if(strcmpi(AnnotDesc.Name, "function") == 0)
                pEffect->lpVtbl->GetString(pEffect, hAnnot, &pstrFunction);
            else if(strcmpi(AnnotDesc.Name, "target") == 0)
                pEffect->lpVtbl->GetString(pEffect, hAnnot, &pstrTarget);
            else if(strcmpi(AnnotDesc.Name, "width") == 0)
                pEffect->lpVtbl->GetInt(pEffect, hAnnot, &Width);
            else if(strcmpi(AnnotDesc.Name, "height") == 0)
                pEffect->lpVtbl->GetInt(pEffect, hAnnot, &Height);
            else if(strcmpi(AnnotDesc.Name, "depth") == 0)
                pEffect->lpVtbl->GetInt(pEffect, hAnnot, &Depth);
            else if(strcmpi(AnnotDesc.Name, "type") == 0)
                pEffect->lpVtbl->GetString(pEffect, hAnnot, &pstrTextureType);
        }

	if(pstrFunction != NULL) {
	    LPD3DXBUFFER pTextureShader = NULL;
	    LPD3DXBUFFER lpErrors = 0;

	    if(pstrTarget == NULL || strcmp(pstrTarget,"tx_1_1"))
                pstrTarget = "tx_1_0";

	    if(SUCCEEDED(hr = EffectCompiler->lpVtbl->CompileShader(EffectCompiler,
				pstrFunction, pstrTarget, 0, &pTextureShader, &lpErrors, NULL))) {
		LPD3DXTEXTURESHADER ppTextureShader;
		if (lpErrors)
		    lpErrors->lpVtbl->Release (lpErrors);

		if(Width == D3DX_DEFAULT)
                    Width = 64;
		if(Height == D3DX_DEFAULT)
                    Height = 64;
		if(Depth == D3DX_DEFAULT)
                    Depth = 64;

		D3DXCreateTextureShader((DWORD *)pTextureShader->lpVtbl->GetBufferPointer(pTextureShader), &ppTextureShader);

		if(pstrTextureType != NULL) {
                    if(strcmpi(pstrTextureType, "volume") == 0) {
                        LPDIRECT3DVOLUMETEXTURE9 pVolumeTex = NULL;
                        if(SUCCEEDED(hr = D3DXCreateVolumeTexture(d3ddev,
                        	Width, Height, Depth, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pVolumeTex))) {
                    	    if(SUCCEEDED(hr = D3DXFillVolumeTextureTX(pVolumeTex, ppTextureShader))) {
                                pTex = pVolumeTex;
                            }
                        }
                    } else if(strcmpi(pstrTextureType, "cube") == 0) {
                        LPDIRECT3DCUBETEXTURE9 pCubeTex = NULL;
                        if(SUCCEEDED(hr = D3DXCreateCubeTexture(d3ddev,
                        	Width, D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &pCubeTex))) {
                            if(SUCCEEDED(hr = D3DXFillCubeTextureTX(pCubeTex, ppTextureShader))) {
                                pTex = pCubeTex;
                            }
                        }
                    }
		} else {
                    LPDIRECT3DTEXTURE9 p2DTex = NULL;
                    if(SUCCEEDED(hr = D3DXCreateTexture(d3ddev, Width, Height,
                    	    D3DX_DEFAULT, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &p2DTex))) {
                        if(SUCCEEDED(hr = D3DXFillTextureTX(p2DTex, ppTextureShader))) {
                            pTex = p2DTex;
                        }
                    }
		}
		pEffect->lpVtbl->SetTexture(pEffect, pEffect->lpVtbl->GetParameter(pEffect, NULL, iParam), pTex);
		if (pTex)
		    pTex->lpVtbl->Release (pTex);
		if (pTextureShader)
		    pTextureShader->lpVtbl->Release (pTextureShader);
		if (ppTextureShader)
		    ppTextureShader->lpVtbl->Release (ppTextureShader);
	    } else {
		write_log (L"D3D: Could not compile texture shader: %s\n", D3DX_ErrorString (hr, lpErrors));
		if (lpErrors)
		    lpErrors->lpVtbl->Release (lpErrors);
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
    HANDLE h;
    LPDIRECT3D9 d3dx;
    D3DCAPS9 d3dCaps;

    if (d3d_yesno < 0)
	return 0;
    if (d3d_yesno > 0)
	return 1;
    d3d_yesno = -1;
    h = LoadLibrary (L"d3dx9_41.dll");
    if (h != NULL) {
	FreeLibrary (h);
	d3dx = Direct3DCreate9 (D3D_SDK_VERSION);
	if (d3dx != NULL) {
	    if (SUCCEEDED (IDirect3D9_GetDeviceCaps (d3dx, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &d3dCaps))) {
		if(d3dCaps.PixelShaderVersion >= D3DPS_VERSION(2,0)) {
		    write_log (L"D3D: Pixel shader 2.0+ support detected, shader filters enabled.\n");
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
	write_log (L"D3D: D3DXCreateEffectCompilerFromFile failed: %s\n", D3DX_ErrorString (hr, Errors));
	goto end;
    }
    hr = EffectCompiler->lpVtbl->CompileEffect (EffectCompiler, 0, &BufferEffect, &Errors);
    if (FAILED (hr)) {
	write_log (L"D3D: CompileEffect failed: %s\n", D3DX_ErrorString (hr, Errors));
	goto end;
    }
    hr = D3DXCreateEffect (d3ddev,
	BufferEffect->lpVtbl->GetBufferPointer (BufferEffect),
	BufferEffect->lpVtbl->GetBufferSize (BufferEffect),
	NULL, NULL,
	0,
	NULL, &pEffect, &Errors);
    if (FAILED (hr)) {
	write_log (L"D3D: D3DXCreateEffect failed: %s\n", D3DX_ErrorString (hr, Errors));
	goto end;
    }
    pEffect->lpVtbl->GetDesc (pEffect, &EffectDesc);
    if (!psEffect_ParseParameters (EffectCompiler))
	goto end;
    ret = 1;
end:
    if (Errors)
	Errors->lpVtbl->Release (Errors);
    if (BufferEffect)
	BufferEffect->lpVtbl->Release (BufferEffect);
    if (EffectCompiler)
	EffectCompiler->lpVtbl->Release (EffectCompiler);

    psActive = FALSE;
    psPreProcess = FALSE;
    if (ret) {
	psActive = TRUE;
	if (psEffect_hasPreProcess ())
	    psPreProcess = TRUE;
	write_log (L"D3D: pixelshader filter '%s' enabled, preproc=%d\n", tmp, psPreProcess);
    } else {
	write_log (L"D3D: pixelshader filter '%s' failed to initialize\n", tmp);
    }
    return ret;
}

static int psEffect_SetMatrices (D3DXMATRIX *matProj, D3DXMATRIX *matView, D3DXMATRIX *matWorld)
{
    HRESULT hr;

    if (m_MatWorldEffectHandle) {
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatWorldEffectHandle, matWorld);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matWorld %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_MatViewEffectHandle) {
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatViewEffectHandle, matView);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matView %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_MatProjEffectHandle) {
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatProjEffectHandle, matProj);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matProj %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_MatWorldViewEffectHandle) {
	D3DXMATRIX matWorldView;
	D3DXMatrixMultiply (&matWorldView, matWorld, matView);
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatWorldViewEffectHandle, &matWorldView);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matWorldView %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_MatViewProjEffectHandle) {
	D3DXMATRIX matViewProj;
	D3DXMatrixMultiply (&matViewProj, matView, matProj);
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatViewProjEffectHandle, &matViewProj);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matViewProj %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_MatWorldViewProjEffectHandle) {
	D3DXMATRIX tmp, matWorldViewProj;
	D3DXMatrixMultiply (&tmp, matWorld, matView);
	D3DXMatrixMultiply (&matWorldViewProj, &tmp, matProj);
	hr = pEffect->lpVtbl->SetMatrix (pEffect, m_MatWorldViewProjEffectHandle, &matWorldViewProj);
	if (FAILED (hr)) {
	    write_log (L"D3D:Create:SetMatrix:matWorldViewProj %s\n", D3D_ErrorString (hr));
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
	write_log (L"D3D: Texture with SOURCETEXTURE semantic not found\n");
	return 0;
    }
    hr = pEffect->lpVtbl->SetTexture (pEffect, m_SourceTextureEffectHandle, (LPDIRECT3DBASETEXTURE9)lpSource);
    if (FAILED (hr)) {
	write_log (L"D3D:SetTextures:lpSource %s\n", D3D_ErrorString (hr));
	return 0;
    }
    if(m_WorkingTexture1EffectHandle) {
	hr = pEffect->lpVtbl->SetTexture (pEffect, m_WorkingTexture1EffectHandle, (LPDIRECT3DBASETEXTURE9)lpWorking1);
	if (FAILED (hr)) {
	    write_log (L"D3D:SetTextures:lpWorking1 %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if(m_WorkingTexture2EffectHandle) {
	hr = pEffect->lpVtbl->SetTexture (pEffect, m_WorkingTexture2EffectHandle, (LPDIRECT3DBASETEXTURE9)lpWorking2);
	if (FAILED (hr)) {
	    write_log (L"D3D:SetTextures:lpWorking2 %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if(m_Hq2xLookupTextureHandle) {
	hr = pEffect->lpVtbl->SetTexture (pEffect, m_Hq2xLookupTextureHandle, (LPDIRECT3DBASETEXTURE9)lpHq2xLookupTexture);
	if (FAILED (hr)) {
	    write_log (L"D3D:SetTextures:lpHq2xLookupTexture %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    fDims.x = 256; fDims.y = 256; fDims.z  = 1; fDims.w = 1;
    fTexelSize.x = 1; fTexelSize.y = 1; fTexelSize.z = 1; fTexelSize.w = 1; 
    if (lpSource) {
	D3DSURFACE_DESC Desc;
	lpSource->lpVtbl->GetLevelDesc (lpSource, 0, &Desc);
	fDims.x = (FLOAT) Desc.Width;
	fDims.y = (FLOAT) Desc.Height;
    }
    fTexelSize.x = 1 / fDims.x;
    fTexelSize.y = 1 / fDims.y;
    if (m_SourceDimsEffectHandle) {
	hr = pEffect->lpVtbl->SetVector (pEffect, m_SourceDimsEffectHandle, &fDims);
	if (FAILED (hr)) {
	    write_log (L"D3D:SetTextures:SetVector:Source %s\n", D3D_ErrorString (hr));
	    return 0;
	}
    }
    if (m_TexelSizeEffectHandle) {
	hr = pEffect->lpVtbl->SetVector (pEffect, m_TexelSizeEffectHandle, &fTexelSize);
	if (FAILED (hr)) {
	    write_log (L"D3D:SetTextures:SetVector:Texel %s\n", D3D_ErrorString (hr));
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
	    hr = pEffect->lpVtbl->SetTechnique (pEffect, m_PreprocessTechnique1EffectHandle);
	    break;
	case psEffect_PreProcess2:
	    hr = pEffect->lpVtbl->SetTechnique (pEffect, m_PreprocessTechnique2EffectHandle);
	    break;
	case psEffect_Combine:
	    hr = pEffect->lpVtbl->SetTechnique (pEffect, m_CombineTechniqueEffectHandle);
	    break;
    }
    if(FAILED(hr)) {
	write_log (L"D3D: SetTechnique: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    hr = pEffect->lpVtbl->Begin (pEffect, pPasses, D3DXFX_DONOTSAVESTATE|D3DXFX_DONOTSAVESHADERSTATE);
    if(FAILED(hr)) {
	write_log (L"D3D: Begin: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    return 1;
}
static int psEffect_BeginPass (UINT Pass)
{
    HRESULT hr;

    hr = pEffect->lpVtbl->BeginPass (pEffect, Pass);
    if (FAILED (hr)) {
	write_log (L"D3D: BeginPass: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    return 1;
}
static int psEffect_EndPass (void)
{
    HRESULT hr;

    hr = pEffect->lpVtbl->EndPass (pEffect);
    if (FAILED (hr)) {
	write_log (L"D3D: EndPass: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    return 1;
}
static int psEffect_End (void)
{
    HRESULT hr;

    hr = pEffect->lpVtbl->End (pEffect);
    if (FAILED (hr)) {
	write_log (L"D3D: End: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    return 1;
}

static LPDIRECT3DTEXTURE9 createtext (int *ww, int *hh, D3DFORMAT format)
{
    LPDIRECT3DTEXTURE9 t;
    HRESULT hr;
    int w, h;

    w = *ww;
    h = *hh;
    if (!tex_pow2) {
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

    if (tex_dynamic) {
        hr = IDirect3DDevice9_CreateTexture (d3ddev, w, h, 1, D3DUSAGE_DYNAMIC, format,
	    D3DPOOL_DEFAULT, &t, NULL);
    } else {
        hr = IDirect3DDevice9_CreateTexture (d3ddev, w, h, 1, 0, format,
	    D3DPOOL_MANAGED, &t, NULL);
    }
    if (FAILED (hr)) {
        write_log (L"IDirect3DDevice9_CreateTexture failed: %s\n", D3D_ErrorString (hr));
	return 0;
    }

    *ww = w;
    *hh = h;
    return t;
}


static int createtexture (int w, int h)
{
    HRESULT hr;
    UINT ww = w;
    UINT hh = h;

    texture = createtext (&ww, &hh, tformat);
    if (!texture)
	return 0;
    twidth = ww;
    theight = hh;
    write_log (L"D3D: %d*%d texture allocated, bits per pixel %d\n", ww, hh, t_depth);
    if (psActive) {
	D3DLOCKED_BOX lockedBox;
	if (FAILED (hr = IDirect3DDevice9_CreateTexture (d3ddev, ww, hh, 1,
	    D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture1, NULL))) {
		write_log (L"D3D:Failed to create working texture1: %s\n", D3D_ErrorString (hr));
		return 0;
	}
	if (FAILED (hr = IDirect3DDevice9_CreateTexture (d3ddev, ww, hh, 1,
	    D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &lpWorkTexture2, NULL))) {
		write_log (L"D3D:Failed to create working texture2: %s\n", D3D_ErrorString (hr));
		return 0;
	}
	if (FAILED (hr = IDirect3DDevice9_CreateVolumeTexture (d3ddev, 256, 16, 256, 1, 0,
	    D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &lpHq2xLookupTexture, NULL))) {
		write_log (L"D3D:Failed to create volume texture: %s\n", D3D_ErrorString (hr));
		return 0;
	}
	if (FAILED (hr = IDirect3DVolumeTexture9_LockBox (lpHq2xLookupTexture, 0, &lockedBox, NULL, 0))) {
	    write_log (L"D3D: Failed to lock box of volume texture: %s\n", D3D_ErrorString (hr));
	    return 0;
	}
	//BuildHq2xLookupTexture(tin_w, tin_w, window_w, window_h, (unsigned char*)lockedBox.pBits);
	BuildHq2xLookupTexture(window_w, window_h, tin_w, tin_w,  (unsigned char*)lockedBox.pBits);
	IDirect3DVolumeTexture9_UnlockBox (lpHq2xLookupTexture, 0);

    }
    return 1;
}

static int createsltexture (void)
{
    UINT ww = required_sl_texture_w;
    UINT hh = required_sl_texture_h;

    sltexture = createtext (&ww, &hh, D3DFMT_A4R4G4B4);
    if (!sltexture)
	return 0;
    required_sl_texture_w = ww;
    required_sl_texture_h = hh;
    write_log (L"D3D: SL %d*%d texture allocated\n", ww, hh);

    scanlines_ok = 1;
    return 1;
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
    switch (currprefs.gfx_filter_filtermode & 1)
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

//    write_log (L"%dx%d %dx%d %dx%d\n", twidth, theight, tin_w, tin_h, window_w, window_h);

    getfilterrect2 (&dr, &sr, &zr, window_w, window_h, tin_w, tin_h, 1, tin_w, tin_h);
//    write_log (L"(%d %d %d %d) - (%d %d %d %d) (%d %d)\n",
//	dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom, zr.left, zr.top);

    dw = dr.right - dr.left;
    dh = dr.bottom - dr.top;
    w = sr.right - sr.left;
    h = sr.bottom - sr.top;
//    write_log (L"%.1fx%.1f %.1fx%.1f\n", dw, dh, w, h);

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
    // Additional vertices required for some PS effects
    if (psPreProcess) {
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
    }
    hr = IDirect3DVertexBuffer9_Unlock (vertexBuffer);
}

static void settransformsl (void)
{
    HRESULT hr;

    hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_PROJECTION, &m_matProj);
    hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_VIEW, &m_matView);
    hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_WORLD, &m_matWorld);
}

static void settransform (void)
{
    HRESULT hr;

    if (!psActive) {
	// Disable Shaders
	hr = IDirect3DDevice9_SetVertexShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetPixelShader (d3ddev, 0);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_PROJECTION, &m_matProj);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_VIEW, &m_matView);
	hr = IDirect3DDevice9_SetTransform (d3ddev, D3DTS_WORLD, &m_matWorld);
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

    if (!scanlines_ok)
	return;
    if (osl1 == currprefs.gfx_filter_scanlines && osl3 == currprefs.gfx_filter_scanlinelevel && osl2 == currprefs.gfx_filter_scanlineratio && !force)
	return;
    osl1 = currprefs.gfx_filter_scanlines;
    osl3 = currprefs.gfx_filter_scanlinelevel;
    osl2 = currprefs.gfx_filter_scanlineratio;
    sl4 = currprefs.gfx_filter_scanlines * 16 / 100;
    sl42 = currprefs.gfx_filter_scanlinelevel * 16 / 100;
    if (sl4 > 15)
	sl4 = 15;
    if (sl42 > 15)
	sl42 = 15;
    l1 = currprefs.gfx_filter_scanlineratio & 15;
    l2 = currprefs.gfx_filter_scanlineratio >> 4;

    hr = IDirect3DTexture9_LockRect (sltexture, 0, &locked, NULL, D3DLOCK_DISCARD);
    if (FAILED (hr)) {
	write_log (L"SL IDirect3DTexture9_LockRect failed: %s\n", D3D_ErrorString (hr));
	return;
    }
    sld = (uae_u8*)locked.pBits;
    for (y = 0; y < required_sl_texture_h; y++)
	memset (sld + y * locked.Pitch, 0, required_sl_texture_w * 2);
    for (y = 1; y < required_sl_texture_h; y += l1 + l2) {
	for (yy = 0; yy < l2 && y + yy < required_sl_texture_h; yy++) {
	    for (x = 0; x < required_sl_texture_w; x++) {
		/* 16-bit, A4R4G4B4 */
		uae_u8 sll = sl42;
		p = &sld[(y + yy) * locked.Pitch + (x * 2)];
		p[1] = (sl4 << 4) | (sll << 0);
		p[0] = (sll << 4) | (sll << 0);
	    }
	}
    }
    IDirect3DTexture9_UnlockRect (sltexture, 0);
    if (scanlines_ok) {
	/* enable alpha blending for scanlines */
	IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_ALPHABLENDENABLE, TRUE);
	IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    } else {
	IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_ALPHABLENDENABLE, FALSE);
    }
}


static void invalidatedeviceobjects (void)
{
    if (texture) {
	IDirect3DTexture9_Release (texture);
	texture = NULL;
    }
    if (sltexture) {
	IDirect3DTexture9_Release (sltexture);
	sltexture = NULL;
    }
    if (lpWorkTexture1) {
	IDirect3DTexture9_Release (lpWorkTexture1);
	lpWorkTexture1 = NULL;
    }
    if (lpWorkTexture2) {
	IDirect3DTexture9_Release (lpWorkTexture2);
	lpWorkTexture2 = NULL;
    }
    if (lpHq2xLookupTexture) {
	IDirect3DVolumeTexture9_Release (lpHq2xLookupTexture);
	lpHq2xLookupTexture = NULL;
    }
    if (pEffect) {
	pEffect->lpVtbl->Release (pEffect);
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

    vbsize = sizeof (struct TLVERTEX) * 4;
    if (psPreProcess)
	vbsize = sizeof (struct TLVERTEX) * 8;
    hr = IDirect3DDevice9_SetFVF (d3ddev, D3DFVF_TLVERTEX);
    if (FAILED (IDirect3DDevice9_CreateVertexBuffer (d3ddev, vbsize, D3DUSAGE_WRITEONLY,
    	D3DFVF_TLVERTEX, D3DPOOL_MANAGED, &vertexBuffer, NULL))) {
	    write_log (L"D3D: failed to create vertex buffer: %s\n", D3D_ErrorString (hr));
	    return 0;
    }
    createvertex ();
    hr = IDirect3DDevice9_SetStreamSource (d3ddev, 0, vertexBuffer, 0, sizeof (struct TLVERTEX));

    // Turn off culling
    hr = IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_CULLMODE, D3DCULL_NONE);
    // turn off lighting
    hr = IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_LIGHTING, FALSE);
    // turn of zbuffer
    hr = IDirect3DDevice9_SetRenderState (d3ddev, D3DRS_ZENABLE, FALSE);

    setupscenescaled ();
    setupscenecoords ();
    settransform ();

    return 1;
}

void D3D_free (void)
{
    D3D_clear ();
    invalidatedeviceobjects ();
    if (d3ddev) {
	IDirect3DDevice9_Release (d3ddev);
	d3ddev = NULL;
    }
    if (d3d) {
	IDirect3D9_Release (d3d);
	d3d = NULL;
    }
    d3d_enabled = 0;
    psPreProcess = 0;
    psActive = 0;
    resetcount = 0;
}

const TCHAR *D3D_init (HWND ahwnd, int w_w, int w_h, int t_w, int t_h, int depth)
{
    HRESULT ret, hr;
    static TCHAR errmsg[100] = { 0 };
    D3DDISPLAYMODE mode;
    D3DDISPLAYMODEEX modeex;
    D3DCAPS9 d3dCaps;
    int adapter;
    DWORD flags;
    HINSTANCE d3dDLL;

    D3D_free ();
    D3D_canshaders ();
    d3d_enabled = 0;
    scanlines_ok = 0;
    if (currprefs.gfx_filter != UAE_FILTER_DIRECT3D) {
	_tcscpy (errmsg, L"D3D: not enabled");
	return errmsg;
    }

    d3d_ex = FALSE;
    d3dDLL = LoadLibrary (L"D3D9.DLL");
    if (d3dDLL == NULL) {
	_tcscpy (errmsg, L"Direct3D: DirectX 9 or newer required");
	return errmsg;
    } else {
	typedef HRESULT (WINAPI *LPDIRECT3DCREATE9EX)(UINT, IDirect3D9Ex**);
        LPDIRECT3DCREATE9EX d3dexp  = (LPDIRECT3DCREATE9EX)GetProcAddress (d3dDLL, "Direct3DCreate9Ex");
	if (d3dexp)
	    d3d_ex = TRUE;
    }
    FreeLibrary (d3dDLL);
    hr = -1;
    if (d3d_ex && D3DEX) {
        hr = Direct3DCreate9Ex (D3D_SDK_VERSION, &d3dex);
	if (FAILED (hr))
	    write_log (L"Direct3D: failed to create D3DEx object: %s\n", D3D_ErrorString (hr));
        d3d = (IDirect3D9*)d3dex;
    }
    if (FAILED (hr)) {
	d3dex = NULL;
	d3d = Direct3DCreate9 (D3D_SDK_VERSION);
	if (d3d == NULL) {
	    D3D_free ();
	    _tcscpy (errmsg, L"Direct3D: failed to create D3D object");
	    return errmsg;
        }
    }

    adapter = currprefs.gfx_display - 1;
    if (adapter < 0)
	adapter = 0;
    if (adapter >= IDirect3D9_GetAdapterCount (d3d))
	adapter = 0;

    modeex.Size = sizeof modeex;
    if (d3dex && D3DEX) {
	LUID luid;
	hr = IDirect3D9Ex_GetAdapterLUID (d3dex, adapter, &luid);
	hr = IDirect3D9Ex_GetAdapterDisplayModeEx (d3dex, adapter, &modeex, NULL);
    }
    if (FAILED (hr = IDirect3D9_GetAdapterDisplayMode (d3d, adapter, &mode)))
        write_log (L"D3D: IDirect3D9_GetAdapterDisplayMode failed %s\n", D3D_ErrorString (hr));
    if (FAILED (hr = IDirect3D9_GetDeviceCaps (d3d, adapter, D3DDEVTYPE_HAL, &d3dCaps)))
        write_log (L"D3D: IDirect3D9_GetDeviceCaps failed %s\n", D3D_ErrorString (hr));

    memset (&dpp, 0, sizeof (dpp));
    dpp.Windowed = isfullscreen() <= 0;
    dpp.BackBufferFormat = mode.Format;
    dpp.BackBufferCount = 1;
    dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    dpp.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
    dpp.BackBufferWidth = w_w;
    dpp.BackBufferHeight = w_h;
    dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

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
	    if (currprefs.gfx_avsync > 85) {
		if (d3dCaps.PresentationIntervals & D3DPRESENT_INTERVAL_TWO)
		    dpp.PresentationInterval = D3DPRESENT_INTERVAL_TWO;
		else
		    vsync2 = 1;
	    }
	}
    }

    d3dhwnd = ahwnd;

   // Check if hardware vertex processing is available
    if(d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT)
	flags = D3DCREATE_HARDWARE_VERTEXPROCESSING;
    else
	flags = D3DCREATE_SOFTWARE_VERTEXPROCESSING;
    flags |= D3DCREATE_NOWINDOWCHANGES | D3DCREATE_FPU_PRESERVE;

    if (d3d_ex && D3DEX) {
	ret = IDirect3D9Ex_CreateDeviceEx (d3dex, adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &modeex, &d3ddevex);
	d3ddev = (LPDIRECT3DDEVICE9)d3ddevex;
    } else {
	ret = IDirect3D9_CreateDevice (d3d, adapter, D3DDEVTYPE_HAL, d3dhwnd, flags, &dpp, &d3ddev);
    }
    if (FAILED (ret)) {
	_stprintf (errmsg, L"%s failed, %s\n", d3d_ex && D3DEX ? L"CreateDeviceEx" : L"CreateDevice", D3D_ErrorString (ret));
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
	if((d3dCaps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) && tex_dynamic) {
	    psEnabled = TRUE;
	    tex_pow2 = TRUE;
	} else {
	    psEnabled = FALSE;
	}
    }else {
	psEnabled = FALSE;
    }

    max_texture_w = d3dCaps.MaxTextureWidth;
    max_texture_h = d3dCaps.MaxTextureHeight;

    write_log (L"D3D: PS=%d.%d VS=%d.%d Square=%d, Pow2=%d, Tex Size=%d*%d\n",
	(d3dCaps.PixelShaderVersion >> 8) & 0xff, d3dCaps.PixelShaderVersion & 0xff,
	(d3dCaps.VertexShaderVersion >> 8) & 0xff, d3dCaps.VertexShaderVersion & 0xff,
	tex_square, tex_pow2,
	max_texture_w, max_texture_h);

    if (max_texture_w < t_w || max_texture_h < t_h) {
	_stprintf (errmsg, L"Direct3D: %d * %d or bigger texture support required\nYour card's maximum texture size is only %d * %d",
	    t_w, t_h, max_texture_w, max_texture_h);
	return errmsg;
    }

    required_sl_texture_w = w_w;
    required_sl_texture_h = w_h;
    if (currprefs.gfx_filter_scanlines > 0 && (max_texture_w < w_w || max_texture_h < w_h)) {
	gui_message (L"Direct3D: %d * %d or bigger texture support required for scanlines (max is only %d * %d)\n"
	    L"Scanlines disabled.",
	    required_sl_texture_w, required_sl_texture_h, max_texture_w, max_texture_h);
	changed_prefs.gfx_filter_scanlines = currprefs.gfx_filter_scanlines = 0;
    }

    t_depth = depth;
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
	_stprintf (errmsg, L"Direct3D: texture creation failed");
	return errmsg;
    }

    createscanlines (1);
    d3d_enabled = 1;
    return 0;
}

int D3D_needreset (void)
{
    HRESULT hr = IDirect3DDevice9_TestCooperativeLevel (d3ddev);
    if (hr == D3DERR_DEVICENOTRESET) {
	hr = IDirect3DDevice9_Reset (d3ddev, &dpp);
	if (FAILED (hr)) {
	    write_log (L"D3D: Reset failed %s\n", D3D_ErrorString (hr));
	    resetcount++;
	    if (resetcount > 2)
		changed_prefs.gfx_filter = 0;
	}
	return 1;
    }
    return 0;
}

void D3D_clear (void)
{
    int i;
    HRESULT hr;

    if (!d3ddev)
	return;
    hr = IDirect3DDevice9_TestCooperativeLevel (d3ddev);
    if (FAILED (hr))
	return;
    for (i = 0; i < 2; i++) {
	IDirect3DDevice9_Clear (d3ddev, 0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0L);
	IDirect3DDevice9_Present (d3ddev, NULL, NULL, NULL, NULL);
    }
}

static void D3D_render2 (int clear)
{
    HRESULT hr;
    if (!d3d_enabled)
	return;
    if (FAILED (IDirect3DDevice9_TestCooperativeLevel (d3ddev)))
	return;

    if (clear) {
	setupscenescaled ();
    }
    setupscenecoords ();
    settransform ();
    if (clear || needclear) {
	int i;
	for (i = 0; i < 2; i++) {
	    hr = IDirect3DDevice9_Clear (d3ddev, 0L, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0L );
	    if (FAILED (hr))
		write_log (L"IDirect3DDevice9_Clear() failed: %s\n", D3D_ErrorString (hr));
	    IDirect3DDevice9_Present (d3ddev, NULL, NULL, NULL, NULL);
	}
	needclear = 0;
    }
    hr = IDirect3DDevice9_BeginScene (d3ddev);
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
	    IDirect3DDevice9_GetRenderTarget (d3ddev, 0, &lpRenderTarget);
	    lpWorkTexture = lpWorkTexture1;
	    pass2:
	    IDirect3DTexture9_GetSurfaceLevel (lpWorkTexture, 0, &lpNewRenderTarget);
	    if (FAILED (hr = IDirect3DDevice9_SetRenderTarget (d3ddev, 0, lpNewRenderTarget))) {
		write_log (L"D3D: IDirect3DDevice9_SetRenderTarget: %s\n", D3D_ErrorString (hr));
		return;
	    }
	    if (lpRenderTarget)
		IDirect3DSurface9_Release (lpRenderTarget);
	    uPasses = 0;
	    if (!psEffect_Begin ((lpWorkTexture == lpWorkTexture1) ?
		psEffect_PreProcess1 : psEffect_PreProcess2, &uPasses))
		return;
	    for (uPass = 0; uPass < uPasses; uPass++) {
		if (!psEffect_BeginPass (uPass))
		    return;
	        IDirect3DDevice9_DrawPrimitive (d3ddev, D3DPT_TRIANGLESTRIP, 4, 2);
		psEffect_EndPass ();
	    }
	    if (!psEffect_End ())
		return;
	    if (psEffect_hasPreProcess2 () && lpWorkTexture == lpWorkTexture1) {
		lpWorkTexture = lpWorkTexture2;
		goto pass2;
	    }
	    IDirect3DDevice9_SetRenderTarget (d3ddev, 0, lpRenderTarget);
	    if (lpRenderTarget)
		IDirect3DTexture9_Release (lpRenderTarget);
	    if (!psEffect_SetMatrices (&m_matProj, &m_matView, &m_matWorld))
		return;
	}
	uPasses = 0;
	if (!psEffect_Begin (psEffect_Combine, &uPasses))
	    return;
	for (uPass = 0; uPass < uPasses; uPass++) {
	    if (!psEffect_BeginPass (uPass))
		return;
	    IDirect3DDevice9_DrawPrimitive (d3ddev, D3DPT_TRIANGLESTRIP, 0, 2);
	    psEffect_EndPass ();
	}
	if (!psEffect_End ())
	    return;

    } else {

	hr = IDirect3DDevice9_SetTexture (d3ddev, 0, (IDirect3DBaseTexture9*)texture);
	hr = IDirect3DDevice9_DrawPrimitive (d3ddev, D3DPT_TRIANGLESTRIP, 0, 2);

	if (scanlines_ok) {
	    setupscenecoordssl ();
	    settransformsl ();
	    hr = IDirect3DDevice9_SetTexture (d3ddev, 0, (IDirect3DBaseTexture9*)sltexture);
	    hr = IDirect3DDevice9_DrawPrimitive (d3ddev, D3DPT_TRIANGLESTRIP, 0, 2);
	}

    }

    hr = IDirect3DDevice9_EndScene (d3ddev);
    hr = IDirect3DDevice9_Present (d3ddev, NULL, NULL, NULL, NULL);
}

void D3D_render (void)
{
    D3D_render2 (1);
}

void D3D_unlocktexture (void)
{
    HRESULT hr;
    RECT r;

    hr = IDirect3DTexture9_UnlockRect (texture, 0);
    r.left = 0; r.right = window_w;
    r.top = 0; r.bottom = window_h;
    hr = IDirect3DTexture9_AddDirtyRect (texture, &r);

    D3D_render2 (0);
    if (vsync2 && !currprefs.turbo_emulation)
	D3D_render2 (0);
}

int D3D_locktexture (void)
{
    D3DLOCKED_RECT locked;
    HRESULT hr;

    hr = IDirect3DDevice9_TestCooperativeLevel (d3ddev);
    if (FAILED (hr)) {
	if (hr == D3DERR_DEVICELOST) {
	    if (!dpp.Windowed && IsWindow (d3dhwnd) && !IsIconic (d3dhwnd)) {
		write_log (L"D3D: minimize\n");
		ShowWindow (d3dhwnd, SW_MINIMIZE);
	    }
	}
	return 0;
    }

    locked.pBits = NULL;
    locked.Pitch = 0;
    hr = IDirect3DTexture9_LockRect (texture, 0, &locked, NULL, D3DLOCK_NO_DIRTY_UPDATE);
    if (FAILED (hr)) {
	if (hr != D3DERR_DRIVERINTERNALERROR) {
	    write_log (L"IDirect3DTexture9_LockRect failed: %s\n", D3D_ErrorString (hr));
	    D3D_unlocktexture ();
	    return 0;
	}
    }
    if (locked.pBits == NULL || locked.Pitch == 0) {
	write_log (L"IDirect3DTexture9_LockRect return NULL texture\n");
        D3D_unlocktexture ();
	return 0;
    }
    gfxvidinfo.bufmem = locked.pBits;
    gfxvidinfo.rowbytes = locked.Pitch;
    init_row_map ();
    return 1;
}

void D3D_refresh (void)
{
    if (!d3d_enabled)
	return;
    createscanlines (1);
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
    if (!d3d_enabled)
	return;
    hr = IDirect3DDevice9_SetDialogBoxMode (d3ddev, guion);
    if (FAILED (hr))
	write_log (L"D3D: SetDialogBoxMode %s\n", D3D_ErrorString (hr));
    guimode = guion;
}

HDC D3D_getDC (HDC hdc)
{
    static LPDIRECT3DSURFACE9 bb;
    HRESULT hr;

    if (!d3d_enabled)
	return 0;
    if (!hdc) {
	hr = IDirect3DDevice9_GetBackBuffer (d3ddev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
	if (FAILED (hr)) {
	    write_log (L"IDirect3DDevice9_GetBackBuffer() failed: %s\n", D3D_ErrorString (hr));
	    return 0;
	}
	hr = IDirect3DSurface9_GetDC (bb, &hdc);
	if (SUCCEEDED (hr))
	    return hdc;
        write_log (L"IDirect3DSurface9_GetDC() failed: %s\n", D3D_ErrorString (hr));
	return 0;
    }
    IDirect3DSurface9_ReleaseDC (bb, hdc);
    IDirect3DSurface9_Release (bb);
    return 0;
}

int D3D_isenabled (void)
{
    return d3d_enabled;
}

#endif
