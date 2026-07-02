/*
    SPDX-FileCopyrightText: 2008 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "tts.h"

#include <QSet>

#include <KLocalizedString>

#include "settings.h"

#if HAVE_OPENAI_TTS
#include "openaitts.h"
#endif

/* Private storage. */
class OkularTTS::Private
{
public:
    explicit Private(OkularTTS *qq)
        : q(qq)
    {
    }

    ~Private()
    {
        delete speech;
        speech = nullptr;
        // openai is a QObject child of q and is deleted with it.
    }

    void createBackend();
    void destroyBackend();
    void clearActiveSpeech();
    void emitSpeechState(OkularTTS::SpeechState state);
    void handleBackendCurrentSegmentChanged(int index);
    void handleBackendSpeechState(OkularTTS::SpeechState state);
    void finishNativeSegments();
    void startBackendSegments(const QStringList &segments, int segmentOffset);
    void speakNextNativeSegment();
    static double speedToQtRate(double speed);

    OkularTTS *q;
    QTextToSpeech *speech = nullptr;
#if HAVE_OPENAI_TTS
    OkularOpenAITTS *openai = nullptr;
#endif
    QStringList pendingNativeSegments;
    bool speakingNativeSegments = false;
    int nativeSegmentIndex = -1;
    int nativeSegmentCount = 0;
    QStringList activeSegments;
    int currentSegmentIndex = -1;
    int backendSegmentOffset = 0;
    bool pendingEngineResume = false;
    double configuredSpeed = Okular::Settings::ttsOpenAISpeed();
    double speed = configuredSpeed;
    OkularTTS::SpeechState speechState = OkularTTS::SpeechState::Idle;
    // Which speech engine was used when the backend was created.
    // When the setting changes, we need to stop speaking and recreate.
    QString speechEngine;
    Q_DISABLE_COPY(Private)
};

void OkularTTS::Private::createBackend()
{
    const QString engine = Okular::Settings::ttsEngine();
    speechEngine = engine;

#if HAVE_OPENAI_TTS
    if (engine == OKULAR_OPENAI_TTS_ENGINE_ID) {
        openai = new OkularOpenAITTS(q);
        QObject::connect(openai, &OkularOpenAITTS::isSpeaking, q, &OkularTTS::isSpeaking);
        QObject::connect(openai, &OkularOpenAITTS::canPauseOrResume, q, &OkularTTS::canPauseOrResume);
        QObject::connect(openai, &OkularOpenAITTS::currentSegmentChanged, q, [this](int index) { handleBackendCurrentSegmentChanged(index); });
        QObject::connect(openai, &OkularOpenAITTS::speechStateChanged, q, [this](OkularOpenAITTS::SpeechState state) {
            switch (state) {
            case OkularOpenAITTS::SpeechState::Idle:
                handleBackendSpeechState(OkularTTS::SpeechState::Idle);
                break;
            case OkularOpenAITTS::SpeechState::Preparing:
                handleBackendSpeechState(OkularTTS::SpeechState::Preparing);
                break;
            case OkularOpenAITTS::SpeechState::Playing:
                handleBackendSpeechState(OkularTTS::SpeechState::Playing);
                break;
            case OkularOpenAITTS::SpeechState::Paused:
                handleBackendSpeechState(OkularTTS::SpeechState::Paused);
                break;
            }
        });
        QObject::connect(openai, &OkularOpenAITTS::errorOccurred, q, &OkularTTS::errorOccurred);
        return;
    }
#endif

    speech = new QTextToSpeech(engine);
    speech->setRate(speedToQtRate(speed));
    const QList<QVoice> voices = speech->availableVoices();
    const QString voiceName = Okular::Settings::ttsVoice();
    for (const QVoice &voice : voices) {
        if (voice.name() == voiceName) {
            speech->setVoice(voice);
        }
    }
    QObject::connect(speech, &QTextToSpeech::stateChanged, q, &OkularTTS::slotSpeechStateChanged);
    QObject::connect(speech, &QTextToSpeech::errorOccurred, q, [this](QTextToSpeech::ErrorReason reason, const QString &errorString) {
        Q_UNUSED(reason)
        finishNativeSegments();
        Q_EMIT q->errorOccurred(i18n("Text-to-speech failed: %1", errorString));
    });
}

void OkularTTS::Private::destroyBackend()
{
    pendingNativeSegments.clear();
    speakingNativeSegments = false;
    nativeSegmentIndex = -1;
    nativeSegmentCount = 0;

    if (speech) {
        QObject::disconnect(speech, nullptr, q, nullptr);
        speech->stop();
        delete speech;
        speech = nullptr;
    }
#if HAVE_OPENAI_TTS
    if (openai) {
        QObject::disconnect(openai, nullptr, q, nullptr);
        openai->stop();
        openai->deleteLater();
        openai = nullptr;
    }
#endif
}

void OkularTTS::Private::clearActiveSpeech()
{
    activeSegments.clear();
    currentSegmentIndex = -1;
    backendSegmentOffset = 0;
    pendingEngineResume = false;
}

void OkularTTS::Private::emitSpeechState(OkularTTS::SpeechState state)
{
    speechState = state;
    Q_EMIT q->speechStateChanged(state);
}

void OkularTTS::Private::handleBackendCurrentSegmentChanged(int index)
{
    if (index < 0) {
        Q_EMIT q->currentSegmentChanged(-1);
        return;
    }

    const int mappedIndex = backendSegmentOffset + index;
    if (mappedIndex < 0 || mappedIndex >= activeSegments.count()) {
        Q_EMIT q->currentSegmentChanged(-1);
        return;
    }

    currentSegmentIndex = mappedIndex;
    Q_EMIT q->currentSegmentChanged(currentSegmentIndex);
}

void OkularTTS::Private::handleBackendSpeechState(OkularTTS::SpeechState state)
{
    if (state == OkularTTS::SpeechState::Idle) {
        clearActiveSpeech();
    }
    emitSpeechState(state);
}

void OkularTTS::Private::finishNativeSegments()
{
    pendingNativeSegments.clear();
    speakingNativeSegments = false;
    nativeSegmentIndex = -1;
    nativeSegmentCount = 0;
    clearActiveSpeech();
    Q_EMIT q->currentSegmentChanged(-1);
    Q_EMIT q->isSpeaking(false);
    Q_EMIT q->canPauseOrResume(false);
    emitSpeechState(OkularTTS::SpeechState::Idle);
}

void OkularTTS::Private::startBackendSegments(const QStringList &segments, int segmentOffset)
{
    backendSegmentOffset = segmentOffset;
#if HAVE_OPENAI_TTS
    if (openai) {
        openai->saySegments(segments);
        return;
    }
#endif
    if (speech) {
        speakingNativeSegments = false;
        pendingNativeSegments.clear();
        pendingNativeSegments = segments;
        speakingNativeSegments = true;
        nativeSegmentIndex = -1;
        nativeSegmentCount = segments.count();
        speakNextNativeSegment();
    }
}

void OkularTTS::Private::speakNextNativeSegment()
{
    if (!speech || pendingNativeSegments.isEmpty()) {
        finishNativeSegments();
        return;
    }

    const QString segment = pendingNativeSegments.takeFirst();
    ++nativeSegmentIndex;
    speech->setRate(speedToQtRate(speed));
    handleBackendCurrentSegmentChanged(nativeSegmentIndex);
    Q_EMIT q->isSpeaking(true);
    Q_EMIT q->canPauseOrResume(true);
    emitSpeechState(OkularTTS::SpeechState::Preparing);
    speech->say(segment);
}

double OkularTTS::Private::speedToQtRate(double speed)
{
    speed = qBound(0.25, speed, 4.0);
    if (speed >= 1.0) {
        return qMin(1.0, (speed - 1.0) / 3.0);
    }
    return qMax(-1.0, speed - 1.0);
}

OkularTTS::OkularTTS(QObject *parent)
    : QObject(parent)
    , d(new Private(this))
{
    d->createBackend();
    connect(Okular::Settings::self(), &KCoreConfigSkeleton::configChanged, this, &OkularTTS::slotConfigChanged);
}

OkularTTS::~OkularTTS()
{
    delete d;
}

void OkularTTS::say(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    saySegments({text});
}

void OkularTTS::saySegments(const QStringList &segments)
{
    QStringList speakableSegments;
    for (const QString &segment : segments) {
        const QString text = segment.trimmed();
        if (!text.isEmpty()) {
            speakableSegments.append(text);
        }
    }
    if (speakableSegments.isEmpty()) {
        return;
    }

    d->clearActiveSpeech();
    d->activeSegments = speakableSegments;
    d->startBackendSegments(d->activeSegments, 0);
}

void OkularTTS::stopAllSpeechs()
{
    d->clearActiveSpeech();
#if HAVE_OPENAI_TTS
    if (d->openai) {
        d->openai->stop();
        return;
    }
#endif
    if (!d->speech) {
        return;
    }

    d->pendingNativeSegments.clear();
    d->speakingNativeSegments = false;
    d->nativeSegmentIndex = -1;
    d->nativeSegmentCount = 0;
    d->speech->stop();
    Q_EMIT currentSegmentChanged(-1);
    Q_EMIT isSpeaking(false);
    Q_EMIT canPauseOrResume(false);
    d->emitSpeechState(OkularTTS::SpeechState::Idle);
}

void OkularTTS::pauseResumeSpeech()
{
    if (d->pendingEngineResume) {
        if (d->activeSegments.isEmpty()) {
            d->clearActiveSpeech();
            Q_EMIT currentSegmentChanged(-1);
            Q_EMIT isSpeaking(false);
            Q_EMIT canPauseOrResume(false);
            d->emitSpeechState(OkularTTS::SpeechState::Idle);
            return;
        }

        const int startIndex = qBound(0, d->currentSegmentIndex >= 0 ? d->currentSegmentIndex : 0, d->activeSegments.count() - 1);
        const QStringList resumeSegments = d->activeSegments.mid(startIndex);
        if (resumeSegments.isEmpty()) {
            d->clearActiveSpeech();
            Q_EMIT currentSegmentChanged(-1);
            Q_EMIT isSpeaking(false);
            Q_EMIT canPauseOrResume(false);
            d->emitSpeechState(OkularTTS::SpeechState::Idle);
            return;
        }

        d->pendingEngineResume = false;
        d->startBackendSegments(resumeSegments, startIndex);
        return;
    }

#if HAVE_OPENAI_TTS
    if (d->openai) {
        d->openai->pauseResume();
        return;
    }
#endif
    if (!d->speech) {
        return;
    }

    if (d->speech->state() == QTextToSpeech::Speaking) {
        d->speech->pause();
    } else {
        d->speech->resume();
    }
}

void OkularTTS::slotSpeechStateChanged(QTextToSpeech::State state)
{
    if (state == QTextToSpeech::Speaking) {
        Q_EMIT isSpeaking(true);
        Q_EMIT canPauseOrResume(true);
        d->handleBackendSpeechState(OkularTTS::SpeechState::Playing);
    } else if (state == QTextToSpeech::Synthesizing) {
        Q_EMIT isSpeaking(true);
        Q_EMIT canPauseOrResume(true);
        d->handleBackendSpeechState(OkularTTS::SpeechState::Preparing);
    } else if (state == QTextToSpeech::Ready && d->speakingNativeSegments) {
        d->speakNextNativeSegment();
    } else {
        Q_EMIT isSpeaking(false);
        if (state == QTextToSpeech::Paused) {
            Q_EMIT canPauseOrResume(true);
            d->handleBackendSpeechState(OkularTTS::SpeechState::Paused);
        } else {
            Q_EMIT canPauseOrResume(false);
            d->handleBackendSpeechState(OkularTTS::SpeechState::Idle);
        }
    }
}

void OkularTTS::setSpeed(double speed)
{
    d->speed = qBound(0.25, speed, 4.0);
#if HAVE_OPENAI_TTS
    if (d->openai) {
        d->openai->setSpeed(d->speed);
        return;
    }
#endif
    if (d->speech) {
        d->speech->setRate(Private::speedToQtRate(d->speed));
    }
}

double OkularTTS::speed() const
{
    return d->speed;
}

void OkularTTS::slotConfigChanged()
{
    const QString engine = Okular::Settings::ttsEngine();
    const double configuredSpeed = qBound(0.25, Okular::Settings::ttsOpenAISpeed(), 4.0);
    if (configuredSpeed != d->configuredSpeed) {
        d->configuredSpeed = configuredSpeed;
        d->speed = configuredSpeed;
    }

    if (engine != d->speechEngine) {
        const bool preserveForResume = !d->activeSegments.isEmpty() && d->speechState != OkularTTS::SpeechState::Idle;
        int resumeIndex = d->currentSegmentIndex;
        if (preserveForResume) {
            resumeIndex = qBound(0, resumeIndex >= 0 ? resumeIndex : 0, d->activeSegments.count() - 1);
        }

        d->destroyBackend();
        d->createBackend();

        if (preserveForResume) {
            d->currentSegmentIndex = resumeIndex;
            d->backendSegmentOffset = resumeIndex;
            d->pendingEngineResume = true;
            Q_EMIT currentSegmentChanged(resumeIndex);
            Q_EMIT isSpeaking(false);
            Q_EMIT canPauseOrResume(true);
            d->emitSpeechState(OkularTTS::SpeechState::Paused);
        }
        return;
    }

#if HAVE_OPENAI_TTS
    if (d->openai) {
        // Same engine, possibly new server parameters; they apply from the
        // next chunk request onwards without interrupting playback.
        d->openai->reloadSettings();
        d->openai->setSpeed(d->speed);
        return;
    }
#endif

    if (d->speech) {
        d->speech->setRate(Private::speedToQtRate(d->speed));
        const QString voiceName = Okular::Settings::ttsVoice();
        const QList<QVoice> voices = d->speech->availableVoices();
        for (const QVoice &voice : voices) {
            if (voice.name() == voiceName) {
                d->speech->setVoice(voice);
                break;
            }
        }
    }
}
