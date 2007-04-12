
#include "sysconfig.h"
#include "sysdeps.h"

#include "filter.h"

uae_u32 gamma_red[256 * 3];
uae_u32 gamma_grn[256 * 3];
uae_u32 gamma_blu[256 * 3];

uae_u32 gamma_red_fac[256 * 3];
uae_u32 gamma_grn_fac[256 * 3];
uae_u32 gamma_blu_fac[256 * 3];

uae_u32 color_red[256];
uae_u32 color_grn[256];
uae_u32 color_blu[256];

static float video_gamma(float value, float gamma, float bri, float con)
{
    double factor;
    float ret;

    value += bri;
    value *= con;

    if (value <= 0.0f)
        return 0.0f;

    factor = pow(255.0f, 1.0f - gamma);
    ret = (float)(factor * pow(value, gamma));

    if (ret < 0.0f)
        ret = 0.0f;

    return ret;
}

static int color_brightness = 1000;
static int color_contrast = 1000;
static int color_gamma = 1000;
static int pal_scanlineshade = 667;

static void video_calc_gammatable(void)
{
    int i;
    float bri, con, gam, scn, v;
    uae_u32 vi;

    bri = ((float)(color_brightness - 1000))
          * (128.0f / 1000.0f);
    con = ((float)(color_contrast   )) / 1000.0f;
    gam = ((float)(color_gamma      )) / 1000.0f;
    scn = ((float)(pal_scanlineshade)) / 1000.0f;

    for (i = 0; i < (256 * 3); i++) {
        v = video_gamma((float)(i - 256), gam, bri, con);

        vi = (uae_u32)v;
        if (vi > 255)
            vi = 255;
        gamma_red[i] = color_red[vi];
        gamma_grn[i] = color_grn[vi];
        gamma_blu[i] = color_blu[vi];

        vi = (uae_u32)(v * scn);
        if (vi > 255)
            vi = 255;
        gamma_red_fac[i] = color_red[vi];
        gamma_grn_fac[i] = color_grn[vi];
        gamma_blu_fac[i] = color_blu[vi];
    }
}