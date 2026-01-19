#include "SubtitleOverlay.h"
#include <algorithm>
#include <QFont>
#include <QFontMetrics>

QVector<SubtitleMessage> SubtitleOverlay::s_thanksList;

void SubtitleOverlay::initThanksList() {
    if (!s_thanksList.empty()) return;
    
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Mark Jacobs",
        "Color photography expert, collector and enthusiast",
        "For letting me to see his collection and discovering Finlay color photographs in Library of Congress"
    });
    
    // Add more here as requested later
}

SubtitleOverlay::SubtitleOverlay() {
    std::random_device rd;
    m_rng.seed(rd());
    initThanksList();
}

void SubtitleOverlay::addMessage(const QString &l1, const QString &l2, const QString &l3, double duration, const QString &header) {
    SubtitleMessage msg = {header, l1, l2, l3, duration};
    m_queue.append(QueueItem{msg});
}

void SubtitleOverlay::start(const QString &name, const QString &dates, const QString &desc) {
    m_queue.clear();
    m_currentIndex = 0;
    m_state = Hidden;
    m_stateTimer = 0.0;
    m_opacity = 0.0;

    // 1. Intro
    addMessage("Color-Screen 2.0", "Developed by Jan Hubicka 2022-2026", "", 4.0);
    
    // 2. Animation Details
    addMessage(name, dates, desc, 5.0);
    
    // 3. Thanks List (Randomized)
    QVector<SubtitleMessage> shuffled = s_thanksList;
    std::shuffle(shuffled.begin(), shuffled.end(), m_rng);
    
    for (auto &msg : shuffled) {
        msg.header = "Special thanks to";
        m_queue.append(QueueItem{msg});
    }

    // Start
    if (!m_queue.empty()) {
        m_state = FadeIn;
    }
}

void SubtitleOverlay::update(double dt) {
    if (m_queue.empty() || m_currentIndex >= m_queue.size()) return;
    
    const auto &currentMsg = m_queue[m_currentIndex].msg;
    
    m_stateTimer += dt;
    
    switch (m_state) {
        case Hidden:
            // Should verify if we have next message
            m_state = FadeIn;
            m_stateTimer = 0.0;
            break;
            
        case FadeIn:
            if (m_stateTimer >= m_fadeInDuration) {
                m_opacity = 1.0;
                m_state = Hold;
                m_stateTimer = 0.0;
            } else {
                m_opacity = m_stateTimer / m_fadeInDuration;
            }
            break;
            
        case Hold:
            if (m_stateTimer >= currentMsg.displayDuration) {
                m_state = FadeOut;
                m_stateTimer = 0.0;
            }
            break;
            
        case FadeOut:
            if (m_stateTimer >= m_fadeOutDuration) {
                m_opacity = 0.0;
                m_currentIndex++;
                if (m_currentIndex < m_queue.size()) {
                    m_state = FadeIn; // Proceed to next
                    m_stateTimer = 0.0;
                } else {
                    m_state = Finished;
                }
            } else {
                m_opacity = 1.0 - (m_stateTimer / m_fadeOutDuration);
            }
            break;
            
        case Finished:
            break;
    }
}

void SubtitleOverlay::paint(QPainter *painter, const QRect &bounds) {
    if (m_queue.empty() || m_currentIndex >= m_queue.size() || m_state == Finished || m_opacity <= 0.0) return;

    const auto &msg = m_queue[m_currentIndex].msg;

    painter->save();
    painter->setOpacity(m_opacity);
    
    // Setup Fonts
    QFont titleFont = painter->font();
    titleFont.setBold(true);
    titleFont.setPointSize(24);
    
    QFont subFont = painter->font();
    subFont.setPointSize(14);
    
    QFont descFont = painter->font();
    descFont.setPointSize(12);
    descFont.setItalic(true);

    QFontMetrics fmTitle(titleFont);
    QFontMetrics fmSub(subFont);
    QFontMetrics fmDesc(descFont);
    
    int hTitle = fmTitle.height();
    int hSub = fmSub.height();
    int hDesc = fmDesc.height();
    
    // Header font
    QFont headerFont = painter->font();
    headerFont.setPointSize(10);
    headerFont.setCapitalization(QFont::SmallCaps);
    headerFont.setLetterSpacing(QFont::AbsoluteSpacing, 2.0);
    QFontMetrics fmHeader(headerFont);
    int hHeader = fmHeader.height();
    
    int spacing = 5;
    int totalHeight = hTitle;
    if (!msg.header.isEmpty()) totalHeight += hHeader + spacing;
    if (!msg.line2.isEmpty()) totalHeight += hSub + spacing;
    if (!msg.line3.isEmpty()) totalHeight += hDesc + spacing;
    
    // Position at bottom with some margin
    int bottomMargin = 40;
    int yPos = bounds.bottom() - bottomMargin - totalHeight;
    int centerX = bounds.center().x();
    
    // Draw background (optional, similar to subtitles? Black with low alpha?)
    // Let's do a subtle shadow/outline for readability on top of animation
    auto drawTextWithShadow = [&](int y, const QString &text, const QFont &font) {
        painter->setFont(font);
        QRect textRect(bounds.left(), y, bounds.width(), QFontMetrics(font).height());
        
        // Shadow/Outline
        painter->setPen(QColor(0, 0, 0, 180));
        QRect shadowRect = textRect.translated(2, 2);
        painter->drawText(shadowRect, Qt::AlignCenter, text);
        
        // Text
        painter->setPen(Qt::white);
        painter->drawText(textRect, Qt::AlignCenter, text);
        
        return textRect.bottom() + spacing;
    };
    
    int currentY = yPos;
    
    if (!msg.header.isEmpty()) {
         currentY = drawTextWithShadow(currentY, msg.header, headerFont);
    }
    
    currentY = drawTextWithShadow(currentY, msg.line1, titleFont);
    
    if (!msg.line2.isEmpty()) {
        currentY = drawTextWithShadow(currentY, msg.line2, subFont);
    }
    
    if (!msg.line3.isEmpty()) {
        drawTextWithShadow(currentY, msg.line3, descFont);
    }

    painter->restore();
}
