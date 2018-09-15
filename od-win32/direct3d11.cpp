
#include <windows.h>
#include "resource.h"

#include <DXGI1_5.h>
#include <d3d11.h>
#include <D3Dcompiler.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#include "sysconfig.h"
#include "sysdeps.h"

#include "options.h"
#include "xwin.h"
#include "dxwrap.h"
#include "win32.h"
#include "win32gfx.h"
#include "direct3d.h"
#include "uae.h"
#include "custom.h"
#include "gfxfilter.h"
#include "zfile.h"
#include "statusline.h"
#include "hq2x_d3d.h"
#include "gui.h"
#include "gfxboard.h"

#include "d3dx.h"

#include "shaders/PixelShaderPlain.h"
#include "shaders/PixelShaderAlpha.h"
#include "shaders/PixelShaderMask.h"
#include "shaders/VertexShader.h"

#include "FX11/d3dx11effect.h"

#include <process.h>
#include <Dwmapi.h>

void (*D3D_free)(int, bool immediate);
const TCHAR* (*D3D_init)(HWND ahwnd, int, int w_w, int h_h, int depth, int *freq, int mmult);
bool (*D3D_alloctexture)(int, int, int);
void(*D3D_refresh)(int);
bool(*D3D_renderframe)(int, int,bool);
void(*D3D_showframe)(int);
void(*D3D_showframe_special)(int, int);
uae_u8* (*D3D_locktexture)(int, int*, int*, bool);
void (*D3D_unlocktexture)(int, int, int);
void (*D3D_flushtexture)(int, int miny, int maxy);
void (*D3D_guimode)(int, int);
HDC (*D3D_getDC)(int, HDC hdc);
int (*D3D_isenabled)(int);
void (*D3D_clear)(int);
int (*D3D_canshaders)(void);
int (*D3D_goodenough)(void);
bool (*D3D_setcursor)(int, int x, int y, int width, int height, bool visible, bool noscale);
uae_u8* (*D3D_setcursorsurface)(int, int *pitch);
float (*D3D_getrefreshrate)(int);
void(*D3D_restore)(int, bool);
void(*D3D_resize)(int, int);
void (*D3D_change)(int, int);
bool(*D3D_getscalerect)(int, float *mx, float *my, float *sx, float *sy);
bool(*D3D_run)(int);
int(*D3D_debug)(int, int);
void(*D3D_led)(int, int, int);
bool(*D3D_getscanline)(int*, bool*);

static HMODULE hd3d11, hdxgi, hd3dcompiler, dwmapi;

static int d3d11_feature_level;

static struct gfx_filterdata *filterd3d;
static int filterd3didx;
static int leds[LED_MAX];
static int debugcolors;
static bool cannoclear;
static bool noclear;
static int clearcnt;
static int slicecnt;

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

#define SHADERTYPE_BEFORE 1
#define SHADERTYPE_AFTER 2
#define SHADERTYPE_MIDDLE 3
#define SHADERTYPE_MASK_BEFORE 3
#define SHADERTYPE_MASK_AFTER 4
#define SHADERTYPE_POST 10

struct shadertex
{
	ID3D11Texture2D *tex;
	ID3D11ShaderResourceView *rv;
	ID3D11RenderTargetView *rt;
};

#define MAX_TECHNIQUE_LAYOUTS 8
struct shaderdata11
{
	int type;
	bool psPreProcess;
	int worktex_width;
	int worktex_height;
	int targettex_width;
	int targettex_height;
	ID3D11ShaderResourceView *masktexturerv;
	ID3D11Texture2D *masktexture;
	struct shadertex lpWorkTexture1;
	struct shadertex lpWorkTexture2;
	struct shadertex lpTempTexture;
	ID3D11Texture3D *lpHq2xLookupTexture;
	ID3D11ShaderResourceView *lpHq2xLookupTexturerv;
	int masktexture_w, masktexture_h;
	ID3DX11Effect *effect;
	ID3DX11EffectScalarVariable *framecounterHandle;
	ID3D11InputLayout *layouts[MAX_TECHNIQUE_LAYOUTS];
	ID3D11Buffer *indexBuffer;
	ID3D11Buffer *vertexBuffer;
	D3D11_VIEWPORT viewport;

	// Technique stuff
	ID3DX11EffectTechnique *m_PreprocessTechnique1EffectHandle;
	int PreprocessTechnique1EffectIndex;
	ID3DX11EffectTechnique *m_PreprocessTechnique2EffectHandle;
	int PreprocessTechnique2EffectIndex;
	ID3DX11EffectTechnique *m_CombineTechniqueEffectHandle;
	int CombineTechniqueEffectIndex;
	// Matrix Handles
	ID3DX11EffectMatrixVariable *m_MatWorldEffectHandle;
	ID3DX11EffectMatrixVariable *m_MatViewEffectHandle;
	ID3DX11EffectMatrixVariable *m_MatProjEffectHandle;
	ID3DX11EffectMatrixVariable *m_MatWorldViewEffectHandle;
	ID3DX11EffectMatrixVariable *m_MatViewProjEffectHandle;
	ID3DX11EffectMatrixVariable *m_MatWorldViewProjEffectHandle;
	// Vector Handles
	ID3DX11EffectVectorVariable *m_SourceDimsEffectHandle;
	ID3DX11EffectVectorVariable *m_InputDimsEffectHandle;
	ID3DX11EffectVectorVariable *m_TargetDimsEffectHandle;
	ID3DX11EffectVectorVariable *m_TexelSizeEffectHandle;
	// Texture Handles
	ID3DX11EffectShaderResourceVariable *m_SourceTextureEffectHandle;
	ID3DX11EffectShaderResourceVariable *m_WorkingTexture1EffectHandle;
	ID3DX11EffectShaderResourceVariable *m_WorkingTexture2EffectHandle;
	ID3DX11EffectShaderResourceVariable *m_Hq2xLookupTextureHandle;

	ID3DX11EffectStringVariable *m_strName;
	ID3DX11EffectScalarVariable *m_scale;
	TCHAR loadedshader[256];
};
#define MAX_SHADERS (2 * MAX_FILTERSHADERS + 2)
#define SHADER_POST 0

#define VERTEXCOUNT 4
#define INDEXCOUNT 6

struct d3d11sprite
{
	ID3D11Texture2D *texture;
	ID3D11ShaderResourceView *texturerv;
	ID3D11VertexShader *vertexshader;
	ID3D11PixelShader *pixelshader;
	ID3D11InputLayout *layout;
	ID3D11Buffer *vertexbuffer, *indexbuffer;
	ID3D11Buffer *matrixbuffer;
	int width, height;
	float x, y;
	float outwidth, outheight;
	bool enabled;
	bool alpha;
	bool bilinear;
};

struct d3d11struct
{
	IDXGISwapChain1 *m_swapChain;
	ID3D11Device *m_device;
	ID3D11DeviceContext *m_deviceContext;
	ID3D11RenderTargetView *m_renderTargetView;
	ID3D11RasterizerState *m_rasterState;
	D3DXMATRIX m_orthoMatrix;
	D3DXMATRIX m_worldMatrix;
	D3DXMATRIX m_viewMatrix;

	D3DXMATRIX m_matPreProj;
	D3DXMATRIX m_matPreView;
	D3DXMATRIX m_matPreWorld;
	D3DXMATRIX m_matProj;
	D3DXMATRIX m_matView;
	D3DXMATRIX m_matWorld;
	D3DXMATRIX m_matProj2;
	D3DXMATRIX m_matView2;
	D3DXMATRIX m_matWorld2;
	D3DXMATRIX m_matProj_out;
	D3DXMATRIX m_matView_out;
	D3DXMATRIX m_matWorld_out;

	D3D11_VIEWPORT viewport;
	ID3D11Buffer *m_vertexBuffer, *m_indexBuffer;
	ID3D11Buffer *m_matrixBuffer;
	int m_screenWidth, m_screenHeight;
	int m_bitmapWidth, m_bitmapHeight;
	int m_bitmapWidth2, m_bitmapHeight2;
	int m_bitmapWidthX, m_bitmapHeightX;
	int index_buffer_bytes;
	float m_positionX, m_positionY, m_positionZ;
	float m_rotationX, m_rotationY, m_rotationZ;
	ID3D11ShaderResourceView *texture2drv;
	ID3D11ShaderResourceView *sltexturerv;
	ID3D11Texture2D *texture2d, *texture2dstaging;
	ID3D11Texture2D *sltexture;
	ID3D11Texture2D *screenshottexture;
	ID3D11VertexShader *m_vertexShader;
	ID3D11PixelShader *m_pixelShader, *m_pixelShaderSL, *m_pixelShaderMask;
	ID3D11SamplerState *m_sampleState_point_clamp, *m_sampleState_linear_clamp;
	ID3D11SamplerState *m_sampleState_point_wrap, *m_sampleState_linear_wrap;
	ID3D11InputLayout *m_layout;
	ID3D11BlendState *m_alphaEnableBlendingState;
	ID3D11BlendState *m_alphaDisableBlendingState;
	struct shadertex lpPostTempTexture;
	int texturelocked;
	DXGI_FORMAT scrformat;
	DXGI_FORMAT texformat;
	DXGI_FORMAT intformat;
	bool m_tearingSupport;
	int dmult, dmultx;
	int xoffset, yoffset;
	float xmult, ymult;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC  fsSwapChainDesc;
	IDXGIOutput *outputAdapter;
	HWND ahwnd;
	int fsmode;
	bool fsresizedo;
	bool fsmodechange;
	bool invalidmode;
	int vblankintervals;
	bool blackscreen;
	int framecount;
	UINT syncinterval;
	float vblank;
	DWM_FRAME_COUNT lastframe;
	int frames_since_init;
	bool resizeretry;
	bool d3dinit_done;

	struct d3d11sprite osd;
	struct d3d11sprite hwsprite;
	struct d3d11sprite mask2texture;
	struct d3d11sprite mask2textureleds[9], mask2textureled_power_dim;
	int mask2textureledoffsets[9 * 2];
	struct d3d11sprite blanksprite;

	float mask2texture_w, mask2texture_h, mask2texture_ww, mask2texture_wh;
	float mask2texture_wwx, mask2texture_hhx, mask2texture_minusx, mask2texture_minusy;
	float mask2texture_multx, mask2texture_multy, mask2texture_offsetw;
	RECT mask2rect;

	IDXGISurface1 *hdc_surface;
	HANDLE filenotificationhandle;

	RECT sr2, dr2, zr2;
	int guimode;
	int delayedfs;
	int device_errors;
	bool delayedrestore;
	bool reloadshaders;
	int ledwidth, ledheight;
	int statusbar_hx, statusbar_vx;

	int cursor_offset_x, cursor_offset_y, cursor_offset2_x, cursor_offset2_y;
	float cursor_x, cursor_y;
	bool cursor_v, cursor_scale;

	struct shaderdata11 shaders[MAX_SHADERS];
	ID3DX11EffectTechnique *technique;
	ID3DX11EffectPass *effectpass;

#ifndef NDEBUG
	ID3D11InfoQueue *m_debugInfoQueue;
	ID3D11Debug *m_debug;
#endif
};

#define NUMVERTICES 8

struct TLVERTEX {
	D3DXVECTOR3 position;       // vertex position
	D3DCOLOR    diffuse;
	D3DXVECTOR2 texcoord;       // texture coords
};


struct VertexType
{
	D3DXVECTOR3 position;
	D3DXVECTOR2 texture;
	D3DXVECTOR2 sltexture;
};

struct MatrixBufferType
{
	D3DXMATRIX world;
	D3DXMATRIX view;
	D3DXMATRIX projection;
};

static struct d3d11struct d3d11data[MAX_AMIGAMONITORS];

typedef HRESULT (WINAPI* CREATEDXGIFACTORY1)(REFIID riid, void **ppFactory);
typedef HRESULT (WINAPI* D3DCOMPILEFROMFILE)(LPCWSTR pFileName,
	CONST D3D_SHADER_MACRO* pDefines,
	ID3DInclude* pInclude,
	LPCSTR pEntrypoint,
	LPCSTR pTarget,
	UINT Flags1,
	UINT Flags2,
	ID3DBlob** ppCode,
	ID3DBlob** ppErrorMsgs);
typedef HRESULT(WINAPI* D3DCOMPILE)(LPCVOID pSrcData,
	SIZE_T SrcDataSize,
	LPCSTR pSourceName,
	CONST D3D_SHADER_MACRO* pDefines,
	ID3DInclude* pInclude,
	LPCSTR pEntrypoint,
	LPCSTR pTarget,
	UINT Flags1,
	UINT Flags2,
	ID3DBlob** ppCode,
	ID3DBlob** ppErrorMsgs);
typedef HRESULT (WINAPI* D3DCOMPILE2)(LPCVOID pSrcData,
	SIZE_T SrcDataSize,
	LPCSTR pSourceName,
	CONST D3D_SHADER_MACRO* pDefines,
	ID3DInclude* pInclude,
	LPCSTR pEntrypoint,
	LPCSTR pTarget,
	UINT Flags1,
	UINT Flags2,
	UINT SecondaryDataFlags,
	LPCVOID pSecondaryData,
	SIZE_T SecondaryDataSize,
	ID3DBlob** ppCode,
	ID3DBlob** ppErrorMsgs);
typedef HRESULT(WINAPI *D3DREFLECT)(LPCVOID pSrcData, SIZE_T SrcDataSize, REFIID pInterface, void **ppReflector);
typedef HRESULT(WINAPI *D3DGETBLOBPART)(LPCVOID pSrcData, SIZE_T SrcDataSize, D3D_BLOB_PART Part, UINT Flags, ID3DBlob** ppPart);
typedef HRESULT(WINAPI *DWMGETCOMPOSITIONTIMINGINFO)(HWND hwnd, DWM_TIMING_INFO *pTimingInfo);

static PFN_D3D11_CREATE_DEVICE pD3D11CreateDevice;
static CREATEDXGIFACTORY1 pCreateDXGIFactory1;
static DWMGETCOMPOSITIONTIMINGINFO pDwmGetCompositionTimingInfo;
D3DCOMPILE ppD3DCompile;
D3DCOMPILE2 ppD3DCompile2;
D3DCOMPILEFROMFILE pD3DCompileFromFile;
D3DREFLECT pD3DReflect;
D3DGETBLOBPART pD3DGetBlobPart;

static int isfs(struct d3d11struct *d3d)
{
	int fs = isfullscreen();
	if (fs > 0 && d3d->guimode)
		return -1;
	return fs;
}

static void TurnOnAlphaBlending(struct d3d11struct *d3d)
{
	float blendFactor[4];

	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn on the alpha blending.
	d3d->m_deviceContext->OMSetBlendState(d3d->m_alphaEnableBlendingState, blendFactor, 0xffffffff);
}

static void TurnOffAlphaBlending(struct d3d11struct *d3d)
{
	float blendFactor[4];

	// Setup the blend factor.
	blendFactor[0] = 0.0f;
	blendFactor[1] = 0.0f;
	blendFactor[2] = 0.0f;
	blendFactor[3] = 0.0f;

	// Turn off the alpha blending.
	d3d->m_deviceContext->OMSetBlendState(d3d->m_alphaDisableBlendingState, blendFactor, 0xffffffff);
}

#define D3DX_DEFAULT ((UINT) -1)

static bool psEffect_ParseParameters(struct d3d11struct *d3d, ID3DX11Effect *effect, struct shaderdata11 *s, char *fxname, char *data, UINT flags1)
{
	HRESULT hr = S_OK;
	// Look at parameters for semantics and annotations that we know how to interpret

	if (effect == NULL || !effect->IsValid())
		return false;

	D3DX11_EFFECT_DESC effectDesc;
	hr = effect->GetDesc(&effectDesc);
	if (FAILED(hr))
		return false;

	if (!effectDesc.Techniques) {
		write_log(_T("D3D11 No techniques found!\n"));
		return false;
	}

	ID3DX11EffectVariable *v = effect->GetVariableByName("framecounter");
	if (v && v->IsValid()) {
		s->framecounterHandle = v->AsScalar();
	}

	for (UINT iParam = 0; iParam < effectDesc.GlobalVariables; iParam++) {
		ID3DX11EffectVariable *hParam;
		D3DX11_EFFECT_VARIABLE_DESC ParamDesc;

		hParam = effect->GetVariableByIndex(iParam);
		if (!hParam->IsValid())
			continue;
		hr = hParam->GetDesc(&ParamDesc);

		TCHAR *name = au(ParamDesc.Name);
		TCHAR *semantic = au(ParamDesc.Semantic);
		write_log(_T("FX Flags=%08x Ans=%d Name='%s' Semantic='%s'\n"), ParamDesc.Flags, ParamDesc.Annotations, name, semantic);
		xfree(semantic);
		xfree(name);

		if (ParamDesc.Semantic) {
			if (strcmpi(ParamDesc.Semantic, "world") == 0)
				s->m_MatWorldEffectHandle = hParam->AsMatrix();
			else if (strcmpi(ParamDesc.Semantic, "view") == 0)
				s->m_MatViewEffectHandle = hParam->AsMatrix();
			else if (strcmpi(ParamDesc.Semantic, "projection") == 0)
				s->m_MatProjEffectHandle = hParam->AsMatrix();
			else if (strcmpi(ParamDesc.Semantic, "worldview") == 0)
				s->m_MatWorldViewEffectHandle = hParam->AsMatrix();
			else if (strcmpi(ParamDesc.Semantic, "viewprojection") == 0)
				s->m_MatViewProjEffectHandle = hParam->AsMatrix();
			else if (strcmpi(ParamDesc.Semantic, "worldviewprojection") == 0)
				s->m_MatWorldViewProjEffectHandle = hParam->AsMatrix();

			if (strcmpi(ParamDesc.Semantic, "sourcedims") == 0)
				s->m_SourceDimsEffectHandle = hParam->AsVector();
			if (strcmpi(ParamDesc.Semantic, "inputdims") == 0)
				s->m_InputDimsEffectHandle = hParam->AsVector();
			if (strcmpi(ParamDesc.Semantic, "targetdims") == 0)
				s->m_TargetDimsEffectHandle = hParam->AsVector();
			else if (strcmpi(ParamDesc.Semantic, "texelsize") == 0)
				s->m_TexelSizeEffectHandle = hParam->AsVector();

			if (strcmpi(ParamDesc.Semantic, "SCALING") == 0)
				s->m_scale = hParam->AsScalar();

			if (strcmpi(ParamDesc.Semantic, "SOURCETEXTURE") == 0)
				s->m_SourceTextureEffectHandle = hParam->AsShaderResource();
			if (strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE") == 0)
				s->m_WorkingTexture1EffectHandle = hParam->AsShaderResource();
			if (strcmpi(ParamDesc.Semantic, "WORKINGTEXTURE1") == 0)
				s->m_WorkingTexture2EffectHandle = hParam->AsShaderResource();
			if (strcmpi(ParamDesc.Semantic, "HQ2XLOOKUPTEXTURE") == 0)
				s->m_Hq2xLookupTextureHandle = hParam->AsShaderResource();

			ID3DX11EffectStringVariable *pstrTechnique = NULL;
			LPCSTR name;
			if (strcmpi(ParamDesc.Semantic, "COMBINETECHNIQUE") == 0) {
				pstrTechnique = hParam->AsString();
				pstrTechnique->GetString(&name);
				s->m_CombineTechniqueEffectHandle = effect->GetTechniqueByName(name);
			} else if (strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE") == 0) {
				pstrTechnique = hParam->AsString();
				pstrTechnique->GetString(&name);
				s->m_PreprocessTechnique1EffectHandle = effect->GetTechniqueByName(name);
			} else if (strcmpi(ParamDesc.Semantic, "PREPROCESSTECHNIQUE1") == 0) {
				pstrTechnique = hParam->AsString();
				pstrTechnique->GetString(&name);
				s->m_PreprocessTechnique2EffectHandle = effect->GetTechniqueByName(name);
			} else if (strcmpi(ParamDesc.Semantic, "NAME") == 0) {
				s->m_strName = hParam->AsString();
			}
			if (pstrTechnique)
				pstrTechnique->Release();
		}

		LPCSTR pstrFunctionHandle = NULL;
		LPCSTR pstrTarget = NULL;
		LPCSTR pstrTextureType = NULL;
		INT Width = D3DX_DEFAULT;
		INT Height = D3DX_DEFAULT;
		INT Depth = D3DX_DEFAULT;

		for (UINT iAnnot = 0; iAnnot < ParamDesc.Annotations; iAnnot++) {
			ID3DX11EffectVariable *hAnnot = hParam->GetAnnotationByIndex(iAnnot);
			D3DX11_EFFECT_VARIABLE_DESC AnnotDesc;
			ID3DX11EffectStringVariable *pstrName = NULL;

			hr = hAnnot->GetDesc(&AnnotDesc);
			if (FAILED(hr)) {
				write_log(_T("Direct3D11: GetParameterDescAnnot(%d) failed: %s\n"), iAnnot, hr);
				continue;
			}

			TCHAR *name = au(AnnotDesc.Name);
			TCHAR *semantic = au(AnnotDesc.Semantic);
			write_log(_T("Effect Annotation Name='%s' Semantic='%s'\n"), name, semantic);
			xfree(semantic);
			xfree(name);

			if (strcmpi(AnnotDesc.Name, "name") == 0) {
				pstrName = hAnnot->AsString();
			} else if (strcmpi(AnnotDesc.Name, "function") == 0) {
				hAnnot->AsString()->GetString(&pstrFunctionHandle);
			} else if (strcmpi(AnnotDesc.Name, "target") == 0) {
				hAnnot->AsString()->GetString(&pstrTarget);
			} else if (strcmpi(AnnotDesc.Name, "width") == 0) {
				hAnnot->AsScalar()->GetInt(&Width);
			} else if (strcmpi(AnnotDesc.Name, "height") == 0) {
				hAnnot->AsScalar()->GetInt(&Height);
			} else if (strcmpi(AnnotDesc.Name, "depth") == 0) {
				hAnnot->AsScalar()->GetInt(&Depth);
			} else if (strcmpi(AnnotDesc.Name, "type") == 0) {
				hAnnot->AsString()->GetString(&pstrTextureType);
			}
			if (pstrName)
				pstrName->Release();
			hAnnot->Release();
		}

		if (pstrFunctionHandle != NULL) {
#if 0
			if (pstrTarget == NULL || strcmp(pstrTarget, "tx_1_1"))
				pstrTarget = "tx_1_0";

			ID3DBlob *errors = NULL;
			ID3DBlob *code = NULL;
			TCHAR *n = au(pstrFunctionHandle);
			hr = ppD3DCompile2(data, strlen(data), fxname, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, pstrFunctionHandle, pstrTarget, flags1, 0, 0, NULL, 0, &code, &errors);
			if (SUCCEEDED(hr)) {

				if (Width == D3DX_DEFAULT)
					Width = 64;
				if (Height == D3DX_DEFAULT)
					Height = 64;
				if (Depth == D3DX_DEFAULT)
					Depth = 64;

				ID3D11Resource *res = NULL;
				D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
				memset(&srvDesc, 0, sizeof srvDesc);
				if (pstrTextureType && strcmpi(pstrTextureType, "volume") == 0) {

				} else if (pstrTextureType && strcmpi(pstrTextureType, "cube") == 0) {

				} else {
					ID3D11Texture2D *texture;
					D3D11_TEXTURE2D_DESC desc;
					memset(&desc, 0, sizeof desc);
					desc.Width = Width;
					desc.Height = Height;
					desc.MipLevels = 1;
					desc.ArraySize = 1;
					desc.Format = d3d->scrformat;
					desc.SampleDesc.Count = 1;
					desc.SampleDesc.Quality = 0;
					desc.Usage = D3D11_USAGE_DEFAULT;
					desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
					desc.CPUAccessFlags = 0;
					hr = d3d->m_device->CreateTexture2D(&desc, NULL, &texture);
					if (SUCCEEDED(hr)) {
						res = texture;
						srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
					} else {
						write_log(_T("CreateTexture2D ('%s' %d%*%d) failed: %08x\n"), n, Width, Height, hr);
					}
				}
				if (res) {
					ID3D11ShaderResourceView *rv = NULL;
					srvDesc.Texture2D.MostDetailedMip = 0;
					srvDesc.Texture2D.MipLevels = 1;
					srvDesc.Format = d3d->scrformat;
					hr = d3d->m_device->CreateShaderResourceView(res, &srvDesc, &rv);
					if (SUCCEEDED(hr)) {
						hParam->AsShaderResource()->SetResource(rv);
						rv->Release();
					} else {
						write_log(_T("CreateShaderResourceView ('%s' %dx%d) failed: %08x\n"), n, Width, Height, hr);
					}
					res->Release();
				}
				
			} else {
				void *p = errors->GetBufferPointer();
				TCHAR *s = au((char*)p);
				write_log(_T("Effect compiler '%s' errors:\n%s\n"), n, s);
				xfree(s);
			}
			xfree(n);
			if (errors)
				errors->Release();
#endif
		}

		hParam->Release();
	}

	if (!s->m_CombineTechniqueEffectHandle && effectDesc.Techniques > 0) {
		s->m_CombineTechniqueEffectHandle = effect->GetTechniqueByIndex(0);
	}

	return true;
}

static bool allocfxdata(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	struct TLVERTEX *vertices[NUMVERTICES] = { 0 };
	D3D11_BUFFER_DESC vertexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData;
	D3D11_BUFFER_DESC indexBufferDesc;
	D3D11_SUBRESOURCE_DATA indexData;
	uae_u32 indices[INDEXCOUNT * 2];

	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(struct TLVERTEX) * NUMVERTICES;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	HRESULT hr = d3d->m_device->CreateBuffer(&vertexBufferDesc, &vertexData, &s->vertexBuffer);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateBuffer(fxvertex) %08x\n"), hr);
		return false;
	}

	static const uae_u16 indexes[INDEXCOUNT * 2] = {
		2, 1, 0, 2, 3, 1,
		6, 5, 5, 6, 7, 5
	};
	// Load the index array with data.
	for (int i = 0; i < INDEXCOUNT * 2; i++)
	{
		if (d3d->index_buffer_bytes == 4)
			((uae_u32*)indices)[i] = indexes[i];
		else
			((uae_u16*)indices)[i] = indexes[i];
	}

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = d3d->index_buffer_bytes * INDEXCOUNT;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	hr = d3d->m_device->CreateBuffer(&indexBufferDesc, &indexData, &s->indexBuffer);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateBuffer(index) %08x\n"), hr);
		return false;
	}

	return true;
}

static bool createfxvertices(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	HRESULT hr;
	struct TLVERTEX *vertices;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	float sizex, sizey;

	sizex = 1.0f;
	sizey = 1.0f;
	hr = d3d->m_deviceContext->Map(s->vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11DeviceContext map(fxvertex) %08x\n"), hr);
		return false;
	}
	vertices = (struct TLVERTEX*)mappedResource.pData;

	//Setup vertices

	vertices[0].position.x = -0.5f; vertices[0].position.y = -0.5f;
	vertices[0].diffuse = 0xFFFFFFFF;
	vertices[0].texcoord.x = 0.0f; vertices[0].texcoord.y = sizey;

	vertices[1].position.x = -0.5f; vertices[1].position.y = 0.5f;
	vertices[1].diffuse = 0xFFFFFFFF;
	vertices[1].texcoord.x = 0.0f; vertices[1].texcoord.y = 0.0f;

	vertices[2].position.x = 0.5f; vertices[2].position.y = -0.5f;
	vertices[2].diffuse = 0xFFFFFFFF;
	vertices[2].texcoord.x = sizex; vertices[2].texcoord.y = sizey;

	vertices[3].position.x = 0.5f; vertices[3].position.y = 0.5f;
	vertices[3].diffuse = 0xFFFFFFFF;
	vertices[3].texcoord.x = sizex; vertices[3].texcoord.y = 0.0f;

	// Additional vertices required for some PS effects
	vertices[4].position.x = 0.0f; vertices[4].position.y = 0.0f;
	vertices[4].diffuse = 0xFFFFFF00;
	vertices[4].texcoord.x = 0.0f; vertices[4].texcoord.y = 1.0f;
	vertices[5].position.x = 0.0f; vertices[5].position.y = 1.0f;
	vertices[5].diffuse = 0xFFFFFF00;
	vertices[5].texcoord.x = 0.0f; vertices[5].texcoord.y = 0.0f;
	vertices[6].position.x = 1.0f; vertices[6].position.y = 0.0f;
	vertices[6].diffuse = 0xFFFFFF00;
	vertices[6].texcoord.x = 1.0f; vertices[6].texcoord.y = 1.0f;
	vertices[7].position.x = 1.0f; vertices[7].position.y = 1.0f;
	vertices[7].diffuse = 0xFFFFFF00;
	vertices[7].texcoord.x = 1.0f; vertices[7].texcoord.y = 0.0f;

	d3d->m_deviceContext->Unmap(s->vertexBuffer, 0);

	return true;
}

static int createfxlayout(struct d3d11struct *d3d, struct shaderdata11 *s, ID3DX11EffectTechnique *technique, int idx)
{
	HRESULT hr;

	if (!technique || !technique->IsValid())
		return -1;

	D3DX11_TECHNIQUE_DESC techDesc;
	hr = technique->GetDesc(&techDesc);
	for (int pass = 0; pass < techDesc.Passes && idx < MAX_TECHNIQUE_LAYOUTS; pass++) {
		ID3DX11EffectPass *effectpass = technique->GetPassByIndex(pass);
		D3DX11_PASS_SHADER_DESC effectVsDesc;
		D3DX11_EFFECT_SHADER_DESC effectVsDesc2;

		hr = effectpass->GetVertexShaderDesc(&effectVsDesc);
		hr = effectVsDesc.pShaderVariable->GetShaderDesc(effectVsDesc.ShaderIndex, &effectVsDesc2);
		const void *vsCodePtr = effectVsDesc2.pBytecode;
		unsigned vsCodeLen = effectVsDesc2.BytecodeLength;

		D3D11_INPUT_ELEMENT_DESC polygonLayout[3];
		int pidx = 0;

		// Create the vertex input layout description.
		// This setup needs to match the VertexType stucture in the ModelClass and in the shader.
		polygonLayout[pidx].SemanticName = "POSITION";
		polygonLayout[pidx].SemanticIndex = 0;
		polygonLayout[pidx].Format = DXGI_FORMAT_R32G32B32_FLOAT;
		polygonLayout[pidx].InputSlot = 0;
		polygonLayout[pidx].AlignedByteOffset = 0;
		polygonLayout[pidx].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		polygonLayout[pidx].InstanceDataStepRate = 0;
		pidx++;

		polygonLayout[pidx].SemanticName = "DIFFUSE";
		polygonLayout[pidx].SemanticIndex = 0;
		polygonLayout[pidx].Format = DXGI_FORMAT_R32_UINT;
		polygonLayout[pidx].InputSlot = 0;
		polygonLayout[pidx].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		polygonLayout[pidx].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		polygonLayout[pidx].InstanceDataStepRate = 0;
		pidx++;

		polygonLayout[pidx].SemanticName = "TEXCOORD";
		polygonLayout[pidx].SemanticIndex = 0;
		polygonLayout[pidx].Format = DXGI_FORMAT_R32G32_FLOAT;
		polygonLayout[pidx].InputSlot = 0;
		polygonLayout[pidx].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
		polygonLayout[pidx].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
		polygonLayout[pidx].InstanceDataStepRate = 0;
		pidx++;

		hr = d3d->m_device->CreateInputLayout(polygonLayout, pidx, vsCodePtr, vsCodeLen, &s->layouts[idx]);
		if (FAILED(hr)) {
			write_log(_T("technique CreateInputLayout %08x %d\n"), hr, idx);
		}
		idx++;
	}
	return idx;
}

static bool psEffect_hasPreProcess(struct shaderdata11 *s) { return s->m_PreprocessTechnique1EffectHandle != 0; }
static bool psEffect_hasPreProcess2(struct shaderdata11 *s) { return s->m_PreprocessTechnique2EffectHandle != 0; }

static bool isws(char c)
{
	return c == '\n' || c == '\r' || c == '\t' || c == ' ';
}
static bool islf(char c)
{
	return c == '\n' || c == '\r' || c == ';';
}

static bool fxneedconvert(char *s)
{
	char *t = s;
	int len = strlen(s);
	while (len > 0) {
		if (t != s && isws(t[-1]) && (!strnicmp(t, "technique10", 11) || !strnicmp(t, "technique11", 11)) && isws(t[11])) {
			return false;
		}
		len--;
		t++;
	}
	return true;
}

static void fxspecials(char *s, char *dst)
{
	char *t = s;
	char *d = dst;
	*d = 0;
	int len = strlen(s);
	while (len > 0) {
		bool found = false;
		if (t != s && !strnicmp(t, "minfilter", 9) && (isws(t[9]) || t[9] == '=') && isws(t[-1])) {
			found = true;
			t += 10;
			len -= 10;
			while (!islf(*t) && len > 0) {
				if (!strnicmp(t, "point", 5)) {
					strcpy(d, "Filter=MIN_MAG_MIP_POINT");
					d += strlen(d);
					write_log("FX: 'minfilter' -> 'Filter=MIN_MAG_MIP_POINT'\n");
				}
				if (!strnicmp(t, "linear", 6)) {
					strcpy(d, "Filter=MIN_MAG_MIP_LINEAR");
					d += strlen(d);
					write_log("FX: 'minfiler' -> 'Filter=MIN_MAG_MIP_LINEAR'\n");
				}
				t++;
				len--;
			}
		}
		if (!found) {
			*d++ = *t++;
			len--;
		}
	}
	*d = 0;
}

static void fxconvert(char *s, char *dst, const char **convert1, const char **convert2)
{
	char *t = s;
	char *d = dst;
	int len = strlen(s);
	while (len > 0) {
		bool found = false;
		for (int i = 0; convert1[i]; i++) {
			int slen = strlen(convert1[i]);
			int dlen = strlen(convert2[i]);
			if (len > slen && !strnicmp(t, convert1[i], slen)) {
				if ((t == s || isws(t[-1])) && isws(t[slen])) {
					memcpy(d, convert2[i], dlen);
					t += slen;
					d += dlen;
					len -= slen;
					found = true;
					write_log("FX: '%s' -> '%s'\n", convert1[i], convert2[i]);
				}
			}
		}
		if (!found) {
			*d++ = *t++;
			len--;
		}
	}
	*d = 0;
}

static void fxremoveline(char *s, char *dst, const char **lines)
{
	char *t = s;
	char *d = dst;
	int len = strlen(s);
	while (len > 0) {
		bool found = false;
		for (int i = 0; lines[i]; i++) {
			int slen = strlen(lines[i]);
			if (len > slen && !strnicmp(t, lines[i], slen)) {
				d--;
				while (d != dst) {
					if (islf(*d)) {
						d++;
						break;
					}
					d--;
				}
				while (*t && len > 0) {
					if (islf(*t)) {
						t++;
						len--;
						break;
					}
					t++;
					len--;
				}
				found = true;
				write_log("FX: '%s' line removed\n", lines[i]);
			}
		}
		if (!found) {
			*d++ = *t++;
			len--;
		}
	}
	*d = 0;
}


static bool psEffect_LoadEffect(struct d3d11struct *d3d, const TCHAR *shaderfile, struct shaderdata11 *s, int num)
{
	int ret = 0;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH];
	TCHAR tmp2[MAX_DPATH], tmp3[MAX_DPATH];
	static int first;
	int canusefile = 0, existsfile = 0;
	bool plugin_path;
	struct zfile *z = NULL;
	char *fx1 = NULL;
	char *fx2 = NULL;
	char *name = NULL;

	if (!pD3DCompileFromFile || !ppD3DCompile) {
		write_log(_T("D3D11 No shader compiler available (D3DCompiler_46.dll or D3DCompiler_47.dll).\n"));
		return false;
	}

	DWORD dwShaderFlags = D3DCOMPILE_ENABLE_BACKWARDS_COMPATIBILITY;
#ifndef NDEBUG
	//dwShaderFlags |= D3DCOMPILE_DEBUG;
	//Disable optimizations to further improve shader debugging
	//dwShaderFlags |= D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

	GetCurrentDirectory(MAX_DPATH, tmp3);
	name = ua(shaderfile);

	plugin_path = get_plugin_path(tmp2, sizeof tmp2 / sizeof(TCHAR), _T("filtershaders\\direct3d"));
	_tcscpy(tmp, tmp2);

	d3d->frames_since_init = 0;
	if (!d3d->filenotificationhandle)
		d3d->filenotificationhandle = FindFirstChangeNotification(tmp, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE);

	_tcscat(tmp, shaderfile);
	write_log(_T("Direct3D11: Attempting to load '%s'\n"), tmp);
	_tcscpy(s->loadedshader, shaderfile);

	ID3DX11Effect *g_pEffect = NULL;
	ID3DBlob *errors = NULL;

	z = zfile_fopen(tmp, _T("rb"));
	if (!z) {
		write_log(_T("Failed to open '%s'\n"), tmp);
		goto end;
	}
	int size = zfile_size(z);
	fx1 = xcalloc(char, size * 4);
	fx2 = xcalloc(char, size * 4);
	if (zfile_fread(fx1, 1, size, z) != size) {
		write_log(_T("Failed to read '%s'\n"), tmp);
		goto end;

	}
	zfile_fclose(z);
	z = NULL;

	char *fx = fx1;
	if (fxneedconvert(fx1)) {
		static const char *converts1[] = { "technique", "vs_3_0", "vs_2_0", "vs_1_1", "ps_3_0", "ps_2_0", NULL };
		static const char *converts2[] = { "technique10", "vs_4_0_level_9_3", "vs_4_0_level_9_3", "vs_4_0_level_9_3", "ps_4_0_level_9_3", "ps_4_0_level_9_3", NULL };
		fxconvert(fx1, fx2, converts1, converts2);

		static const char *lines[] = { "alphablendenable", "colorwriteenable", "srgbwriteenable", "magfilter", NULL };
		fxremoveline(fx2, fx1, lines);

		fxspecials(fx1, fx2);
		
		fx = fx2;
	}

	SetCurrentDirectory(tmp2);
	hr = D3DX11CompileEffectFromMemory(fx, strlen(fx), name, NULL, D3D_COMPILE_STANDARD_FILE_INCLUDE, dwShaderFlags, 0, d3d->m_device, &g_pEffect, &errors);

#if 0
	hr = D3DX11CompileEffectFromFile(tmp, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, dwShaderFlags, 0, d3d->m_device, &g_pEffect, &errors);
#endif

	if (FAILED(hr)) {
		write_log(_T("Direct3D11: D3DX11CompileEffectFromMemory('%s') failed: %08x\n"), tmp, hr);
		void *p = errors->GetBufferPointer();
		TCHAR *s = au((char*)p);
		write_log(_T("Effect compiler errors:\n%s\n"), s);
		xfree(s);
		goto end;
	}

	if (errors) {
		void *p = errors->GetBufferPointer();
		TCHAR *s = au((char*)p);
		write_log(_T("Effect compiler warnings:\n%s\n"), s);
		errors->Release();
	}

	if (!psEffect_ParseParameters(d3d, g_pEffect, s, name, fx, dwShaderFlags))
		goto end;

	SetCurrentDirectory(tmp3);

	s->effect = g_pEffect;

	int layout = 0;
	s->CombineTechniqueEffectIndex = layout;
	layout = createfxlayout(d3d, s, s->m_CombineTechniqueEffectHandle, layout);
	s->PreprocessTechnique1EffectIndex = layout;
	layout = createfxlayout(d3d, s, s->m_PreprocessTechnique1EffectHandle, layout);
	s->PreprocessTechnique2EffectIndex = layout;
	layout = createfxlayout(d3d, s, s->m_PreprocessTechnique2EffectHandle, layout);

	allocfxdata(d3d, s);
	createfxvertices(d3d, s);

	s->viewport.Width = (float)d3d->m_screenWidth * d3d->dmultx;
	s->viewport.Height = (float)d3d->m_screenHeight * d3d->dmultx;
	s->viewport.MinDepth = 0.0f;
	s->viewport.MaxDepth = 1.0f;
	s->viewport.TopLeftX = 0.0f;
	s->viewport.TopLeftY = 0.0f;

	s->psPreProcess = false;
	if (psEffect_hasPreProcess(s))
		s->psPreProcess = true;

	xfree(fx1);
	xfree(fx2);
	xfree(name);

	return true;

end:
	SetCurrentDirectory(tmp3);
	if (g_pEffect)
		g_pEffect->Release();
	if (errors)
		errors->Release();
	zfile_fclose(z);
	s->loadedshader[0] = 0;
	xfree(fx1);
	xfree(fx2);
	xfree(name);
	return false;
}

static bool psEffect_SetMatrices(D3DXMATRIX *matProj, D3DXMATRIX *matView, D3DXMATRIX *matWorld, struct shaderdata11 *s)
{
	if (s->m_MatWorldEffectHandle) {
		s->m_MatWorldEffectHandle->SetMatrix((float*)matWorld);
	}
	if (s->m_MatViewEffectHandle) {
		s->m_MatViewEffectHandle->SetMatrix((float*)matView);
	}
	if (s->m_MatProjEffectHandle) {
		s->m_MatProjEffectHandle->SetMatrix((float*)matProj);
	}
	if (s->m_MatWorldViewEffectHandle) {
		D3DXMATRIX matWorldView;
		xD3DXMatrixMultiply(&matWorldView, matWorld, matView);
		s->m_MatWorldViewEffectHandle->SetMatrix((float*)&matWorldView);
	}
	if (s->m_MatViewProjEffectHandle) {
		D3DXMATRIX matViewProj;
		xD3DXMatrixMultiply(&matViewProj, matView, matProj);
		s->m_MatViewProjEffectHandle->SetMatrix((float*)&matViewProj);
	}
	if (s->m_MatWorldViewProjEffectHandle) {
		D3DXMATRIX tmp, matWorldViewProj;
		xD3DXMatrixMultiply(&tmp, matWorld, matView);
		xD3DXMatrixMultiply(&matWorldViewProj, &tmp, matProj);
		s->m_MatWorldViewProjEffectHandle->SetMatrix((float*)&matWorldViewProj);
	}
	return true;
}

static void settransform_pre(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	xD3DXMatrixTranslation(&d3d->m_matView, -0.5f / d3d->m_bitmapWidthX, 0.5f / d3d->m_bitmapHeightX, 0.0f);
	// Identity for world
	xD3DXMatrixIdentity(&d3d->m_matWorld);
}

static void settransform(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	xD3DXMatrixTranslation(&d3d->m_matPreView, -0.5f / d3d->m_bitmapWidthX, 0.5f / d3d->m_bitmapHeightX, 0.0f);
	// Identity for world
	xD3DXMatrixIdentity(&d3d->m_matPreWorld);

	if (s)
		psEffect_SetMatrices(&d3d->m_matProj, &d3d->m_matView, &d3d->m_matWorld, s);

	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matProj2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	xD3DXMatrixTranslation(&d3d->m_matView2, 0.5f - 0.5f / d3d->m_bitmapWidthX, 0.5f + 0.5f / d3d->m_bitmapHeightX, 0.0f);

	xD3DXMatrixIdentity(&d3d->m_matWorld2);
}

static void settransform2(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	// Projection is (0,0,0) -> (1,1,1)
	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matPreProj, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);
	// Align texels with pixels
	xD3DXMatrixTranslation(&d3d->m_matPreView, -0.5f / d3d->m_bitmapWidth, 0.5f / d3d->m_bitmapHeight, 0.0f);
	// Identity for world
	xD3DXMatrixIdentity(&d3d->m_matPreWorld);

	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matProj2, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f);

	xD3DXMatrixTranslation(&d3d->m_matView2, 0.5f - 0.5f / d3d->m_bitmapWidth, 0.5f + 0.5f / d3d->m_bitmapHeight, 0.0f);
	xD3DXMatrixIdentity(&d3d->m_matWorld2);
}

static int psEffect_SetTextures(ID3D11Texture2D *lpSourceTex, ID3D11ShaderResourceView *lpSourcerv, struct shaderdata11 *s)
{
	D3DXVECTOR4 fDims, fTexelSize;

	if (!s->m_SourceTextureEffectHandle) {
		write_log(_T("D3D11 Texture with SOURCETEXTURE semantic not found\n"));
		return 0;
	}
	s->m_SourceTextureEffectHandle->SetResource(lpSourcerv);
	if (s->m_WorkingTexture1EffectHandle) {
		s->m_WorkingTexture1EffectHandle->SetResource(s->lpWorkTexture1.rv);
	}
	if (s->m_WorkingTexture2EffectHandle) {
		s->m_WorkingTexture2EffectHandle->SetResource(s->lpWorkTexture2.rv);
	}
	if (s->m_Hq2xLookupTextureHandle) {
		s->m_Hq2xLookupTextureHandle->SetResource(s->lpHq2xLookupTexturerv);
	}
	fDims.x = 256; fDims.y = 256; fDims.z = 1; fDims.w = 1;
	fTexelSize.x = 1; fTexelSize.y = 1; fTexelSize.z = 1; fTexelSize.w = 1;
	if (lpSourceTex) {
		D3D11_TEXTURE2D_DESC desc;
		lpSourceTex->GetDesc(&desc);
		fDims.x = (FLOAT)desc.Width;
		fDims.y = (FLOAT)desc.Height;
	}

	fTexelSize.x = 1.0f / fDims.x;
	fTexelSize.y = 1.0f / fDims.y;

	if (s->m_SourceDimsEffectHandle) {
		s->m_SourceDimsEffectHandle->SetFloatVector((float*)&fDims);
	}
	if (s->m_InputDimsEffectHandle) {
		s->m_InputDimsEffectHandle->SetFloatVector((float*)&fDims);
	}
	if (s->m_TargetDimsEffectHandle) {
		D3DXVECTOR4 fDimsTarget;
		fDimsTarget.x = s->targettex_width;
		fDimsTarget.y = s->targettex_height;
		fDimsTarget.z = 1;
		fDimsTarget.w = 1;
		s->m_TargetDimsEffectHandle->SetFloatVector((float*)&fDimsTarget);
	}
	if (s->m_TexelSizeEffectHandle) {
		s->m_TexelSizeEffectHandle->SetFloatVector((float*)&fTexelSize);
	}
	if (s->framecounterHandle) {
		s->framecounterHandle->SetFloat((float)timeframes);
	}

	return 1;
}

enum psEffect_Pass { psEffect_None, psEffect_PreProcess1, psEffect_PreProcess2, psEffect_Combine };

static bool psEffect_Begin(struct d3d11struct *d3d, enum psEffect_Pass pass, int *pPasses, int *pIndex, struct shaderdata11 *s)
{
	ID3DX11Effect *effect = s->effect;
	D3DX11_TECHNIQUE_DESC desc;
	int idx = 0;

	d3d->technique = NULL;
	switch (pass)
	{
	case psEffect_PreProcess1:
		d3d->technique = s->m_PreprocessTechnique1EffectHandle;
		*pIndex = s->PreprocessTechnique1EffectIndex;
		break;
	case psEffect_PreProcess2:
		d3d->technique = s->m_PreprocessTechnique2EffectHandle;
		*pIndex = s->PreprocessTechnique2EffectIndex;
		break;
	case psEffect_Combine:
		d3d->technique = s->m_CombineTechniqueEffectHandle;
		*pIndex = s->CombineTechniqueEffectIndex;
		break;
	}
	if (!d3d->technique)
		return false;
	HRESULT hr = d3d->technique->GetDesc(&desc);
	if (FAILED(hr)) {
		write_log(_T("technique->GetDesc %08x\n"), hr);
		return false;
	}
	*pPasses = desc.Passes;
	return true;
}

static bool psEffect_BeginPass(struct d3d11struct *d3d, struct shaderdata11 *s, int Pass, int Index)
{
	ID3DX11EffectPass *effectpass = d3d->technique->GetPassByIndex(Pass);
	if (!effectpass->IsValid()) {
		write_log(_T("psEffect_BeginPass pass %d not valid\n"), Pass);
		return false;
	}
	d3d->effectpass = effectpass;
	HRESULT hr = effectpass->Apply(0, d3d->m_deviceContext);
	if (FAILED(hr)) {
		write_log(_T("effectpass->Apply %08x\n"), hr);
	}
	UINT stride = sizeof(TLVERTEX);
	UINT offset = 0;
	d3d->m_deviceContext->IASetVertexBuffers(0, 1, &s->vertexBuffer, &stride, &offset);
	d3d->m_deviceContext->IASetIndexBuffer(s->indexBuffer, d3d->index_buffer_bytes == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);
	d3d->m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	d3d->m_deviceContext->IASetInputLayout(s->layouts[Index + Pass]);
	return true;
}
static bool psEffect_EndPass(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	d3d->technique = NULL;
	d3d->effectpass = NULL;
	return true;
}
static bool psEffect_End(struct d3d11struct *d3d, struct shaderdata11 *s)
{
	return true;
}

static bool processshader(struct d3d11struct *d3d, struct shadertex *st, struct shaderdata11 *s, bool rendertarget)
{
	int uPasses, uPass, uIndex;
	ID3D11RenderTargetView *lpRenderTarget;
	ID3D11RenderTargetView *lpNewRenderTarget;
	struct shadertex *lpWorkTexture;

	d3d->m_deviceContext->RSSetViewports(1, &s->viewport);

	TurnOffAlphaBlending(d3d);
	
	if (!psEffect_SetTextures(st->tex, st->rv, s))
		return NULL;

	if (s->psPreProcess) {

		if (!psEffect_SetMatrices(&d3d->m_matPreProj, &d3d->m_matPreView, &d3d->m_matPreWorld, s))
			return NULL;

		d3d->m_deviceContext->OMGetRenderTargets(1, &lpRenderTarget, NULL);

		lpWorkTexture = &s->lpWorkTexture1;
		lpNewRenderTarget = lpWorkTexture->rt;

		d3d->m_deviceContext->OMSetRenderTargets(1, &lpNewRenderTarget, NULL);
pass2:
		uPasses = 0;
		if (psEffect_Begin(d3d, (lpWorkTexture == &s->lpWorkTexture1) ? psEffect_PreProcess1 : psEffect_PreProcess2, &uPasses, &uIndex, s)) {
			for (uPass = 0; uPass < uPasses; uPass++) {
				if (psEffect_BeginPass(d3d, s, uPass, uIndex)) {
					d3d->m_deviceContext->DrawIndexed(6, 6, 0);
					psEffect_EndPass(d3d, s);
				}
			}
			psEffect_End(d3d, s);
		}

		if (lpRenderTarget)
			d3d->m_deviceContext->OMSetRenderTargets(1, &lpRenderTarget, NULL);
		lpNewRenderTarget = NULL;

		if (psEffect_hasPreProcess2(s) && lpWorkTexture == &s->lpWorkTexture1) {
			lpWorkTexture = &s->lpWorkTexture2;
			goto pass2;
		}
		
		lpRenderTarget = NULL;
	}

	psEffect_SetMatrices(&d3d->m_matProj2, &d3d->m_matView2, &d3d->m_matWorld2, s);

	if (rendertarget) {
		d3d->m_deviceContext->OMGetRenderTargets(1, &lpRenderTarget, NULL);
		d3d->m_deviceContext->OMSetRenderTargets(1, &s->lpTempTexture.rt, NULL);
	}

	TurnOffAlphaBlending(d3d);

	uPasses = 0;
	if (psEffect_Begin(d3d, psEffect_Combine, &uPasses, &uIndex, s)) {
		for (uPass = 0; uPass < uPasses; uPass++) {
			if (!psEffect_BeginPass(d3d, s, uPass, uIndex))
				return NULL;
			d3d->m_deviceContext->DrawIndexed(6, 0, 0);
			psEffect_EndPass(d3d, s);
		}
		psEffect_End(d3d, s);
	}

	if (rendertarget) {

		d3d->m_deviceContext->OMSetRenderTargets(1, &lpRenderTarget, NULL);
	}

	memcpy(st, &s->lpTempTexture, sizeof(struct shadertex));

	return true;
}

static bool UpdateVertexArray(struct d3d11struct *d3d, ID3D11Buffer *vertexbuffer,
	float left, float top, float right, float bottom,
	float slleft, float sltop, float slright, float slbottom)
{
	VertexType* verticesPtr;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	HRESULT result;
	VertexType vertices[4];

	if (!vertexbuffer)
		return false;

	// Load the vertex array with data.
	vertices[0].position = D3DXVECTOR3(left, top, 0.0f);  // Top left.
	vertices[0].texture = D3DXVECTOR2(0.0f, 0.0f);
	vertices[0].sltexture = D3DXVECTOR2(slleft, sltop);

	vertices[1].position = D3DXVECTOR3(right, top, 0.0f);  // Top right.
	vertices[1].texture = D3DXVECTOR2(1.0f, 0.0f);
	vertices[1].sltexture = D3DXVECTOR2(slright, sltop);

	vertices[2].position = D3DXVECTOR3(right, bottom, 0.0f);  // Bottom right.
	vertices[2].texture = D3DXVECTOR2(1.0f, 1.0f);
	vertices[2].sltexture = D3DXVECTOR2(slright, slbottom);

	vertices[3].position = D3DXVECTOR3(left, bottom, 0.0f);  // Bottom left.
	vertices[3].texture = D3DXVECTOR2(0.0f, 1.0f);
	vertices[3].sltexture = D3DXVECTOR2(slleft, slbottom);

	// Lock the vertex buffer so it can be written to.
	result = d3d->m_deviceContext->Map(vertexbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		write_log(_T("ID3D11DeviceContext map(vertex) %08x\n"), result);
		return false;
	}

	// Get a pointer to the data in the vertex buffer.
	verticesPtr = (VertexType*)mappedResource.pData;

	// Copy the data into the vertex buffer.
	memcpy(verticesPtr, (void*)vertices, (sizeof(VertexType) * VERTEXCOUNT));

	// Unlock the vertex buffer.
	d3d->m_deviceContext->Unmap(vertexbuffer, 0);

	return true;
}

static bool UpdateBuffers(struct d3d11struct *d3d)
{
	float left, right, top, bottom;
	int positionX, positionY;
	int bw = d3d->m_bitmapWidth;
	int bh = d3d->m_bitmapHeight;
	int sw = d3d->m_screenWidth;
	int sh = d3d->m_screenHeight;

	positionX = (sw - bw) / 2 + d3d->xoffset;
	positionY = (sh - bh) / 2 + d3d->yoffset;

	// Calculate the screen coordinates of the left side of the bitmap.
	left = (sw + 1) / -2;
	left += positionX;

	// Calculate the screen coordinates of the right side of the bitmap.
	right = left + bw;

	// Calculate the screen coordinates of the top of the bitmap.
	top = (sh + 1) / 2;
	top -= positionY;

	// Calculate the screen coordinates of the bottom of the bitmap.
	bottom = top - bh;

	float slleft = 0;
	float sltop = 0;
	float slright = (float)bw / sw * d3d->xmult;
	float slbottom = (float)bh / sh * d3d->ymult;

	slright = slleft + slright;
	slbottom = sltop + slbottom;

	left *= d3d->xmult;
	right *= d3d->xmult;
	top *= d3d->ymult;
	bottom *= d3d->ymult;

	write_log(_T("-> %f %f %f %f %f %f\n"), left, top, right, bottom, d3d->xmult, d3d->ymult);

	UpdateVertexArray(d3d, d3d->m_vertexBuffer, left, top, right, bottom, slleft, sltop, slright, slbottom);
	return true;
}

static void setupscenecoords(struct d3d11struct *d3d, bool normalrender)
{
	RECT sr, dr, zr;

	if (!normalrender)
		return;

	getfilterrect2(d3d - d3d11data, &dr, &sr, &zr, d3d->m_screenWidth, d3d->m_screenHeight, d3d->m_bitmapWidth / d3d->dmult, d3d->m_bitmapHeight / d3d->dmult, d3d->dmult, d3d->m_bitmapWidth, d3d->m_bitmapHeight);

	if (!memcmp(&sr, &d3d->sr2, sizeof RECT) && !memcmp(&dr, &d3d->dr2, sizeof RECT) && !memcmp(&zr, &d3d->zr2, sizeof RECT)) {
		return;
	}
	if (1) {
		write_log(_T("POS (%d %d %d %d) - (%d %d %d %d)[%d,%d] (%d %d) S=%d*%d B=%d*%d\n"),
			dr.left, dr.top, dr.right, dr.bottom,
			sr.left, sr.top, sr.right, sr.bottom,
			sr.right - sr.left, sr.bottom - sr.top,
			zr.left, zr.top,
			d3d->m_screenWidth, d3d->m_screenHeight,
			d3d->m_bitmapWidth, d3d->m_bitmapHeight);
	}
	d3d->sr2 = sr;
	d3d->dr2 = dr;
	d3d->zr2 = zr;

	float dw = dr.right - dr.left;
	float dh = dr.bottom - dr.top;
	float w = sr.right - sr.left;
	float h = sr.bottom - sr.top;

	int tx = ((dr.right - dr.left) * d3d->m_bitmapWidth) / (d3d->m_screenWidth * 2);
	int ty = ((dr.bottom - dr.top) * d3d->m_bitmapHeight) / (d3d->m_screenHeight * 2);

	float sw = dw / d3d->m_screenWidth;
	float sh = dh / d3d->m_screenHeight;

	int xshift = -zr.left - sr.left;
	int yshift = -zr.top - sr.top;

	xshift -= ((sr.right - sr.left) - d3d->m_screenWidth) / 2;
	yshift -= ((sr.bottom - sr.top) - d3d->m_screenHeight) / 2;

	d3d->xoffset = tx + xshift - d3d->m_screenWidth / 2;
	d3d->yoffset = ty + yshift - d3d->m_screenHeight / 2;

	d3d->xmult = d3d->m_screenWidth / w;
	d3d->ymult = d3d->m_screenHeight / h;

	d3d->cursor_offset_x = -zr.left;
	d3d->cursor_offset_y = -zr.top;

	write_log(_T("%d %d %.f %.f\n"), d3d->xoffset, d3d->yoffset, d3d->xmult, d3d->ymult);

	UpdateBuffers(d3d);

	xD3DXMatrixOrthoOffCenterLH(&d3d->m_matProj_out, 0, w + 0.05f, 0, h + 0.05f, 0.0f, 1.0f);
	xD3DXMatrixTranslation(&d3d->m_matView_out, tx, ty, 1.0f);
	sw *= d3d->m_bitmapWidth;
	sh *= d3d->m_bitmapHeight;
	xD3DXMatrixScaling(&d3d->m_matWorld_out, sw + 0.5f / sw, sh + 0.5f / sh, 1.0f);
}

static void updateleds(struct d3d11struct *d3d)
{
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE map;
	static uae_u32 rc[256], gc[256], bc[256], a[256];
	static int done;
	int osdx, osdy;

	if (!done) {
		for (int i = 0; i < 256; i++) {
			rc[i] = i << 16;
			gc[i] = i << 8;
			bc[i] = i << 0;
			a[i] = i << 24;
		}
		done = 1;
	}

	if (!d3d->osd.texture || d3d != d3d11data)
		return;

	statusline_getpos(d3d - d3d11data, &osdx, &osdy, d3d->m_screenWidth, d3d->m_screenHeight, d3d->statusbar_hx, d3d->statusbar_vx);
	d3d->osd.x = osdx;
	d3d->osd.y = osdy;

	hr = d3d->m_deviceContext->Map(d3d->osd.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (FAILED(hr)) {
		write_log(_T("Led Map failed %08x\n"), hr);
		return;
	}
	for (int y = 0; y < TD_TOTAL_HEIGHT * d3d->statusbar_vx; y++) {
		uae_u8 *buf = (uae_u8*)map.pData + y * map.RowPitch;
		statusline_single_erase(d3d - d3d11data, buf, 32 / 8, y, d3d->ledwidth * d3d->statusbar_hx);
	}
	statusline_render(d3d - d3d11data, (uae_u8*)map.pData, 32 / 8, map.RowPitch, d3d->ledwidth, d3d->ledheight, rc, gc, bc, a);

	int y = 0;
	for (int yy = 0; yy < d3d->statusbar_vx * TD_TOTAL_HEIGHT; yy++) {
		uae_u8 *buf = (uae_u8*)map.pData + yy * map.RowPitch;
		draw_status_line_single(d3d - d3d11data, buf, 32 / 8, y, d3d->ledwidth, rc, gc, bc, a);
		if ((yy % d3d->statusbar_vx) == 0)
			y++;
	}

	d3d->m_deviceContext->Unmap(d3d->osd.texture, 0);
}

static bool createvertexshader(struct d3d11struct *d3d, ID3D11VertexShader **vertexshader, ID3D11Buffer **matrixbuffer, ID3D11InputLayout **layout)
{
	D3D11_INPUT_ELEMENT_DESC polygonLayout[3];
	HRESULT hr;
	unsigned int numElements;
	D3D11_BUFFER_DESC matrixBufferDesc;

	// Create the vertex shader from the buffer.
	hr = d3d->m_device->CreateVertexShader(VertexShader, sizeof(VertexShader), NULL, vertexshader);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateVertexShader %08x\n"), hr);
		return false;
	}
	// Create the vertex input layout description.
	// This setup needs to match the VertexType stucture in the ModelClass and in the shader.
	polygonLayout[0].SemanticName = "POSITION";
	polygonLayout[0].SemanticIndex = 0;
	polygonLayout[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	polygonLayout[0].InputSlot = 0;
	polygonLayout[0].AlignedByteOffset = 0;
	polygonLayout[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[0].InstanceDataStepRate = 0;

	polygonLayout[1].SemanticName = "TEXCOORD";
	polygonLayout[1].SemanticIndex = 0;
	polygonLayout[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout[1].InputSlot = 0;
	polygonLayout[1].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[1].InstanceDataStepRate = 0;

	polygonLayout[2].SemanticName = "TEXCOORD";
	polygonLayout[2].SemanticIndex = 1;
	polygonLayout[2].Format = DXGI_FORMAT_R32G32_FLOAT;
	polygonLayout[2].InputSlot = 0;
	polygonLayout[2].AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
	polygonLayout[2].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
	polygonLayout[2].InstanceDataStepRate = 0;

	// Get a count of the elements in the layout.
	numElements = sizeof(polygonLayout) / sizeof(polygonLayout[0]);

	// Create the vertex input layout.
	hr = d3d->m_device->CreateInputLayout(polygonLayout, numElements, VertexShader, sizeof(VertexShader), layout);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateInputLayout %08x\n"), hr);
		return false;
	}

	// Setup the description of the dynamic matrix constant buffer that is in the vertex shader.
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(MatrixBufferType);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	matrixBufferDesc.MiscFlags = 0;
	matrixBufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	hr = d3d->m_device->CreateBuffer(&matrixBufferDesc, NULL, matrixbuffer);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateBuffer(matrix) %08x\n"), hr);
		return false;
	}
	return true;
}

static void FreeTexture2D(ID3D11Texture2D **t, ID3D11ShaderResourceView **v)
{
	if (t && *t) {
		(*t)->Release();
		(*t) = NULL;
	}
	if (v && *v) {
		(*v)->Release();
		(*v) = NULL;
	}
}

static void freesprite(struct d3d11sprite *s)
{
	FreeTexture2D(&s->texture, &s->texturerv);

	if (s->pixelshader)
		s->pixelshader->Release();
	if (s->vertexshader)
		s->vertexshader->Release();
	if (s->layout)
		s->layout->Release();
	if (s->vertexbuffer)
		s->vertexbuffer->Release();
	if (s->indexbuffer)
		s->indexbuffer->Release();
	if (s->matrixbuffer)
		s->matrixbuffer->Release();

	memset(s, 0, sizeof(struct d3d11sprite));
}

static void FreeShaderTex(struct shadertex *t)
{
	FreeTexture2D(&t->tex, &t->rv);
	if (t->rt)
		t->rt->Release();
	t->rt = NULL;
}

static void freeshaderdata(struct shaderdata11 *s)
{
	if (s->effect) {
		s->effect->Release();
		s->effect = NULL;
	}
	FreeTexture2D(&s->masktexture, &s->masktexturerv);
	FreeShaderTex(&s->lpWorkTexture1);
	FreeShaderTex(&s->lpWorkTexture2);
	FreeShaderTex(&s->lpTempTexture);
	if (s->lpHq2xLookupTexture) {
		s->lpHq2xLookupTexture->Release();
		s->lpHq2xLookupTexture = NULL;
	}
	if (s->lpHq2xLookupTexturerv) {
		s->lpHq2xLookupTexturerv->Release();
		s->lpHq2xLookupTexturerv = NULL;
	}
	for (int j = 0; j < MAX_TECHNIQUE_LAYOUTS; j++) {
		if (s->layouts[j])
			s->layouts[j]->Release();
		s->layouts[j] = NULL;
	}
	if (s->vertexBuffer) {
		s->vertexBuffer->Release();
		s->vertexBuffer = NULL;
	}
	if (s->indexBuffer) {
		s->indexBuffer->Release();
		s->indexBuffer = NULL;
	}
	memset(s, 0, sizeof(struct shaderdata11));
}

static void FreeTextures(struct d3d11struct *d3d)
{
	FreeTexture2D(&d3d->texture2d, &d3d->texture2drv);
	FreeTexture2D(&d3d->texture2dstaging, NULL);
	FreeTexture2D(&d3d->screenshottexture, NULL);

	freesprite(&d3d->osd);
	freesprite(&d3d->hwsprite);
	freesprite(&d3d->mask2texture);
	for (int i = 0; overlayleds[i]; i++) {
		freesprite(&d3d->mask2textureleds[i]);
	}
	freesprite(&d3d->mask2textureled_power_dim);
	freesprite(&d3d->blanksprite);

	for (int i = 0; i < MAX_SHADERS; i++) {
		freeshaderdata(&d3d->shaders[i]);
	}
}

static bool InitializeBuffers(struct d3d11struct *d3d, ID3D11Buffer **vertexBuffer, ID3D11Buffer **indexBuffer)
{
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;
	VertexType vertices[VERTEXCOUNT];
	uae_u32 indices[INDEXCOUNT];

	d3d->index_buffer_bytes = d3d11_feature_level >= D3D10_FEATURE_LEVEL_9_2 ? sizeof(uae_u32) : sizeof(uae_u16);

	// Initialize vertex array to zeros at first.
	memset(vertices, 0, (sizeof(VertexType) * VERTEXCOUNT));

	static const uae_u16 indexes[6] = { 0, 2, 1, 2, 0, 3 };
	// Load the index array with data.
	for (int i = 0; i < INDEXCOUNT; i++)
	{
		if (d3d->index_buffer_bytes == 4)
			((uae_u32*)indices)[i] = indexes[i];
		else
			((uae_u16*)indices)[i] = indexes[i];
	}

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(VertexType) * VERTEXCOUNT;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	result = d3d->m_device->CreateBuffer(&vertexBufferDesc, &vertexData, vertexBuffer);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBuffer(vertex) %08x\n"), result);
		return false;
	}

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = d3d->index_buffer_bytes * INDEXCOUNT;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = d3d->m_device->CreateBuffer(&indexBufferDesc, &indexData, indexBuffer);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBuffer(index) %08x\n"), result);
		return false;
	}

	return true;
}

static void setsprite(struct d3d11struct *d3d, struct d3d11sprite *s, float x, float y)
{
	s->x = x;
	s->y = y;
}

static bool allocsprite(struct d3d11struct *d3d, struct d3d11sprite *s, int width, int height, bool alpha)
{
	D3D11_TEXTURE2D_DESC desc;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	HRESULT hr;

	freesprite(s);
	s->width = width;
	s->height = height;
	s->alpha = alpha;

	if (!InitializeBuffers(d3d, &s->vertexbuffer, &s->indexbuffer))
		goto err;

	hr = d3d->m_device->CreatePixelShader(PS_PostPlain, sizeof(PS_PostPlain), NULL, &s->pixelshader);
	if (FAILED(hr))
		goto err;

	if (!createvertexshader(d3d, &s->vertexshader, &s->matrixbuffer, &s->layout))
		goto err;

	memset(&desc, 0, sizeof desc);
	desc.Width = s->width;
	desc.Height = s->height;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->scrformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &s->texture);
	if (FAILED(hr)) {
		write_log(_T("CreateTexture2D (%dx%d) failed: %08x\n"), width, height, hr);
		goto err;
	}

	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->scrformat;

	hr = d3d->m_device->CreateShaderResourceView(s->texture, &srvDesc, &s->texturerv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView (%dx%d) failed: %08x\n"), width, height, hr);
		goto err;
	}

	setsprite(d3d, s, 0, 0);

	return true;
err:
	freesprite(s);
	return false;
}

#if 0
static void erasetexture(struct d3d11struct *d3d)
{
	int pitch;
	uae_u8 *p = D3D_locktexture(d3d - &d3d11data[0], &pitch, NULL, true);
	if (p) {
		for (int i = 0; i < d3d->m_bitmapHeight; i++) {
			memset(p, 255, d3d->m_bitmapWidth * d3d->texdepth / 8);
			p += pitch;
		}
		D3D_unlocktexture(d3d - &d3d11data[0], -1, -1);
	}
}
#endif

static bool CreateTexture(struct d3d11struct *d3d)
{
	D3D11_TEXTURE2D_DESC desc;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	HRESULT hr;

	memset(&d3d->sr2, 0, sizeof(RECT));
	memset(&d3d->dr2, 0, sizeof(RECT));
	memset(&d3d->zr2, 0, sizeof(RECT));

	FreeTextures(d3d);

	memset(&desc, 0, sizeof desc);
	desc.Width = d3d->m_bitmapWidth;
	desc.Height = d3d->m_bitmapHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->texformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &d3d->texture2d);
	if (FAILED(hr)) {
		write_log(_T("CreateTexture2D (main) failed: %08x\n"), hr);
		return false;
	}

	memset(&desc, 0, sizeof desc);
	desc.Width = d3d->m_bitmapWidth;
	desc.Height = d3d->m_bitmapHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->texformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &d3d->texture2dstaging);
	if (FAILED(hr)) {
		write_log(_T("CreateTexture2D (staging) failed: %08x\n"), hr);
		return false;
	}

	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->texformat;

	hr = d3d->m_device->CreateShaderResourceView(d3d->texture2d, &srvDesc, &d3d->texture2drv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView MAIN failed: %08x\n"), hr);
		return false;
	}

	ID3D11Texture2D* pSurface;
	hr = d3d->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast< void** >(&pSurface));
	if (SUCCEEDED(hr)) {
		memset(&desc, 0, sizeof desc);
		pSurface->GetDesc(&desc);
		desc.BindFlags = 0;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		desc.Usage = D3D11_USAGE_STAGING;

		hr = d3d->m_device->CreateTexture2D(&desc, nullptr, &d3d->screenshottexture);
		if (FAILED(hr)) {
			write_log(_T("CreateTexture2D (screenshot) failed: %08x\n"), hr);
		}
		pSurface->Release();
	}

	UpdateVertexArray(d3d, d3d->m_vertexBuffer, 0, 0, 0, 0, 0, 0, 0, 0);

	d3d->ledwidth = d3d->m_screenWidth;
	d3d->ledheight = TD_TOTAL_HEIGHT;
	if (d3d->statusbar_hx < 1)
		d3d->statusbar_hx = 1;
	if (d3d->statusbar_vx < 1)
		d3d->statusbar_vx = 1;
	allocsprite(d3d, &d3d->osd, d3d->ledwidth, d3d->ledheight, true);
	d3d->osd.enabled = true;

	d3d->cursor_v = false;
	d3d->cursor_scale = false;
	allocsprite(d3d, &d3d->hwsprite, CURSORMAXWIDTH, CURSORMAXHEIGHT, true);

	write_log(_T("D3D11 %dx%d main texture allocated\n"), d3d->m_bitmapWidth, d3d->m_bitmapHeight);

	return true;
}

static bool allocshadertex(struct d3d11struct *d3d, struct shadertex *t, int w, int h, int idx)
{
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	D3D11_TEXTURE2D_DESC desc;
	HRESULT hr;

	memset(&desc, 0, sizeof desc);
	desc.Width = w;
	desc.Height = h;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->scrformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
	desc.CPUAccessFlags = 0;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &t->tex);
	if (FAILED(hr)) {
		write_log(_T("D3D11 Failed to create working texture: %08x:%d\n"), hr, idx);
		return 0;
	}

	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->scrformat;

	hr = d3d->m_device->CreateShaderResourceView(t->tex, &srvDesc, &t->rv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView texture failed: %08x:%d\n"), hr, idx);
		return false;
	}

	hr = d3d->m_device->CreateRenderTargetView(t->tex, NULL, &t->rt);
	if (FAILED(hr))
	{
		write_log(_T("ID3D11Device CreateRenderTargetView %08x:%d\n"), hr, idx);
		return false;
	}

	return true;
}

static bool allocextratextures(struct d3d11struct *d3d, struct shaderdata11 *s, int w, int h)
{
	if (!allocshadertex(d3d, &s->lpWorkTexture1, w, h, s - &d3d->shaders[0]))
		return false;
	if (!allocshadertex(d3d, &s->lpWorkTexture2, w, h, s - &d3d->shaders[0]))
		return false;

	write_log(_T("D3D11 %d*%d working texture:%d\n"), w, h, s - &d3d->shaders[0]);
	return true;
}

static bool createextratextures(struct d3d11struct *d3d, int ow, int oh, int win_w, int win_h)
{
	bool haveafter = false;

	int zw, zh;

	if (ow > win_w * d3d->dmultx && oh > win_h * d3d->dmultx) {
		zw = ow;
		zh = oh;
	} else {
		zw = win_w * d3d->dmultx;
		zh = win_h * d3d->dmultx;
	}
	for (int i = 0; i < MAX_SHADERS; i++) {
		struct shaderdata11 *s = &d3d->shaders[i];
		if (s->type == SHADERTYPE_BEFORE || s->type == SHADERTYPE_AFTER || s->type == SHADERTYPE_MIDDLE) {
			int w2, h2, w, h;
			if (s->type == SHADERTYPE_AFTER) {
				w2 = zw; h2 = zh;
				w = zw; h = zh;
				haveafter = true;
				if (!allocextratextures(d3d, &d3d->shaders[i], d3d->m_screenWidth, d3d->m_screenHeight))
					return 0;
			} else if (s->type == SHADERTYPE_MIDDLE) {
				// worktex_width = 800
				// extratex = amiga res
				w2 = zw; h2 = zh;
				w = zw; h = zh;
				if (!allocextratextures(d3d, &d3d->shaders[i], ow, oh))
					return 0;
			} else {
				w2 = ow;
				h2 = oh;
				w = ow;
				h = oh;
			}
			d3d->shaders[i].targettex_width = w2;
			d3d->shaders[i].targettex_height = h2;
			if (!allocshadertex(d3d, &s->lpTempTexture, w2, h2, s - &d3d->shaders[0]))
				return false;
			write_log(_T("D3D11 %d*%d temp texture:%d:%d\n"), w2, h2, i, d3d->shaders[i].type);
			d3d->shaders[i].worktex_width = w;
			d3d->shaders[i].worktex_height = h;
		}
	}
	if (haveafter) {
		if (!allocshadertex(d3d, &d3d->lpPostTempTexture, d3d->m_screenWidth, d3d->m_screenHeight, -1))
			return 0;
		write_log(_T("D3D11 %d*%d after texture\n"), d3d->m_screenWidth, d3d->m_screenHeight);
	}
	return 1;
}

static bool createsltexture(struct d3d11struct *d3d)
{
	D3D11_TEXTURE2D_DESC desc;
	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	HRESULT hr;

	FreeTexture2D(&d3d->sltexture, &d3d->sltexturerv);

	memset(&desc, 0, sizeof desc);
	desc.Width = d3d->m_screenWidth;
	desc.Height = d3d->m_screenHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->scrformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &d3d->sltexture);
	if (FAILED(hr)) {
		write_log(_T("CreateTexture2D (main) failed: %08x\n"), hr);
		return false;
	}

	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->scrformat;

	hr = d3d->m_device->CreateShaderResourceView(d3d->sltexture, &srvDesc, &d3d->sltexturerv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView SL failed: %08x\n"), hr);
		FreeTexture2D(&d3d->sltexture, NULL);
		return false;
	}

	write_log(_T("SL %d*%d texture allocated\n"), desc.Width, desc.Height);
	return true;
}


static void createscanlines(struct d3d11struct *d3d, int force)
{
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE map;
	static int osl1, osl2, osl3;
	int sl4, sl42;
	int l1, l2;
	int x, y, yy;
	uae_u8 *sld, *p;
	int bpp;

	if (osl1 == filterd3d->gfx_filter_scanlines && osl3 == filterd3d->gfx_filter_scanlinelevel && osl2 == filterd3d->gfx_filter_scanlineratio && !force)
		return;
	bpp = 4;
	osl1 = filterd3d->gfx_filter_scanlines;
	osl3 = filterd3d->gfx_filter_scanlinelevel;
	osl2 = filterd3d->gfx_filter_scanlineratio;
	sl4 = filterd3d->gfx_filter_scanlines * 16 / 100;
	sl42 = filterd3d->gfx_filter_scanlinelevel * 16 / 100;
	if (sl4 > 15)
		sl4 = 15;
	if (sl42 > 15)
		sl42 = 15;
	l1 = (filterd3d->gfx_filter_scanlineratio >> 0) & 15;
	l2 = (filterd3d->gfx_filter_scanlineratio >> 4) & 15;

	if (l1 + l2 <= 0)
		return;

	if (!d3d->sltexture) {
		if (osl1 == 0 && osl3 == 0)
			return;
		if (!createsltexture(d3d))
			return;
	}

	hr = d3d->m_deviceContext->Map(d3d->sltexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (FAILED(hr)) {
		write_log(_T("SL Map failed %08x\n"), hr);
		return;
	}
	sld = (uae_u8*)map.pData;
	for (y = 0; y < d3d->m_screenHeight; y++)
		memset(sld + y * map.RowPitch, 0, d3d->m_screenWidth * bpp);
	for (y = 1; y < d3d->m_screenHeight; y += l1 + l2) {
		for (yy = 0; yy < l2 && y + yy < d3d->m_screenHeight; yy++) {
			for (x = 0; x < d3d->m_screenWidth; x++) {
				uae_u8 sll = sl42;
				p = &sld[(y + yy) * map.RowPitch + (x * bpp)];
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
	d3d->m_deviceContext->Unmap(d3d->sltexture, 0);
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

static int createmask2texture(struct d3d11struct *d3d, const TCHAR *filename)
{
	struct AmigaMonitor *mon = &AMonitors[d3d - d3d11data];
	struct zfile *zf;
	TCHAR tmp[MAX_DPATH];
	ID3D11Texture2D *tx = NULL;
	HRESULT hr;
	TCHAR filepath[MAX_DPATH];

	freesprite(&d3d->mask2texture);
	for (int i = 0; overlayleds[i]; i++) {
		freesprite(&d3d->mask2textureleds[i]);
	}
	freesprite(&d3d->mask2textureled_power_dim);
	freesprite(&d3d->blanksprite);

	if (filename[0] == 0 || WIN32GFX_IsPicassoScreen(mon))
		return 0;

	zf = NULL;
	for (int i = 0; i < 2; i++) {
		if (i == 0) {
			get_plugin_path(tmp, sizeof tmp / sizeof(TCHAR), _T("overlays"));
			_tcscat(tmp, filename);
		} else {
			_tcscpy(tmp, filename);
		}
		TCHAR tmp2[MAX_DPATH], tmp3[MAX_DPATH];
		_tcscpy(tmp3, tmp);
		TCHAR *s = _tcsrchr(tmp3, '.');
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
				if (!_istdigit(v))
					break;
				s2--;
			}
			_tcscpy(tmp2, s);
			_stprintf(s, _T("_%dx%d%s"), d3d->m_screenWidth, d3d->m_screenHeight, tmp2);
			_tcscpy(filepath, tmp3);
			zf = zfile_fopen(tmp3, _T("rb"), ZFD_NORMAL);
			if (zf)
				break;
			float aspect = (float)d3d->m_screenWidth / d3d->m_screenHeight;
			int ax = -1, ay = -1;
			if (abs(aspect - 16.0 / 10.0) <= 0.1)
				ax = 16, ay = 10;
			if (abs(aspect - 16.0 / 9.0) <= 0.1)
				ax = 16, ay = 9;
			if (abs(aspect - 4.0 / 3.0) <= 0.1)
				ax = 4, ay = 3;
			if (ax > 0 && ay > 0) {
				_stprintf(s, _T("_%dx%d%s"), ax, ay, tmp2);
				_tcscpy(filepath, tmp3);
				zf = zfile_fopen(tmp3, _T("rb"), ZFD_NORMAL);
				if (zf)
					break;
			}
		}
		_tcscpy(filepath, tmp);
		zf = zfile_fopen(tmp, _T("rb"), ZFD_NORMAL);
		if (zf)
			break;
	}
	if (!zf) {
		write_log(_T("Couldn't open overlay '%s'\n"), filename);
		return 0;
	}
	struct uae_image img;
	if (!load_png_image(zf, &img)) {
		write_log(_T("Overlay texture '%s' load failed.\n"), filename);
		goto end;
	}
	d3d->mask2texture_w = img.width;
	d3d->mask2texture_h = img.height;
	if (!allocsprite(d3d, &d3d->mask2texture, img.width, img.height, true))
		goto end;

	D3D11_MAPPED_SUBRESOURCE map;
	hr = d3d->m_deviceContext->Map(d3d->mask2texture.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (FAILED(hr))
		goto end;
	for (int i = 0; i < img.height; i++) {
		memcpy((uae_u8*)map.pData + i * map.RowPitch, img.data + i * img.pitch, img.width * 4);
	}
	d3d->m_deviceContext->Unmap(d3d->mask2texture.texture, 0);

	d3d->mask2rect.left = 0;
	d3d->mask2rect.top = 0;
	d3d->mask2rect.right = d3d->mask2texture_w;
	d3d->mask2rect.bottom = d3d->mask2texture_h;

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
	d3d->mask2texture_multx = (float)d3d->m_screenWidth / d3d->mask2texture_w;
	d3d->mask2texture_multy = (float)d3d->m_screenHeight / d3d->mask2texture_h;
	d3d->mask2texture_offsetw = 0;

	if (isfs(d3d) > 0) {
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

	d3d->mask2texture_wh = d3d->m_screenHeight;
	d3d->mask2texture_ww = d3d->mask2texture_w * d3d->mask2texture_multx;

	d3d->mask2texture_offsetw = (d3d->m_screenWidth - d3d->mask2texture_ww) / 2;

	if (d3d->mask2texture_offsetw > 0) {
		allocsprite(d3d, &d3d->blanksprite, d3d->mask2texture_offsetw + 1, d3d->m_screenHeight, false);
	}

	float xmult = d3d->mask2texture_multx;
	float ymult = d3d->mask2texture_multy;

	d3d->mask2rect.left *= xmult;
	d3d->mask2rect.right *= xmult;
	d3d->mask2rect.top *= ymult;
	d3d->mask2rect.bottom *= ymult;
	d3d->mask2texture_wwx = d3d->mask2texture_w * xmult;
	if (d3d->mask2texture_wwx > d3d->m_screenWidth)
		d3d->mask2texture_wwx = d3d->m_screenWidth;
	if (d3d->mask2texture_wwx < d3d->mask2rect.right - d3d->mask2rect.left)
		d3d->mask2texture_wwx = d3d->mask2rect.right - d3d->mask2rect.left;
	if (d3d->mask2texture_wwx > d3d->mask2texture_ww)
		d3d->mask2texture_wwx = d3d->mask2texture_ww;

	d3d->mask2texture_minusx = -((d3d->m_screenWidth - d3d->mask2rect.right) + d3d->mask2rect.left);
	if (d3d->mask2texture_offsetw > 0)
		d3d->mask2texture_minusx += d3d->mask2texture_offsetw * xmult;

	d3d->mask2texture_minusy = -(d3d->m_screenHeight - (d3d->mask2rect.bottom - d3d->mask2rect.top));

	d3d->mask2texture_hhx = d3d->mask2texture_h * ymult;

	write_log(_T("Overlay: '%s' %.0f*%.0f (%d*%d - %d*%d) (%d*%d)\n"),
		tmp, d3d->mask2texture_w, d3d->mask2texture_h,
		d3d->mask2rect.left, d3d->mask2rect.top, d3d->mask2rect.right, d3d->mask2rect.bottom,
		d3d->mask2rect.right - d3d->mask2rect.left, d3d->mask2rect.bottom - d3d->mask2rect.top);

	d3d->mask2texture.enabled = true;
	d3d->mask2texture.bilinear = true;

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
					if (allocsprite(d3d, &d3d->mask2textureleds[i], ledimg.width, ledimg.height, true)) {
						D3D11_MAPPED_SUBRESOURCE map;
						hr = d3d->m_deviceContext->Map(d3d->mask2textureleds[i].texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
						if (SUCCEEDED(hr)) {
							for (int j = 0; j < ledimg.height; j++) {
								memcpy((uae_u8*)map.pData + j * map.RowPitch, ledimg.data + j * ledimg.pitch, ledimg.width * 4);
							}
							d3d->m_deviceContext->Unmap(d3d->mask2textureleds[i].texture, 0);
						}
						d3d->mask2textureleds[i].enabled = true;
						d3d->mask2textureleds[i].bilinear = true;
						if (ledtypes[i] == LED_POWER) {
							if (allocsprite(d3d, &d3d->mask2textureled_power_dim, ledimg.width, ledimg.height, true)) {
								hr = d3d->m_deviceContext->Map(d3d->mask2textureled_power_dim.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
								if (SUCCEEDED(hr)) {
									for (int j = 0; j < ledimg.height; j++) {
										uae_u8 *pd = (uae_u8*)map.pData + j * map.RowPitch;
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
									d3d->m_deviceContext->Unmap(d3d->mask2textureled_power_dim.texture, 0);
								}
								d3d->mask2textureled_power_dim.enabled = true;
								d3d->mask2textureled_power_dim.bilinear = true;
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
	freesprite(&d3d->mask2texture);
	freesprite(&d3d->blanksprite);
	return 0;
}

static int createmasktexture(struct d3d11struct *d3d, const TCHAR *filename, struct shaderdata11 *sd)
{
	struct zfile *zf;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH];
	int maskwidth, maskheight;
	int idx = 0;// sd - &d3d->shaders[0];
	D3D11_MAPPED_SUBRESOURCE map;
	D3D11_TEXTURE2D_DESC desc;

	if (filename[0] == 0)
		return 0;
	get_plugin_path(tmp, sizeof tmp / sizeof(TCHAR), _T("masks"));
	_tcscat(tmp, filename);
	zf = zfile_fopen(tmp, _T("rb"), ZFD_NORMAL);
	if (!zf) {
		zf = zfile_fopen(filename, _T("rb"), ZFD_NORMAL);
		if (!zf) {
			write_log(_T("Couldn't open mask '%s':%d\n"), filename, idx);
			return 0;
		}
	}

	struct uae_image img;
	if (!load_png_image(zf, &img)) {
		write_log(_T("Temp mask texture '%s' load failed:%d\n"), filename, idx);
		goto end;
	}

	sd->masktexture_w = img.width;
	sd->masktexture_h = img.height;

	// both must be divisible by mask size
	maskwidth = ((d3d->m_screenWidth + sd->masktexture_w - 1) / sd->masktexture_w) * sd->masktexture_w;
	maskheight = ((d3d->m_screenHeight + sd->masktexture_h - 1) / sd->masktexture_h) * sd->masktexture_h;

	memset(&desc, 0, sizeof desc);
	desc.Width = maskwidth;
	desc.Height = maskheight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->scrformat;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &sd->masktexture);
	if (FAILED(hr)) {
		write_log(_T("Mask texture creation failed: %s:%d\n"), hr, idx);
		return false;
	}

	hr = d3d->m_deviceContext->Map(sd->masktexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (SUCCEEDED(hr)) {
		int x, y, sx, sy;
		uae_u32 *sptr, *ptr;
		sy = 0;
		for (y = 0; y < maskheight; y++) {
			sx = 0;
			for (x = 0; x < maskwidth; x++) {
				uae_u32 v;
				sptr = (uae_u32*)((uae_u8*)img.data + sy * img.pitch + sx * 4);
				ptr = (uae_u32*)((uae_u8*)map.pData + y * map.RowPitch + x * 4);
				v = *sptr;
				*ptr = v;
				sx++;
				if (sx >= sd->masktexture_w)
					sx = 0;
			}
			sy++;
			if (sy >= sd->masktexture_h)
				sy = 0;
		}
		d3d->m_deviceContext->Unmap(sd->masktexture, 0);
	}
	sd->masktexture_w = maskwidth;
	sd->masktexture_h = maskheight;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->scrformat;

	hr = d3d->m_device->CreateShaderResourceView(sd->masktexture, &srvDesc, &sd->masktexturerv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView MASK failed: %08x\n"), hr);
		goto end;
	}

	write_log(_T("Mask %d*%d (%d*%d) %d*%d ('%s':%d) texture allocated\n"), sd->masktexture_w, sd->masktexture_h,
		img.width, img.height, 0, 0, filename, idx);

	free_uae_image(&img);
	return 1;
end:
	FreeTexture2D(&sd->masktexture, NULL);
	free_uae_image(&img);
	return 0;
}

#if 0
static void OutputShaderErrorMessage(ID3D10Blob* errorMessage, HWND hwnd, TCHAR* shaderFilename)
{
	char *compileErrors;
	unsigned long bufferSize;

	if (errorMessage) {
		// Get a pointer to the error message text buffer.
		compileErrors = (char*)(errorMessage->GetBufferPointer());

		// Get the length of the message.
		bufferSize = errorMessage->GetBufferSize();

		TCHAR *s = au(compileErrors);

		write_log(_T("D3D11 Shader error: %s"), s);

		xfree(s);

		// Release the error message.
		errorMessage->Release();
	} else {

		write_log(_T("D3D11 Shader error: %08x"), GetLastError());

	}
}
#endif


static bool TextureShaderClass_InitializeShader(struct d3d11struct *d3d)
{
	HRESULT result;
	D3D11_SAMPLER_DESC samplerDesc;

	for (int i = 0; i < 3; i++) {
		ID3D10Blob* pixelShaderBuffer = NULL;
		ID3D11PixelShader **ps = NULL;
		const BYTE *Buffer = NULL;
		int BufferSize = 0;
		char *name;

		switch (i)
		{
		case 0:
			name = "PS_PostPlain";
			ps = &d3d->m_pixelShader;
			Buffer = PS_PostPlain;
			BufferSize = sizeof(PS_PostPlain);
			break;
		case 1:
			name = "PS_PostMask";
			ps = &d3d->m_pixelShaderMask;
			Buffer = PS_PostMask;
			BufferSize = sizeof(PS_PostMask);
			break;
		case 2:
			name = "PS_PostAlpha";
			ps = &d3d->m_pixelShaderSL;
			Buffer = PS_PostAlpha;
			BufferSize = sizeof(PS_PostAlpha);
			break;
		}
		// Create the pixel shader from the buffer.
		result = d3d->m_device->CreatePixelShader(Buffer, BufferSize, NULL, ps);
		if (FAILED(result))
		{
			write_log(_T("ID3D11Device CreatePixelShader %08x\n"), result);
			if (pixelShaderBuffer) {
				pixelShaderBuffer->Release();
				pixelShaderBuffer = 0;
			}
			return false;
		}

		if (pixelShaderBuffer) {
			pixelShaderBuffer->Release();
			pixelShaderBuffer = 0;
		}
	}

	if (!createvertexshader(d3d, &d3d->m_vertexShader, &d3d->m_matrixBuffer, &d3d->m_layout))
		return false;

	// Create a texture sampler state description.
	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	samplerDesc.MipLODBias = 0.0f;
	samplerDesc.MaxAnisotropy = 1;
	samplerDesc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
	samplerDesc.BorderColor[0] = 0;
	samplerDesc.BorderColor[1] = 0;
	samplerDesc.BorderColor[2] = 0;
	samplerDesc.BorderColor[3] = 0;
	samplerDesc.MinLOD = 0;
	samplerDesc.MaxLOD = D3D11_FLOAT32_MAX;

	// Create the texture sampler state.
	result = d3d->m_device->CreateSamplerState(&samplerDesc, &d3d->m_sampleState_point_clamp);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateSamplerState1 %08x\n"), result);
		return false;
	}

	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

	result = d3d->m_device->CreateSamplerState(&samplerDesc, &d3d->m_sampleState_linear_clamp);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateSamplerState2 %08x\n"), result);
		return false;
	}

	samplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
	samplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
	// Create the texture sampler state.
	result = d3d->m_device->CreateSamplerState(&samplerDesc, &d3d->m_sampleState_point_wrap);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateSamplerState1 %08x\n"), result);
		return false;
	}

	samplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

	result = d3d->m_device->CreateSamplerState(&samplerDesc, &d3d->m_sampleState_linear_wrap);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateSamplerState2 %08x\n"), result);
		return false;
	}

	return true;
}

static bool initd3d(struct d3d11struct *d3d)
{
	HRESULT result;
	ID3D11Texture2D* backBufferPtr;
	D3D11_RASTERIZER_DESC rasterDesc;

	if (d3d->d3dinit_done)
		return true;

	write_log(_T("D3D11 initd3d start\n"));

	// Get the pointer to the back buffer.
	result = d3d->m_swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBufferPtr);
	if (FAILED(result))
	{
		write_log(_T("IDXGISwapChain1 GetBuffer %08x\n"), result);
		return false;
	}

	// Create the render target view with the back buffer pointer.
	result = d3d->m_device->CreateRenderTargetView(backBufferPtr, NULL, &d3d->m_renderTargetView);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateRenderTargetView %08x\n"), result);
		return false;
	}

	// Release pointer to the back buffer as we no longer need it.
	backBufferPtr->Release();
	backBufferPtr = 0;

	// Setup the raster description which will determine how and what polygons will be drawn.
	rasterDesc.AntialiasedLineEnable = false;
	rasterDesc.CullMode = D3D11_CULL_NONE;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = d3d11_feature_level < D3D10_FEATURE_LEVEL_10_0 ? true : false;
	rasterDesc.FillMode = D3D11_FILL_SOLID;
	rasterDesc.FrontCounterClockwise = false;
	rasterDesc.MultisampleEnable = false;
	rasterDesc.ScissorEnable = false;
	rasterDesc.SlopeScaledDepthBias = 0.0f;

	// Create the rasterizer state from the description we just filled out.
	result = d3d->m_device->CreateRasterizerState(&rasterDesc, &d3d->m_rasterState);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateRasterizerState %08x\n"), result);
		return false;
	}

	// Now set the rasterizer state.
	d3d->m_deviceContext->RSSetState(d3d->m_rasterState);
	d3d->m_deviceContext->OMSetDepthStencilState(0, 0);

	D3D11_BLEND_DESC blendStateDescription;
	// Clear the blend state description.
	ZeroMemory(&blendStateDescription, sizeof(D3D11_BLEND_DESC));
	// Create an alpha enabled blend state description.
	blendStateDescription.RenderTarget[0].BlendEnable = TRUE;
	//blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	blendStateDescription.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	blendStateDescription.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	blendStateDescription.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	blendStateDescription.RenderTarget[0].RenderTargetWriteMask = 0x0f;

	// Create the blend state using the description.
	result = d3d->m_device->CreateBlendState(&blendStateDescription, &d3d->m_alphaEnableBlendingState);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBlendState Alpha %08x\n"), result);
		return false;
	}
	// Modify the description to create an alpha disabled blend state description.
	blendStateDescription.RenderTarget[0].BlendEnable = FALSE;
	// Create the blend state using the description.
	result = d3d->m_device->CreateBlendState(&blendStateDescription, &d3d->m_alphaDisableBlendingState);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBlendState NoAlpha %08x\n"), result);
		return false;
	}

	// Setup the viewport for rendering.
	d3d->viewport.Width = (float)d3d->m_screenWidth;
	d3d->viewport.Height = (float)d3d->m_screenHeight;
	d3d->viewport.MinDepth = 0.0f;
	d3d->viewport.MaxDepth = 1.0f;
	d3d->viewport.TopLeftX = 0.0f;
	d3d->viewport.TopLeftY = 0.0f;

	// Create the viewport.
	d3d->m_deviceContext->RSSetViewports(1, &d3d->viewport);

	// Initialize the world matrix to the identity matrix.
	xD3DXMatrixIdentity(&d3d->m_worldMatrix);

	// Create an orthographic projection matrix for 2D rendering.
	xD3DXMatrixOrthoLH(&d3d->m_orthoMatrix, (float)d3d->m_screenWidth, (float)d3d->m_screenHeight, 0.0f, 1.0f);

	d3d->m_positionX = 0.0f;
	d3d->m_positionY = 0.0f;
	d3d->m_positionZ = 1.0f;

	d3d->m_rotationX = 0.0f;
	d3d->m_rotationY = 0.0f;
	d3d->m_rotationZ = 0.0f;

	if (!TextureShaderClass_InitializeShader(d3d))
		return false;
	if (!InitializeBuffers(d3d, &d3d->m_vertexBuffer, &d3d->m_indexBuffer))
		return false;
	if (!UpdateBuffers(d3d))
		return false;

	settransform(d3d, NULL);

	d3d->d3dinit_done = true;

	write_log(_T("D3D11 initd3d end\n"));
	return true;
}

static void setswapchainmode(struct d3d11struct *d3d, int fs)
{
	struct amigadisplay *ad = &adisplays[d3d - d3d11data];
	struct apmode *apm = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	// It is recommended to always use the tearing flag when it is supported.
	d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	if (d3d->m_tearingSupport && (d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)) {
		d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}
	if (0 && os_win8 > 1 && fs <= 0) {
		d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
	}
	d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	// tearing flag is not fullscreen compatible
	if (fs > 0) {
		d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	d3d->fsSwapChainDesc.Windowed = TRUE;
}

static const TCHAR *d3dcompiler2 = _T("D3DCompiler_46.dll");
static const TCHAR *d3dcompiler1 = _T("D3DCompiler_47.dll");
static const TCHAR *d3dcompiler = NULL;

int can_D3D11(bool checkdevice)
{
	static bool detected;
	static int detected_val = 0;
	HRESULT hr;
	int ret = 0;

	if (detected && !checkdevice)
		return detected_val;

	detected = true;

	if (!os_win7)
		return 0;

	if (!hd3d11)
		hd3d11 = LoadLibrary(_T("D3D11.dll"));
	if (!hdxgi)
		hdxgi = LoadLibrary(_T("Dxgi.dll"));
	if (!dwmapi)
		dwmapi = LoadLibrary(_T("Dwmapi.dll"));

	if (!hd3dcompiler) {
		d3dcompiler = d3dcompiler1;
		hd3dcompiler = LoadLibrary(d3dcompiler);
		if (!hd3dcompiler) {
			d3dcompiler = d3dcompiler2;
			hd3dcompiler = LoadLibrary(d3dcompiler);
		}
		if (!hd3dcompiler) {
			d3dcompiler = NULL;
		}
	}
	if (!hd3d11 || !hdxgi) {
		write_log(_T("D3D11.dll=%p Dxgi.dll=%p\n"), hd3d11, hdxgi);
		return 0;
	}

	pD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(GetModuleHandle(_T("D3D11.dll")), "D3D11CreateDevice");
	pCreateDXGIFactory1 = (CREATEDXGIFACTORY1)GetProcAddress(GetModuleHandle(_T("Dxgi.dll")), "CreateDXGIFactory1");
	if (hd3dcompiler && d3dcompiler) {
		HMODULE h = GetModuleHandle(d3dcompiler);
		pD3DCompileFromFile = (D3DCOMPILEFROMFILE)GetProcAddress(h, "D3DCompileFromFile");
		ppD3DCompile = (D3DCOMPILE)GetProcAddress(h, "D3DCompile");
		ppD3DCompile2 = (D3DCOMPILE2)GetProcAddress(h, "D3DCompile2");
		pD3DReflect = (D3DREFLECT)GetProcAddress(h, "D3DReflect");
		pD3DGetBlobPart = (D3DGETBLOBPART)GetProcAddress(h, "D3DGetBlobPart");
	}

	if (!pD3D11CreateDevice || !pCreateDXGIFactory1) {
		write_log(_T("pD3D11CreateDevice=%p pCreateDXGIFactory1=%p\n"),
			pD3D11CreateDevice, pCreateDXGIFactory1);
		return 0;
	}

	if (!pDwmGetCompositionTimingInfo && dwmapi) {
		pDwmGetCompositionTimingInfo = (DWMGETCOMPOSITIONTIMINGINFO)GetProcAddress(dwmapi, "DwmGetCompositionTimingInfo");
	}

	if (ppD3DCompile && pD3DReflect && pD3DGetBlobPart)
		ret |= 4;


	// Create a DirectX graphics interface factory.
	ComPtr<IDXGIFactory4> factory4;
	hr = pCreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory4);
	if (SUCCEEDED(hr)) {
		ComPtr<IDXGIFactory5> factory5;
		BOOL allowTearing = FALSE;
		hr = factory4.As(&factory5);
		if (SUCCEEDED(hr)) {
			hr = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
			if (SUCCEEDED(hr) && allowTearing) {
				ret |= 2;
			}
		}
	}

	if (checkdevice) {
		static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_9_1 };
		UINT cdflags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		ID3D11Device *m_device;
		ID3D11DeviceContext *m_deviceContext;
		HRESULT hr = pD3D11CreateDevice(NULL, currprefs.gfx_api_options == 0 ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP,
			NULL, cdflags, levels, 1, D3D11_SDK_VERSION, &m_device, NULL, &m_deviceContext);
		if (FAILED(hr)) {
			return 0;
		}
		m_deviceContext->Release();
		m_device->Release();
	}

	ret |= 1;
	detected_val = ret;
	return ret;
}

static bool device_error(struct d3d11struct *d3d)
{
	d3d->device_errors++;
	if (d3d->device_errors > 2) {
		d3d->delayedfs = -1;
		return true;
	}
	return false;
}

static void do_black(struct d3d11struct *d3d)
{
	float color[4];
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	color[3] = 0;
	// Clear the back buffer.
	d3d->m_deviceContext->ClearRenderTargetView(d3d->m_renderTargetView, color);
}

static void do_present(struct d3d11struct *d3d)
{
	struct amigadisplay *ad = &adisplays[d3d - d3d11data];
	struct apmode *apm = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	HRESULT hr;
	UINT presentFlags = 0;

	int vsync = isvsync();
	UINT syncinterval = d3d->vblankintervals;
	// only if no vsync or low latency vsync
	if (d3d->m_tearingSupport && (d3d->swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING) && (!vsync || apm->gfx_vsyncmode)) {
		if (apm->gfx_vsyncmode || d3d - d3d11data > 0 || currprefs.turbo_emulation) {
			presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
			syncinterval = 0;
		}
	}
	if (!vsync) {
		if (apm->gfx_backbuffers == 0 || (presentFlags & DXGI_PRESENT_ALLOW_TEARING) || (apm->gfx_vflip == 0 && isfs(d3d) <= 0) || (isfs(d3d) > 0 && apm->gfx_vsyncmode))
			syncinterval = 0;
	}
	d3d->syncinterval = syncinterval;
	if (currprefs.turbo_emulation) {
		static int skip;
		if (--skip > 0)
			return;
		skip = 10;
		if (os_win8)
			presentFlags |= DXGI_PRESENT_DO_NOT_WAIT;
		syncinterval = 0;
	}

	hr = d3d->m_swapChain->Present(syncinterval, presentFlags);
	if (currprefs.turbo_emulation && hr == DXGI_ERROR_WAS_STILL_DRAWING)
		hr = S_OK;
	if (FAILED(hr) && hr != DXGI_STATUS_OCCLUDED) {
		if (hr == DXGI_ERROR_DEVICE_REMOVED) {
			device_error(d3d);
		} else if (hr == E_OUTOFMEMORY) {
			d3d->invalidmode = true;
		}
		write_log(_T("D3D11 Present %08x\n"), hr);
	}
	slicecnt++;
}

static float xD3D_getrefreshrate(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	d3d->lastframe = 0;
	if (isfs(d3d) != 0 && d3d->fsSwapChainDesc.RefreshRate.Denominator) {
		d3d->vblank = (float)d3d->fsSwapChainDesc.RefreshRate.Numerator / d3d->fsSwapChainDesc.RefreshRate.Denominator;
		return d3d->vblank;
	}
	if (!pDwmGetCompositionTimingInfo)
		return 0;
	DWM_TIMING_INFO ti;
	ti.cbSize = sizeof ti;
	HRESULT hr = pDwmGetCompositionTimingInfo(NULL, &ti);
	if (FAILED(hr)) {
		write_log(_T("DwmGetCompositionTimingInfo1 %08x\n"), hr);
		return 0;
	}
	d3d->vblank = (float)ti.rateRefresh.uiNumerator / ti.rateRefresh.uiDenominator;
	return d3d->vblank;
}

static int xxD3D11_init2(HWND ahwnd, int monid, int w_w, int w_h, int t_w, int t_h, int depth, int *freq, int mmult)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	struct amigadisplay *ad = &adisplays[monid];
	struct apmode *apm = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];

	HRESULT result;
	int ret = 0;
	ComPtr<IDXGIFactory2> factory2;
	ComPtr<IDXGIFactory4> factory4;
	ComPtr<IDXGIFactory5> factory5;
	IDXGIAdapter1* adapter;
	IDXGIOutput* adapterOutput;
	DXGI_ADAPTER_DESC1 adesc;
	DXGI_OUTPUT_DESC odesc;
	unsigned int numModes;
	DXGI_MODE_DESC1* displayModeList;
	DXGI_ADAPTER_DESC adapterDesc;

	write_log(_T("D3D11 init start. (%d*%d) (%d*%d) RTG=%d Depth=%d.\n"), w_w, w_h, t_w, t_h, ad->picasso_on, depth);

	filterd3didx = ad->picasso_on;
	filterd3d = &currprefs.gf[filterd3didx];

	d3d->delayedfs = 0;
	d3d->device_errors = 0;

	if (depth != 32)
		return 0;

	if (!can_D3D11(false))
		return 0;

	d3d->m_bitmapWidth = t_w;
	d3d->m_bitmapHeight = t_h;
	d3d->m_screenWidth = w_w;
	d3d->m_screenHeight = w_h;
	d3d->ahwnd = ahwnd;
	d3d->texformat = DXGI_FORMAT_B8G8R8A8_UNORM;
	d3d->intformat = DXGI_FORMAT_B8G8R8A8_UNORM; // _SRGB;
	d3d->scrformat = DXGI_FORMAT_B8G8R8A8_UNORM;
	d3d->dmultx = mmult;

	struct MultiDisplay *md = getdisplay(&currprefs, monid);
	POINT pt;
	pt.x = (md->rect.right - md->rect.left) / 2 + md->rect.left;
	pt.y = (md->rect.bottom - md->rect.top) / 2 + md->rect.top;
	HMONITOR winmon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

	// Create a DirectX graphics interface factory.
	result = pCreateDXGIFactory1(__uuidof(IDXGIFactory4), (void**)&factory4);
	if (FAILED(result))
	{
		write_log(_T("D3D11 CreateDXGIFactory4 %08x\n"), result);
		result = pCreateDXGIFactory1(__uuidof(IDXGIFactory2), (void**)&factory2);
		if (FAILED(result)) {
			write_log(_T("D3D11 CreateDXGIFactory2 %08x\n"), result);
			if (!os_win8) {
				gui_message(_T("WinUAE Direct3D 11 mode requires Windows 7 Platform Update (KB2670838). Check Windows Update optional updates or download it from: https://www.microsoft.com/en-us/download/details.aspx?id=36805"));
			}
			return false;
		}
	} else {
		BOOL allowTearing = FALSE;
		result = factory4.As(&factory5);
		if (SUCCEEDED(result)) {
			factory2 = factory5;
			if (!d3d->m_tearingSupport) {
				result = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
				d3d->m_tearingSupport = SUCCEEDED(result) && allowTearing;
				write_log(_T("CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING) = %08x %d\n"), result, allowTearing);
			}
		} else {
			factory2 = factory4;
		}
	}

	// Use the factory to create an adapter for the primary graphics interface (video card).
	UINT adapterNum = 0;
	bool outputFound = false;
	for (;;) {
		adapterOutput = NULL;
		result = factory2->EnumAdapters1(adapterNum, &adapter);
		if (FAILED(result))
		{
			if (adapterNum > 0)
				break;
			write_log(_T("IDXGIFactory2 EnumAdapters1 %08x\n"), result);
			return 0;
		}
		result = adapter->GetDesc1(&adesc);

		UINT adapterOutNum = 0;
		// Enumerate the monitor
		for(;;) {
			result = adapter->EnumOutputs(adapterOutNum, &adapterOutput);
			if (FAILED(result))
				break;
			result = adapterOutput->GetDesc(&odesc);
			if (SUCCEEDED(result)) {
				if (odesc.Monitor == winmon || !_tcscmp(odesc.DeviceName, md->adapterid)) {
					outputFound = true;
					break;
				}
			}
			adapterOutput->Release();
			adapterOutput = NULL;
			adapterOutNum++;
		}
		if (outputFound)
			break;
		adapter->Release();
		adapter = NULL;
		adapterNum++;
	}
	if (!outputFound) {
		if (adapter)
			adapter->Release();
		adapter = NULL;
		result = factory2->EnumAdapters1(0, &adapter);
		if (FAILED(result)) {
			write_log(_T("EnumAdapters1 Default %08x\n"), result);
			return 0;
		}
		result = adapter->EnumOutputs(0, &adapterOutput);
		if (FAILED(result)) {
			adapter->Release();
			adapter = NULL;
			write_log(_T("EnumOutputs Default %08x\n"), result);
			return 0;
		}
	}

	d3d->outputAdapter = adapterOutput;

	ComPtr<IDXGIOutput1> adapterOutput1;
	result = adapterOutput->QueryInterface(__uuidof(IDXGIOutput1), &adapterOutput1);
	if (FAILED(result)) {
		write_log(_T("IDXGIOutput QueryInterface %08x\n"), result);
		return 0;
	}

	// Get the number of modes that fit the display format for the adapter output (monitor).
	result = adapterOutput1->GetDisplayModeList1(d3d->scrformat, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);
	if (FAILED(result))
	{
		write_log(_T("IDXGIOutput1 GetDisplayModeList1 %08x\n"), result);
		return 0;
	}

	// Create a list to hold all the possible display modes for this monitor/video card combination.
	displayModeList = new DXGI_MODE_DESC1[numModes];
	if (!displayModeList)
	{
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return 0;
	}

	// Now fill the display mode list structures.
	result = adapterOutput1->GetDisplayModeList1(d3d->scrformat, DXGI_ENUM_MODES_INTERLACED, &numModes, displayModeList);
	if (FAILED(result))
	{
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return 0;
	}

	ZeroMemory(&d3d->fsSwapChainDesc, sizeof(d3d->fsSwapChainDesc));

	int hz = getrefreshrate(monid, w_w, w_h);

	// Now go through all the display modes and find the one that matches the screen width and height.
	// When a match is found store the numerator and denominator of the refresh rate for that monitor.
	d3d->fsSwapChainDesc.RefreshRate.Denominator = 0;
	d3d->fsSwapChainDesc.RefreshRate.Numerator = 0;
	d3d->fsSwapChainDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
	for (int i = 0; i < numModes; i++) {
		DXGI_MODE_DESC1 *m = &displayModeList[i];
		if (m->Format != d3d->scrformat)
			continue;
		if (apm->gfx_interlaced && m->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST)
			continue;
		if (!apm->gfx_interlaced && m->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE)
			continue;
		if (m->Width == w_w && m->Height == w_h) {
			d3d->fsSwapChainDesc.ScanlineOrdering = m->ScanlineOrdering;
			d3d->fsSwapChainDesc.Scaling = m->Scaling;
			if (!hz) {
				write_log(_T("D3D11 found matching fullscreen mode. SLO=%d S=%d. Default refresh rate.\n"), m->ScanlineOrdering, m->Scaling);
				break;
			}
			if (isfs(d3d) != 0 && m->RefreshRate.Numerator && m->RefreshRate.Denominator) {
				float mhz = (float)m->RefreshRate.Numerator / m->RefreshRate.Denominator;
				if ((int)(mhz + 0.5) == hz || (int)(mhz) == hz) {
					d3d->fsSwapChainDesc.RefreshRate.Denominator = m->RefreshRate.Denominator;
					d3d->fsSwapChainDesc.RefreshRate.Numerator = m->RefreshRate.Numerator;
					write_log(_T("D3D11 found matching refresh rate %d/%d=%.2f. SLO=%d\n"), m->RefreshRate.Numerator, m->RefreshRate.Denominator, (float)mhz, m->ScanlineOrdering);
					*freq = hz;
					break;
				}
			}
		}
	}
	if (isfs(d3d) > 0 && (hz == 0 || (d3d->fsSwapChainDesc.RefreshRate.Denominator == 0 && d3d->fsSwapChainDesc.RefreshRate.Numerator == 0))) {
		// find highest frequency for selected mode
		float ffreq = 0;
		for (int i = 0; i < numModes; i++) {
			DXGI_MODE_DESC1 *m = &displayModeList[i];
			if (m->Format != d3d->scrformat)
				continue;
			if (apm->gfx_interlaced && m->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST)
				continue;
			if (!apm->gfx_interlaced && m->ScanlineOrdering != DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE)
				continue;
			if (m->Width != w_w || m->Height != w_h)
				continue;
			if (!m->RefreshRate.Numerator || !m->RefreshRate.Denominator)
				continue;
			float nfreq = (float)m->RefreshRate.Numerator / m->RefreshRate.Denominator;
			if (nfreq > ffreq) {
				ffreq = nfreq;
				d3d->fsSwapChainDesc.RefreshRate.Denominator = m->RefreshRate.Denominator;
				d3d->fsSwapChainDesc.RefreshRate.Numerator = m->RefreshRate.Numerator;
				if (!currprefs.gfx_variable_sync) {
					*freq = nfreq;
				}
			}
		}
		write_log(_T("D3D11 Highest freq: %d/%d=%.2f W=%d H=%d\n"),
			d3d->fsSwapChainDesc.RefreshRate.Numerator, d3d->fsSwapChainDesc.RefreshRate.Denominator, ffreq, w_w, w_h);
		// then re-confirm with FindClosestMatchingMode1()
		DXGI_MODE_DESC1 md1 = { 0 }, md2;
		md1.Format = d3d->scrformat;
		md1.RefreshRate.Numerator = d3d->fsSwapChainDesc.RefreshRate.Numerator;
		md1.RefreshRate.Denominator = d3d->fsSwapChainDesc.RefreshRate.Denominator;
		md1.Width = w_w;
		md1.Height = w_h;
		md1.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		md1.ScanlineOrdering = apm->gfx_interlaced ? DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST : DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
		result = adapterOutput1->FindClosestMatchingMode1(&md1, &md2, NULL);
		if (FAILED(result)) {
			write_log(_T("FindClosestMatchingMode1 %08x\n"), result);
		} else {
			d3d->fsSwapChainDesc.RefreshRate.Denominator = md2.RefreshRate.Denominator;
			d3d->fsSwapChainDesc.RefreshRate.Numerator = md2.RefreshRate.Numerator;
			if (!currprefs.gfx_variable_sync) {
				*freq = 0;
				if (md2.RefreshRate.Denominator && md2.RefreshRate.Numerator)
					*freq = md2.RefreshRate.Numerator / md2.RefreshRate.Denominator;
				write_log(_T("D3D11 FindClosestMatchingMode1() %d/%d=%.2f SLO=%d W=%d H=%d\n"),
					md2.RefreshRate.Numerator, md2.RefreshRate.Denominator,
					(float)md2.RefreshRate.Numerator / md2.RefreshRate.Denominator, md1.ScanlineOrdering,
					md2.Width, md2.Height);
			}
		}
	}

	if (isfs(d3d) <= 0 && !currprefs.gfx_variable_sync) {
		*freq = (int)xD3D_getrefreshrate(monid);
	}

	// Get the adapter (video card) description.
	result = adapter->GetDesc(&adapterDesc);
	if (FAILED(result)) {
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return 0;
	}

	result = adapterOutput->GetDesc(&odesc);
	if (FAILED(result)) {
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return 0;
	}

	write_log(_T("D3D11 Device: %s [%s] (%d,%d,%d,%d)\n"), adapterDesc.Description, odesc.DeviceName,
		odesc.DesktopCoordinates.left, odesc.DesktopCoordinates.top,
		odesc.DesktopCoordinates.right, odesc.DesktopCoordinates.bottom);

	// Release the display mode list.
	delete[] displayModeList;
	displayModeList = 0;

	// Release the adapter.
	adapter->Release();
	adapter = 0;

	UINT cdflags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifndef NDEBUG
	cdflags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	static const D3D_FEATURE_LEVEL levels111[] = { D3D_FEATURE_LEVEL_11_1 };
	D3D_FEATURE_LEVEL outlevel;
	D3D_DRIVER_TYPE dt = currprefs.gfx_api_options == 0 ? D3D_DRIVER_TYPE_HARDWARE : D3D_DRIVER_TYPE_WARP;
	result = pD3D11CreateDevice(NULL, dt, NULL, cdflags, levels111, 1, D3D11_SDK_VERSION, &d3d->m_device, &outlevel, &d3d->m_deviceContext);
	if (FAILED(result)) {
		write_log(_T("D3D11CreateDevice LEVEL_11_1: %08x\n"), result);
		if (result == E_INVALIDARG || result == DXGI_ERROR_UNSUPPORTED) {
			result = pD3D11CreateDevice(NULL, dt, NULL, cdflags, NULL, 0, D3D11_SDK_VERSION, &d3d->m_device, &outlevel, &d3d->m_deviceContext);
		}
		if (FAILED(result)) {
			write_log(_T("D3D11CreateDevice %08x. Hardware does not support Direct3D11 Level 9.1 or higher.\n"), result);
			return 0;
		}
	}
	write_log(_T("D3D11CreateDevice succeeded with level %d.%d. %s.\n"), outlevel >> 12, (outlevel >> 8) & 15,
		currprefs.gfx_api_options ? _T("Software WARP driver") : _T("Hardware accelerated"));
	d3d11_feature_level = outlevel;

	UINT flags = 0;
	result = d3d->m_device->CheckFormatSupport(d3d->texformat, &flags);
	if (FAILED(result) || !(flags & D3D11_FORMAT_SUPPORT_TEXTURE2D)) {
		if (depth != 32)
			write_log(_T("Direct3D11: 16-bit texture format is not supported %08x\n"), result);
		else
			write_log(_T("Direct3D11: 32-bit texture format is not supported!? %08x\n"), result);
		if (depth == 32)
			return 0;
		write_log(_T("Direct3D11: Retrying in 32-bit mode\n"), result);
		return -1;
	}

#ifndef NDEBUG
	d3d->m_device->QueryInterface(IID_ID3D11InfoQueue, (void**)&d3d->m_debugInfoQueue);
	if (0 && d3d->m_debugInfoQueue)
	{
		d3d->m_debugInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, TRUE);
		d3d->m_debugInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, TRUE);
		d3d->m_debugInfoQueue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, TRUE);
	}
	d3d->m_device->QueryInterface(IID_ID3D11Debug, (void**)&d3d->m_debug);
#endif

	// Initialize the swap chain description.
	ZeroMemory(&d3d->swapChainDesc, sizeof(d3d->swapChainDesc));

	// Set the width and height of the back buffer.
	d3d->swapChainDesc.Width = w_w;
	d3d->swapChainDesc.Height = w_h;

	// Set regular 32-bit surface for the back buffer.
	d3d->swapChainDesc.Format = d3d->scrformat;

	// Turn multisampling off.
	d3d->swapChainDesc.SampleDesc.Count = 1;
	d3d->swapChainDesc.SampleDesc.Quality = 0;

	// Set the usage of the back buffer.
	d3d->swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	d3d->swapChainDesc.BufferCount = apm->gfx_backbuffers + 1;
	if (d3d->swapChainDesc.BufferCount < 2)
		d3d->swapChainDesc.BufferCount = 2;

	if (d3d->swapChainDesc.BufferCount > 2 && isfullscreen() <= 0 && !apm->gfx_vsync) {
		write_log(_T("Switch from triple buffer to double buffer (%d).\n"), apm->gfx_vflip);
		d3d->swapChainDesc.BufferCount = 2;
		apm->gfx_vflip = 0;
	}

	d3d->swapChainDesc.SwapEffect = os_win8 ? (os_win10 ? DXGI_SWAP_EFFECT_FLIP_DISCARD : DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL) : DXGI_SWAP_EFFECT_SEQUENTIAL;
	if (apm->gfx_vsyncmode && isfs(d3d) > 0 && !os_win10) {
		d3d->swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	}

	d3d->vblankintervals = 0;
	if (!monid && apm->gfx_backbuffers > 2 && !isvsync())
		d3d->vblankintervals = 1;
	cannoclear = false;

	if (apm->gfx_vsyncmode) {
		cannoclear = true;
	}

	d3d->swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	setswapchainmode(d3d, isfs(d3d));

	d3d->swapChainDesc.Scaling = (d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD) ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;

	d3d->blackscreen = false;
	if (!monid && isvsync()) {
		int vsync = isvsync();
		int hzmult = 0;
		getvsyncrate(monid, *freq, &hzmult);
		if (hzmult < 0 && !currprefs.gfx_variable_sync && apm->gfx_vsyncmode == 0) {
			if (!apm->gfx_strobo) {
				d3d->vblankintervals = 2;
			} else {
				d3d->vblankintervals = 1;
				d3d->blackscreen = true;
			}
		}
		if (vsync > 0 && !apm->gfx_vsyncmode) {
			if (apm->gfx_strobo)
				d3d->blackscreen = true;
			d3d->vblankintervals = 1;
			int hzmult;
			getvsyncrate(monid, hz, &hzmult);
			if (hzmult < 0) {
				d3d->vblankintervals = 1 + (-hzmult) - (d3d->blackscreen ? 1 : 0);
			}
		}
	}
	if (d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD && d3d->vblankintervals > 0)
		d3d->swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	result = factory2->CreateSwapChainForHwnd(d3d->m_device, ahwnd, &d3d->swapChainDesc, isfs(d3d) != 0 ? &d3d->fsSwapChainDesc : NULL, NULL, &d3d->m_swapChain);
	if (FAILED(result)) {
		write_log(_T("IDXGIFactory2 CreateSwapChainForHwnd %08x\n"), result);
		return 0;
	}

	{
		ComPtr<IDXGIDevice1> dxgiDevice;
		result = d3d->m_device->QueryInterface(__uuidof(IDXGIDevice1), &dxgiDevice);
		if (FAILED(result)) {
			write_log(_T("QueryInterface IDXGIDevice1 %08x\n"), result);
		}
		else {
			int f = apm->gfx_backbuffers <= 1 ? 1 : 2;
			if (d3d->blackscreen)
				f++;
			result = dxgiDevice->SetMaximumFrameLatency(f);
			if (FAILED(result)) {
				write_log(_T("IDXGIDevice1 SetMaximumFrameLatency %08x\n"), result);
			}
		}
	}

	IDXGIFactory1 *pFactory = NULL;
	result = d3d->m_swapChain->GetParent(__uuidof (IDXGIFactory1), (void **)&pFactory);
	if (SUCCEEDED(result)) {
		result = pFactory->MakeWindowAssociation(ahwnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
		if (FAILED(result)) {
			write_log(_T("IDXGIFactory2 MakeWindowAssociation %08x\n"), result);
		}
		pFactory->Release();
	}

	result = d3d->m_swapChain->GetDesc1(&d3d->swapChainDesc);
	if (FAILED(result)) {
		write_log(_T("IDXGIFactory2 GetDesc1 %08x\n"), result);
	}

	d3d->invalidmode = false;
	d3d->fsmode = 0;
	clearcnt = 0;

	write_log(_T("D3D11 Buffers=%d Flags=%08x Format=%08x Scaling=%d SwapEffect=%d VBI=%d\n"),
		d3d->swapChainDesc.BufferCount, d3d->swapChainDesc.Flags, d3d->swapChainDesc.Format,
		d3d->swapChainDesc.Scaling, d3d->swapChainDesc.SwapEffect, d3d->vblankintervals);

	if (isfs(d3d) > 0)
		D3D_resize(monid, 1);
	D3D_resize(monid, 0);

	ret = 1;

	write_log(_T("D3D11 init end\n"));
	return ret;
}

static void freed3d(struct d3d11struct *d3d)
{
	write_log(_T("D3D11 freed3d start\n"));

	d3d->d3dinit_done = false;

	if (d3d->m_rasterState) {
		d3d->m_rasterState->Release();
		d3d->m_rasterState = NULL;
	}
	if (d3d->m_alphaEnableBlendingState) {
		d3d->m_alphaEnableBlendingState->Release();
		d3d->m_alphaEnableBlendingState = NULL;
	}
	if (d3d->m_alphaDisableBlendingState) {
		d3d->m_alphaDisableBlendingState->Release();
		d3d->m_alphaDisableBlendingState = NULL;
	}

	if (d3d->m_renderTargetView) {
		d3d->m_renderTargetView->Release();
		d3d->m_renderTargetView = NULL;
	}

	if (d3d->m_layout) {
		d3d->m_layout->Release();
		d3d->m_layout = NULL;
	}
	if (d3d->m_vertexShader) {
		d3d->m_vertexShader->Release();
		d3d->m_vertexShader = NULL;
	}
	if (d3d->m_pixelShader) {
		d3d->m_pixelShader->Release();
		d3d->m_pixelShader = NULL;
	}
	if (d3d->m_pixelShaderMask) {
		d3d->m_pixelShaderMask->Release();
		d3d->m_pixelShaderMask = NULL;
	}
	if (d3d->m_pixelShaderSL) {
		d3d->m_pixelShaderSL->Release();
		d3d->m_pixelShaderSL = NULL;
	}

	if (d3d->m_sampleState_point_wrap) {
		d3d->m_sampleState_point_wrap->Release();
		d3d->m_sampleState_point_wrap = NULL;
	}
	if (d3d->m_sampleState_linear_wrap) {
		d3d->m_sampleState_linear_wrap->Release();
		d3d->m_sampleState_linear_wrap = NULL;
	}
	if (d3d->m_sampleState_point_clamp) {
		d3d->m_sampleState_point_clamp->Release();
		d3d->m_sampleState_point_clamp = NULL;
	}
	if (d3d->m_sampleState_linear_clamp) {
		d3d->m_sampleState_linear_clamp->Release();
		d3d->m_sampleState_linear_clamp = NULL;
	}

	if (d3d->m_indexBuffer) {
		d3d->m_indexBuffer->Release();
		d3d->m_indexBuffer = 0;
	}
	if (d3d->m_vertexBuffer) {
		d3d->m_vertexBuffer->Release();
		d3d->m_vertexBuffer = 0;
	}
	if (d3d->m_matrixBuffer) {
		d3d->m_matrixBuffer->Release();
		d3d->m_matrixBuffer = 0;
	}

	FreeTextures(d3d);
	FreeTexture2D(&d3d->sltexture, &d3d->sltexturerv);
	FreeShaderTex(&d3d->lpPostTempTexture);

	for (int i = 0; i < MAX_SHADERS; i++) {
		struct shaderdata11 *s = &d3d->shaders[i];
		freeshaderdata(s);
	}

	if (d3d->filenotificationhandle)
		CloseHandle(d3d->filenotificationhandle);
	d3d->filenotificationhandle = NULL;

	if (d3d->m_deviceContext) {
		d3d->m_deviceContext->ClearState();
	}
	write_log(_T("D3D11 freed3d end\n"));
}

static void xD3D11_free(int monid, bool immediate)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	write_log(_T("D3D11 free start\n"));

	//freethread(d3d);

	freed3d(d3d);

	if (d3d->m_swapChain) {
		// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
		d3d->m_swapChain->SetFullscreenState(false, NULL);
		d3d->m_swapChain->Release();
		d3d->m_swapChain = NULL;
	}
	if (d3d->m_deviceContext) {
		d3d->m_deviceContext->ClearState();
		d3d->m_deviceContext->Flush();
		d3d->m_deviceContext->Release();
		d3d->m_deviceContext = NULL;
	}

	if (d3d->m_device) {
		d3d->m_device->Release();
		d3d->m_device = NULL;
	}

	if (d3d->outputAdapter) {
		d3d->outputAdapter->Release();
		d3d->outputAdapter = NULL;
	}

#ifndef NDEBUG
	if (d3d->m_debugInfoQueue) {
		d3d->m_debugInfoQueue->Release();
		d3d->m_debugInfoQueue = NULL;
	}
	if (d3d->m_debug) {
		d3d->m_debug->Release();
		d3d->m_debug = NULL;
	}
#endif

	d3d->device_errors = 0;

	changed_prefs.leds_on_screen &= ~STATUSLINE_TARGET;
	currprefs.leds_on_screen &= ~STATUSLINE_TARGET;

	for (int i = 0; i < LED_MAX; i++) {
		leds[i] = 0;
	}

	write_log(_T("D3D11 free end\n"));
}

static int xxD3D11_init(HWND ahwnd, int monid, int w_w, int w_h, int depth, int *freq, int mmult)
{
	return xxD3D11_init2(ahwnd, monid, w_w, w_h, w_w, w_h, depth, freq, mmult);
}

static const TCHAR *xD3D11_init(HWND ahwnd, int monid, int w_w, int w_h, int depth, int *freq, int mmult)
{
	if (!can_D3D11(false))
		return _T("D3D11 FAILED TO INIT");
	int v = xxD3D11_init(ahwnd, monid, w_w, w_h, depth, freq, mmult);
	if (v > 0)
		return NULL;
	xD3D11_free(monid, true);
	if (v <= 0)
		return _T("");
	return _T("D3D11 INITIALIZATION ERROR");
}

static bool setmatrix(struct d3d11struct *d3d, ID3D11Buffer *matrixbuffer, D3DXMATRIX worldMatrix, D3DXMATRIX viewMatrix, D3DXMATRIX projectionMatrix)
{
	HRESULT result;
	D3DXMATRIX worldMatrix2, viewMatrix2, projectionMatrix2;
	MatrixBufferType *dataPtr;
	D3D11_MAPPED_SUBRESOURCE mappedResource;

	// Transpose the matrices to prepare them for the shader.
	xD3DXMatrixTranspose(&worldMatrix2, &worldMatrix);
	xD3DXMatrixTranspose(&viewMatrix2, &viewMatrix);
	xD3DXMatrixTranspose(&projectionMatrix2, &projectionMatrix);

	// Lock the constant buffer so it can be written to.
	result = d3d->m_deviceContext->Map(matrixbuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		write_log(_T("ID3D11DeviceContext map(matrix) %08x\n"), result);
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	dataPtr = (MatrixBufferType*)mappedResource.pData;

	// Copy the matrices into the constant buffer.
	dataPtr->world = worldMatrix2;
	dataPtr->view = viewMatrix2;
	dataPtr->projection = projectionMatrix2;

	// Unlock the constant buffer.
	d3d->m_deviceContext->Unmap(matrixbuffer, 0);
	return true;
}

static void RenderBuffers(struct d3d11struct *d3d, ID3D11Buffer *vertexbuffer, ID3D11Buffer *indexbuffer)
{
	unsigned int stride;
	unsigned int offset;

	// Set vertex buffer stride and offset.
	stride = sizeof(VertexType);
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	d3d->m_deviceContext->IASetVertexBuffers(0, 1, &vertexbuffer, &stride, &offset);

	// Set the index buffer to active in the input assembler so it can be rendered.
	d3d->m_deviceContext->IASetIndexBuffer(indexbuffer, d3d->index_buffer_bytes == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0);

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	d3d->m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void EndScene(struct d3d11struct *d3d)
{
#if 0
	if ((d3d->syncinterval || (d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_DISCARD)) && d3d->flipped) {
		WaitForSingleObject(flipevent2, 100);
		d3d->flipped = false;
	}
	SetEvent(flipevent);
#endif
	do_present(d3d);
}

static void TextureShaderClass_RenderShader(struct d3d11struct *d3d)
{
	// Set the vertex input layout.
	d3d->m_deviceContext->IASetInputLayout(d3d->m_layout);

	// Set the vertex and pixel shaders that will be used to render this triangle.
	d3d->m_deviceContext->VSSetShader(d3d->m_vertexShader, NULL, 0);
	bool mask = false;
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].type == SHADERTYPE_MASK_AFTER && d3d->shaders[i].masktexturerv) {
			mask = true;
		}
	}
	if (mask) {
		d3d->m_deviceContext->PSSetShader(d3d->m_pixelShaderMask, NULL, 0);
	} else if (d3d->sltexture) {
		d3d->m_deviceContext->PSSetShader(d3d->m_pixelShaderSL, NULL, 0);
	} else {
		d3d->m_deviceContext->PSSetShader(d3d->m_pixelShader, NULL, 0);
	}

	// Set the sampler state in the pixel shader.
	d3d->m_deviceContext->PSSetSamplers(0, 1, filterd3d->gfx_filter_bilinear ? &d3d->m_sampleState_linear_clamp : &d3d->m_sampleState_point_clamp);
	d3d->m_deviceContext->PSSetSamplers(1, 1, filterd3d->gfx_filter_bilinear ? &d3d->m_sampleState_linear_wrap : &d3d->m_sampleState_point_wrap);

	// Render the triangle.
	d3d->m_deviceContext->DrawIndexed(INDEXCOUNT, 0, 0);
}

static void RenderSprite(struct d3d11struct *d3d, struct d3d11sprite *spr)
{
	float left, top, right, bottom;

	if (!spr->enabled)
		return;
	
	left = (d3d->m_screenWidth + 1) / -2;
	left += spr->x;
	top = (d3d->m_screenHeight + 1) / 2;
	top -= spr->y;

	if (spr->outwidth) {
		right = left + spr->outwidth;
	} else {
		right = left + spr->width;
	}
	if (spr->outheight) {
		bottom = top - spr->outheight;
	} else {
		bottom = top - spr->height;
	}

	UpdateVertexArray(d3d, spr->vertexbuffer, left, top, right, bottom, 0, 0, 0, 0);

	RenderBuffers(d3d, spr->vertexbuffer, spr->indexbuffer);

	if (!setmatrix(d3d, spr->matrixbuffer, d3d->m_worldMatrix, d3d->m_viewMatrix, d3d->m_orthoMatrix))
		return;

	if (spr->alpha)
		TurnOnAlphaBlending(d3d);

	// Now set the constant buffer in the vertex shader with the updated values.
	d3d->m_deviceContext->VSSetConstantBuffers(0, 1, &spr->matrixbuffer);

	d3d->m_deviceContext->PSSetShaderResources(0, 1, &spr->texturerv);
	// Set the vertex input layout.
	d3d->m_deviceContext->IASetInputLayout(spr->layout);
	// Set the vertex and pixel shaders that will be used to render this triangle.
	d3d->m_deviceContext->VSSetShader(spr->vertexshader, NULL, 0);
	d3d->m_deviceContext->PSSetShader(spr->pixelshader, NULL, 0);
	// Set the sampler state in the pixel shader.
	d3d->m_deviceContext->PSSetSamplers(0, 1, spr->bilinear ? &d3d->m_sampleState_linear_clamp : &d3d->m_sampleState_point_clamp);
	// Render the triangle.
	d3d->m_deviceContext->DrawIndexed(INDEXCOUNT, 0, 0);

	if (spr->alpha)
		TurnOffAlphaBlending(d3d);
}

static void setspritescaling(struct d3d11sprite *spr, float w, float h)
{
	spr->outwidth = (int)(w * spr->width + 0.5);
	spr->outheight = (int)(h * spr->height + 0.5);
}

static void renderoverlay(struct d3d11struct *d3d)
{
	if (!d3d->mask2texture.enabled)
		return;

	struct d3d11sprite *spr = &d3d->mask2texture;

	float srcw = d3d->mask2texture_w;
	float srch = d3d->mask2texture_h;
	float aspectsrc = srcw / srch;
	float aspectdst = (float)d3d->m_screenWidth / d3d->m_screenHeight;

	setspritescaling(spr, d3d->mask2texture_multx, d3d->mask2texture_multy);

#if 0
	v.x = 0;
	if (filterd3d->gfx_filteroverlay_pos.x == -1)
		v.x = (d3d->m_screenWidth - (d3d->mask2texture_w * w)) / 2;
	else if (filterd3d->gfx_filteroverlay_pos.x > -24000)
		v.x = filterd3d->gfx_filteroverlay_pos.x;
	else
		v.x = (d3d->m_screenWidth - (d3d->mask2texture_w * w)) / 2 + (-filterd3d->gfx_filteroverlay_pos.x - 30100) * d3d->m_screenHeight / 100.0;

	v.y = 0;
	if (filterd3d->gfx_filteroverlay_pos.y == -1)
		v.y = (d3d->m_screenWidth - (d3d->mask2texture_h * h)) / 2;
	else if (filterd3d->gfx_filteroverlay_pos.y > -24000)
		v.y = filterd3d->gfx_filteroverlay_pos.y;
	else
		v.y = (d3d->m_screenWidth - (d3d->mask2texture_h * h)) / 2 + (-filterd3d->gfx_filteroverlay_pos.y - 30100) * d3d->m_screenHeight / 100.0;
#endif

	setsprite(d3d, spr, d3d->mask2texture_offsetw, 0);
	RenderSprite(d3d, spr);

	for (int i = 0; overlayleds[i]; i++) {
		bool led = leds[ledtypes[i]] != 0;
		if (led || (ledtypes[i] == LED_POWER && currprefs.power_led_dim)) {
			struct d3d11sprite *sprled = &d3d->mask2textureleds[i];
			if (!led && ledtypes[i] == LED_POWER && currprefs.power_led_dim)
				sprled = &d3d->mask2textureled_power_dim;
			if (sprled) {
				setspritescaling(sprled, d3d->mask2texture_multx, d3d->mask2texture_multy);
				setsprite(d3d, sprled,
					d3d->mask2texture_offsetw + d3d->mask2textureledoffsets[i * 2 + 0] * d3d->mask2texture_multx,
					d3d->mask2textureledoffsets[i * 2 + 1] * d3d->mask2texture_multy);
				RenderSprite(d3d, sprled);
			}
		}
	}

	if (d3d->mask2texture_offsetw > 0) {
		struct d3d11sprite *bspr = &d3d->blanksprite;
		setsprite(d3d, bspr, 0, 0);
		RenderSprite(d3d, bspr);
		setsprite(d3d, bspr, d3d->mask2texture_offsetw + d3d->mask2texture_ww, 0);
		RenderSprite(d3d, bspr);
	}
}

static void xD3D11_led(int led, int on, int brightness)
{
	struct d3d11struct *d3d = &d3d11data[0];
	leds[led] = on;
}

static int xD3D11_debug(int monid, int mode)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	int old = debugcolors ? 1 : 0;
	debugcolors = (mode & 1) != 0;
	noclear = debugcolors ? false : true;
	clearcnt = 0;
	return old;
}

static void clearrt(struct d3d11struct *d3d)
{
	// Setup the color to clear the buffer to.
	float color[4];
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	color[3] = 0;

	if (noclear && cannoclear) {
		if (clearcnt > 3)
			return;
		clearcnt++;
	}

	if (!noclear && debugcolors && slicecnt > 0) {
		int cnt = slicecnt - 1;
		int v = cnt % 3;
		if (cnt / 3 == 1)
			color[(v + 1) % 3] = 0.3;
		color[v] = 0.3;
	}

	// Clear the back buffer.
	d3d->m_deviceContext->ClearRenderTargetView(d3d->m_renderTargetView, color);
}

static bool renderframe(struct d3d11struct *d3d)
{
	ID3D11ShaderResourceView *empty = NULL;
	struct shadertex st;
	st.tex = d3d->texture2d;
	st.rv = d3d->texture2drv;
	st.rt = NULL;

	TurnOffAlphaBlending(d3d);

	d3d->m_deviceContext->PSSetShaderResources(0, 1, &empty);

	for (int i = 0; i < MAX_SHADERS; i++) {
		struct shaderdata11 *s = &d3d->shaders[i];
		if (s->type == SHADERTYPE_BEFORE)
			settransform_pre(d3d, s);
		if (s->type == SHADERTYPE_MIDDLE) {
			d3d->m_matProj = d3d->m_matProj_out;
			d3d->m_matView = d3d->m_matView_out;
			d3d->m_matWorld = d3d->m_matWorld_out;
		}
		if (s->type == SHADERTYPE_BEFORE || s->type == SHADERTYPE_MIDDLE) {
			settransform(d3d, s);
			if (!processshader(d3d, &st, s, true))
				return false;
		}
	}

	d3d->m_matProj = d3d->m_matProj_out;
	d3d->m_matView = d3d->m_matView_out;
	d3d->m_matWorld = d3d->m_matWorld_out;

	d3d->m_deviceContext->RSSetViewports(1, &d3d->viewport);

	// Set shader texture resource in the pixel shader.
	d3d->m_deviceContext->PSSetShaderResources(0, 1, &st.rv);
	bool mask = false;
	int after = -1;
	for (int i = 0; i < MAX_SHADERS; i++) {
		struct shaderdata11 *s = &d3d->shaders[i];
		if (s->type == SHADERTYPE_MASK_AFTER && s->masktexturerv) {
			d3d->m_deviceContext->PSSetShaderResources(1, 1, &s->masktexturerv);
			mask = true;
		}
		if (s->type == SHADERTYPE_AFTER)
			after = i;
	}
	if (!mask && d3d->sltexturerv) {
		d3d->m_deviceContext->PSSetShaderResources(1, 1, &d3d->sltexturerv);
	}

	// Set the shader parameters that it will use for rendering.
	if (!setmatrix(d3d, d3d->m_matrixBuffer, d3d->m_worldMatrix, d3d->m_viewMatrix, d3d->m_orthoMatrix))
		return false;

	// Put the bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers(d3d, d3d->m_vertexBuffer, d3d->m_indexBuffer);

	// Now set the constant buffer in the vertex shader with the updated values.
	d3d->m_deviceContext->VSSetConstantBuffers(0, 1, &d3d->m_matrixBuffer);

	ID3D11RenderTargetView *lpRenderTarget = NULL;

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	if (after >= 0) {
		d3d->m_deviceContext->OMSetRenderTargets(1, &d3d->lpPostTempTexture.rt, NULL);
	} else {
		d3d->m_deviceContext->OMSetRenderTargets(1, &d3d->m_renderTargetView, NULL);
		clearrt(d3d);
	}

	// Now render the prepared buffers with the shader.
	TextureShaderClass_RenderShader(d3d);

	if (after >= 0) {
		d3d->m_deviceContext->OMSetRenderTargets(1, &d3d->m_renderTargetView, NULL);
		clearrt(d3d);
	}

	if (after >= 0) {
		memcpy(&st, &d3d->lpPostTempTexture, sizeof(struct shadertex));
		for (int i = 0; i < MAX_SHADERS; i++) {
			struct shaderdata11 *s = &d3d->shaders[i];
			if (s->type == SHADERTYPE_AFTER) {
				settransform2(d3d, s);
				if (!processshader(d3d, &st, s, i != after))
					return false;
			}
		}
	}
	return true;
}

static bool TextureShaderClass_Render(struct d3d11struct *d3d)
{
	renderframe(d3d);

	RenderSprite(d3d, &d3d->hwsprite);

	renderoverlay(d3d);

	RenderSprite(d3d, &d3d->osd);

	return true;
}

static void CameraClass_Render(struct d3d11struct *d3d)
{
	D3DXVECTOR3 up, position, lookAt;
	float yaw, pitch, roll;
	D3DXMATRIX rotationMatrix;

	// Setup the vector that points upwards.
	up.x = 0.0f;
	up.y = 1.0f;
	up.z = 0.0f;

	// Setup the position of the camera in the world.
	position.x = d3d->m_positionX;
	position.y = d3d->m_positionY;
	position.z = d3d->m_positionZ;

	// Setup where the camera is looking by default.
	lookAt.x = 0.0f;
	lookAt.y = 0.0f;
	lookAt.z = 1.0f;

	// Set the yaw (Y axis), pitch (X axis), and roll (Z axis) rotations in radians.
	pitch = d3d->m_rotationX * 0.0174532925f;
	yaw = d3d->m_rotationY * 0.0174532925f;
	roll = d3d->m_rotationZ * 0.0174532925f;

	// Create the rotation matrix from the yaw, pitch, and roll values.
	xD3DXMatrixRotationYawPitchRoll(&rotationMatrix, yaw, pitch, roll);

	// Transform the lookAt and up vector by the rotation matrix so the view is correctly rotated at the origin.
	xD3DXVec3TransformCoord(&lookAt, &lookAt, &rotationMatrix);
	xD3DXVec3TransformCoord(&up, &up, &rotationMatrix);

	// Translate the rotated camera position to the location of the viewer.
	lookAt = position + lookAt;

	// Finally create the view matrix from the three updated vectors.
	xD3DXMatrixLookAtLH(&d3d->m_viewMatrix, &position, &lookAt, &up);
}

static bool GraphicsClass_Render(struct d3d11struct *d3d, float rotation, bool normalrender)
{
	bool result;

	setupscenecoords(d3d, normalrender);

	// Generate the view matrix based on the camera's position.
	CameraClass_Render(d3d);

	// Render the bitmap with the texture shader.
	result = TextureShaderClass_Render(d3d);

	if (!result)
		return false;

	return true;
}

static struct shaderdata11 *allocshaderslot(struct d3d11struct *d3d, int type)
{
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].type == 0) {
			d3d->shaders[i].type = type;
			return &d3d->shaders[i];
		}
	}
	return NULL;
}

static bool restore(struct d3d11struct *d3d)
{
	for (int i = 0; i < MAX_FILTERSHADERS; i++) {
		if (filterd3d->gfx_filtershader[i][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_BEFORE);
			if (!psEffect_LoadEffect(d3d, filterd3d->gfx_filtershader[i], s, i)) {
				freeshaderdata(s);
				filterd3d->gfx_filtershader[i][0] = changed_prefs.gf[filterd3didx].gfx_filtershader[i][0] = 0;
				break;
			}
		}
		if (filterd3d->gfx_filtermask[i][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_BEFORE);
			if (!createmasktexture(d3d, filterd3d->gfx_filtermask[i], s)) {
				freeshaderdata(s);
			}
		}
	}
	if (filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS][0]) {
		struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MIDDLE);
		if (!psEffect_LoadEffect(d3d, filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS], s, 2 * MAX_FILTERSHADERS)) {
			freeshaderdata(s);
			filterd3d->gfx_filtershader[2 * MAX_FILTERSHADERS][0] = changed_prefs.gf[filterd3didx].gfx_filtershader[2 * MAX_FILTERSHADERS][0] = 0;
		}
	}
	if (filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS][0]) {
		struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_AFTER);
		if (!createmasktexture(d3d, filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS], s)) {
			freeshaderdata(s);
		}
	}
	for (int i = 0; i < MAX_FILTERSHADERS; i++) {
		if (filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_AFTER);
			if (!psEffect_LoadEffect(d3d, filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS], s, i + MAX_FILTERSHADERS)) {
				freeshaderdata(s);
				filterd3d->gfx_filtershader[i + MAX_FILTERSHADERS][0] = changed_prefs.gf[filterd3didx].gfx_filtershader[i + MAX_FILTERSHADERS][0] = 0;
				break;
			}
		}
		if (filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_AFTER);
			if (!createmasktexture(d3d, filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS], s)) {
				freeshaderdata(s);
			}
		}
	}


	createscanlines(d3d, 1);
	createmask2texture(d3d, filterd3d->gfx_filteroverlay);

	int w = d3d->m_bitmapWidth;
	int h = d3d->m_bitmapHeight;

	if (!createextratextures(d3d, d3d->m_bitmapWidthX, d3d->m_bitmapHeightX, d3d->m_screenWidth, d3d->m_screenHeight))
		return false;

	for (int i = 0; i < MAX_SHADERS; i++) {
		int w2, h2;
		int type = d3d->shaders[i].type;
		if (type == SHADERTYPE_BEFORE) {
			w2 = d3d->shaders[i].worktex_width;
			h2 = d3d->shaders[i].worktex_height;
			if (!allocextratextures(d3d, &d3d->shaders[i], w, h))
				return 0;
		} else if (type == SHADERTYPE_MIDDLE) {
			w2 = d3d->shaders[i].worktex_width;
			h2 = d3d->shaders[i].worktex_height;
		} else {
			w2 = d3d->m_screenWidth;
			h2 = d3d->m_screenHeight;
		}
		if (type == SHADERTYPE_BEFORE || type == SHADERTYPE_AFTER || type == SHADERTYPE_MIDDLE) {
			D3D11_TEXTURE3D_DESC desc;
			D3D11_MAPPED_SUBRESOURCE map;
			HRESULT hr;
			memset(&desc, 0, sizeof(desc));
			desc.Width = 256;
			desc.Height = 16;
			desc.Depth = 256;
			desc.MipLevels = 1;
			desc.Format = d3d->scrformat;
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			hr = d3d->m_device->CreateTexture3D(&desc, NULL, &d3d->shaders[i].lpHq2xLookupTexture);
			if (FAILED(hr)) {
				write_log(_T("D3D11 Failed to create volume texture: %08x:%d\n"), hr, i);
				return false;
			}
			hr = d3d->m_deviceContext->Map(d3d->shaders[i].lpHq2xLookupTexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
			if (FAILED(hr)) {
				write_log(_T("D3D11 Failed to lock box of volume texture: %08x:%d\n"), hr, i);
				return false;
			}
			write_log(_T("HQ2X texture (%dx%d) (%dx%d):%d\n"), w2, h2, w, h, i);
			BuildHq2xLookupTexture(w2, h2, w, h, (unsigned char*)map.pData);
			d3d->m_deviceContext->Unmap(d3d->shaders[i].lpHq2xLookupTexture, 0);
			D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
			memset(&srvDesc, 0, sizeof srvDesc);
			ID3D11ShaderResourceView *rv = NULL;
			srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
			srvDesc.Texture2D.MostDetailedMip = 0;
			srvDesc.Texture2D.MipLevels = 1;
			srvDesc.Format = d3d->scrformat;
			hr = d3d->m_device->CreateShaderResourceView(d3d->shaders[i].lpHq2xLookupTexture, &srvDesc, &d3d->shaders[i].lpHq2xLookupTexturerv);
			if (FAILED(hr)) {
				write_log(_T("D3D11 Failed to create volume texture resource view: %08x:%d\n"), hr, i);
				return false;
			}
		}
	}

	write_log(_T("D3D11 Shader and extra textures restored\n"));

	return true;
}

static void resizemode(struct d3d11struct *d3d);

static bool xD3D11_renderframe(int monid, int mode, bool immediate)
{
	struct amigadisplay *ad = &adisplays[monid];
	struct apmode *apm = ad->picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	struct d3d11struct *d3d = &d3d11data[monid];

	d3d->frames_since_init++;

	if (mode > 0 && (mode & 2))
		slicecnt = 0;
	else if (mode < 0)
		slicecnt = slicecnt == 2 ? 0 : slicecnt;

	if (!d3d->m_swapChain)
		return false;

	if (d3d->fsmodechange)
		D3D_resize(monid, 0);

	if (d3d->invalidmode)
		return false;

	if (d3d->delayedrestore) {
		d3d->delayedrestore = false;
		restore(d3d);
	}

	if (d3d->delayedfs || !d3d->texture2d || !d3d->d3dinit_done)
		return false;

	GraphicsClass_Render(d3d, 0, mode < 0 || (mode & 1));

	if (d3d->filenotificationhandle != NULL) {
		bool notify = false;
		while (WaitForSingleObject(d3d->filenotificationhandle, 0) == WAIT_OBJECT_0) {
			if (FindNextChangeNotification(d3d->filenotificationhandle)) {
				if (d3d->frames_since_init > 50)
					notify = true;
			}
		}
		if (notify) {
			write_log(_T("D3D11 shader file modification notification.\n"));
			D3D_resize(monid, 0);
		}
	}
	if (apm->gfx_vsyncmode)
		d3d->m_deviceContext->Flush();

	return true;
}

static void xD3D11_showframe_special(int monid, int mode)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	if (d3d->invalidmode || d3d->delayedfs || !d3d->texture2d || !d3d->d3dinit_done)
		return;
	if (!d3d->m_swapChain)
		return;
	if (mode == 1)
		do_present(d3d);
	if (mode == 2)
		do_black(d3d);
}

static void xD3D11_showframe(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	if (d3d->invalidmode || d3d->delayedfs || !d3d->texture2d || !d3d->d3dinit_done)
		return;
	if (!d3d->m_swapChain)
		return;
	// Present the rendered scene to the screen.
	EndScene(d3d);
}

static void xD3D11_clear(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	if (d3d->invalidmode)
		return;
	if (!d3d->m_swapChain)
		return;

	// Setup the color to clear the buffer to.
	float color[4];
	color[0] = 0;
	color[1] = 0;
	color[2] = 0;
	color[3] = 0;
	// Bind the render target view and depth stencil buffer to the output render pipeline.
	d3d->m_deviceContext->OMSetRenderTargets(1, &d3d->m_renderTargetView, NULL);
	// Clear the back buffer.
	d3d->m_deviceContext->ClearRenderTargetView(d3d->m_renderTargetView, color);
	d3d->m_deviceContext->Flush();
	clearcnt = 0;
}


static bool xD3D11_quit(struct d3d11struct *d3d)
{
	if (quit_program != -UAE_QUIT && quit_program != UAE_QUIT)
		return false;
	if (d3d->m_swapChain && (!d3d->invalidmode || d3d->fsmode > 0)) {
		d3d->m_swapChain->SetFullscreenState(FALSE, NULL);
		FreeTextures(d3d);
	}
	d3d->fsmode = 0;
	d3d->invalidmode = true;
	d3d->fsmodechange = 0;
	return true;
}

static void xD3D11_refresh(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	if (!d3d->m_swapChain)
		return;

	createscanlines(d3d, 0);
	if (xD3D11_renderframe(monid, true, true)) {
		xD3D11_showframe(monid);
	}
	clearcnt = 0;
}


static bool D3D11_resize_do(struct d3d11struct *d3d)
{
	HRESULT hr;

	if (!d3d->fsresizedo)
		return false;
	if (!d3d->m_swapChain)
		return false;

	d3d->fsresizedo = false;

	write_log(_T("D3D11 resize do\n"));

	if (d3d->fsmodechange && d3d->fsmode > 0) {
		write_log(_T("D3D11_resize -> fullscreen\n"));
		ShowWindow(d3d->ahwnd, SW_SHOWNORMAL);
		hr = d3d->m_swapChain->SetFullscreenState(TRUE, d3d->outputAdapter);
		if (FAILED(hr)) {
			write_log(_T("SetFullscreenState(TRUE) failed %08X\n"), hr);
			toggle_fullscreen(d3d - d3d11data, 10);
		} else {
			d3d->fsmode = 0;
		}
		d3d->fsmodechange = 0;
		d3d->invalidmode = false;
	} else if (d3d->fsmodechange && d3d->fsmode < 0) {
		write_log(_T("D3D11_resize -> window\n"));
		hr = d3d->m_swapChain->SetFullscreenState(FALSE, NULL);
		if (FAILED(hr))
			write_log(_T("SetFullscreenState(FALSE) failed %08X\n"), hr);
		ShowWindow(d3d->ahwnd, SW_MINIMIZE);
		d3d->fsmode = 0;
		d3d->invalidmode = true;
		d3d->fsmodechange = 0;
	} else {
		write_log(_T("D3D11_resize -> none\n"));
	}

	resizemode(d3d);

	write_log(_T("D3D11 resize exit\n"));
	return true;
}


static bool recheck(struct d3d11struct *d3d)
{
	bool r = false;
	if (xD3D11_quit(d3d))
		return r;
	r = D3D11_resize_do(d3d);
	if (d3d->resizeretry) {
		resizemode(d3d);
		return r;
	}
	if (!d3d->delayedfs)
		return r;
	xD3D11_free(d3d - d3d11data, true);
	d3d->delayedfs = 0;
	ShowWindow(d3d->ahwnd, SW_SHOWNORMAL);
	int freq = 0;
	if (!xxD3D11_init2(d3d->ahwnd, d3d - d3d11data, d3d->m_screenWidth, d3d->m_screenHeight, d3d->m_bitmapWidth2, d3d->m_bitmapHeight2, 32, &freq, d3d->dmultx))
		d3d->invalidmode = true;
	return false;
}

static bool xD3D11_alloctexture(int monid, int w, int h)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	bool v;

	recheck(d3d);

	if (d3d->invalidmode)
		return false;
	d3d->m_bitmapWidth = w;
	d3d->m_bitmapHeight = h;
	d3d->m_bitmapWidth2 = d3d->m_bitmapWidth;
	d3d->m_bitmapHeight2 = d3d->m_bitmapHeight;
	d3d->dmult = S2X_getmult(monid);
	d3d->m_bitmapWidthX = d3d->m_bitmapWidth * d3d->dmultx;
	d3d->m_bitmapHeightX = d3d->m_bitmapHeight * d3d->dmultx;

	v = CreateTexture(d3d);
	if (!v)
		return false;

	if (d3d->reloadshaders) {
		d3d->reloadshaders = false;
		restore(d3d);
	} else {
		d3d->delayedrestore = true;
	}

	setupscenecoords(d3d, true);

	changed_prefs.leds_on_screen |= STATUSLINE_TARGET;
	currprefs.leds_on_screen |= STATUSLINE_TARGET;

	return true;
}

static uae_u8 *xD3D11_locktexture(int monid, int *pitch, int *height, bool fullupdate)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	// texture allocation must not cause side-effects

	if (d3d->invalidmode || !d3d->texture2d)
		return NULL;

	D3D11_MAPPED_SUBRESOURCE map;
	HRESULT hr = d3d->m_deviceContext->Map(d3d->texture2dstaging, 0, D3D11_MAP_WRITE, 0, &map);

	if (FAILED(hr)) {
		write_log(_T("D3D11 Map() %08x\n"), hr);
		return NULL;
	}
	*pitch = map.RowPitch;
	if (height)
		*height = d3d->m_bitmapHeight;
	d3d->texturelocked++;
	return (uae_u8*)map.pData;
}

static void xD3D11_unlocktexture(int monid, int y_start, int y_end)
{
	struct AmigaMonitor *mon = &AMonitors[monid];
	struct d3d11struct *d3d = &d3d11data[monid];

	if (!d3d->texturelocked || d3d->invalidmode)
		return;
	d3d->texturelocked--;

	d3d->m_deviceContext->Unmap(d3d->texture2dstaging, 0);

	bool rtg = WIN32GFX_IsPicassoScreen(mon);
	if (((currprefs.leds_on_screen & STATUSLINE_CHIPSET) && !rtg) || ((currprefs.leds_on_screen & STATUSLINE_RTG) && rtg)) {
		d3d->osd.enabled = true;
		updateleds(d3d);
	} else {
		d3d->osd.enabled = false;
	}
	if (y_start < 0) {
		d3d->m_deviceContext->CopyResource(d3d->texture2d, d3d->texture2dstaging);
	} else {
		D3D11_BOX box = { 0 };
		box.right = d3d->m_bitmapWidth;
		box.top = y_start;
		box.bottom = y_end;
		box.back = 1;
		d3d->m_deviceContext->CopySubresourceRegion(d3d->texture2d, 0, 0, y_start, 0, d3d->texture2dstaging, 0, &box);
	}
}

static void xD3D11_flushtexture(int monid, int miny, int maxy)
{
	struct d3d11struct *d3d = &d3d11data[monid];
}

static void xD3D11_restore(int monid, bool checkonly)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	recheck(d3d);
}

static void xD3D11_vblank_reset(double freq)
{
}

static int xD3D11_canshaders(void)
{
	return (can_D3D11(false) & 4) != 0;
}

static int xD3D11_goodenough(void)
{
	return 1;
}

static void xD3D11_change(int monid, int temp)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	clearcnt = 0;
}

static void resizemode(struct d3d11struct *d3d)
{
	d3d->resizeretry = false;
	if (!d3d->invalidmode) {
		write_log(_T("D3D11 resizemode start\n"));
		freed3d(d3d);
		int fs = isfs(d3d);
		setswapchainmode(d3d, fs);
		write_log(_T("D3D11 resizemode %dx%d, %dx%d %d %08x FS=%d\n"), d3d->m_screenWidth, d3d->m_screenHeight, d3d->m_bitmapWidth, d3d->m_bitmapHeight,
			d3d->swapChainDesc.BufferCount, d3d->swapChainDesc.Flags, fs);
		HRESULT hr = d3d->m_swapChain->ResizeBuffers(d3d->swapChainDesc.BufferCount, d3d->m_screenWidth, d3d->m_screenHeight, d3d->scrformat, d3d->swapChainDesc.Flags);
		if (FAILED(hr)) {
			write_log(_T("ResizeBuffers %08x\n"), hr);
			if (!device_error(d3d)) {
				d3d->resizeretry = true;
			}
		}
		if (!d3d->invalidmode) {
			if (!initd3d(d3d)) {
				xD3D11_free(d3d - d3d11data, true);
				gui_message(_T("D3D11 Resize failed."));
				d3d->invalidmode = true;
			} else {
				xD3D11_alloctexture(d3d - d3d11data, d3d->m_bitmapWidth, d3d->m_bitmapHeight);
			}
		}
		write_log(_T("D3D11 resizemode end\n"));
	}
}

static void xD3D11_resize(int monid, int activate)
{
	static int recursive;
	struct d3d11struct *d3d = &d3d11data[monid];

	write_log(_T("D3D11_resize %d %d %d (%d)\n"), activate, d3d->fsmodechange, d3d->fsmode, d3d->guimode);

	if (d3d->delayedfs)
		return;

	if (d3d->guimode && isfullscreen() > 0)
		return;

	if (quit_program == -UAE_QUIT || quit_program == UAE_QUIT)
		return;

	if (activate) {
		d3d->fsmode = activate;
		d3d->fsmodechange = true;
		ShowWindow(d3d->ahwnd, d3d->fsmode > 0 ? SW_SHOWNORMAL : SW_MINIMIZE);
		write_log(_T("D3D11 resize activate\n"));
	}

	d3d->fsresizedo = true;
}

static void xD3D11_guimode(int monid, int guion)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	d3d->reloadshaders = true;

	if (isfullscreen() <= 0)
		return;
	
	write_log(_T("fs guimode %d\n"), guion);
	d3d->guimode = guion;
	if (guion > 0) {
		xD3D11_free(d3d - d3d11data, true);
		ShowWindow(d3d->ahwnd, SW_HIDE);
	} else if (guion == 0) {
		d3d->delayedfs = 1;
	}
	write_log(_T("fs guimode end\n"));
}

static int xD3D11_isenabled(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	return d3d->m_device != NULL ? 2 : 0;
}

static bool xD3D_getvblankpos(int *vp)
{
	*vp = 0;
	return false;
}

static HDC xD3D_getDC(int monid, HDC hdc)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	IDXGISurface1 *g_pSurface1 = NULL;
	HRESULT hr;

	if (hdc) {
		RECT empty = { 0 };
		g_pSurface1->ReleaseDC(&empty);
		g_pSurface1->Release();
		return NULL;
	} else {
		HDC g_hDC;
		//Setup the device and and swapchain
		hr = d3d->m_swapChain->GetBuffer(0, __uuidof(IDXGISurface1), (void**)&g_pSurface1);
		if (FAILED(hr)) {
			write_log(_T("GetDC GetBuffer() %08x\n"), hr);
			return NULL;
		}
		hr = g_pSurface1->GetDC(FALSE, &g_hDC);
		if (FAILED(hr)) {
			write_log(_T("GetDC GetDC() %08x\n"), hr);
			g_pSurface1->Release();
			return NULL;
		}
		d3d->hdc_surface = g_pSurface1;
		return g_hDC;
	}
}

bool D3D11_capture(int monid, void **data, int *w, int *h, int *pitch)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	HRESULT hr;

	if (!d3d->screenshottexture)
		return false;

	if (!w || !h) {
		d3d->m_deviceContext->Unmap(d3d->screenshottexture, 0);
		return true;
	} else {
		D3D11_MAPPED_SUBRESOURCE map;
		ID3D11Resource* pSurface = NULL;
		d3d->m_renderTargetView->GetResource(&pSurface);
		if (pSurface) {
			d3d->m_deviceContext->CopyResource(d3d->screenshottexture, pSurface);
			D3D11_TEXTURE2D_DESC desc;
			d3d->screenshottexture->GetDesc(&desc);
			hr = d3d->m_deviceContext->Map(d3d->screenshottexture, 0, D3D11_MAP_READ, 0, &map);
			if (FAILED(hr)) {
				write_log(_T("Screenshot DeviceContext->Map() failed %08x\n"), hr);
				return false;
			}
			pSurface->Release();
			*data = map.pData;
			*pitch = map.RowPitch;
			*w = desc.Width;
			*h = desc.Height;
			return true;
		} else {
			write_log(_T("Screenshot RenderTargetView->GetResource() failed\n"));
		}
	}
	return false;
}

static bool xD3D_setcursor(int monid, int x, int y, int width, int height, bool visible, bool noscale)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	//write_log(_T("setcursor %d %dx%d %dx%d %d %d\n"), monid, x, y, width, height, visible, noscale);

	if (width < 0 || height < 0)
		return true;

	if (width && height) {
		d3d->cursor_offset2_x = d3d->cursor_offset_x * d3d->m_screenWidth / width;
		d3d->cursor_offset2_y = d3d->cursor_offset_y * d3d->m_screenHeight / height;
		d3d->cursor_x = x * d3d->m_screenWidth / width;
		d3d->cursor_y = y * d3d->m_screenHeight / height;
	} else {
		d3d->cursor_x = d3d->cursor_y = 0;
		d3d->cursor_offset2_x = d3d->cursor_offset2_y = 0;
	}

	//write_log(_T("%.1fx%.1f %dx%d\n"), d3d->cursor_x, d3d->cursor_y, d3d->cursor_offset2_x, d3d->cursor_offset2_y);

	float multx = 1.0;
	float multy = 1.0;
	if (d3d->cursor_scale) {
		multx = ((float)(d3d->m_screenWidth) / ((d3d->m_bitmapWidth * d3d->dmult) + 2 * d3d->cursor_offset2_x));
		multy = ((float)(d3d->m_screenHeight) / ((d3d->m_bitmapHeight * d3d->dmult) + 2 * d3d->cursor_offset2_y));
	}
	setspritescaling(&d3d->hwsprite, 1.0 / multx, 1.0 / multy);

	d3d->hwsprite.x = d3d->cursor_x * multx + d3d->cursor_offset2_x * multx;
	d3d->hwsprite.y = d3d->cursor_y * multy + d3d->cursor_offset2_y * multy;

	//write_log(_T("-> %.1fx%.1f %.1f %.1f\n"), d3d->hwsprite.x, d3d->hwsprite.y, multx, multy);

	d3d->cursor_scale = !noscale;
	d3d->cursor_v = visible;
	d3d->hwsprite.enabled = visible;
	return true;
}

static uae_u8 *xD3D_setcursorsurface(int monid, int *pitch)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	if (!d3d->hwsprite.texture)
		return NULL;
	if (pitch) {
		D3D11_MAPPED_SUBRESOURCE map;
		HRESULT hr = d3d->m_deviceContext->Map(d3d->hwsprite.texture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
		if (FAILED(hr)) {
			write_log(_T("HWSprite Map failed %08x\n"), hr);
			return NULL;
		}
		*pitch = map.RowPitch;
		return (uae_u8*)map.pData;
	} else {
		d3d->m_deviceContext->Unmap(d3d->hwsprite.texture, 0);
		return NULL;
	}
}

static bool xD3D11_getscalerect(int monid, float *mx, float *my, float *sx, float *sy)
{
	struct d3d11struct *d3d = &d3d11data[monid];
	struct vidbuf_description *vidinfo = &adisplays[monid].gfxvidinfo;
	if (!d3d->mask2texture.enabled)
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

static bool xD3D11_run(int monid)
{
	struct d3d11struct *d3d = &d3d11data[monid];

	if (xD3D11_quit(d3d))
		return false;
	if (recheck(d3d))
		return true;
	return D3D11_resize_do(d3d);
}

void d3d11_select(void)
{
	D3D_free = xD3D11_free;
	D3D_init = xD3D11_init;
	D3D_renderframe = xD3D11_renderframe;
	D3D_alloctexture = xD3D11_alloctexture;
	D3D_refresh = xD3D11_refresh;
	D3D_restore = xD3D11_restore;

	D3D_locktexture = xD3D11_locktexture;
	D3D_unlocktexture = xD3D11_unlocktexture;
	D3D_flushtexture = xD3D11_flushtexture;

	D3D_showframe = xD3D11_showframe;
	D3D_showframe_special = xD3D11_showframe_special;
	D3D_guimode = xD3D11_guimode;
	D3D_getDC = xD3D_getDC;
	D3D_isenabled = xD3D11_isenabled;
	D3D_clear = xD3D11_clear;
	D3D_canshaders = xD3D11_canshaders;
	D3D_goodenough = xD3D11_goodenough;
	D3D_setcursor = xD3D_setcursor;
	D3D_setcursorsurface = xD3D_setcursorsurface;
	D3D_getrefreshrate = xD3D_getrefreshrate;
	D3D_resize = xD3D11_resize;
	D3D_change = xD3D11_change;
	D3D_getscalerect = xD3D11_getscalerect;
	D3D_run = xD3D11_run;
	D3D_debug = xD3D11_debug;
	D3D_led = xD3D11_led;
	D3D_getscanline = NULL;
}

void d3d_select(struct uae_prefs *p)
{
	if (p->gfx_api == 2)
		d3d11_select();
	else
		d3d9_select();
}
