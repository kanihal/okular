/*
    SPDX-FileCopyrightText: 2026 Jagadeesha

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _OKULAR_OPENAITTS_H_
#define _OKULAR_OPENAITTS_H_

#include <QMediaPlayer>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QStringList>

#include <deque>
#include <memory>

class QNetworkReply;
class QTemporaryFile;

/**
 * Text-to-speech backend that synthesizes speech through an OpenAI-compatible
 * HTTP API (POST <server>/v1/audio/speech) and plays the returned audio with
 * Qt Multimedia.
 *
 * Text passed to say() is split into sentence-boundary chunks so that
 * playback starts quickly and arbitrarily long documents can be spoken with
 * bounded memory: at most one HTTP request is in flight and at most two
 * synthesized chunks (the one playing and one prefetched) are kept as
 * temporary WAV files at any time.
 */
class OkularOpenAITTS : public QObject
{
    Q_OBJECT
public:
    enum class SpeechState {
        Idle,
        Preparing,
        Playing,
        Paused,
    };
    Q_ENUM(SpeechState)

    explicit OkularOpenAITTS(QObject *parent = nullptr);
    ~OkularOpenAITTS() override;

    void say(const QString &text);
    void saySegments(const QStringList &segments);
    void stop();
    void pauseResume();
    void setSpeed(double speed);
    /// Re-read server URL, model, voice, speed and API key from the settings.
    /// Applies to subsequent chunk requests; current playback continues.
    void reloadSettings();

    /// Split @p text into speakable chunks at sentence boundaries. The first
    /// chunk is kept small for fast time-to-first-audio. Pure function,
    /// exposed for unit testing.
    static QStringList chunkText(const QString &text, int firstChunkSize = 250, int chunkSize = 600);

Q_SIGNALS:
    void isSpeaking(bool speaking);
    void canPauseOrResume(bool speakingOrPaused);
    void speechStateChanged(OkularOpenAITTS::SpeechState state);
    void currentSegmentChanged(int index);
    void errorOccurred(const QString &message);

private:
    enum class State { Idle, Synthesizing, Playing, Paused };
    struct PendingSegment {
        int index = -1;
        QString text;
    };
    struct ReadyFile {
        int index = -1;
        std::unique_ptr<QTemporaryFile> file;
    };

    void requestNextChunk();
    void handleReplyFinished(QNetworkReply *reply);
    void handleMediaStatus(QMediaPlayer::MediaStatus status);
    void handlePlayerError(QMediaPlayer::Error error, const QString &errorString);
    void startPlayback();
    /// Abort the pipeline without emitting any signals.
    void teardown();
    /// teardown() + Idle state signals + errorOccurred.
    void failWithError(const QString &message);
    void setState(State state);
    QUrl speechEndpoint() const;

    State m_state = State::Idle;
    // Pause was pressed while waiting for the first chunk of audio.
    bool m_pauseRequested = false;
    std::deque<PendingSegment> m_pendingSegments;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_inFlightReply;
    int m_inFlightSegmentIndex = -1;
    // Synthesized chunks waiting to be played; bounded to 2 entries.
    std::deque<ReadyFile> m_readyFiles;
    // The file currently loaded in the player; QTemporaryFile removes the
    // file from disk on destruction.
    std::unique_ptr<QTemporaryFile> m_currentFile;
    int m_currentSegmentIndex = -1;
    QMediaPlayer *m_player = nullptr;

    QString m_url;
    QString m_model;
    QString m_voice;
    QString m_apiKey;
    double m_speed = 1.0;
};

#endif
