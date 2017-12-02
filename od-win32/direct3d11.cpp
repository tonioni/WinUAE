
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

#include "d3dx.h"

void (*D3D_free)(bool immediate);
const TCHAR* (*D3D_init)(HWND ahwnd, int w_w, int h_h, int depth, int *freq, int mmult);
bool (*D3D_alloctexture)(int, int);
void(*D3D_refresh)(void);
bool(*D3D_renderframe)(bool);
void(*D3D_showframe)(void);
void(*D3D_showframe_special)(int);
uae_u8* (*D3D_locktexture)(int*, int*, bool);
void (*D3D_unlocktexture)(void);
void (*D3D_flushtexture)(int miny, int maxy);
void (*D3D_guimode)(int);
HDC (*D3D_getDC)(HDC hdc);
int (*D3D_isenabled)(void);
void (*D3D_clear)(void);
int (*D3D_canshaders)(void);
int (*D3D_goodenough)(void);
bool (*D3D_setcursor)(int x, int y, int width, int height, bool visible, bool noscale);
bool (*D3D_getvblankpos)(int *vpos);
double (*D3D_getrefreshrate)(void);
void (*D3D_vblank_reset)(double freq);
void(*D3D_restore)(void);
void(*D3D_resize)(int);
void (*D3D_change)(int);

static HANDLE hd3d11, hdxgi, hd3dcompiler;

static struct gfx_filterdata *filterd3d;
static int filterd3didx;

#define SHADERTYPE_BEFORE 1
#define SHADERTYPE_AFTER 2
#define SHADERTYPE_MIDDLE 3
#define SHADERTYPE_MASK_BEFORE 3
#define SHADERTYPE_MASK_AFTER 4
#define SHADERTYPE_POST 10

struct shaderdata11
{
	int type;
	ID3D11ShaderResourceView *masktexturerv;
	ID3D11Texture2D *masktexture;
	int masktexture_w, masktexture_h;
};
#define MAX_SHADERS (2 * MAX_FILTERSHADERS + 2)

struct d3d11struct
{
	IDXGISwapChain1* m_swapChain;
	ID3D11Device* m_device;
	ID3D11DeviceContext* m_deviceContext;
	ID3D11RenderTargetView* m_renderTargetView;
	ID3D11RasterizerState* m_rasterState;
	D3DXMATRIX m_worldMatrix;
	D3DXMATRIX m_orthoMatrix;
	ID3D11Buffer *m_vertexBuffer, *m_indexBuffer;
	ID3D11Buffer* m_matrixBuffer;
	int m_screenWidth, m_screenHeight;
	int m_bitmapWidth, m_bitmapHeight;
	int m_vertexCount, m_indexCount;
	float m_positionX, m_positionY, m_positionZ;
	float m_rotationX, m_rotationY, m_rotationZ;
	D3DXMATRIX m_viewMatrix;
	ID3D11ShaderResourceView *texture2drv;
	ID3D11ShaderResourceView *sltexturerv;
	ID3D11ShaderResourceView *ledtexturerv;
	ID3D11Texture2D *texture2d, *texture2dstaging;
	ID3D11Texture2D *sltexture, *mask2texture;
	ID3D11Texture2D *screenshottexture;
	ID3D11Texture2D *ledtexture;
	ID3D11VertexShader *m_vertexShader;
	ID3D11PixelShader *m_pixelShader, *m_pixelShaderSL, *m_pixelShaderMask;
	ID3D11SamplerState *m_sampleState_point_clamp, *m_sampleState_linear_clamp;
	ID3D11SamplerState *m_sampleState_point_wrap, *m_sampleState_linear_wrap;
	ID3D11InputLayout *m_layout;
	int texturelocked;
	DXGI_FORMAT format;
	bool m_tearingSupport;
	int dmult;
	int xoffset, yoffset;
	float xmult, ymult;
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc;
	DXGI_SWAP_CHAIN_FULLSCREEN_DESC  fsSwapChainDesc;
	IDXGIOutput *outputAdapter;
	HWND ahwnd;
	int fsmode;
	bool fsmodechange;
	bool invalidmode;

	float mask2texture_w, mask2texture_h, mask2texture_ww, mask2texture_wh;
	float mask2texture_wwx, mask2texture_hhx, mask2texture_minusx, mask2texture_minusy;
	float mask2texture_multx, mask2texture_multy, mask2texture_offsetw;
	RECT mask2rect;

	IDXGISurface1 *hdc_surface;

	RECT sr2, dr2, zr2;
	int guimode;
	int ledwidth, ledheight;
	int statusbar_hx, statusbar_vx;

	struct shaderdata11 shaders[MAX_SHADERS];
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

static struct d3d11struct d3d11data[1];

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

static PFN_D3D11_CREATE_DEVICE pD3D11CreateDevice;
static CREATEDXGIFACTORY1 pCreateDXGIFactory1;
static D3DCOMPILEFROMFILE pD3DCompileFromFile;
static D3DCOMPILE ppD3DCompile;

static const char *uae_shader_ps =
{
	"Texture2D shaderTexture;\n"
	"Texture2D maskTexture;\n"
	"SamplerState SampleTypeClamp;\n"
	"SamplerState SampleTypeWrap;\n"
	"struct PixelInputType\n"
	"{\n"
	"	float4 position : SV_POSITION;\n"
	"	float2 tex : TEXCOORD0;\n"
	"	float2 sl : TEXCOORD1;\n"
	"};\n"
	"float4 PS_PostPlain(PixelInputType input) : SV_TARGET\n"
	"{\n"
	"	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);\n"
	"	return textureColor;\n"
	"}\n"
	"float4 PS_PostMask(PixelInputType input) : SV_TARGET\n"
	"{\n"
	"	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);\n"
	"	float4 maskColor = maskTexture.Sample(SampleTypeWrap, input.sl);\n"
	"	return textureColor * maskColor;\n"
	"}\n"
	"float4 PS_PostAlpha(PixelInputType input) : SV_TARGET\n"
	"{\n"
	"	float4 textureColor = shaderTexture.Sample(SampleTypeClamp, input.tex);\n"
	"	float4 maskColor = maskTexture.Sample(SampleTypeWrap, input.sl);\n"
	"	return textureColor * (1 - maskColor.a) + (maskColor * maskColor.a);\n"
	"}\n"
};
static const char *uae_shader_vs =
{
	"cbuffer MatrixBuffer\n"
	"{\n"
	"	matrix worldMatrix;\n"
	"	matrix viewMatrix;\n"
	"	matrix projectionMatrix;\n"
	"};\n"
	"struct VertexInputType\n"
	"{\n"
	"	float4 position : POSITION;\n"
	"	float2 tex : TEXCOORD0;\n"
	"	float2 sl : TEXCOORD1;\n"
	"};\n"
	"struct PixelInputType\n"
	"{\n"
	"	float4 position : SV_POSITION;\n"
	"	float2 tex : TEXCOORD0;\n"
	"	float2 sl : TEXCOORD1;\n"
	"};\n"
	"PixelInputType TextureVertexShader(VertexInputType input)\n"
	"{\n"
	"	PixelInputType output;\n"
	"	input.position.w = 1.0f;\n"
	"	output.position = mul(input.position, projectionMatrix);\n"
	"	output.position.z = 0.0f;\n"
	"	output.tex = input.tex;\n"
	"	output.sl = input.sl;\n"
	"	return output;\n"
	"}\n"
};

static int isfs(struct d3d11struct *d3d)
{
	int fs = isfullscreen();
	if (fs > 0 && d3d->guimode)
		return -1;
	return fs;
}

static bool UpdateBuffers(struct d3d11struct *d3d)
{
	float left, right, top, bottom;
	VertexType* vertices;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	VertexType* verticesPtr;
	HRESULT result;
	int positionX, positionY;

	positionX = (d3d->m_screenWidth - d3d->m_bitmapWidth) / 2 + d3d->xoffset;
	positionY = (d3d->m_screenHeight - d3d->m_bitmapHeight) / 2 + d3d->yoffset;

	// Calculate the screen coordinates of the left side of the bitmap.
	left = (d3d->m_screenWidth + 1) / -2;
	left += positionX;

	// Calculate the screen coordinates of the right side of the bitmap.
	right = left + d3d->m_bitmapWidth;

	// Calculate the screen coordinates of the top of the bitmap.
	top = (d3d->m_screenHeight + 1) / 2;
	top -= positionY;

	// Calculate the screen coordinates of the bottom of the bitmap.
	bottom = top - d3d->m_bitmapHeight;

	float slleft = 0;
	float sltop = 0;
	float slright = (float)d3d->m_bitmapWidth / d3d->m_screenWidth * d3d->xmult;
	float slbottom = (float)d3d->m_bitmapHeight / d3d->m_screenHeight * d3d->ymult;

	slright = slleft + slright;
	slbottom = sltop + slbottom;

	left *= d3d->xmult;
	right *= d3d->xmult;
	top *= d3d->ymult;
	bottom *= d3d->ymult;

	write_log(_T("-> %f %f %f %f %f %f\n"), left, top, right, bottom, d3d->xmult, d3d->ymult);

	// Create the vertex array.
	vertices = new VertexType[d3d->m_vertexCount];
	if (!vertices)
	{
		return false;
	}

	// Load the vertex array with data.
	// First triangle.
	vertices[0].position = D3DXVECTOR3(left, top, 0.0f);  // Top left.
	vertices[0].texture = D3DXVECTOR2(0.0f, 0.0f);
	vertices[0].sltexture = D3DXVECTOR2(slleft, sltop);

	vertices[1].position = D3DXVECTOR3(right, bottom, 0.0f);  // Bottom right.
	vertices[1].texture = D3DXVECTOR2(1.0f, 1.0f);
	vertices[1].sltexture = D3DXVECTOR2(slright, slbottom);

	vertices[2].position = D3DXVECTOR3(left, bottom, 0.0f);  // Bottom left.
	vertices[2].texture = D3DXVECTOR2(0.0f, 1.0f);
	vertices[2].sltexture = D3DXVECTOR2(slleft, slbottom);

	// Second triangle.
	vertices[3].position = D3DXVECTOR3(left, top, 0.0f);  // Top left.
	vertices[3].texture = D3DXVECTOR2(0.0f, 0.0f);
	vertices[3].sltexture = D3DXVECTOR2(slleft, sltop);

	vertices[4].position = D3DXVECTOR3(right, top, 0.0f);  // Top right.
	vertices[4].texture = D3DXVECTOR2(1.0f, 0.0f);
	vertices[4].sltexture = D3DXVECTOR2(slright, sltop);

	vertices[5].position = D3DXVECTOR3(right, bottom, 0.0f);  // Bottom right.
	vertices[5].texture = D3DXVECTOR2(1.0f, 1.0f);
	vertices[5].sltexture = D3DXVECTOR2(slright, slbottom);

	// Lock the vertex buffer so it can be written to.
	result = d3d->m_deviceContext->Map(d3d->m_vertexBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		write_log(_T("ID3D11DeviceContext map(vertex) %08x\n"), result);
		return false;
	}

	// Get a pointer to the data in the vertex buffer.
	verticesPtr = (VertexType*)mappedResource.pData;

	// Copy the data into the vertex buffer.
	memcpy(verticesPtr, (void*)vertices, (sizeof(VertexType) * d3d->m_vertexCount));

	// Unlock the vertex buffer.
	d3d->m_deviceContext->Unmap(d3d->m_vertexBuffer, 0);

	// Release the vertex array as it is no longer needed.
	delete[] vertices;
	vertices = 0;

	return true;
}

static void setupscenecoords(struct d3d11struct *d3d)
{
	RECT sr, dr, zr;

	getfilterrect2(&dr, &sr, &zr, d3d->m_screenWidth, d3d->m_screenHeight, d3d->m_bitmapWidth / d3d->dmult, d3d->m_bitmapHeight / d3d->dmult, d3d->dmult, d3d->m_bitmapWidth, d3d->m_bitmapHeight);

	if (!memcmp(&sr, &d3d->sr2, sizeof RECT) && !memcmp(&dr, &d3d->dr2, sizeof RECT) && !memcmp(&zr, &d3d->zr2, sizeof RECT)) {
		return;
	}
	if (1) {
		write_log(_T("POS (%d %d %d %d) - (%d %d %d %d)[%d,%d] (%d %d)\n"),
			dr.left, dr.top, dr.right, dr.bottom, sr.left, sr.top, sr.right, sr.bottom,
			sr.right - sr.left, sr.bottom - sr.top,
			zr.left, zr.top);
	}
	d3d->sr2 = sr;
	d3d->dr2 = dr;
	d3d->zr2 = zr;

	float dw = dr.right - dr.left;
	float dh = dr.bottom - dr.top;
	float w = sr.right - sr.left;
	float h = sr.bottom - sr.top;

	int tx = dw * d3d->m_bitmapWidth / d3d->m_screenWidth / 2;
	int ty = dh * d3d->m_bitmapHeight / d3d->m_screenHeight / 2;

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

	write_log(_T("%d %d %.f %.f\n"), d3d->xoffset, d3d->yoffset, d3d->xmult, d3d->ymult);

	UpdateBuffers(d3d);
}

static void updateleds(struct d3d11struct *d3d)
{
	HRESULT hr;
	D3D11_MAPPED_SUBRESOURCE map;
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

	hr = d3d->m_deviceContext->Map(d3d->ledtexture, 0, D3D11_MAP_WRITE_DISCARD, 0, &map);
	if (FAILED(hr)) {
		write_log(_T("Led Map failed %08x\n"), hr);
		return;
	}
	for (int y = 0; y < TD_TOTAL_HEIGHT * d3d->statusbar_vx; y++) {
		uae_u8 *buf = (uae_u8*)map.pData + y * map.RowPitch;
		statusline_single_erase(buf, 32 / 8, y, d3d->ledwidth * d3d->statusbar_hx);
	}
	statusline_render((uae_u8*)map.pData, 32 / 8, map.RowPitch, d3d->ledwidth, d3d->ledheight, rc, gc, bc, a);

	int y = 0;
	for (int yy = 0; yy < d3d->statusbar_vx * TD_TOTAL_HEIGHT; yy++) {
		uae_u8 *buf = (uae_u8*)map.pData + yy * map.RowPitch;
		draw_status_line_single(buf, 32 / 8, y, d3d->ledwidth, rc, gc, bc, a);
		if ((yy % d3d->statusbar_vx) == 0)
			y++;
	}

	d3d->m_deviceContext->Unmap(d3d->ledtexture, 0);
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

static void FreeTexture(struct d3d11struct *d3d)
{
	FreeTexture2D(&d3d->texture2d, &d3d->texture2drv);
	FreeTexture2D(&d3d->texture2dstaging, NULL);
	FreeTexture2D(&d3d->screenshottexture, NULL);
	FreeTexture2D(&d3d->ledtexture, &d3d->ledtexturerv);

	for (int i = 0; i < MAX_SHADERS; i++) {
		FreeTexture2D(&d3d->shaders[i].masktexture, &d3d->shaders[i].masktexturerv);
		memset(&d3d->shaders[i], 0, sizeof(struct shaderdata11));
	}
}

static bool CreateTexture(struct d3d11struct *d3d)
{
	D3D11_TEXTURE2D_DESC desc;
	HRESULT hr;

	memset(&d3d->sr2, 0, sizeof(RECT));
	memset(&d3d->dr2, 0, sizeof(RECT));
	memset(&d3d->zr2, 0, sizeof(RECT));

	FreeTexture(d3d);

	memset(&desc, 0, sizeof desc);
	desc.Width = d3d->m_bitmapWidth;
	desc.Height = d3d->m_bitmapHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->format;
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
	desc.Format = d3d->format;
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

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->format;

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

	memset(&desc, 0, sizeof desc);
	d3d->ledwidth = d3d->m_screenWidth;
	d3d->ledheight = TD_TOTAL_HEIGHT;
	if (d3d->statusbar_hx < 1)
		d3d->statusbar_hx = 1;
	if (d3d->statusbar_vx < 1)
		d3d->statusbar_vx = 1;
	desc.Width = d3d->ledwidth;
	desc.Height = d3d->ledheight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->format;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	hr = d3d->m_device->CreateTexture2D(&desc, NULL, &d3d->ledtexture);
	if (FAILED(hr)) {
		write_log(_T("CreateTexture2D (led) failed: %08x\n"), hr);
		return false;
	}

	hr = d3d->m_device->CreateShaderResourceView(d3d->ledtexture, &srvDesc, &d3d->ledtexturerv);
	if (FAILED(hr)) {
		write_log(_T("CreateShaderResourceView Led failed: %08x\n"), hr);
		FreeTexture2D(&d3d->ledtexture, NULL);
		return false;
	}

	return true;
}

static bool createsltexture(struct d3d11struct *d3d)
{
	D3D11_TEXTURE2D_DESC desc;
	HRESULT hr;

	FreeTexture2D(&d3d->sltexture, &d3d->sltexturerv);

	memset(&desc, 0, sizeof desc);
	desc.Width = d3d->m_screenWidth;
	desc.Height = d3d->m_screenHeight;
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = d3d->format;
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

	D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc;
	srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = desc.MipLevels;
	srvDesc.Format = d3d->format;

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

static int createmask2texture(struct d3d11struct *d3d, const TCHAR *filename)
{
	struct zfile *zf;
	HRESULT hr;
	TCHAR tmp[MAX_DPATH];
	ID3D11Texture2D *tx = NULL;

	FreeTexture2D(&d3d->mask2texture, NULL);

	if (filename[0] == 0 || WIN32GFX_IsPicassoScreen())
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
				zf = zfile_fopen(tmp3, _T("rb"), ZFD_NORMAL);
				if (zf)
					break;
			}
		}
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
		write_log(_T("Overlay texture load failed %08x\n"), hr);
		goto end;
	}
	d3d->mask2texture_w = img.width;
	d3d->mask2texture_h = img.height;
	d3d->mask2texture = tx;
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
		struct MultiDisplay *md = getdisplay(&currprefs);
		float deskw = md->rect.right - md->rect.left;
		float deskh = md->rect.bottom - md->rect.top;
		//deskw = 800; deskh = 600;
		float dstratio = deskw / deskh;
		float srcratio = d3d->mask2texture_w / d3d->mask2texture_h;
		d3d->mask2texture_multx *= srcratio / dstratio;
	} else {
		d3d->mask2texture_multx = d3d->mask2texture_multy;
	}

	d3d->mask2texture_wh = d3d->m_screenWidth;
	d3d->mask2texture_ww = d3d->mask2texture_w * d3d->mask2texture_multx;

	d3d->mask2texture_offsetw = (d3d->m_screenWidth - d3d->mask2texture_ww) / 2;

#if 0
	if (d3d->mask2texture_offsetw > 0) {
		d3d->blanktexture = createtext(d3d, d3d->mask2texture_offsetw + 1, d3d->m_screenHeight, D3DFMT_X8R8G8B8);
	}
#endif

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

	write_log(_T("Overlay '%s' %.0f*%.0f (%d*%d - %d*%d) (%d*%d)\n"),
		tmp, d3d->mask2texture_w, d3d->mask2texture_h,
		d3d->mask2rect.left, d3d->mask2rect.top, d3d->mask2rect.right, d3d->mask2rect.bottom,
		d3d->mask2rect.right - d3d->mask2rect.left, d3d->mask2rect.bottom - d3d->mask2rect.top);

	return 1;
end:
	if (tx)
		tx->Release();
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
	desc.Format = d3d->format;
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
	srvDesc.Format = d3d->format;

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

static bool TextureShaderClass_InitializeShader(struct d3d11struct *d3d)
{
	HRESULT result;
	ID3D10Blob* errorMessage;
	ID3D10Blob* vertexShaderBuffer;
	D3D11_INPUT_ELEMENT_DESC polygonLayout[3];
	D3D11_SAMPLER_DESC samplerDesc;
	unsigned int numElements;
	D3D11_BUFFER_DESC matrixBufferDesc;
	bool plugin_path;
	TCHAR tmp[MAX_DPATH], tmp2[MAX_DPATH];

	plugin_path = get_plugin_path(tmp, sizeof tmp / sizeof(TCHAR), _T("filtershaders\\direct3d11"));

	// Initialize the pointers this function will use to null.
	errorMessage = 0;
	vertexShaderBuffer = 0;

	// Compile the vertex shader code.
	result = E_FAIL;
	if (plugin_path) {
		_tcscpy(tmp2, tmp);
		_tcscat(tmp2, _T("_winuae.vs"));
		result = pD3DCompileFromFile(tmp2, NULL, NULL, "TextureVertexShader", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertexShaderBuffer, &errorMessage);
	}
	if (FAILED(result))
	{
		if (plugin_path) {
			OutputShaderErrorMessage(errorMessage, d3d->ahwnd, tmp2);
			write_log(_T("Trying built-in shader.\n"));
		}
		result = ppD3DCompile(uae_shader_vs, strlen(uae_shader_vs), "uae_shader_vs", NULL, NULL, "TextureVertexShader", "vs_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &vertexShaderBuffer, &errorMessage);
		if (FAILED(result))
		{
			OutputShaderErrorMessage(errorMessage, d3d->ahwnd, tmp2);
			return false;
		}
	}

	// Create the vertex shader from the buffer.
	result = d3d->m_device->CreateVertexShader(vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), NULL, &d3d->m_vertexShader);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateVertexShader %08x\n"), result);
		return false;
	}

	for (int i = 0; i < 3; i++) {
		ID3D10Blob* pixelShaderBuffer = NULL;
		ID3D11PixelShader **ps = NULL;
		char *name;

		switch (i)
		{
		case 0:
			name = "PS_PostPlain";
			ps = &d3d->m_pixelShader;
			break;
		case 1:
			name = "PS_PostMask";
			ps = &d3d->m_pixelShaderMask;
			break;
		case 2:
			name = "PS_PostAlpha";
			ps = &d3d->m_pixelShaderSL;
			break;
		}
		// Compile the pixel shader code.
		result = E_FAIL;
		if (plugin_path) {
			_tcscpy(tmp2, tmp);
			_tcscat(tmp2, _T("_winuae.ps"));
			result = pD3DCompileFromFile(tmp2, NULL, NULL, name, "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixelShaderBuffer, &errorMessage);
		}
		if (FAILED(result))
		{
			if (plugin_path) {
				OutputShaderErrorMessage(errorMessage, d3d->ahwnd, tmp2);
				write_log(_T("Trying built-in shader.\n"));
			}
			result = ppD3DCompile(uae_shader_ps, strlen(uae_shader_ps), "uae_shader_ps", NULL, NULL, name, "ps_5_0", D3DCOMPILE_ENABLE_STRICTNESS, 0, &pixelShaderBuffer, &errorMessage);
			if (FAILED(result)) {
				OutputShaderErrorMessage(errorMessage, d3d->ahwnd, tmp2);
				return false;
			}
		}

		// Create the pixel shader from the buffer.
		result = d3d->m_device->CreatePixelShader(pixelShaderBuffer->GetBufferPointer(), pixelShaderBuffer->GetBufferSize(), NULL, ps);
		if (FAILED(result))
		{
			write_log(_T("ID3D11Device CreatePixelShader %08x\n"), result);
			pixelShaderBuffer->Release();
			pixelShaderBuffer = 0;
			return false;
		}

		pixelShaderBuffer->Release();
		pixelShaderBuffer = 0;
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
	result = d3d->m_device->CreateInputLayout(polygonLayout, numElements, vertexShaderBuffer->GetBufferPointer(), vertexShaderBuffer->GetBufferSize(), &d3d->m_layout);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateInputLayout %08x\n"), result);
		return false;
	}

	// Release the vertex shader buffer and pixel shader buffer since they are no longer needed.
	vertexShaderBuffer->Release();
	vertexShaderBuffer = 0;

	// Setup the description of the dynamic matrix constant buffer that is in the vertex shader.
	matrixBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	matrixBufferDesc.ByteWidth = sizeof(MatrixBufferType);
	matrixBufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	matrixBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	matrixBufferDesc.MiscFlags = 0;
	matrixBufferDesc.StructureByteStride = 0;

	// Create the constant buffer pointer so we can access the vertex shader constant buffer from within this class.
	result = d3d->m_device->CreateBuffer(&matrixBufferDesc, NULL, &d3d->m_matrixBuffer);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBuffer(matrix) %08x\n"), result);
		return false;
	}

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

static bool InitializeBuffers(struct d3d11struct *d3d)
{
	VertexType* vertices;
	unsigned long* indices;
	D3D11_BUFFER_DESC vertexBufferDesc, indexBufferDesc;
	D3D11_SUBRESOURCE_DATA vertexData, indexData;
	HRESULT result;
	int i;


	// Set the number of vertices in the vertex array.
	d3d->m_vertexCount = 6;

	// Set the number of indices in the index array.
	d3d->m_indexCount = d3d->m_vertexCount;

	// Create the vertex array.
	vertices = new VertexType[d3d->m_vertexCount];
	if (!vertices)
	{
		return false;
	}

	// Create the index array.
	indices = new unsigned long[d3d->m_indexCount];
	if (!indices)
	{
		return false;
	}

	// Initialize vertex array to zeros at first.
	memset(vertices, 0, (sizeof(VertexType) * d3d->m_vertexCount));

	// Load the index array with data.
	for (i = 0; i < d3d->m_indexCount; i++)
	{
		indices[i] = i;
	}

	// Set up the description of the static vertex buffer.
	vertexBufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	vertexBufferDesc.ByteWidth = sizeof(VertexType) * d3d->m_vertexCount;
	vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertexBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	vertexBufferDesc.MiscFlags = 0;
	vertexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the vertex data.
	vertexData.pSysMem = vertices;
	vertexData.SysMemPitch = 0;
	vertexData.SysMemSlicePitch = 0;

	// Now create the vertex buffer.
	result = d3d->m_device->CreateBuffer(&vertexBufferDesc, &vertexData, &d3d->m_vertexBuffer);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBuffer(vertex) %08x\n"), result);
		return false;
	}

	// Set up the description of the static index buffer.
	indexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
	indexBufferDesc.ByteWidth = sizeof(unsigned long) * d3d->m_indexCount;
	indexBufferDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	indexBufferDesc.CPUAccessFlags = 0;
	indexBufferDesc.MiscFlags = 0;
	indexBufferDesc.StructureByteStride = 0;

	// Give the subresource structure a pointer to the index data.
	indexData.pSysMem = indices;
	indexData.SysMemPitch = 0;
	indexData.SysMemSlicePitch = 0;

	// Create the index buffer.
	result = d3d->m_device->CreateBuffer(&indexBufferDesc, &indexData, &d3d->m_indexBuffer);
	if (FAILED(result))
	{
		write_log(_T("ID3D11Device CreateBuffer(index) %08x\n"), result);
		return false;
	}

	// Release the arrays now that the vertex and index buffers have been created and loaded.
	delete[] vertices;
	vertices = 0;

	delete[] indices;
	indices = 0;

	return true;
}

static bool initd3d(struct d3d11struct *d3d)
{
	HRESULT result;
	ID3D11Texture2D* backBufferPtr;
	D3D11_RASTERIZER_DESC rasterDesc;
	D3D11_VIEWPORT viewport;

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
	rasterDesc.CullMode = D3D11_CULL_BACK;
	rasterDesc.DepthBias = 0;
	rasterDesc.DepthBiasClamp = 0.0f;
	rasterDesc.DepthClipEnable = false;
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

	// Setup the viewport for rendering.
	viewport.Width = (float)d3d->m_screenWidth;
	viewport.Height = (float)d3d->m_screenHeight;
	viewport.MinDepth = 0.0f;
	viewport.MaxDepth = 1.0f;
	viewport.TopLeftX = 0.0f;
	viewport.TopLeftY = 0.0f;

	// Create the viewport.
	d3d->m_deviceContext->RSSetViewports(1, &viewport);

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
	if (!InitializeBuffers(d3d))
		return false;
	if (!UpdateBuffers(d3d))
		return false;

	write_log(_T("D3D11 initd3d end\n"));
	return true;
}

static void setswapchainmode(struct d3d11struct *d3d, int fs)
{
	struct apmode *apm = picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	// It is recommended to always use the tearing flag when it is supported.
	d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	if (d3d->m_tearingSupport && (d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL || d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_DISCARD)) {
		d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	if (fs) {
		d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
		if (apm->gfx_backbuffers > 0)
			d3d->swapChainDesc.Flags &= ~DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
	}

	d3d->fsSwapChainDesc.Windowed = TRUE;
	//d3d->swapChainDesc.Flags |= DXGI_SWAP_CHAIN_FLAG_GDI_COMPATIBLE;
}

bool can_D3D11(bool checkdevice)
{
	if (!os_win7)
		return false;

	if (!hd3d11)
		hd3d11 = LoadLibrary(_T("D3D11.dll"));
	if (!hdxgi)
		hdxgi = LoadLibrary(_T("Dxgi.dll"));
	if (!hd3dcompiler)
		hd3dcompiler = LoadLibrary(_T("D3DCompiler_47.dll"));

	if (!hd3d11 || !hdxgi || !hd3dcompiler) {
		write_log(_T("D3D11.dll=%p Dxgi.dll=%p D3DCompiler_47.dll=%p\n"), hd3d11, hdxgi, hd3dcompiler);
		return false;
	}

	pD3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(GetModuleHandle(_T("D3D11.dll")), "D3D11CreateDevice");
	pCreateDXGIFactory1 = (CREATEDXGIFACTORY1)GetProcAddress(GetModuleHandle(_T("Dxgi.dll")), "CreateDXGIFactory1");
	pD3DCompileFromFile = (D3DCOMPILEFROMFILE)GetProcAddress(GetModuleHandle(_T("D3DCompiler_47.dll")), "D3DCompileFromFile");
	ppD3DCompile = (D3DCOMPILE)GetProcAddress(GetModuleHandle(_T("D3DCompiler_47.dll")), "D3DCompile");

	if (!pD3D11CreateDevice || !pCreateDXGIFactory1 || !pD3DCompileFromFile || !ppD3DCompile) {
		write_log(_T("pD3D11CreateDevice=%p pCreateDXGIFactory1=%p pD3DCompileFromFile=%p ppD3DCompile=%p\n"),
			pD3D11CreateDevice, pCreateDXGIFactory1, pD3DCompileFromFile, ppD3DCompile);
		return false;
	}

	if (checkdevice) {
		static const D3D_FEATURE_LEVEL levels0[] = { D3D_FEATURE_LEVEL_11_0 };
		UINT cdflags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
		ID3D11Device *m_device;
		ID3D11DeviceContext *m_deviceContext;
		HRESULT hr = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, cdflags, levels0, 1, D3D11_SDK_VERSION, &m_device, NULL, &m_deviceContext);
		if (FAILED(hr)) {
			return false;
		}
		m_deviceContext->Release();
		m_device->Release();
	}
	return true;
}

static bool xxD3D11_init(HWND ahwnd, int w_w, int w_h, int depth, int *freq, int mmult)
{
	struct d3d11struct *d3d = &d3d11data[0];
	struct apmode *apm = picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];

	HRESULT result;
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

	write_log(_T("D3D11 init start\n"));

	filterd3didx = picasso_on;
	filterd3d = &currprefs.gf[filterd3didx];

	if (depth != 32 && depth != 16)
		return false;

	if (!can_D3D11(false))
		return false;

	d3d->m_bitmapWidth = w_w;
	d3d->m_bitmapHeight = w_h;
	d3d->m_screenWidth = w_w;
	d3d->m_screenHeight = w_h;
	d3d->ahwnd = ahwnd;
	d3d->format = depth == 32 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_B5G6R5_UNORM;

	struct MultiDisplay *md = getdisplay(&currprefs);
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
			return false;
		}
	} else {
		BOOL allowTearing = FALSE;
		result = factory4.As(&factory5);
		factory2 = factory5;
		if (!d3d->m_tearingSupport) {
			result = factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
			d3d->m_tearingSupport = SUCCEEDED(result) && allowTearing;
			write_log(_T("CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING) = %08x %d\n"), result, allowTearing);
		}
	}

	// Use the factory to create an adapter for the primary graphics interface (video card).
	result = factory2->EnumAdapters1(0, &adapter);
	if (FAILED(result))
	{
		write_log(_T("IDXGIFactory2 EnumAdapters1 %08x\n"), result);
		return false;
	}
	result = adapter->GetDesc1(&adesc);

	UINT adapterNum = 0;
	bool outputFound = false;
	// Enumerate the primary adapter output (monitor).
	while (adapter->EnumOutputs(adapterNum, &adapterOutput) != DXGI_ERROR_NOT_FOUND) {
		result = adapterOutput->GetDesc(&odesc);
		if (SUCCEEDED(result)) {
			if (odesc.Monitor == winmon || !_tcscmp(odesc.DeviceName, md->adapterid)) {
				outputFound = true;
				break;
			}
		}
		adapterNum++;
	}
	if (!outputFound) {
		result = adapter->EnumOutputs(0, &adapterOutput);
		if (FAILED(result)) {
			write_log(_T("EnumOutputs %08x\n"), result);
			return false;
		}
	}

	ComPtr<IDXGIOutput1> adapterOutput1;
	result = adapterOutput->QueryInterface(__uuidof(IDXGIOutput1), &adapterOutput1);
	if (FAILED(result)) {
		write_log(_T("IDXGIOutput QueryInterface %08x\n"), result);
	}


	// Get the number of modes that fit the display format for the adapter output (monitor).
	result = adapterOutput1->GetDisplayModeList1(d3d->format, DXGI_ENUM_MODES_INTERLACED, &numModes, NULL);
	if (FAILED(result))
	{
		write_log(_T("IDXGIOutput1 GetDisplayModeList1 %08x\n"), result);
		return false;
	}

	// Create a list to hold all the possible display modes for this monitor/video card combination.
	displayModeList = new DXGI_MODE_DESC1[numModes];
	if (!displayModeList)
	{
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return false;
	}

	// Now fill the display mode list structures.
	result = adapterOutput1->GetDisplayModeList1(d3d->format, DXGI_ENUM_MODES_INTERLACED, &numModes, displayModeList);
	if (FAILED(result))
	{
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return false;
	}

	ZeroMemory(&d3d->fsSwapChainDesc, sizeof(d3d->fsSwapChainDesc));

	int hz = getrefreshrate(w_w, w_h);

	// Now go through all the display modes and find the one that matches the screen width and height.
	// When a match is found store the numerator and denominator of the refresh rate for that monitor.
	d3d->fsSwapChainDesc.RefreshRate.Denominator = 0;
	d3d->fsSwapChainDesc.RefreshRate.Numerator = 0;
	for (int i = 0; i < numModes; i++)
	{
		DXGI_MODE_DESC1 *m = &displayModeList[i];
		if (m->Format != d3d->format)
			continue;
		if (apm->gfx_interlaced && !(m->ScanlineOrdering & DXGI_MODE_SCANLINE_ORDER_UPPER_FIELD_FIRST))
			continue;
		if (m->Width == w_w && m->Height == w_h) {
			d3d->fsSwapChainDesc.ScanlineOrdering = m->ScanlineOrdering;
			d3d->fsSwapChainDesc.Scaling = m->Scaling;
			if (!hz)
				break;
			if (isfs(d3d) > 0) {
				double mhz = (double)m->RefreshRate.Numerator / m->RefreshRate.Denominator;
				if ((int)(mhz + 0.5) == hz || (int)(mhz) == hz) {
					d3d->fsSwapChainDesc.RefreshRate.Denominator = m->RefreshRate.Denominator;
					d3d->fsSwapChainDesc.RefreshRate.Numerator = m->RefreshRate.Numerator;
					break;
				}
			}
		}
	}
	if (isfs(d3d) > 0 && (hz == 0 || (d3d->fsSwapChainDesc.RefreshRate.Denominator == 0 && d3d->fsSwapChainDesc.RefreshRate.Numerator == 0))) {
		DXGI_MODE_DESC1 md1 = { 0 }, md2;
		md1.Format = d3d->format;
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
		}
	}

	// Get the adapter (video card) description.
	result = adapter->GetDesc(&adapterDesc);
	if (FAILED(result)) {
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return false;
	}

	result = adapterOutput->GetDesc(&odesc);
	if (FAILED(result)) {
		write_log(_T("IDXGIAdapter1 GetDesc %08x\n"), result);
		return false;
	}

	write_log(_T("D3D11 Device: %s [%s] (%d,%d,%d,%d)\n"), adapterDesc.Description, odesc.DeviceName,
		odesc.DesktopCoordinates.left, odesc.DesktopCoordinates.top,
		odesc.DesktopCoordinates.right, odesc.DesktopCoordinates.bottom);


	// Release the display mode list.
	delete[] displayModeList;
	displayModeList = 0;

	d3d->outputAdapter = adapterOutput;

	// Release the adapter.
	adapter->Release();
	adapter = 0;

	static const D3D_FEATURE_LEVEL levels1[] = { D3D_FEATURE_LEVEL_11_1 };
	UINT cdflags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	cdflags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
	result = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, cdflags, levels1, 1, D3D11_SDK_VERSION, &d3d->m_device, NULL, &d3d->m_deviceContext);
	if (FAILED(result)) {
		write_log(_T("D3D11CreateDevice LEVEL_11_1: %08x\n"), result);
		if (result == E_INVALIDARG || result == DXGI_ERROR_UNSUPPORTED) {
			static const D3D_FEATURE_LEVEL levels0[] = { D3D_FEATURE_LEVEL_11_0 };
			result = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, cdflags, levels0, 1, D3D11_SDK_VERSION, &d3d->m_device, NULL, &d3d->m_deviceContext);
		}
		if (FAILED(result)) {
			D3D_FEATURE_LEVEL outlevel;
			result = pD3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, cdflags, NULL, 0, D3D11_SDK_VERSION, &d3d->m_device, &outlevel, &d3d->m_deviceContext);
			if (FAILED(result)) {
				write_log(_T("D3D11CreateDevice %08x\n"), result);
			} else {
				d3d->m_deviceContext->Release();
				d3d->m_deviceContext = NULL;
				d3d->m_device->Release();
				d3d->m_device = NULL;
				gui_message(_T("Direct3D11 Level 11 capable hardware required\nDetected hardware level is: %d.%d"), outlevel >> 12, (outlevel >> 8) & 15);
			}
			return false;
		}
	}

	ComPtr<IDXGIDevice1> dxgiDevice;
	result = d3d->m_device->QueryInterface(__uuidof(IDXGIDevice1), &dxgiDevice);
	if (FAILED(result)) {
		write_log(_T("QueryInterface IDXGIDevice1 %08x\n"), result);
	} else {
		result = dxgiDevice->SetMaximumFrameLatency(1);
		if (FAILED(result)) {
			write_log(_T("IDXGIDevice1 SetMaximumFrameLatency %08x\n"), result);
		}
	}

	// Initialize the swap chain description.
	ZeroMemory(&d3d->swapChainDesc, sizeof(d3d->swapChainDesc));

	// Set the width and height of the back buffer.
	d3d->swapChainDesc.Width = w_w;
	d3d->swapChainDesc.Height = w_h;

	// Set regular 32-bit surface for the back buffer.
	d3d->swapChainDesc.Format = d3d->format;

	// Turn multisampling off.
	d3d->swapChainDesc.SampleDesc.Count = 1;
	d3d->swapChainDesc.SampleDesc.Quality = 0;

	// Set the usage of the back buffer.
	d3d->swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	d3d->swapChainDesc.BufferCount = apm->gfx_backbuffers + 1;
	if (d3d->swapChainDesc.BufferCount < 2)
		d3d->swapChainDesc.BufferCount = 2;

	d3d->swapChainDesc.SwapEffect = os_win8 ? DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL : DXGI_SWAP_EFFECT_SEQUENTIAL;

	d3d->swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;

	setswapchainmode(d3d, isfs(d3d) > 0);

	d3d->swapChainDesc.Scaling = d3d->swapChainDesc.SwapEffect == DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL ? DXGI_SCALING_NONE : DXGI_SCALING_STRETCH;

	// Create the swap chain, Direct3D device, and Direct3D device context.
	result = factory2->CreateSwapChainForHwnd(d3d->m_device, ahwnd, &d3d->swapChainDesc, isfs(d3d) > 0 ? &d3d->fsSwapChainDesc : NULL, NULL, &d3d->m_swapChain);
	if (FAILED(result)) {
		write_log(_T("IDXGIFactory2 CreateSwapChainForHwnd %08x\n"), result);
		return false;
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

	d3d->invalidmode = false;
	d3d->fsmode = 0;
	if (isfs(d3d) > 0)
		D3D_resize(1);
	D3D_resize(0);

	write_log(_T("D3D11 init end\n"));
	return true;
}

static void freed3d(struct d3d11struct *d3d)
{
	write_log(_T("D3D11 freed3d start\n"));

	// Before shutting down set to windowed mode or when you release the swap chain it will throw an exception.
	if (d3d->m_rasterState) {
		d3d->m_rasterState->Release();
		d3d->m_rasterState = 0;
	}

	if (d3d->m_renderTargetView) {
		d3d->m_renderTargetView->Release();
		d3d->m_renderTargetView = 0;
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

	FreeTexture(d3d);
	FreeTexture2D(&d3d->sltexture, &d3d->sltexturerv);

	if (d3d->m_deviceContext) {
		d3d->m_deviceContext->ClearState();
	}
	write_log(_T("D3D11 freed3d end\n"));
}

static void xD3D11_free(bool immediate)
{
	struct d3d11struct *d3d = &d3d11data[0];

	write_log(_T("D3D11 free start\n"));

	freed3d(d3d);

	if (d3d->m_swapChain) {
		d3d->m_swapChain->SetFullscreenState(false, NULL);
		d3d->m_swapChain->Release();
		d3d->m_swapChain = 0;
	}
	if (d3d->m_deviceContext) {
		d3d->m_deviceContext->ClearState();
		d3d->m_deviceContext->Flush();
		d3d->m_deviceContext->Release();
		d3d->m_deviceContext = 0;
	}

	if (d3d->m_device) {
		d3d->m_device->Release();
		d3d->m_device = 0;
	}

	if (d3d->outputAdapter) {
		d3d->outputAdapter->Release();
		d3d->outputAdapter = NULL;
	}

	write_log(_T("D3D11 free end\n"));
}

static const TCHAR *xD3D11_init(HWND ahwnd, int w_w, int w_h, int depth, int *freq, int mmult)
{
	if (!can_D3D11(false))
		return false;
	if (xxD3D11_init(ahwnd, w_w, w_h, depth, freq, mmult))
		return NULL;
	xD3D11_free(true);
	return _T("D3D11 ERROR!");
}

static bool TextureShaderClass_SetShaderParameters(struct d3d11struct *d3d, D3DXMATRIX worldMatrix, D3DXMATRIX viewMatrix,D3DXMATRIX projectionMatrix)
{
	HRESULT result;
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	MatrixBufferType *dataPtr;

	// Transpose the matrices to prepare them for the shader.
	xD3DXMatrixTranspose(&worldMatrix, &worldMatrix);
	xD3DXMatrixTranspose(&viewMatrix, &viewMatrix);
	xD3DXMatrixTranspose(&projectionMatrix, &projectionMatrix);

	// Lock the constant buffer so it can be written to.
	result = d3d->m_deviceContext->Map(d3d->m_matrixBuffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	if (FAILED(result))
	{
		write_log(_T("ID3D11DeviceContext map(matrix) %08x\n"), result);
		return false;
	}

	// Get a pointer to the data in the constant buffer.
	dataPtr = (MatrixBufferType*)mappedResource.pData;

	// Copy the matrices into the constant buffer.
	dataPtr->world = worldMatrix;
	dataPtr->view = viewMatrix;
	dataPtr->projection = projectionMatrix;

	// Unlock the constant buffer.
	d3d->m_deviceContext->Unmap(d3d->m_matrixBuffer, 0);

	// Now set the constant buffer in the vertex shader with the updated values.
	d3d->m_deviceContext->VSSetConstantBuffers(0, 1, &d3d->m_matrixBuffer);

	// Set shader texture resource in the pixel shader.
	d3d->m_deviceContext->PSSetShaderResources(0, 1, &d3d->texture2drv);
	bool mask = false;
	for (int i = 0; i < MAX_SHADERS; i++) {
		if (d3d->shaders[i].type == SHADERTYPE_MASK_AFTER && d3d->shaders[i].masktexturerv) {
			d3d->m_deviceContext->PSSetShaderResources(1, 1, &d3d->shaders[i].masktexturerv);
			mask = true;
		}
	}
	if (!mask && d3d->sltexturerv) {
		d3d->m_deviceContext->PSSetShaderResources(1, 1, &d3d->sltexturerv);
	}
	return true;
}

static void RenderBuffers(struct d3d11struct *d3d)
{
	unsigned int stride;
	unsigned int offset;

	// Set vertex buffer stride and offset.
	stride = sizeof(VertexType);
	offset = 0;

	// Set the vertex buffer to active in the input assembler so it can be rendered.
	d3d->m_deviceContext->IASetVertexBuffers(0, 1, &d3d->m_vertexBuffer, &stride, &offset);

	// Set the index buffer to active in the input assembler so it can be rendered.
	d3d->m_deviceContext->IASetIndexBuffer(d3d->m_indexBuffer, DXGI_FORMAT_R32_UINT, 0);

	// Set the type of primitive that should be rendered from this vertex buffer, in this case triangles.
	d3d->m_deviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

static void BeginScene(struct d3d11struct *d3d, float red, float green, float blue, float alpha)
{
	float color[4];

	// Setup the color to clear the buffer to.
	color[0] = red;
	color[1] = green;
	color[2] = blue;
	color[3] = alpha;

	// Bind the render target view and depth stencil buffer to the output render pipeline.
	d3d->m_deviceContext->OMSetRenderTargets(1, &d3d->m_renderTargetView, NULL);

	// Clear the back buffer.
	d3d->m_deviceContext->ClearRenderTargetView(d3d->m_renderTargetView, color);
}

static void EndScene(struct d3d11struct *d3d)
{
	HRESULT hr;
	UINT presentFlags = 0;

	struct apmode *apm = picasso_on ? &currprefs.gfx_apmode[APMODE_RTG] : &currprefs.gfx_apmode[APMODE_NATIVE];
	if (currprefs.turbo_emulation)
		presentFlags |= DXGI_PRESENT_DO_NOT_WAIT;
	if (isfs(d3d) > 0) {
		hr = d3d->m_swapChain->Present(apm->gfx_vflip == 0 ? 0 : 1, presentFlags);
	} else {
		if (d3d->m_tearingSupport && (d3d->swapChainDesc.Flags & DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)) {
			presentFlags |= DXGI_PRESENT_ALLOW_TEARING;
		}
		hr = d3d->m_swapChain->Present(0, presentFlags);
	}
	if (currprefs.turbo_emulation && hr == DXGI_ERROR_WAS_STILL_DRAWING)
		return;
	if (FAILED(hr))
		write_log(_T("D3D11 Present %08x\n"), hr);
}

static void TextureShaderClass_RenderShader(struct d3d11struct *d3d, int indexCount)
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
	d3d->m_deviceContext->DrawIndexed(indexCount, 0, 0);
}

static bool TextureShaderClass_Render(struct d3d11struct *d3d, int indexCount, D3DXMATRIX worldMatrix, D3DXMATRIX viewMatrix, D3DXMATRIX projectionMatrix)
{
	bool result;

	// Set the shader parameters that it will use for rendering.
	result = TextureShaderClass_SetShaderParameters(d3d, worldMatrix, viewMatrix, projectionMatrix);
	if (!result)
	{
		return false;
	}

	// Now render the prepared buffers with the shader.
	TextureShaderClass_RenderShader(d3d, indexCount);

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

static bool GraphicsClass_Render(struct d3d11struct *d3d, float rotation)
{
	bool result;

	// Clear the buffers to begin the scene.
	BeginScene(d3d, 0.0f, 0.0f, 0.0f, 1.0f);

	setupscenecoords(d3d);

	// Generate the view matrix based on the camera's position.
	CameraClass_Render(d3d);

	// Put the bitmap vertex and index buffers on the graphics pipeline to prepare them for drawing.
	RenderBuffers(d3d);

	// Render the bitmap with the texture shader.
	result = TextureShaderClass_Render(d3d, d3d->m_indexCount, d3d->m_worldMatrix, d3d->m_viewMatrix, d3d->m_orthoMatrix);
	if (!result)
	{
		return false;
	}

	return true;
}

static bool xD3D11_renderframe(bool immediate)
{
	struct d3d11struct *d3d = &d3d11data[0];

	if (d3d->fsmodechange)
		D3D_resize(0);

	if (d3d->invalidmode)
		return false;

	GraphicsClass_Render(d3d, 0);

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

static void restore(struct d3d11struct *d3d)
{
	createscanlines(d3d, 1);
	for (int i = 0; i < MAX_FILTERSHADERS; i++) {
		if (filterd3d->gfx_filtermask[i][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_BEFORE);
			createmasktexture(d3d, filterd3d->gfx_filtermask[i], s);
		}
	}
	if (filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS][0]) {
		struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_AFTER);
		createmasktexture(d3d, filterd3d->gfx_filtermask[2 * MAX_FILTERSHADERS], s);
	}
	for (int i = 0; i < MAX_FILTERSHADERS; i++) {
		if (filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS][0]) {
			struct shaderdata11 *s = allocshaderslot(d3d, SHADERTYPE_MASK_AFTER);
			createmasktexture(d3d, filterd3d->gfx_filtermask[i + MAX_FILTERSHADERS], s);
		}
	}

}

static void xD3D11_showframe(void)
{
	struct d3d11struct *d3d = &d3d11data[0];
	if (d3d->invalidmode)
		return;
	// Present the rendered scene to the screen.
	EndScene(d3d);
}

static void xD3D11_clear(void)
{
	struct d3d11struct *d3d = &d3d11data[0];
	if (d3d->invalidmode)
		return;
	BeginScene(d3d, 0, 0, 0, 0);
}

static void xD3D11_refresh(void)
{
	struct d3d11struct *d3d = &d3d11data[0];

	createscanlines(d3d, 0);
	if (xD3D11_renderframe(true)) {
		xD3D11_showframe();
	}
}

static bool xD3D11_alloctexture(int w, int h)
{
	struct d3d11struct *d3d = &d3d11data[0];
	bool v;

	if (d3d->invalidmode)
		return false;
	d3d->m_bitmapWidth = w;
	d3d->m_bitmapHeight = h;
	d3d->dmult = S2X_getmult();
	v = CreateTexture(d3d);
	if (!v)
		return false;
	restore(d3d);
	setupscenecoords(d3d);
	return true;
}

static uae_u8 *xD3D11_locktexture(int *pitch, int *height, bool fullupdate)
{
	struct d3d11struct *d3d = &d3d11data[0];

	if (d3d->invalidmode)
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

static void xD3D11_unlocktexture(void)
{
	struct d3d11struct *d3d = &d3d11data[0];

	if (!d3d->texturelocked || d3d->invalidmode)
		return;
	d3d->texturelocked--;

	d3d->m_deviceContext->Unmap(d3d->texture2dstaging, 0);

#if 0
	if (currprefs.leds_on_screen & (STATUSLINE_CHIPSET | STATUSLINE_RTG)) {
		updateleds(d3d);
	}
#endif

	D3D11_BOX box;
	box.front = 0;
	box.back = 1;
	box.left = 0;
	box.right = d3d->m_bitmapWidth;
	box.top = 0;
	box.bottom = d3d->m_bitmapHeight;
	d3d->m_deviceContext->CopySubresourceRegion(d3d->texture2d, 0, 0, 0, 0, d3d->texture2dstaging, 0, &box);

}

static void xD3D11_flushtexture(int miny, int maxy)
{
	struct d3d11struct *d3d = &d3d11data[0];
}

static void xD3D11_restore(void)
{
	struct d3d11struct *d3d = &d3d11data[0];
	if (!d3d->texture2d)
		return;
	createscanlines(d3d, 0);
}

static void xD3D11_vblank_reset(double freq)
{

}

static int xD3D11_canshaders(void)
{
	return 0;
}
static int xD3D11_goodenough(void)
{
	return 1;
}

static void xD3D11_change(int temp)
{
	struct d3d11struct *d3d = &d3d11data[0];
}

static void resizemode(struct d3d11struct *d3d)
{
	if (!d3d->invalidmode) {
		freed3d(d3d);
		write_log(_T("D3D11 resize %d %d, %d %d\n"), d3d->m_screenWidth, d3d->m_screenHeight, d3d->m_bitmapWidth, d3d->m_bitmapHeight);
		setswapchainmode(d3d, d3d->fsmode);
		HRESULT hr = d3d->m_swapChain->ResizeBuffers(d3d->swapChainDesc.BufferCount, d3d->m_screenWidth, d3d->m_screenHeight, d3d->format, d3d->swapChainDesc.Flags);
		if (FAILED(hr)) {
			write_log(_T("ResizeBuffers %08x\n"), hr);
			d3d->invalidmode = true;
		}
		if (!d3d->invalidmode) {
			if (!initd3d(d3d)) {
				xD3D11_free(true);
				currprefs.gfx_api = changed_prefs.gfx_api = 1;
				d3d->invalidmode = true;
				d3d9_select();
			} else {
				xD3D11_alloctexture(d3d->m_bitmapWidth, d3d->m_bitmapHeight);
			}
		}
	}
}

static void xD3D11_resize(int activate)
{
	static int recursive;
	HRESULT hr;
	struct d3d11struct *d3d = &d3d11data[0];

	write_log(_T("D3D11_resize %d %d %d (%d)\n"), activate, d3d->fsmodechange, d3d->fsmode, d3d->guimode);

	if (d3d->guimode && isfullscreen() > 0)
		return;

	if (activate) {
		if (activate != d3d->fsmode) {
			d3d->fsmode = activate;
			d3d->fsmodechange = TRUE;
		}
		return;
	}

	if (d3d->m_swapChain && quit_program) {
		d3d->m_swapChain->SetFullscreenState(FALSE, NULL);
		FreeTexture(d3d);
		d3d->fsmode = 0;
		d3d->invalidmode = true;
		d3d->fsmodechange = 0;
		return;
	}

	if (recursive)
		return;
	recursive++;

	if (d3d->m_swapChain) {
		if (d3d->fsmodechange && d3d->fsmode > 0) {
			write_log(_T("D3D11_resize -> fullscreen\n"));
			ShowWindow(d3d->ahwnd, SW_SHOWNORMAL);
			hr = d3d->m_swapChain->SetFullscreenState(TRUE, d3d->outputAdapter);
			if (FAILED(hr)) {
				write_log(_T("SetFullscreenState(TRUE) failed %08X\n"), hr);
				toggle_fullscreen(10);
			} else {
				d3d->fsmode = 1;
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
		}
		resizemode(d3d);
	}

	recursive--;
}

static void xD3D11_guimode(int guion)
{
	struct d3d11struct *d3d = &d3d11data[0];

	if (isfullscreen() <= 0)
		return;

	write_log(_T("guimode %d\n"), guion);
	d3d->guimode = guion;
	if (guion > 0) {
		;
	} else if (guion == 0) {
		d3d->fsmode = 0;
		xD3D11_resize(1);
		xD3D11_resize(0);
	}
	write_log(_T("guimode end\n"));
}

static int xD3D_isenabled(void)
{
	struct d3d11struct *d3d = &d3d11data[0];
	return d3d->m_device != NULL ? 2 : 0;
}

static bool xD3D_getvblankpos(int *vp)
{
	*vp = 0;
	return false;
}

static HDC xD3D_getDC(HDC hdc)
{
	struct d3d11struct *d3d = &d3d11data[0];
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

bool D3D11_capture(void **data, int *w, int *h, int *pitch)
{
	struct d3d11struct *d3d = &d3d11data[0];
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
		d3d->m_deviceContext->CopyResource(d3d->screenshottexture, pSurface);
		hr = d3d->m_deviceContext->Map(d3d->screenshottexture, 0, D3D11_MAP_READ, 0, &map);
		if (FAILED(hr)) {
			return false;
		}
		pSurface->Release();
		*data = map.pData;
		*pitch = map.RowPitch;
		*w = d3d->m_bitmapWidth;
		*h = d3d->m_bitmapHeight;
		return true;
	}
}

static bool xD3D_setcursor(int x, int y, int width, int height, bool visible, bool noscale)
{
	return false;
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
	D3D_showframe_special = NULL;
	D3D_guimode = xD3D11_guimode;
	D3D_getDC = xD3D_getDC;
	D3D_isenabled = xD3D_isenabled;
	D3D_clear = xD3D11_clear;
	D3D_canshaders = xD3D11_canshaders;
	D3D_goodenough = xD3D11_goodenough;
	D3D_setcursor = xD3D_setcursor;
	D3D_getvblankpos = xD3D_getvblankpos;
	D3D_getrefreshrate = NULL;
	D3D_vblank_reset = xD3D11_vblank_reset;
	D3D_resize = xD3D11_resize;
	D3D_change = xD3D11_change;
}

void d3d_select(struct uae_prefs *p)
{
	if (p->gfx_api == 2)
		d3d11_select();
	else
		d3d9_select();
}
