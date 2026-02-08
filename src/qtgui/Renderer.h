#pragma once

#include <QObject>
#include <QImage>
#include <atomic>
#include <memory> // Added for std::shared_ptr
#include <QFuture>
#include <QList>
#include "../libcolorscreen/include/imagedata.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/render-parameters.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/render-type-parameters.h" // Added as per instruction
#include "../libcolorscreen/include/scr-to-img-parameters.h" // Replaces scr-to-img.h and part of colorscreen.h
#include "../libcolorscreen/include/scr-detect-parameters.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/progress-info.h" // Added for colorscreen::progress_info

#include <QMutex>

class Renderer : public QObject
{
    Q_OBJECT
public:
    explicit Renderer(std::shared_ptr<colorscreen::image_data> scan, 
                      const colorscreen::render_parameters &rparams,
                      const colorscreen::scr_to_img_parameters &scrToImg,
                      const colorscreen::scr_detect_parameters &scrDetect,
                      const colorscreen::render_type_parameters &renderType);
    ~Renderer() override;
    
public slots:
    void render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                colorscreen::render_parameters frameParams, 
                std::shared_ptr<colorscreen::progress_info> progress,
                const char* taskName = nullptr);
    
    void updateParameters(const colorscreen::render_parameters &rparams,
                         const colorscreen::scr_to_img_parameters &scrToImg,
                         const colorscreen::scr_detect_parameters &scrDetect,
                         const colorscreen::render_type_parameters &renderType);

signals:
    void imageReady(int reqId, QImage image, double xOffset, double yOffset, double scale, bool success);

private:
    std::shared_ptr<colorscreen::image_data> m_scan;
    
    // Parameters protected by mutex
    mutable QMutex m_mutex;
    colorscreen::render_parameters m_rparams; 
    colorscreen::scr_to_img_parameters m_scrToImg;
    colorscreen::scr_detect_parameters m_scrDetect;
    colorscreen::render_type_parameters m_renderType;
    
    // Futures of active rendering tasks
    QList<QFuture<void>> m_activeFutures;
};
