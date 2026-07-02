/*
    SPDX-FileCopyrightText: 2006 Pino Toscano <toscano.pino@tiscali.it>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef _DLGACCESSIBILITY_H
#define _DLGACCESSIBILITY_H

#include "config-okular.h"
#include <QWidget>

class QComboBox;
class QLabel;
class QStackedWidget;
#if HAVE_OPENAI_TTS
class QDoubleSpinBox;
class QLineEdit;
class QNetworkAccessManager;
class QPushButton;
#endif

class DlgAccessibility : public QWidget
{
    Q_OBJECT

public:
    explicit DlgAccessibility(QWidget *parent = nullptr);

protected Q_SLOTS:
    void slotColorModeSelected(int mode);
#if HAVE_SPEECH
    void slotTTSEngineChanged();
#endif
#if HAVE_OPENAI_TTS
    void slotRefreshOpenAIServerData();
    void slotTestOpenAIConnection();
#endif

protected:
    QStackedWidget *m_colorModeConfigStack;
#if HAVE_SPEECH
    QComboBox *m_ttsEngineBox;
    QComboBox *m_ttsVoiceBox;
    QStackedWidget *m_ttsConfigStack = nullptr;
#endif
#if HAVE_OPENAI_TTS
    QLineEdit *m_ttsOpenAIUrlEdit = nullptr;
    QLineEdit *m_ttsOpenAIApiKeyEdit = nullptr;
    QComboBox *m_ttsOpenAIModelBox = nullptr;
    QComboBox *m_ttsOpenAIVoiceBox = nullptr;
    QDoubleSpinBox *m_ttsOpenAISpeedSpinBox = nullptr;
    QPushButton *m_ttsOpenAIRefreshButton = nullptr;
    QPushButton *m_ttsOpenAITestButton = nullptr;
    QLabel *m_ttsOpenAIFetchStatusLabel = nullptr;
    QLabel *m_ttsOpenAITestStatusLabel = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
#endif
};

#endif
