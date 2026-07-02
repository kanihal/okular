/*
    SPDX-FileCopyrightText: 2006 Pino Toscano <toscano.pino@tiscali.it>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "dlgaccessibility.h"

#include "settings.h"

#include <KColorButton>
#include <KLocalizedString>

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QSizePolicy>
#include <QStackedWidget>

#if HAVE_SPEECH
#include <QTextToSpeech>
#endif

#if HAVE_OPENAI_TTS
#include "tts.h"

#include <KColorScheme>

#include <QDoubleSpinBox>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QUrlQuery>
#endif

static void makeFormFieldsGrow(QFormLayout *layout)
{
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
}

static void keepCurrentTextVisible(QComboBox *combo, int minimumContentsLength)
{
    combo->setMinimumContentsLength(minimumContentsLength);
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    QObject::connect(combo, &QComboBox::currentTextChanged, combo, [combo](const QString &text) {
        combo->setToolTip(text);
        if (combo->lineEdit()) {
            combo->lineEdit()->setToolTip(text);
        }
    });
    combo->setToolTip(combo->currentText());
}

#if HAVE_OPENAI_TTS
enum class OpenAIStatusKind {
    Normal,
    Success,
    Error,
};

static void setOpenAIStatus(QLabel *label, const QString &text, OpenAIStatusKind kind)
{
    label->setStyleSheet(QString());
    if (kind != OpenAIStatusKind::Normal) {
        const QPalette palette = label->palette();
        const KColorScheme scheme(palette.currentColorGroup(), KColorScheme::Window);
        const QColor color = scheme.foreground(kind == OpenAIStatusKind::Success ? KColorScheme::PositiveText : KColorScheme::NegativeText).color();
        label->setStyleSheet(QStringLiteral("color: %1").arg(color.name()));
    }
    label->setText(text);
}
#endif

DlgAccessibility::DlgAccessibility(QWidget *parent)
    : QWidget(parent)
    , m_colorModeConfigStack(new QStackedWidget(this))
{
    QFormLayout *layout = new QFormLayout(this);
    makeFormFieldsGrow(layout);

    // BEGIN Checkboxes: draw border around images/links
    // ### not working yet, hide for now
    // QCheckBox *highlightImages = new QCheckBox(this);
    // highlightImages->setText(i18nc("@option:check Config dialog, accessibility page", "Draw border around images"));
    // highlightImages->setObjectName(QStringLiteral("kcfg_HighlightImages"));
    // layout->addRow(QString(), highlightImages);

    QCheckBox *highlightLinks = new QCheckBox(this);
    highlightLinks->setText(i18nc("@option:check Config dialog, accessibility page", "Draw border around links"));
    highlightLinks->setObjectName(QStringLiteral("kcfg_HighlightLinks"));
    layout->addRow(QString(), highlightLinks);
    // END Checkboxes: draw border around images/links

    layout->addRow(new QLabel(this));

    // BEGIN Change colors section
    // Checkbox: enable Change Colors feature
    QCheckBox *enableChangeColors = new QCheckBox(this);
    enableChangeColors->setText(i18nc("@option:check Config dialog, accessibility page", "Change colors"));
    enableChangeColors->setObjectName(QStringLiteral("kcfg_ChangeColors"));
    layout->addRow(QString(), enableChangeColors);

    // Label: Performance warning
    QLabel *warningLabel = new QLabel(this);
    warningLabel->setText(i18nc("@info Config dialog, accessibility page", "<b>Warning:</b> these options can badly affect drawing speed."));
    warningLabel->setWordWrap(true);
    layout->addRow(warningLabel);

    // Combobox: color modes
    QComboBox *colorMode = new QComboBox(this);
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Invert colors"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Change paper color"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Change dark & light colors"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Convert to black & white"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Invert lightness"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Invert luma (sRGB linear)"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Invert luma (symmetric)"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Shift hue positive"));
    colorMode->addItem(i18nc("@item:inlistbox Config dialog, accessibility page", "Shift hue negative"));
    colorMode->setObjectName(QStringLiteral("kcfg_RenderMode"));
    layout->addRow(i18nc("@label:listbox Config dialog, accessibility page", "Color mode:"), colorMode);

    m_colorModeConfigStack->setSizePolicy({QSizePolicy::Preferred, QSizePolicy::Fixed});

    // BEGIN Empty page (Only needed to hide the other pages, but it shouldn’t be huge...)
    QWidget *pageWidget = new QWidget(this);
    QFormLayout *pageLayout = new QFormLayout(pageWidget);
    m_colorModeConfigStack->addWidget(pageWidget);
    // END Empty page

    // BEGIN Change paper color page
    pageWidget = new QWidget(this);
    pageLayout = new QFormLayout(pageWidget);

    // Color button: paper color
    KColorButton *paperColor = new KColorButton(this);
    paperColor->setObjectName(QStringLiteral("kcfg_PaperColor"));
    pageLayout->addRow(i18nc("@label:chooser Config dialog, accessibility page", "Paper color:"), paperColor);

    m_colorModeConfigStack->addWidget(pageWidget);
    // END Change paper color page

    // BEGIN Change to dark & light colors page
    pageWidget = new QWidget(this);
    pageLayout = new QFormLayout(pageWidget);

    // Color button: dark color
    KColorButton *darkColor = new KColorButton(this);
    darkColor->setObjectName(QStringLiteral("kcfg_RecolorForeground"));
    pageLayout->addRow(i18nc("@label:chooser Config dialog, accessibility page", "Dark color:"), darkColor);

    // Color button: light color
    KColorButton *lightColor = new KColorButton(this);
    lightColor->setObjectName(QStringLiteral("kcfg_RecolorBackground"));
    pageLayout->addRow(i18nc("@label:chooser Config dialog, accessibility page", "Light color:"), lightColor);

    m_colorModeConfigStack->addWidget(pageWidget);
    // END Change to dark & light colors page

    // BEGIN Convert to black & white page
    pageWidget = new QWidget(this);
    pageLayout = new QFormLayout(pageWidget);

    // Slider: threshold
    QSlider *thresholdSlider = new QSlider(this);
    thresholdSlider->setMinimum(2);
    thresholdSlider->setMaximum(253);
    thresholdSlider->setOrientation(Qt::Horizontal);
    thresholdSlider->setObjectName(QStringLiteral("kcfg_BWThreshold"));
    pageLayout->addRow(i18nc("@label:slider Config dialog, accessibility page", "Threshold:"), thresholdSlider);

    // Slider: contrast
    QSlider *contrastSlider = new QSlider(this);
    contrastSlider->setMinimum(2);
    contrastSlider->setMaximum(6);
    contrastSlider->setOrientation(Qt::Horizontal);
    contrastSlider->setObjectName(QStringLiteral("kcfg_BWContrast"));
    pageLayout->addRow(i18nc("@label:slider Config dialog, accessibility page", "Contrast:"), contrastSlider);

    m_colorModeConfigStack->addWidget(pageWidget);
    // END Convert to black & white page

    layout->addRow(QString(), m_colorModeConfigStack);

    // Setup controls enabled states:
    colorMode->setCurrentIndex(0);
    slotColorModeSelected(0);
    connect(colorMode, qOverload<int>(&QComboBox::currentIndexChanged), this, &DlgAccessibility::slotColorModeSelected);

    enableChangeColors->setChecked(false);
    colorMode->setEnabled(false);
    connect(enableChangeColors, &QCheckBox::toggled, colorMode, &QComboBox::setEnabled);
    m_colorModeConfigStack->setEnabled(false);
    connect(enableChangeColors, &QCheckBox::toggled, m_colorModeConfigStack, &QWidget::setEnabled);
    // END Change colors section

#if HAVE_SPEECH
    layout->addRow(new QLabel(this));

    // BEGIN Text-to-speech section
    m_ttsEngineBox = new QComboBox(this);
    // Populate tts engines and use their names directly as key and item text:
    const QStringList engines = QTextToSpeech::availableEngines();
    for (const QString &engine : engines) {
        m_ttsEngineBox->addItem(engine);
    }
#if HAVE_OPENAI_TTS
    // Pseudo-engine implemented by Okular itself; the id doubles as display
    // text and stored setting value, hence untranslated (see tts.h).
    m_ttsEngineBox->addItem(OKULAR_OPENAI_TTS_ENGINE_ID);
#endif
    m_ttsEngineBox->setProperty("kcfg_property", QByteArray("currentText"));
    m_ttsEngineBox->setObjectName(QStringLiteral("kcfg_ttsEngine"));
    keepCurrentTextVisible(m_ttsEngineBox, 28);
    layout->addRow(i18nc("@label:listbox Config dialog, accessibility page", "Text-to-speech engine:"), m_ttsEngineBox);

    connect(m_ttsEngineBox, qOverload<int>(&QComboBox::currentIndexChanged), this, &DlgAccessibility::slotTTSEngineChanged);

    m_ttsConfigStack = new QStackedWidget(this);
    m_ttsConfigStack->setSizePolicy({QSizePolicy::Expanding, QSizePolicy::Fixed});

    // BEGIN Qt engine page: voice selection
    QWidget *ttsPageWidget = new QWidget(this);
    QFormLayout *ttsPageLayout = new QFormLayout(ttsPageWidget);
    ttsPageLayout->setContentsMargins(0, 0, 0, 0);
    makeFormFieldsGrow(ttsPageLayout);

    m_ttsVoiceBox = new QComboBox(this);
    m_ttsVoiceBox->setProperty("kcfg_property", QByteArray("currentText"));
    m_ttsVoiceBox->setObjectName(QStringLiteral("kcfg_ttsVoice"));
    keepCurrentTextVisible(m_ttsVoiceBox, 28);
    ttsPageLayout->addRow(i18nc("&label:listbox Config dialog, accessibility page", "Text-to-speech voice:"), m_ttsVoiceBox);

    m_ttsConfigStack->addWidget(ttsPageWidget);
    // END Qt engine page

#if HAVE_OPENAI_TTS
    // BEGIN OpenAI-compatible server page
    ttsPageWidget = new QWidget(this);
    ttsPageLayout = new QFormLayout(ttsPageWidget);
    ttsPageLayout->setContentsMargins(0, 0, 0, 0);
    makeFormFieldsGrow(ttsPageLayout);

    m_ttsOpenAIUrlEdit = new QLineEdit(this);
    m_ttsOpenAIUrlEdit->setPlaceholderText(QStringLiteral("http://127.0.0.1:9876"));
    m_ttsOpenAIUrlEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ttsOpenAIUrlEdit->setObjectName(QStringLiteral("kcfg_ttsOpenAIUrl"));
    ttsPageLayout->addRow(i18nc("@label:textbox Config dialog, accessibility page", "Server URL:"), m_ttsOpenAIUrlEdit);

    m_ttsOpenAIRefreshButton = new QPushButton(i18nc("@action:button Config dialog, accessibility page", "Fetch Models and Voices"), this);
    connect(m_ttsOpenAIRefreshButton, &QPushButton::clicked, this, &DlgAccessibility::slotRefreshOpenAIServerData);

    m_ttsOpenAITestButton = new QPushButton(i18nc("@action:button Config dialog, accessibility page", "Test Connection"), this);
    connect(m_ttsOpenAITestButton, &QPushButton::clicked, this, &DlgAccessibility::slotTestOpenAIConnection);

    auto *openAIFetchRow = new QWidget(this);
    auto *openAIFetchLayout = new QHBoxLayout(openAIFetchRow);
    openAIFetchLayout->setContentsMargins(0, 0, 0, 0);
    openAIFetchLayout->addWidget(m_ttsOpenAIRefreshButton);
    m_ttsOpenAIFetchStatusLabel = new QLabel(this);
    openAIFetchLayout->addWidget(m_ttsOpenAIFetchStatusLabel);
    openAIFetchLayout->addStretch(1);
    ttsPageLayout->addRow(QString(), openAIFetchRow);

    m_ttsOpenAIModelBox = new QComboBox(this);
    m_ttsOpenAIModelBox->setEditable(true);
    m_ttsOpenAIModelBox->lineEdit()->setPlaceholderText(QStringLiteral("mlx-community/Kokoro-82M-bf16"));
    keepCurrentTextVisible(m_ttsOpenAIModelBox, 36);
    m_ttsOpenAIModelBox->setProperty("kcfg_property", QByteArray("currentText"));
    m_ttsOpenAIModelBox->setObjectName(QStringLiteral("kcfg_ttsOpenAIModel"));
    ttsPageLayout->addRow(i18nc("@label:listbox Config dialog, accessibility page", "Model:"), m_ttsOpenAIModelBox);

    m_ttsOpenAIVoiceBox = new QComboBox(this);
    m_ttsOpenAIVoiceBox->setEditable(true);
    m_ttsOpenAIVoiceBox->lineEdit()->setPlaceholderText(QStringLiteral("af_heart"));
    keepCurrentTextVisible(m_ttsOpenAIVoiceBox, 20);
    m_ttsOpenAIVoiceBox->setProperty("kcfg_property", QByteArray("currentText"));
    m_ttsOpenAIVoiceBox->setObjectName(QStringLiteral("kcfg_ttsOpenAIVoice"));
    ttsPageLayout->addRow(i18nc("@label:listbox Config dialog, accessibility page", "Voice:"), m_ttsOpenAIVoiceBox);

    m_ttsOpenAISpeedSpinBox = new QDoubleSpinBox(this);
    m_ttsOpenAISpeedSpinBox->setRange(0.25, 4.0);
    m_ttsOpenAISpeedSpinBox->setSingleStep(0.1);
    m_ttsOpenAISpeedSpinBox->setDecimals(2);
    m_ttsOpenAISpeedSpinBox->setObjectName(QStringLiteral("kcfg_ttsOpenAISpeed"));
    ttsPageLayout->addRow(i18nc("@label:spinbox Config dialog, accessibility page", "Speed:"), m_ttsOpenAISpeedSpinBox);

    m_ttsOpenAIApiKeyEdit = new QLineEdit(this);
    m_ttsOpenAIApiKeyEdit->setEchoMode(QLineEdit::Password);
    m_ttsOpenAIApiKeyEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_ttsOpenAIApiKeyEdit->setObjectName(QStringLiteral("kcfg_ttsOpenAIApiKey"));
    ttsPageLayout->addRow(i18nc("@label:textbox Config dialog, accessibility page", "API key:"), m_ttsOpenAIApiKeyEdit);

    auto *openAITestRow = new QWidget(this);
    auto *openAITestLayout = new QHBoxLayout(openAITestRow);
    openAITestLayout->setContentsMargins(0, 0, 0, 0);
    openAITestLayout->addWidget(m_ttsOpenAITestButton);
    m_ttsOpenAITestStatusLabel = new QLabel(this);
    openAITestLayout->addWidget(m_ttsOpenAITestStatusLabel);
    openAITestLayout->addStretch(1);
    ttsPageLayout->addRow(QString(), openAITestRow);

    m_ttsConfigStack->addWidget(ttsPageWidget);
    // END OpenAI-compatible server page
#endif

    layout->addRow(m_ttsConfigStack);

    slotTTSEngineChanged();
    // END Text-to-speech section
#endif
}

#if HAVE_SPEECH
void DlgAccessibility::slotTTSEngineChanged()
{
    QString engine = m_ttsEngineBox->currentText();
#if HAVE_OPENAI_TTS
    if (engine == OKULAR_OPENAI_TTS_ENGINE_ID) {
        // Not a QTextToSpeech engine; show the server configuration page.
        m_ttsConfigStack->setCurrentIndex(1);
        return;
    }
#endif
    m_ttsConfigStack->setCurrentIndex(0);
    QTextToSpeech *ttsEngine = new QTextToSpeech(engine);
    const QList<QVoice> voices = ttsEngine->availableVoices();
    m_ttsVoiceBox->clear();
    for (const QVoice &voice : voices) {
        m_ttsVoiceBox->addItem(voice.name());
    }
    delete ttsEngine;
}
#endif

#if HAVE_OPENAI_TTS
static QString normalizedServerBase(QString base)
{
    base = base.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    // Accept both "http://host:port" and "http://host:port/v1".
    if (base.endsWith(QLatin1String("/v1"))) {
        base.chop(3);
    }
    return base;
}

void DlgAccessibility::slotRefreshOpenAIServerData()
{
    const QString base = normalizedServerBase(m_ttsOpenAIUrlEdit->text());
    if (base.isEmpty()) {
        setOpenAIStatus(m_ttsOpenAIFetchStatusLabel, i18nc("@info Config dialog, accessibility page", "Enter a server URL first."), OpenAIStatusKind::Error);
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }
    const QByteArray apiKey = m_ttsOpenAIApiKeyEdit->text().toUtf8();

    m_ttsOpenAIRefreshButton->setEnabled(false);
    setOpenAIStatus(m_ttsOpenAIFetchStatusLabel, i18nc("@info Config dialog, accessibility page", "Fetching models and voices…"), OpenAIStatusKind::Normal);

    QNetworkRequest modelsRequest(QUrl(base + QStringLiteral("/v1/models")));
    modelsRequest.setTransferTimeout(10000);
    if (!apiKey.isEmpty()) {
        modelsRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey);
    }
    QNetworkReply *modelsReply = m_nam->get(modelsRequest);
    connect(modelsReply, &QNetworkReply::finished, this, [this, modelsReply, base, apiKey]() {
        modelsReply->deleteLater();
        if (modelsReply->error() == QNetworkReply::NoError) {
            const QJsonArray data = QJsonDocument::fromJson(modelsReply->readAll()).object().value(QStringLiteral("data")).toArray();
            QStringList models;
            for (const QJsonValue &value : data) {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) {
                    models.append(id);
                }
            }
            if (!models.isEmpty()) {
                const QString current = m_ttsOpenAIModelBox->currentText();
                m_ttsOpenAIModelBox->clear();
                m_ttsOpenAIModelBox->addItems(models);
                m_ttsOpenAIModelBox->setCurrentText(current.isEmpty() ? models.constFirst() : current);
            }
        } else {
            m_ttsOpenAIRefreshButton->setEnabled(true);
            setOpenAIStatus(m_ttsOpenAIFetchStatusLabel,
                            i18nc("@info Config dialog, accessibility page, %1 is an error message", "Could not fetch models: %1", modelsReply->errorString()),
                            OpenAIStatusKind::Error);
            return;
        }

        // Voice listing is an extension some OpenAI-compatible servers
        // (e.g. mlx-audio) provide; failures leave the combo box untouched
        // and manual entry always works.
        QUrl voicesUrl(base + QStringLiteral("/v1/audio/voices"));
        QUrlQuery voicesQuery;
        voicesQuery.addQueryItem(QStringLiteral("model"), m_ttsOpenAIModelBox->currentText());
        voicesUrl.setQuery(voicesQuery);
        QNetworkRequest voicesRequest(voicesUrl);
        voicesRequest.setTransferTimeout(10000);
        if (!apiKey.isEmpty()) {
            voicesRequest.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey);
        }
        QNetworkReply *voicesReply = m_nam->get(voicesRequest);
        connect(voicesReply, &QNetworkReply::finished, this, [this, voicesReply]() {
            voicesReply->deleteLater();
            m_ttsOpenAIRefreshButton->setEnabled(true);
            if (voicesReply->error() != QNetworkReply::NoError) {
                setOpenAIStatus(m_ttsOpenAIFetchStatusLabel,
                                i18nc("@info Config dialog, accessibility page, %1 is an error message",
                                      "Fetched models. Could not fetch voices: %1",
                                      voicesReply->errorString()),
                                OpenAIStatusKind::Error);
                return;
            }
            const QJsonArray data = QJsonDocument::fromJson(voicesReply->readAll()).object().value(QStringLiteral("data")).toArray();
            QStringList voiceIds;
            for (const QJsonValue &value : data) {
                const QString id = value.toObject().value(QStringLiteral("id")).toString();
                if (!id.isEmpty()) {
                    voiceIds.append(id);
                }
            }
            if (!voiceIds.isEmpty()) {
                const QString current = m_ttsOpenAIVoiceBox->currentText();
                m_ttsOpenAIVoiceBox->clear();
                m_ttsOpenAIVoiceBox->addItems(voiceIds);
                m_ttsOpenAIVoiceBox->setCurrentText(current.isEmpty() ? voiceIds.constFirst() : current);
            }
            setOpenAIStatus(m_ttsOpenAIFetchStatusLabel, i18nc("@info Config dialog, accessibility page", "Models and voices updated."), OpenAIStatusKind::Success);
        });
    });
}

void DlgAccessibility::slotTestOpenAIConnection()
{
    const QString base = normalizedServerBase(m_ttsOpenAIUrlEdit->text());
    if (base.isEmpty()) {
        setOpenAIStatus(m_ttsOpenAITestStatusLabel, i18nc("@info Config dialog, accessibility page", "Enter a server URL first."), OpenAIStatusKind::Error);
        return;
    }
    if (m_ttsOpenAIModelBox->currentText().trimmed().isEmpty()) {
        setOpenAIStatus(m_ttsOpenAITestStatusLabel, i18nc("@info Config dialog, accessibility page", "Enter a model first."), OpenAIStatusKind::Error);
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
    }

    m_ttsOpenAITestButton->setEnabled(false);
    setOpenAIStatus(m_ttsOpenAITestStatusLabel, i18nc("@info Config dialog, accessibility page", "Testing connection…"), OpenAIStatusKind::Normal);

    QNetworkRequest request(QUrl(base + QStringLiteral("/v1/audio/speech")));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setTransferTimeout(15000);

    const QByteArray apiKey = m_ttsOpenAIApiKeyEdit->text().toUtf8();
    if (!apiKey.isEmpty()) {
        request.setRawHeader("Authorization", QByteArrayLiteral("Bearer ") + apiKey);
    }

    QJsonObject body;
    body[QStringLiteral("model")] = m_ttsOpenAIModelBox->currentText().trimmed();
    body[QStringLiteral("input")] = i18nc("@info sample text sent to a text-to-speech server", "Okular text-to-speech test.");
    body[QStringLiteral("voice")] = m_ttsOpenAIVoiceBox->currentText().trimmed();
    body[QStringLiteral("response_format")] = QStringLiteral("wav");
    body[QStringLiteral("speed")] = qBound(0.25, m_ttsOpenAISpeedSpinBox->value(), 4.0);

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        m_ttsOpenAITestButton->setEnabled(true);

        const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || httpStatus >= 400) {
            QString detail = reply->errorString();
            const QString bodyExcerpt = QString::fromUtf8(reply->readAll().left(200)).trimmed();
            if (httpStatus >= 400) {
                detail = i18nc("@info Config dialog, accessibility page, %1 is a HTTP status code, %2 is server response text",
                               "server returned status %1 (%2)",
                               httpStatus,
                               bodyExcerpt);
            }
            setOpenAIStatus(m_ttsOpenAITestStatusLabel,
                            i18nc("@info Config dialog, accessibility page, %1 is an error message", "Connection test failed: %1", detail),
                            OpenAIStatusKind::Error);
            return;
        }

        const QByteArray audio = reply->readAll();
        if (audio.isEmpty()) {
            setOpenAIStatus(m_ttsOpenAITestStatusLabel,
                            i18nc("@info Config dialog, accessibility page", "Connection test failed: the server returned no audio data."),
                            OpenAIStatusKind::Error);
            return;
        }

        setOpenAIStatus(m_ttsOpenAITestStatusLabel, i18nc("@info Config dialog, accessibility page", "Connection test succeeded."), OpenAIStatusKind::Success);
    });
}
#endif

void DlgAccessibility::slotColorModeSelected(int mode)
{
    if (mode == Okular::Settings::EnumRenderMode::Paper) {
        m_colorModeConfigStack->setCurrentIndex(1);
    } else if (mode == Okular::Settings::EnumRenderMode::Recolor) {
        m_colorModeConfigStack->setCurrentIndex(2);
    } else if (mode == Okular::Settings::EnumRenderMode::BlackWhite) {
        m_colorModeConfigStack->setCurrentIndex(3);
    } else {
        m_colorModeConfigStack->setCurrentIndex(0);
    }
}
