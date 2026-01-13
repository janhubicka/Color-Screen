#pragma once

#include <QObject>
#include <QImage>
#include <atomic>
#include <memory> // Added for std::shared_ptr
#include "../libcolorscreen/include/imagedata.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/render-parameters.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/render-type-parameters.h" // Added as per instruction
#include "../libcolorscreen/include/scr-to-img-parameters.h" // Replaces scr-to-img.h and part of colorscreen.h
#include "../libcolorscreen/include/scr-detect-parameters.h" // Replaces part of colorscreen.h
#include "../libcolorscreen/include/progress-info.h" // Added for colorscreen::progress_info

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
    
    // Optional: method to update params if changed in UI
    // void setProcessParams(...);

public slots:
    // We accept render_parameters by value here to capture state for this render frame
    void render(int reqId, double xOffset, double yOffset, double scale, int width, int height, 
                colorscreen::render_parameters frameParams, 
                std::shared_ptr<colorscreen::progress_info> progress);
    
    // Update internal parameter copies (for non-blocking parameter updates)
    void updateParameters(const colorscreen::render_parameters &rparams,
                         const colorscreen::scr_to_img_parameters &scrToImg,
                         const colorscreen::scr_detect_parameters &scrDetect);

signals:
    void imageReady(int reqId, QImage image, double xOffset, double yOffset, double scale, bool success);

private:
    std::shared_ptr<colorscreen::image_data> m_scan;
    colorscreen::render_parameters m_rparams; // Local copy
    
    // We maintain copies or use defaults for now
    colorscreen::scr_to_img_parameters m_scrToImg;
    colorscreen::scr_detect_parameters m_scrDetect;
    colorscreen::render_type_parameters m_renderType;
};
