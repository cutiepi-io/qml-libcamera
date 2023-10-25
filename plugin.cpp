#include <libcamera/libcamera.h>
#include <QCoreApplication>
#include <QtQuick>
#include <QtQml/qqml.h>
#include <QtQml/QQmlExtensionPlugin>
#include <QImage>
#include <sys/mman.h>
#include <unistd.h>
#include <QQueue>
#include <QDebug>
#include "image.h"

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

class LibCameraModel : public QQuickPaintedItem
{
    Q_OBJECT
    // Q_PROPERTY(QString filename READ filename NOTIFY imageCaptured)
    // Q_PROPERTY(int orientation READ orientation WRITE setOrientation NOTIFY orientationChanged)

public:

    class CaptureEvent : public QEvent
    {
    public:
        CaptureEvent()
            : QEvent(type())
        {
        }

        static Type type()
        {
            static int type = QEvent::registerEventType();
            return static_cast<Type>(type);
        }
    };
    LibCameraModel(QQuickItem* parent = nullptr)
        : QQuickPaintedItem(parent)
        // , m_orientation(0)
        , displayedBuffer(nullptr)
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
             libcamera::StreamRole::Viewfinder/*,
             libcamera::StreamRole::VideoRecording*/
            }
        );
        if (!config) {
            qDebug() << "Failed to generate default configuration for still";
        }
        qDebug() << config->size();
        libcamera::StreamConfiguration &viewConfig = config->at(0);
        // libcamera::StreamConfiguration &videoConfig = config->at(1);

        // Override default resolution
        viewConfig.size = { 2592, 1944 };

        PixelFormat format;

        format = getBestFormat(viewConfig);
        if (format.isValid() == false)
            qWarning("Could not configure pixel format matching licamera and QImage");
        else
            m_viewFinderFormat = nativeFormats[format];
        qDebug() << "Configured pixel format for view";
        qDebug() << viewConfig.size.toString().c_str();
        m_size = viewConfig.size;


        // format = getBestFormat(videoConfig);
        // if (format.isValid() == false)
        //     qWarning("Could not configure pixel format matching licamera and QImage");
        // else
        //     m_secondaryFormat = nativeFormats[format];
        // qDebug() << "Configured pixel format for raw";
        // qDebug() << videoConfig.size.toString().c_str();


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
        m_previewStream = config->at(0).stream();
        // m_secondaryStream = config->at(1).stream();
        configureStreamBuffers(m_previewStream);
        // configureStreamBuffers(m_secondaryStream);


        /* Create requests and fill them with buffers from the viewfinder. */
        while (!freeBuffers[m_previewStream].isEmpty()) {
            FrameBuffer *buffer = freeBuffers[m_previewStream].dequeue();

            std::unique_ptr<Request> request = camera->createRequest();
            if (!request) {
                qWarning() << "Can't create request";
                ret = -ENOMEM;
            }

            ret = request->addBuffer(m_previewStream, buffer);
            if (ret < 0) {
                qWarning() << "Can't set buffer for request";
            }
            qDebug() << "Adding request";
            requests.push_back(std::move(request));
        }

        if (camera->start()) {
            qWarning("Failed to start the camera");
        }
        for (std::unique_ptr<Request> &request : requests) {
            ret = queueRequest(request.get());
            if (ret < 0) {
                qWarning() << "Can't queue request";
            }
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

    void configureStreamBuffers(libcamera::Stream* stream)
    {
        if (allocator->allocate(stream) <= 0) {
            qDebug() << "Failed to allocate buffers";
        }
        for (const std::unique_ptr<FrameBuffer> &buffer : allocator->buffers(stream)) {
			/* Map memory buffers and cache the mappings. */
            qDebug() << "Configuring buffer for " << stream;
			std::unique_ptr<LibCameraImage> image =
				LibCameraImage::fromFrameBuffer(buffer.get());
			assert(image != nullptr);
			mappedBuffers[buffer.get()] = std::move(image);

			/* Store buffers on the free list. */
			freeBuffers[stream].enqueue(buffer.get());
		}
    }

    PixelFormat getBestFormat(libcamera::StreamConfiguration &streamConfig) {
        std::vector<PixelFormat> formats = streamConfig.formats().pixelformats();
        qDebug() << formats.size();
        for (const PixelFormat &format : nativeFormats.keys()) {
            auto match = std::find_if(formats.begin(), formats.end(),
                                      [&](const PixelFormat &f) {
                                          return f == format;
                                      });
            if (match != formats.end()) {
                streamConfig.pixelFormat = format;
                return format;
            }
        }
        return PixelFormat();
    }

    // QString filename() const {
    //     return m_filename;
    // }

    // int orientation() const {
    //     return m_orientation;
    // }

    // void setOrientation(int orientation) {
    //     if (m_orientation == orientation)
    //         return;

    //     m_orientation = orientation;
    //     emit orientationChanged(m_orientation);
    //     update();
    // }

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

        // m_filename = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" +
                // QDateTime::currentDateTime().toString(Qt::ISODate).replace(":", "_") + ".jpg";
    }

signals:
    // void imageCaptured();
    // void orientationChanged(int orientation);

public slots:
    void handleRequestCompleted(libcamera::Request *completedRequest) {
        if(!completedRequest)
            return;
        {
            QMutexLocker locker(&mutex);
            doneQueue.enqueue(completedRequest);
        }
        QCoreApplication::postEvent(this, new CaptureEvent);
    }

    void processCapture()
    {
        FrameBuffer *buffer = nullptr;
        Request *request = nullptr;
        {
            QMutexLocker locker(&mutex);
            if (doneQueue.isEmpty())
                return;

            request = doneQueue.dequeue();
        }

        /* Process buffers. */
        if (request->buffers().count(m_previewStream))
        {
            buffer = request->buffers().at(m_previewStream);
            processViewfinder(buffer);
        }

        request->reuse();
        {
            QMutexLocker locker(&mutex);
            freeQueue.enqueue(request);
        }

    }

    void processViewfinder(FrameBuffer *buffer)
    {
        /* Render the frame on the viewfinder. */
        // size_t size = buffer->metadata().planes()[0].bytesused;

        {
            QMutexLocker locker(&mutex);

            auto image = mappedBuffers[buffer].get();
            // qDebug() << buffer->planes().size() << image << m_viewFinderFormat << size << m_size.toString().c_str();
            assert(buffer->planes().size() >= 1);
            m_image = QImage(image->data(0).data(), m_size.width,
                                m_size.height,
                                m_viewFinderFormat);
            // m_image = m_image.transformed(QTransform().rotate(m_orientation), Qt::FastTransformation);
            std::swap(buffer, displayedBuffer);
        }
        update();
        if (buffer) // we don't care of it anymore, put it to garbage
            renderComplete(buffer);
    }

    void paint(QPainter* painter) {
        painter->drawImage(QRect(0,0,width(),height()), m_image);
    }

    void renderComplete(FrameBuffer *buffer)
    {
        Request *request;
        {
            QMutexLocker locker(&mutex);
            if (freeQueue.isEmpty())
                return;

            request = freeQueue.dequeue();
        }

        request->addBuffer(m_previewStream, buffer);

        queueRequest(request);
    }

    int queueRequest(Request *request)
    {
        return camera->queueRequest(request);
    }

    bool event(QEvent *e)
    {
        if (e->type() == CaptureEvent::type()) {
            processCapture();
            return true;
        }/* else if (e->type() == HotplugEvent::type()) {
            processHotplug(static_cast<HotplugEvent *>(e));
            return true;
        }*/
        return QObject::event(e);
    }

private:
    CameraManager *cm;
    std::shared_ptr<Camera> camera;
    std::unique_ptr<libcamera::CameraConfiguration> config;
    std::unique_ptr<libcamera::Request> request;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator;
    std::map<libcamera::FrameBuffer *, std::unique_ptr<LibCameraImage>> mappedBuffers;
    std::map<const libcamera::Stream *, QQueue<libcamera::FrameBuffer *>> freeBuffers;
    QMutex  mutex;
    QQueue<libcamera::Request *> doneQueue;
	QQueue<libcamera::Request *> freeQueue;
    // QString m_filename;
    // int m_orientation;
    QImage::Format  m_viewFinderFormat;
    QImage::Format  m_secondaryFormat;
    libcamera::Stream *m_previewStream;
    libcamera::Stream *m_secondaryStream;
    std::vector<std::unique_ptr<libcamera::Request>> requests;

    // Actual image / data
    libcamera::Size m_size;
    libcamera::FrameBuffer *displayedBuffer;
    QImage m_image;
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
