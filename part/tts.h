/*
    SPDX-FileCopyrightText: 2008 Pino Toscano <pino@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _TTS_H_
#define _TTS_H_

#include <QObject>
#include <QStringList>
#include <QTextToSpeech>

#include "config-okular.h"

#if HAVE_OPENAI_TTS
// Engine id stored in the ttsEngine setting to select the OpenAI-compatible
// HTTP backend instead of a QTextToSpeech engine. Deliberately NOT translated:
// the config dialog stores the engine combo box text verbatim in the config
// file, so a translated id would change with the locale.
inline const QLatin1StringView OKULAR_OPENAI_TTS_ENGINE_ID("OpenAI-Compatible Server");
#endif

class OkularTTS : public QObject
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

    explicit OkularTTS(QObject *parent = nullptr);
    ~OkularTTS() override;

    void say(const QString &text);
    void saySegments(const QStringList &segments);
    void stopAllSpeechs();
    void pauseResumeSpeech();
    void setSpeed(double speed);
    double speed() const;

public Q_SLOTS:
    void slotSpeechStateChanged(QTextToSpeech::State state);
    void slotConfigChanged();

Q_SIGNALS:
    void isSpeaking(bool speaking);
    void canPauseOrResume(bool speakingOrPaused);
    void speechStateChanged(OkularTTS::SpeechState state);
    void currentSegmentChanged(int index);
    void errorOccurred(const QString &message);

private:
    // private storage
    class Private;
    Private *d;
};

#endif
