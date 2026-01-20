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
        //"For letting me to see his collection and discovering Finlay color photographs in Library of Congress"
	"Without him this project would never happen"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Gemini",
        "Google",
        "For co-implementing the GUI"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Claude Sonnet",
        "Anthropic",
        "For co-implementing the GUI"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Bertrand Lavedrine",
        "Muséum National d'Histoire Naturelle, Paris",
        "For multiple consultations and motivating comments"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Scott Wajon",
        "State Library of New South Wales",
        "For digitizing Frank Huley paget plates in infrared. Best Christmas present ever."
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Bruce York",
        "State Library of New South Wales",
        "For digitizing Frank Huley paget plates in infrared"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Geoffrey Barker",
        "State Library of New South Wales",
        "For digitizing Frank Huley paget plates in infrared"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Russell Perkings",
        "State Library of New South Wales",
        "For digitizing Frank Huley paget plates in infrared"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Lynn Brooks",
        "Library of Congress",
        "For collaboration on Prokudin-Gorskii exhibition and visiting our museum"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Micah Messenheimer",
        "Library of Congress",
        "For digitization of Finlaycolor plates from the Matson collection"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Phil Mitchell",
        "Library of Congress",
        "For consultations and digitization of Finlaycolor plates from the Matson collection"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Helena Zinkham",
        "Library of Congress",
        "For warm welcome and letting me to see Prokudin-Gorskii's negatives"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Thomas Rieger",
        "Library of Congress",
        "For digitization of Finlaycolor plates from the Matson collection"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Melichar Konečný",
        "Charles University",
        "For work in demosaicing algorithms"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Linda Kimrová",
        "Charles University",
        "For imlementing GUI and helping with digitization of Oscar Jordan plates"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Doug Peterson",
        "Digital Transitions, Inc.",
        "For joint adventure on stitching Dufaycolors"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Kenzie Klaeser",
        "Digital Transitions, Inc.",
        "For joint adventure on stitching Dufaycolors"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Peter Siegel",
        "Digital Transitions, Inc.",
        "For joint adventure on stitching Dufaycolors"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Sara Manco",
        "Naional Geographic Society",
        "For collaboration on stitching and color reconstructions of scans at the National Geographic Society."
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Julie McVey",
        "Naional Geographic Society",
        "For collaboration on stitching and color reconstructions of scans at the National Geographic Society."
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Alice Plutino",
        "Department of Media Studies, Universiteit van Amsterdam",
        "For discussion on separating subtractive color processes"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Dan Smith",
        "",
        "For sharing his knowledge of Dufaycolor and other early color photogrpahs"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "High Tift",
        "",
        "For sharing his knowledge of Dufaycolor and other early color photogrpahs"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Kirsten Carter",
        "FDR Presidential Library & Museum",
        "For letting us to digitize Oscar Jordan's negatives"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Matthew Hanson",
        "FDR Presidential Library & Museum",
        "For letting us to digitize Oscar Jordan's negatives"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "William Harris",
        "FDR Presidential Library & Museum",
        "For letting us to digitize Oscar Jordan's negatives"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Luisa Casella",
        "",
        "For discussions about permanence of Autochrome"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Victor Gerasimov",
        "",
        "For scans of Paget plates from Bulgaria"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Janine Freeston",
        "",
        "For organizing Color Photography in the 19th Century and Early 20th Century: Sciences, Technologies, Empires"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Hanin Hannouch",
        "Weltmuseum Wien",
        "For organizing Color Photography in the 19th Century and Early 20th Century: Sciences, Technologies, Empires"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Heather Sonntag",
        "",
        "For taking care of Mark Jacobs' collection and arranging an exhibition"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Andy Kraushaar",
        "",
        "For taking care of Mark Jacobs' collection and digitizing Warner-Powrie plate"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Kendra Meyer",
        "National Museum of Natural History",
        "For discovering Paget plates at the National Museum of American History and scanning examples for us"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Alan Griffiths",
        "",
        "For his work on Luminous-lint and other efforts to get people in this area connected"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Robert Hirsh",
        "",
        "For taking care of Mark Jacobs' collection, consumations and warm welcome in Buffalo"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Ladislav Bezděk",
        "Národní památkový ústav",
        "For work on the first publication in book Živý Film"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Martin Frouz",
        "Národní památkový ústav",
        "For work on the first publication in book Živý Film"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Volker Jansen"
        "Zeutschel GmbH",
        "For arranging vising in Zeutshel and digitizing many samples for us"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Alexander Sander"
        "Zeutschel GmbH",
        "For arranging vising in Zeutshel and digitizing many samples for us"
    });
    s_thanksList.append({
        "", // Header (filled at runtime or here)
        "Giorgio Trumpy",
        "",
        "For sharing his measurement of Dufaycolor dye and his wonderful research"
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
    addMessage("Color-Screen 2.0", "Developed by Jan Hubička  2022-2026", "https://github.com/janhubicka/Color-Screen/wiki", 4.0);
    
    addMessage("Graphical interface", "Vibe-coding experiment, January 2026", "based on Java GUI by Linda Kimrová", 4.0);
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
    headerFont.setPointSize(12);
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
        QRect shadowRect = textRect.translated(4, 4);
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
