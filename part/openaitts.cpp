/*
    SPDX-FileCopyrightText: 2026 Jagadeesha

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "openaitts.h"

#include <QAudioOutput>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryFile>

#include <KLocalizedString>

#include <algorithm>

#include "settings.h"

static const int MaxReadyFiles = 2;
static const int RequestTimeoutMs = 60000;

OkularOpenAITTS::OkularOpenAITTS(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
{
    m_player->setAudioOutput(new QAudioOutput(m_player));
    connect(&m_nam, &QNetworkAccessManager::finished, this, &OkularOpenAITTS::handleReplyFinished);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &OkularOpenAITTS::handleMediaStatus);
    connect(m_player, &QMediaPlayer::errorOccurred, this, &OkularOpenAITTS::handlePlayerError);
    reloadSettings();
}

OkularOpenAITTS::~OkularOpenAITTS()
{
    teardown();
}

void OkularOpenAITTS::reloadSettings()
{
    m_url = Okular::Settings::ttsOpenAIUrl();
    m_model = Okular::Settings::ttsOpenAIModel();
    m_voice = Okular::Settings::ttsOpenAIVoice();
    m_apiKey = Okular::Settings::ttsOpenAIApiKey();
    m_speed = Okular::Settings::ttsOpenAISpeed();
}

void OkularOpenAITTS::say(const QString &text)
{
    saySegments(chunkText(text));
}

void OkularOpenAITTS::saySegments(const QStringList &segments)
{
    teardown();

    int index = 0;
    for (const QString &segment : segments) {
        const QString text = segment.trimmed();
        if (!text.isEmpty()) {
            m_pendingSegments.push_back({index, text});
        }
        ++index;
    }

    if (m_pendingSegments.empty()) {
        Q_EMIT isSpeaking(false);
        Q_EMIT canPauseOrResume(false);
        Q_EMIT speechStateChanged(SpeechState::Idle);
        Q_EMIT currentSegmentChanged(-1);
        return;
    }

    // Treat the synthesis round-trip as already speaking so the Stop and
    // Pause actions are usable while the first chunk is generated.
    setState(State::Synthesizing);
    Q_EMIT isSpeaking(true);
    Q_EMIT canPauseOrResume(true);
    Q_EMIT currentSegmentChanged(-1);
    requestNextChunk();
}

void OkularOpenAITTS::stop()
{
    teardown();
    Q_EMIT isSpeaking(false);
    Q_EMIT canPauseOrResume(false);
    Q_EMIT speechStateChanged(SpeechState::Idle);
    Q_EMIT currentSegmentChanged(-1);
}

void OkularOpenAITTS::pauseResume()
{
    switch (m_state) {
    case State::Playing:
        m_player->pause();
        setState(State::Paused);
        Q_EMIT isSpeaking(false);
        Q_EMIT canPauseOrResume(true);
        break;
    case State::Paused:
        m_player->play();
        setState(State::Playing);
        Q_EMIT isSpeaking(true);
        Q_EMIT canPauseOrResume(true);
        break;
    case State::Synthesizing:
        // No audio to pause yet; remember the request and apply it as soon
        // as the first chunk arrives.
        m_pauseRequested = !m_pauseRequested;
        Q_EMIT isSpeaking(!m_pauseRequested);
        Q_EMIT canPauseOrResume(true);
        break;
    case State::Idle:
        break;
    }
}

void OkularOpenAITTS::setSpeed(double speed)
{
    m_speed = qBound(0.25, speed, 4.0);
}

void OkularOpenAITTS::teardown()
{
    m_state = State::Idle;
    m_pauseRequested = false;
    m_pendingSegments.clear();
    QNetworkReply *reply = m_inFlightReply.data();
    m_inFlightReply.clear();
    m_inFlightSegmentIndex = -1;
    if (reply) {
        reply->abort();
    }
    m_player->stop();
    // Release the file handle before QTemporaryFile deletes the file.
    m_player->setSource(QUrl());
    m_currentFile.reset();
    m_currentSegmentIndex = -1;
    m_readyFiles.clear();
}

void OkularOpenAITTS::failWithError(const QString &message)
{
    teardown();
    Q_EMIT isSpeaking(false);
    Q_EMIT canPauseOrResume(false);
    Q_EMIT speechStateChanged(SpeechState::Idle);
    Q_EMIT currentSegmentChanged(-1);
    Q_EMIT errorOccurred(message);
}

void OkularOpenAITTS::setState(State state)
{
    m_state = state;
    switch (m_state) {
    case State::Idle:
        Q_EMIT speechStateChanged(SpeechState::Idle);
        break;
    case State::Synthesizing:
        Q_EMIT speechStateChanged(SpeechState::Preparing);
        break;
    case State::Playing:
        Q_EMIT speechStateChanged(SpeechState::Playing);
        break;
    case State::Paused:
        Q_EMIT speechStateChanged(SpeechState::Paused);
        break;
    }
}

QUrl OkularOpenAITTS::speechEndpoint() const
{
    QString base = m_url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    // Accept both "http://host:port" and "http://host:port/v1" in the setting.
    if (base.endsWith(QLatin1String("/v1"))) {
        base.chop(3);
    }
    return QUrl(base + QStringLiteral("/v1/audio/speech"));
}

void OkularOpenAITTS::requestNextChunk()
{
    if (m_inFlightReply || static_cast<int>(m_readyFiles.size()) >= MaxReadyFiles || m_pendingSegments.empty()) {
        return;
    }

    const PendingSegment segment = m_pendingSegments.front();
    m_pendingSegments.pop_front();
    m_inFlightSegmentIndex = segment.index;

    QNetworkRequest request(speechEndpoint());
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(RequestTimeoutMs);
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + m_apiKey.toUtf8());
    }

    QJsonObject body;
    body[QStringLiteral("model")] = m_model;
    body[QStringLiteral("input")] = segment.text;
    body[QStringLiteral("voice")] = m_voice;
    body[QStringLiteral("response_format")] = QStringLiteral("wav");
    body[QStringLiteral("speed")] = qBound(0.25, m_speed, 4.0);

    m_inFlightReply = m_nam.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
}

void OkularOpenAITTS::handleReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    const bool current = (reply == m_inFlightReply.data());
    const int segmentIndex = m_inFlightSegmentIndex;
    if (current) {
        m_inFlightReply.clear();
        m_inFlightSegmentIndex = -1;
    }
    // Aborted by teardown(), or a reply that is no longer ours.
    if (!current || reply->error() == QNetworkReply::OperationCanceledError || m_state == State::Idle) {
        return;
    }

    const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
        QString detail = reply->errorString();
        const QString bodyExcerpt = QString::fromUtf8(reply->readAll().left(200)).trimmed();
        if (httpStatus >= 400) {
            detail = i18nc("@info %1 is a HTTP status code, %2 the start of the server response", "server returned status %1 (%2)", httpStatus, bodyExcerpt);
        }
        failWithError(i18n("Text-to-speech request failed: %1", detail));
        return;
    }

    const QByteArray audio = reply->readAll();
    if (audio.isEmpty()) {
        failWithError(i18n("Text-to-speech request failed: the server returned no audio data"));
        return;
    }

    auto file = std::make_unique<QTemporaryFile>(QDir::tempPath() + QStringLiteral("/okular_tts_XXXXXX.wav"));
    if (!file->open() || file->write(audio) != audio.size()) {
        failWithError(i18n("Could not write temporary text-to-speech audio file"));
        return;
    }
    file->close();
    m_readyFiles.push_back({segmentIndex, std::move(file)});

    if (m_state == State::Synthesizing) {
        startPlayback();
    }
    requestNextChunk();
}

void OkularOpenAITTS::startPlayback()
{
    if (m_readyFiles.empty()) {
        return;
    }

    ReadyFile readyFile = std::move(m_readyFiles.front());
    m_readyFiles.pop_front();
    m_currentSegmentIndex = readyFile.index;
    m_currentFile = std::move(readyFile.file);
    m_player->setSource(QUrl::fromLocalFile(m_currentFile->fileName()));
    Q_EMIT currentSegmentChanged(m_currentSegmentIndex);

    if (m_pauseRequested) {
        // Pause was pressed while synthesizing: load the chunk but wait for
        // resume before starting it.
        m_pauseRequested = false;
        setState(State::Paused);
        Q_EMIT isSpeaking(false);
        Q_EMIT canPauseOrResume(true);
        return;
    }

    m_player->play();
    if (m_state != State::Playing) {
        setState(State::Playing);
        Q_EMIT isSpeaking(true);
        Q_EMIT canPauseOrResume(true);
    }
}

void OkularOpenAITTS::handleMediaStatus(QMediaPlayer::MediaStatus status)
{
    if (m_state != State::Playing || status != QMediaPlayer::EndOfMedia) {
        return;
    }

    m_player->setSource(QUrl());
    m_currentFile.reset();
    m_currentSegmentIndex = -1;

    if (!m_readyFiles.empty()) {
        startPlayback();
        requestNextChunk();
    } else if (!m_pendingSegments.empty() || m_inFlightReply) {
        // Synthesis is running behind playback; keep the speaking state and
        // resume automatically when the next chunk arrives.
        Q_EMIT currentSegmentChanged(-1);
        setState(State::Synthesizing);
    } else {
        setState(State::Idle);
        Q_EMIT isSpeaking(false);
        Q_EMIT canPauseOrResume(false);
        Q_EMIT currentSegmentChanged(-1);
    }
}

void OkularOpenAITTS::handlePlayerError(QMediaPlayer::Error error, const QString &errorString)
{
    Q_UNUSED(error)
    if (m_state == State::Idle) {
        return;
    }
    failWithError(i18n("Could not play synthesized speech: %1", errorString));
}

QStringList OkularOpenAITTS::chunkText(const QString &text, int firstChunkSize, int chunkSize)
{
    // Characters that end a sentence; a following whitespace makes the
    // position a preferred split point. A newline alone is also accepted
    // (page breaks are inserted as '\n' by the callers).
    const auto isTerminator = [](QChar c) {
        return c == QLatin1Char('.') || c == QLatin1Char('!') || c == QLatin1Char('?') || c == QChar(0x2026) /* … */;
    };
    const auto isSpeakable = [](const QString &s) {
        return std::any_of(s.cbegin(), s.cend(), [](QChar c) { return c.isLetterOrNumber(); });
    };

    QStringList chunks;
    const int len = text.length();
    // How far past the target we are willing to look for a boundary before
    // falling back to a whitespace or hard split.
    const int slack = 200;
    int target = qMax(1, firstChunkSize);
    int pos = 0;

    while (pos < len) {
        int cut = -1;

        if (len - pos <= target) {
            cut = len;
        } else {
            const int hardLimit = qMin(pos + target + slack, len);
            int lastSentence = -1;
            int lastSpace = -1;

            for (int i = pos; i < hardLimit; ++i) {
                const QChar c = text.at(i);
                if (c == QLatin1Char('\n')) {
                    lastSentence = i;
                    lastSpace = i;
                } else if (c.isSpace()) {
                    lastSpace = i;
                    if (i > pos && isTerminator(text.at(i - 1))) {
                        lastSentence = i;
                    }
                }
                if (i - pos + 1 >= target && lastSentence > pos) {
                    cut = lastSentence;
                    break;
                }
            }

            if (cut < 0) {
                if (lastSentence > pos) {
                    cut = lastSentence;
                } else if (lastSpace > pos) {
                    cut = lastSpace;
                } else {
                    cut = hardLimit;
                }
            }
        }

        const QString chunk = text.mid(pos, cut - pos).trimmed();
        if (isSpeakable(chunk)) {
            chunks.append(chunk);
            target = qMax(1, chunkSize);
        }
        pos = qMax(cut, pos + 1);
    }

    return chunks;
}
