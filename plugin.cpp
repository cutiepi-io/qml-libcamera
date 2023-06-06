#include <libcamera/libcamera.h>
#include <QCoreApplication>
#include <QtQuick>
#include <QtQml/qqml.h>
#include <QtQml/QQmlExtensionPlugin>
#include <QImage>
#include <sys/mman.h>
#include <unistd.h>

using namespace libcamera;

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

        config = camera->generateConfiguration({libcamera::StreamRole::StillCapture});
        if (!config) {
            qDebug() << "Failed to generate default configuration";
        }

        libcamera::StreamConfiguration &streamConfig = config->at(0);
        streamConfig.pixelFormat = libcamera::formats::RGB888;
        streamConfig.size = { 2592, 1944 };
        config->addConfiguration(streamConfig);
        config->validate();

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
        QImage image(static_cast<uchar *>(data), size.width, size.height, QImage::Format_RGB888);
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