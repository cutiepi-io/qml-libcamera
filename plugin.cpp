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
#include "frameconverter.h"
#include <QtConcurrent>

using namespace libcamera;

std::vector<unsigned char> converterBuffer;

typedef std::function<QImage(unsigned char*, const libcamera::Size&)> converterFunction;

static const QMap<libcamera::PixelFormat, QPair<QImage::Format, converterFunction > > nativeFormats
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 2, 0)
    { libcamera::formats::ABGR8888, { { QImage::Format_RGBX8888 }, { converterFunction() } } },
    { libcamera::formats::XBGR8888, { { QImage::Format_RGBX8888 }, { converterFunction() } } },
#endif
    { libcamera::formats::ARGB8888, { { QImage::Format_RGB32 }, { converterFunction() } } },
    { libcamera::formats::XRGB8888, { { QImage::Format_RGB32 }, { converterFunction() } } },
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    { libcamera::formats::RGB888, { { QImage::Format_BGR888 }, { converterFunction() } } },
#endif
    { libcamera::formats::BGR888, { { QImage::Format_RGB888 }, { converterFunction() } } },
    { libcamera::formats::RGB565, { { QImage::Format_RGB16 }, { converterFunction() } } },
    { libcamera::formats::YUYV, { { QImage::Format_RGB888 }, { [](unsigned char* input, const libcamera::Size& size) {
        auto bufferSize = size.width * size.height * 3;

        if (converterBuffer.size() != bufferSize)
            converterBuffer.resize(size.width * size.height * 3);
        yuyv_to_rgb(input, converterBuffer.data(), size.width, size.height);
        return QImage(converterBuffer.data(), size.width, size.height, QImage::Format_RGB888);
    } } } }
};

class LibCameraModel : public QQuickItem
{
    Q_OBJECT

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
        : QQuickItem(parent)
        , m_previewStream(nullptr)
        , m_secondaryStream(nullptr)
        , m_displayedBuffer(nullptr)
    {
        m_cm = new CameraManager();
        int ret = m_cm->start();
        if (ret) {
            qWarning("Failed to start camera manager: %s", strerror(-ret));
            delete m_cm;
            m_cm = nullptr;
        }
        if (m_cm->cameras().empty()) {
            qDebug() << "No cameras available";
        }

        camera = m_cm->cameras()[0];
        camera->acquire();

        m_config = camera->generateConfiguration(
            {
             libcamera::StreamRole::Viewfinder/*,
             libcamera::StreamRole::VideoCapture*/
            }
        );
        if (!m_config) {
            qDebug() << "Failed to generate default configuration for still";
        }
        libcamera::StreamConfiguration &viewConfig = m_config->at(0);

        // Override default resolution
        viewConfig.size = { 2592, 1944 };
        PixelFormat format;

        format = getBestFormat(viewConfig);
        if (format.isValid() == false)
            qWarning("Could not configure pixel format matching licamera and QImage");
        else
        {
            m_viewFinderFormat = nativeFormats[format].first;
            m_viewFinderConverter = nativeFormats[format].second;
        }
        qDebug() << "Configured pixel format for view:" << m_viewFinderFormat;
        qDebug() << viewConfig.size.toString().c_str();

        if (m_config->size() > 1)
        {
            libcamera::StreamConfiguration &videoConfig = m_config->at(1);
            format = getBestFormat(videoConfig);
            if (format.isValid() == false)
                qWarning("Could not configure pixel format matching licamera and QImage");
            else
            {
                m_secondaryFormat = nativeFormats[format].first;
                m_secondaryConverter = nativeFormats[format].second;
            }
            qDebug() << "Configured pixel format for secondary:" << m_secondaryFormat;
            qDebug() << videoConfig.size.toString().c_str();
        }

        libcamera::CameraConfiguration::Status status = m_config->validate();

        if (status == libcamera::CameraConfiguration::Invalid)
        {
            qWarning("Failed to configure the stream");
        }
        else if (status == libcamera::CameraConfiguration::Adjusted)
        {
            qWarning("Configuration of the stream was adjusted");
        }

        if (camera->configure(m_config.get())) {
            qDebug() << "Failed to configure camera";
        }
        m_size = viewConfig.size;
        qDebug() << "Corrected resolution:" << m_size.toString().c_str();

        m_allocator = std::make_unique<libcamera::FrameBufferAllocator>(camera);
        m_previewStream = m_config->at(0).stream();
        if (m_config->size() > 1)
            m_secondaryStream = m_config->at(1).stream();
        configureStreamBuffers(m_previewStream);
        configureStreamBuffers(m_secondaryStream);


        /* Create m_requests and fill them with buffers from the viewfinder. */
        while (!m_freeBuffers[m_previewStream].isEmpty()) {
            FrameBuffer *buffer = m_freeBuffers[m_previewStream].dequeue();

            std::unique_ptr<Request> m_request = camera->createRequest();
            if (!m_request) {
                qWarning() << "Can't create m_request";
                ret = -ENOMEM;
            }

            ret = m_request->addBuffer(m_previewStream, buffer);
            if (ret < 0) {
                qWarning() << "Can't set buffer for m_request";
            }
            m_requests.push_back(std::move(m_request));
        }
        if (m_secondaryStream)
            while (!m_freeBuffers[m_secondaryStream].isEmpty()) {
                FrameBuffer *buffer = m_freeBuffers[m_secondaryStream].dequeue();

                std::unique_ptr<Request> m_request = camera->createRequest();
                if (!m_request) {
                    qWarning() << "Can't create m_request";
                    ret = -ENOMEM;
                }

                ret = m_request->addBuffer(m_secondaryStream, buffer);
                if (ret < 0) {
                    qWarning() << "Can't set buffer for m_request";
                }
                m_requests.push_back(std::move(m_request));
            }
        if (camera->start()) {
            qWarning("Failed to start the camera");
        }
        for (std::unique_ptr<Request> &m_request : m_requests) {
            ret = queueRequest(m_request.get());
            if (ret < 0) {
                qWarning() << "Can't queue m_request";
            }
        }
        camera->requestCompleted.connect(this, &LibCameraModel::handleRequestCompleted);
        setFlag(ItemHasContents, true);
    }

    ~LibCameraModel()
    {
        if (camera) {
            camera->stop();
            camera->release();
        }
        delete m_cm;
    }

    Q_INVOKABLE void saveToFile() {
        QMutexLocker locker(&m_mutex);
        auto image_copy = m_image.copy();
        QtConcurrent::run(QThreadPool::globalInstance(), [image_copy]() {
            image_copy.save(QString("nice-group-picture.%1.png").arg(QDateTime::currentDateTimeUtc().toString("yyyy.MM.dd-hh:mm:ss.z")));
        });
    }

    void configureStreamBuffers(libcamera::Stream* stream)
    {
        if (!stream)
            return ;
        if (m_allocator->allocate(stream) <= 0) {
            qDebug() << "Failed to allocate buffers";
        }
        for (const std::unique_ptr<FrameBuffer> &buffer : m_allocator->buffers(stream)) {
			/* Map memory buffers and cache the mappings. */
			std::unique_ptr<LibCameraImage> image =
				LibCameraImage::fromFrameBuffer(buffer.get());
			assert(image != nullptr);
			m_mappedBuffers[buffer.get()] = std::move(image);

			/* Store buffers on the free list. */
			m_freeBuffers[stream].enqueue(buffer.get());
		}
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
            }
        }
        return PixelFormat();
    }

public slots:
    void handleRequestCompleted(libcamera::Request *completedRequest) {
        if(!completedRequest)
            return;
        {
            QMutexLocker locker(&m_mutex);
            m_doneQueue.enqueue(completedRequest);
        }
        QCoreApplication::postEvent(this, new CaptureEvent);
    }

    void processCapture()
    {
        FrameBuffer *buffer = nullptr;
        Request *m_request = nullptr;
        {
            QMutexLocker locker(&m_mutex);
            if (m_doneQueue.isEmpty())
                return;

            m_request = m_doneQueue.dequeue();
        }

        /* Process buffers. */
        if (m_request->buffers().count(m_previewStream))
        {
            buffer = m_request->buffers().at(m_previewStream);
            processViewfinder(buffer);
        }
        if (m_request->buffers().count(m_secondaryStream))
        {
            buffer = m_request->buffers().at(m_secondaryStream);
            processSecondaryStream(buffer);
        }
        m_request->reuse();
        {
            QMutexLocker locker(&m_mutex);
            m_freeQueue.enqueue(m_request);
        }

    }

    void processViewfinder(FrameBuffer *buffer)
    {
        /* Render the frame on the viewfinder. */
        // size_t size = buffer->metadata().planes()[0].bytesused;
        {
            QMutexLocker locker(&m_mutex);

            auto image = m_mappedBuffers[buffer].get();

            assert(buffer->planes().size() >= 1);
            if (m_viewFinderConverter)
                m_image = m_viewFinderConverter(image->data(0).data(), m_size);
            else
                m_image = QImage(image->data(0).data(), m_size.width,
                                m_size.height,
                                m_viewFinderFormat);
            m_image_scaled = m_image.scaled(width(), height(), Qt::KeepAspectRatio);
            std::swap(buffer, m_displayedBuffer);
        }
        update();
        if (buffer) // we don't care of it anymore, put it to garbage
            renderComplete(buffer, m_previewStream);
    }

    void processSecondaryStream(FrameBuffer *buffer)
    {
        qDebug() << "processSecondaryStream";
        // We will actually do nothing with this!
        if (buffer) // we don't care of it anymore, put it to garbage
            renderComplete(buffer, m_secondaryStream);
    }

    void renderComplete(FrameBuffer *buffer, libcamera::Stream *stream)
    {
        Request *m_request;
        {
            QMutexLocker locker(&m_mutex);
            if (m_freeQueue.isEmpty())
                return;

            m_request = m_freeQueue.dequeue();
        }

        m_request->addBuffer(stream, buffer);

        queueRequest(m_request);
    }

    int queueRequest(Request *m_request)
    {
        return camera->queueRequest(m_request);
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
    CameraManager *m_cm;
    std::shared_ptr<Camera> camera;
    std::unique_ptr<libcamera::CameraConfiguration> m_config;
    std::unique_ptr<libcamera::Request> m_request;
    std::unique_ptr<libcamera::FrameBufferAllocator> m_allocator;
    std::map<libcamera::FrameBuffer *, std::unique_ptr<LibCameraImage>> m_mappedBuffers;
    std::map<const libcamera::Stream *, QQueue<libcamera::FrameBuffer *>> m_freeBuffers;
    QMutex  m_mutex;
    QQueue<libcamera::Request *> m_doneQueue;
	QQueue<libcamera::Request *> m_freeQueue;
    // QString m_filename;
    // int m_orientation;
    QImage::Format  m_viewFinderFormat;
    QImage::Format  m_secondaryFormat;
    converterFunction m_viewFinderConverter;
    converterFunction m_secondaryConverter;
    libcamera::Stream *m_previewStream;
    libcamera::Stream *m_secondaryStream;
    std::vector<std::unique_ptr<libcamera::Request>> m_requests;

    // Actual image / data
    libcamera::Size m_size;
    libcamera::FrameBuffer *m_displayedBuffer;
    QImage m_image;
    QImage m_image_scaled;

protected:
    QSGNode* updatePaintNode(QSGNode* node, UpdatePaintNodeData*) override
    {
        QSGImageNode* imgnode = static_cast<QSGImageNode*>(node);
        if(!imgnode)
            imgnode = window()->createImageNode();

        QSGTexture *texture = window()->createTextureFromImage(m_image_scaled);

        imgnode->setRect(boundingRect());
        imgnode->setSourceRect(QRectF(QPointF(0.0, 0.0), texture->textureSize()));
        imgnode->setTexture(texture);
        imgnode->setOwnsTexture(true);

        return imgnode;
    }
private:
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
