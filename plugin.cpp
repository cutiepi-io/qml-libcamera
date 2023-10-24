#include <libcamera/libcamera.h>
#include <QCoreApplication>
#include <QtQuick>
#include <QtQml/qqml.h>
#include <QtQml/QQmlExtensionPlugin>
#include <QImage>
#include <sys/mman.h>
#include <unistd.h>

using namespace libcamera;

static const QMap<libcamera::PixelFormat, QImage::Format> nativeFormats
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
    { libcamera::formats::ABGR8888, QImage::Format_RGBX8888 },
    { libcamera::formats::XBGR8888, QImage::Format_RGBX8888 },
#endif
    { libcamera::formats::ARGB8888, QImage::Format_RGB32 },
    { libcamera::formats::XRGB8888, QImage::Format_RGB32 },
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    { libcamera::formats::RGB888, QImage::Format_BGR888 },
#endif
    { libcamera::formats::BGR888, QImage::Format_RGB888 },
    { libcamera::formats::RGB565, QImage::Format_RGB16 },
};


class LibCameraModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString filename READ filename NOTIFY imageCaptured)
    Q_PROPERTY(int orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)

public:
    LibCameraModel(QObject* parent = nullptr)
        : QObject(parent)
        , m_orientation(0)
    {
        cm = new CameraManager();
        int ret = cm->start();
        if (ret) {
            qWarning("Failed to start camera manager: %s", strerror(-ret));
            delete cm;
            cm = nullptr;
        }
        if (cm->cameras().empty()) {
            qDebug() << "No cameras available";
        }

        camera = cm->cameras()[0];
        camera->acquire();

        config = camera->generateConfiguration(
            {
             libcamera::StreamRole::Viewfinder,
             libcamera::StreamRole::Raw
            }
        );
        if (!config) {
            qDebug() << "Failed to generate default configuration for still";
        }
        qDebug() << config->size();
        libcamera::StreamConfiguration &viewConfig = config->at(0);
        libcamera::StreamConfiguration &rawConfig = config->at(1);

        PixelFormat format;

        format = getBestFormat(viewConfig);
        if (format.isValid() == false)
            qWarning("Could not configure pixel format matching licamera and QImage");
        qDebug() << viewConfig.size.toString();
        format = getBestFormat(rawConfig);
        if (format.isValid() == false)
            qWarning("Could not configure pixel format matching licamera and QImage");
        qDebug() << rawConfig.size.toString();
        config->addConfiguration(viewConfig);
        config->addConfiguration(rawConfig);


        libcamera::CameraConfiguration::Status status = config->validate();

        if (status == libcamera::CameraConfiguration::Invalid)
        {
            qWarning("Failed to configure the stream");
        }
        else if (status == libcamera::CameraConfiguration::Adjusted)
        {
            qWarning("Configuration of the stream was adjusted");
        }

        if (camera->configure(config.get())) {
            qDebug() << "Failed to configure camera";
        }

        allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
        libcamera::Stream *stream = config->at(0).stream();
        if (allocator->allocate(stream) <= 0) {
            qDebug() << "Failed to allocate buffers";
        }
        if (camera->start()) {
            qWarning("Failed to start the camera");
        }
        camera->requestCompleted.connect(this, &LibCameraModel::handleRequestCompleted);
    }

    ~LibCameraModel()
    {
        if (camera) {
            camera->stop();
            camera->release();
        }
        delete cm;
    }

    PixelFormat getBestFormat(libcamera::StreamConfiguration &streamConfig) {
        std::vector<PixelFormat> formats = streamConfig.formats().pixelformats();

        for (const PixelFormat &format : nativeFormats.keys()) {
            auto match = std::find_if(formats.begin(), formats.end(),
                                      [&](const PixelFormat &f) {
                                          return f == format;
                                      });
            if (match != formats.end()) {
                streamConfig.pixelFormat = format;
                return format;
                m_viewFinderFormat = nativeFormats[format];
                break;
            }
        }
        return PixelFormat();
    }

    QString filename() const {
        return m_filename;
    }

    int orientation() const {
        return m_orientation;
    }

    void setOrientation(int orientation) {
        if (m_orientation == orientation)
            return;

        m_orientation = orientation;
        emit orientationChanged(m_orientation);
    }

    Q_INVOKABLE void captureImage() {
        libcamera::Stream *stream = config->at(0).stream();

        allocator->free(stream);
        camera->stop();
        if (camera->configure(config.get())) {
            qDebug() << "Failed to configure camera";
        }

        if (allocator->allocate(stream) <= 0) {
            qDebug() << "Failed to allocate buffers";
            return;
        }
        const std::vector<std::unique_ptr<libcamera::FrameBuffer>> &buffers = allocator->buffers(stream);
        camera->start();

        request = camera->createRequest();
        if (!request) {
            qDebug() << "Failed to create request";
        }

        request->addBuffer(stream, buffers[0].get());
        camera->queueRequest(request.get());

        m_filename = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" +
                QDateTime::currentDateTime().toString(Qt::ISODate).replace(":", "_") + ".jpg";
    }

signals:
    void imageCaptured();
    void orientationChanged(int orientation);

public slots:
    void handleRequestCompleted(libcamera::Request *completedRequest) {
        qDebug() << "handleRequestCompleted";
        if(!completedRequest)
            return;

        libcamera::Stream *stream = config->at(0).stream();
        libcamera::FrameBuffer *buffer = completedRequest->findBuffer(stream);
        if (!buffer) {
            qDebug() << "No completed buffers";
            return;
        }

        const libcamera::FrameBuffer::Plane &plane = buffer->planes()[0];
        void *data = mmap(NULL, plane.length, PROT_READ, MAP_SHARED, plane.fd.get(), 0);
        if (data == MAP_FAILED) {
            qDebug() << "Failed to map memory";
            return;
        }

        libcamera::Size size = config->at(0).size;
        QImage image(static_cast<uchar *>(data), size.width, size.height, m_viewFinderFormat);
        image = image.transformed(QTransform().rotate(m_orientation), Qt::FastTransformation);
        image.save(m_filename);

        if (munmap(data, plane.length) == -1) {
            qDebug() << "Failed to unmap memory";
        }

        emit imageCaptured();
    }

private:
    CameraManager *cm;
    std::shared_ptr<Camera> camera;
    std::unique_ptr<libcamera::CameraConfiguration> config;
    std::unique_ptr<libcamera::Request> request;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
    QString m_filename;
    int m_orientation;
    QImage::Format  m_viewFinderFormat;
    QImage::Format  m_rawFormat;

};

class LibCameraPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "io.cutiepi.LibCamera" FILE "libcamera.json")

public:
    void registerTypes(const char *uri)
    {
        qmlRegisterType<LibCameraModel>(uri, 1, 0, "LibCamera");
    }
};

#include "plugin.moc"
