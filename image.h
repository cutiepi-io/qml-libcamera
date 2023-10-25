#ifndef IMAGE_H
#define IMAGE_H

#include <memory>
#include <stdint.h>
#include <vector>

#include <libcamera/base/class.h>
#include <libcamera/base/flags.h>
#include <libcamera/base/span.h>

#include <libcamera/framebuffer.h>

class LibCameraImage
{
public:
	static std::unique_ptr<LibCameraImage> fromFrameBuffer(const libcamera::FrameBuffer *buffer);

	~LibCameraImage();

	unsigned int numPlanes() const;

	libcamera::Span<uint8_t> data(unsigned int plane);
	libcamera::Span<const uint8_t> data(unsigned int plane) const;

private:
	LIBCAMERA_DISABLE_COPY(LibCameraImage)

	LibCameraImage();

	std::vector<libcamera::Span<uint8_t>> maps_;
	std::vector<libcamera::Span<uint8_t>> planes_;
};

#endif