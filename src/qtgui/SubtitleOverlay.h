#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QPainter>
#include <random>

struct SubtitleMessage {
    QString header; // Top small header (e.g. "Special thanks to")
    QString line1;
    QString line2; // Affiliation / Subtitle
    QString line3; // Reason / Extra info
    double displayDuration = 5.0; // Seconds to hold
};

class SubtitleOverlay {
public:
    SubtitleOverlay();

    void addMessage(const QString &l1, const QString &l2 = "", const QString &l3 = "", double duration = 5.0, const QString &header = "");
    void start(const QString &name, const QString &dates, const QString &desc); // Initializes sequence
    void update(double dt);
    void paint(QPainter *painter, const QRect &bounds);

private:
    struct QueueItem {
        SubtitleMessage msg;
    };

    enum State {
        Hidden,
        FadeIn,
        Hold,
        FadeOut,
        Finished
    };

    QVector<QueueItem> m_queue;
    int m_currentIndex = 0;
    State m_state = Hidden;
    
    double m_stateTimer = 0.0;
    double m_opacity = 0.0;

    // Configuration
    double m_fadeInDuration = 1.0;
    double m_fadeOutDuration = 1.0;
    
    // Thanks data (static/shared)
    static QVector<SubtitleMessage> s_thanksList;
    static void initThanksList();
    
    std::mt19937 m_rng;
};
