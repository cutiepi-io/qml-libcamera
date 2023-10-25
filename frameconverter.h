#ifndef FRAMECONVERTER_H
#define FRAMECONVERTER_H

void yuyv_to_rgb_pixel(unsigned char *yuyv, unsigned char *rgb);
bool yuyv_to_rgb(unsigned char *yuyv, unsigned char *rgb, int height, int width);

#endif
