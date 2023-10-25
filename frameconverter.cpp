#include "frameconverter.h"

void yuyv_to_rgb_pixel_part(int y, int u, int v, unsigned char *rgb)
{
    float r, g, b;

    r = y + 1.4065 * (v - 128);			     //r0
    g = y - 0.3455 * (u - 128) - 0.7169 * (v - 128); //g0
    b = y + 1.1790 * (u - 128);			     //b0

    if (r < 0)
	    r = 0;
    else if (r > 255)
	    r = 255;
    if (g < 0)
	    g = 0;
    else if (g > 255)
	    g = 255;
    if (b < 0)
	    b = 0;
    else if (b > 255)
	    b = 255;

    rgb[0] = (unsigned char)r;
    rgb[1] = (unsigned char)g;
    rgb[2] = (unsigned char)b;
}

void yuyv_to_rgb_pixel(unsigned char *yuyv, unsigned char *rgb)
{
    yuyv_to_rgb_pixel_part(yuyv[0], yuyv[1], yuyv[3], rgb);
    yuyv_to_rgb_pixel_part(yuyv[2], yuyv[1], yuyv[3], rgb + 3);
}

bool yuyv_to_rgb(unsigned char *yuyv, unsigned char *rgb, int height, int width)
{
    long yuv_size = height * width * 2;
    long rgb_size = height * width * 3;

    if (yuyv == nullptr || rgb == nullptr)
	    return false;

    for (int i = 0, j = 0; i < rgb_size && j < yuv_size; i += 6, j += 4)
	    yuyv_to_rgb_pixel(&yuyv[j], &rgb[i]);
    return true;
}
