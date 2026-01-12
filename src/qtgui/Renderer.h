#pragma once

#include <QObject>
#include <QImage>
#include <atomic>
#include "../libcolorscreen/include/colorscreen.h"
#include "../libcolorscreen/include/scr-to-img.h" // For scr_to_img_parameters

class Renderer : public QObject
{
    Q_OBJECT
public:
    explicit Renderer(std::shared_ptr<colorscreen::image_data> scan, const colorscreen::render_parameters &rparams);
    ~Renderer() override;
    
    // Optional: method to update params if changed in UI
    // void setProcessParams(...);

public slots:
    // We accept render_parameters by value here to capture state for this render frame
    void render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                colorscreen::render_parameters frameParams, 
                std::shared_ptr<colorscreen::progress_info> progress);

signals:
    void imageReady(int reqId, QImage image, double xOffset, double yOffset, double scale);

private:
    std::shared_ptr<colorscreen::image_data> m_scan;
    colorscreen::render_parameters m_rparams; // Local copy
    
    // We maintain copies or use defaults for now
    colorscreen::scr_to_img_parameters m_scrToImg;
    colorscreen::scr_detect_parameters m_scrDetect;
};
