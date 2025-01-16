// Copyright (C) 2016 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "cppfilesettingspage.h"

#include "cppeditortr.h"
#include "cppheadersource.h"

#include <extensionsystem/iplugin.h>

#include <coreplugin/icore.h>
#include <coreplugin/editormanager/editormanager.h>

#include <projectexplorer/project.h>
#include <projectexplorer/projectpanelfactory.h>

#include <utils/aspects.h>
#include <utils/fileutils.h>
#include <utils/hostosinfo.h>
#include <utils/layoutbuilder.h>
#include <utils/macroexpander.h>
#include <utils/mimeconstants.h>
#include <utils/mimeutils.h>
#include <utils/pathchooser.h>
#include <utils/qtcsettings.h>

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QGuiApplication>
#include <QLineEdit>
#include <QLocale>
#include <QTextStream>
#include <QVBoxLayout>

#ifdef WITH_TESTS
#include <QtTest>
#endif

using namespace ProjectExplorer;
using namespace Utils;

namespace CppEditor::Internal {
namespace {
class HeaderGuardExpander : public MacroExpander
{
public:
    HeaderGuardExpander(const FilePath &filePath) : m_filePath(filePath)
    {
        setDisplayName(Tr::tr("Header File Variables"));
        registerFileVariables("Header", Tr::tr("Header file"), [this] {
            return m_filePath;
        });
    }

private:
    const FilePath m_filePath;
};
} // namespace

const char projectSettingsKeyC[] = "CppEditorFileNames";
const char useGlobalKeyC[] = "UseGlobal";
const char headerPrefixesKeyC[] = "HeaderPrefixes";
const char sourcePrefixesKeyC[] = "SourcePrefixes";
const char headerSuffixKeyC[] = "HeaderSuffix";
const char sourceSuffixKeyC[] = "SourceSuffix";
const char headerSearchPathsKeyC[] = "HeaderSearchPaths";
const char sourceSearchPathsKeyC[] = "SourceSearchPaths";
const char headerPragmaOnceC[] = "HeaderPragmaOnce";
const char licenseTemplatePathKeyC[] = "LicenseTemplate";
const char headerGuardTemplateKeyC[] = "HeaderGuardTemplate";

const char *licenseTemplateTemplate = QT_TRANSLATE_NOOP("QtC::CppEditor",
"/**************************************************************************\n"
"** %1 license header template\n"
"**   Special keywords: %USER% %DATE% %YEAR%\n"
"**   Environment variables: %$VARIABLE%\n"
"**   To protect a percent sign, use '%%'.\n"
"**************************************************************************/\n");

void CppFileSettings::toSettings(QtcSettings *s) const
{
    const CppFileSettings def;
    s->beginGroup(Constants::CPPEDITOR_SETTINGSGROUP);
    s->setValueWithDefault(headerPrefixesKeyC, headerPrefixes, def.headerPrefixes);
    s->setValueWithDefault(sourcePrefixesKeyC, sourcePrefixes, def.sourcePrefixes);
    s->setValueWithDefault(headerSuffixKeyC, headerSuffix, def.headerSuffix);
    s->setValueWithDefault(sourceSuffixKeyC, sourceSuffix, def.sourceSuffix);
    s->setValueWithDefault(headerSearchPathsKeyC, headerSearchPaths, def.headerSearchPaths);
    s->setValueWithDefault(sourceSearchPathsKeyC, sourceSearchPaths, def.sourceSearchPaths);
    s->setValueWithDefault(Constants::LOWERCASE_CPPFILES_KEY, lowerCaseFiles, def.lowerCaseFiles);
    s->setValueWithDefault(headerPragmaOnceC, headerPragmaOnce, def.headerPragmaOnce);
    s->setValueWithDefault(licenseTemplatePathKeyC, licenseTemplatePath.toSettings(),
                           def.licenseTemplatePath.toSettings());
    s->setValueWithDefault(headerGuardTemplateKeyC, headerGuardTemplate, def.headerGuardTemplate);
    s->endGroup();
}

void CppFileSettings::fromSettings(QtcSettings *s)
{
    const CppFileSettings def;
    s->beginGroup(Constants::CPPEDITOR_SETTINGSGROUP);
    headerPrefixes = s->value(headerPrefixesKeyC, def.headerPrefixes).toStringList();
    sourcePrefixes = s->value(sourcePrefixesKeyC, def.sourcePrefixes).toStringList();
    headerSuffix = s->value(headerSuffixKeyC, def.headerSuffix).toString();
    sourceSuffix = s->value(sourceSuffixKeyC, def.sourceSuffix).toString();
    headerSearchPaths = s->value(headerSearchPathsKeyC, def.headerSearchPaths).toStringList();
    sourceSearchPaths = s->value(sourceSearchPathsKeyC, def.sourceSearchPaths).toStringList();
    lowerCaseFiles = s->value(Constants::LOWERCASE_CPPFILES_KEY, def.lowerCaseFiles).toBool();
    headerPragmaOnce = s->value(headerPragmaOnceC, def.headerPragmaOnce).toBool();
    licenseTemplatePath = FilePath::fromSettings(s->value(licenseTemplatePathKeyC,
                                                          def.licenseTemplatePath.toSettings()));
    headerGuardTemplate = s->value(headerGuardTemplateKeyC, def.headerGuardTemplate).toString();
    s->endGroup();
}

static bool applySuffixes(const QString &sourceSuffix, const QString &headerSuffix)
{
    Utils::MimeType mt;
    mt = Utils::mimeTypeForName(QLatin1String(Utils::Constants::CPP_SOURCE_MIMETYPE));
    if (!mt.isValid())
        return false;
    mt.setPreferredSuffix(sourceSuffix);
    mt = Utils::mimeTypeForName(QLatin1String(Utils::Constants::CPP_HEADER_MIMETYPE));
    if (!mt.isValid())
        return false;
    mt.setPreferredSuffix(headerSuffix);
    return true;
}

void CppFileSettings::addMimeInitializer() const
{
    Utils::addMimeInitializer([sourceSuffix = sourceSuffix, headerSuffix = headerSuffix] {
        if (!applySuffixes(sourceSuffix, headerSuffix))
            qWarning("Unable to apply cpp suffixes to mime database (cpp mime types not found).\n");
    });
}

bool CppFileSettings::applySuffixesToMimeDB()
{
    return applySuffixes(sourceSuffix, headerSuffix);
}

bool CppFileSettings::equals(const CppFileSettings &rhs) const
{
    return lowerCaseFiles == rhs.lowerCaseFiles
           && headerPragmaOnce == rhs.headerPragmaOnce
           && headerPrefixes == rhs.headerPrefixes
           && sourcePrefixes == rhs.sourcePrefixes
           && headerSuffix == rhs.headerSuffix
           && sourceSuffix == rhs.sourceSuffix
           && headerSearchPaths == rhs.headerSearchPaths
           && sourceSearchPaths == rhs.sourceSearchPaths
           && headerGuardTemplate == rhs.headerGuardTemplate
           && licenseTemplatePath == rhs.licenseTemplatePath;
}

// Replacements of special license template keywords.
static bool keyWordReplacement(const QString &keyWord,
                               QString *value)
{
    if (keyWord == QLatin1String("%YEAR%")) {
        *value = QLatin1String("%{CurrentDate:yyyy}");
        return true;
    }
    if (keyWord == QLatin1String("%MONTH%")) {
        *value = QLatin1String("%{CurrentDate:M}");
        return true;
    }
    if (keyWord == QLatin1String("%DAY%")) {
        *value = QLatin1String("%{CurrentDate:d}");
        return true;
    }
    if (keyWord == QLatin1String("%CLASS%")) {
        *value = QLatin1String("%{Cpp:License:ClassName}");
        return true;
    }
    if (keyWord == QLatin1String("%FILENAME%")) {
        *value = QLatin1String("%{Cpp:License:FileName}");
        return true;
    }
    if (keyWord == QLatin1String("%DATE%")) {
        static QString format;
        // ensure a format with 4 year digits. Some have locales have 2.
        if (format.isEmpty()) {
            QLocale loc;
            format = loc.dateFormat(QLocale::ShortFormat);
            const QChar ypsilon = QLatin1Char('y');
            if (format.count(ypsilon) == 2)
                format.insert(format.indexOf(ypsilon), QString(2, ypsilon));
            format.replace('/', "\\/");
        }
        *value = QString::fromLatin1("%{CurrentDate:") + format + QLatin1Char('}');
        return true;
    }
    if (keyWord == QLatin1String("%USER%")) {
        *value = Utils::HostOsInfo::isWindowsHost() ? QLatin1String("%{Env:USERNAME}")
                                                    : QLatin1String("%{Env:USER}");
        return true;
    }

    // Environment variables (for example '%$EMAIL%').
    if (keyWord.startsWith(QLatin1String("%$"))) {
        const QString varName = keyWord.mid(2, keyWord.size() - 3);
        *value = QString::fromLatin1("%{Env:") + varName + QLatin1Char('}');
        return true;
    }
    return false;
}

// Parse a license template, scan for %KEYWORD% and replace if known.
// Replace '%%' by '%'.
static void parseLicenseTemplatePlaceholders(QString *t)
{
    int pos = 0;
    const QChar placeHolder = QLatin1Char('%');
    do {
        const int placeHolderPos = t->indexOf(placeHolder, pos);
        if (placeHolderPos == -1)
            break;
        if (t->at(placeHolderPos + 1) == QLatin1Char('{')) {
            pos = placeHolderPos + 1;
            continue;
        }
        const int endPlaceHolderPos = t->indexOf(placeHolder, placeHolderPos + 1);
        if (endPlaceHolderPos == -1)
            break;
        if (endPlaceHolderPos == placeHolderPos + 1) { // '%%' -> '%'
            t->remove(placeHolderPos, 1);
            pos = placeHolderPos + 1;
        } else {
            const QString keyWord = t->mid(placeHolderPos, endPlaceHolderPos + 1 - placeHolderPos);
            QString replacement;
            if (keyWordReplacement(keyWord, &replacement)) {
                t->replace(placeHolderPos, keyWord.size(), replacement);
                pos = placeHolderPos + replacement.size();
            } else {
                // Leave invalid keywords as is.
                pos = endPlaceHolderPos + 1;
            }
        }
    } while (pos < t->size());

}

// Convenience that returns the formatted license template.
QString CppFileSettings::licenseTemplate() const
{
    if (licenseTemplatePath.isEmpty())
        return QString();
    QFile file(licenseTemplatePath.toFSPathString());
    if (!file.open(QIODevice::ReadOnly|QIODevice::Text)) {
        qWarning("Unable to open the license template %s: %s",
                 qPrintable(licenseTemplatePath.toUserOutput()),
                 qPrintable(file.errorString()));
        return QString();
    }

    QTextStream licenseStream(&file);
    licenseStream.setAutoDetectUnicode(true);
    QString license = licenseStream.readAll();

    parseLicenseTemplatePlaceholders(&license);

    // Ensure at least one newline at the end of the license template to separate it from the code
    const QChar newLine = QLatin1Char('\n');
    if (!license.endsWith(newLine))
        license += newLine;

    return license;
}

QString CppFileSettings::headerGuard(const Utils::FilePath &headerFilePath) const
{
    return HeaderGuardExpander(headerFilePath).expand(headerGuardTemplate);
}

// ------------------ CppFileSettingsWidget

class CppFileSettingsWidget final : public Core::IOptionsPageWidget
{
    Q_OBJECT

public:
    explicit CppFileSettingsWidget(CppFileSettings *settings);

    void apply() final;
    void setSettings(const CppFileSettings &s);
    CppFileSettings currentSettings() const;

signals:
    void userChange();

private:
    void slotEdit();
    FilePath licenseTemplatePath() const;
    void setLicenseTemplatePath(const FilePath &);

    CppFileSettings *m_settings = nullptr;

    QComboBox *m_headerSuffixComboBox = nullptr;
    QLineEdit *m_headerSearchPathsEdit = nullptr;
    QLineEdit *m_headerPrefixesEdit = nullptr;
    QCheckBox *m_headerPragmaOnceCheckBox = nullptr;
    QComboBox *m_sourceSuffixComboBox = nullptr;
    QLineEdit *m_sourceSearchPathsEdit = nullptr;
    QLineEdit *m_sourcePrefixesEdit = nullptr;
    QCheckBox *m_lowerCaseFileNamesCheckBox = nullptr;
    PathChooser *m_licenseTemplatePathChooser = nullptr;
    StringAspect m_headerGuardAspect;
    HeaderGuardExpander m_headerGuardExpander{{}};
};

CppFileSettingsWidget::CppFileSettingsWidget(CppFileSettings *settings)
    : m_settings(settings)
    , m_headerSuffixComboBox(new QComboBox)
    , m_headerSearchPathsEdit(new QLineEdit)
    , m_headerPrefixesEdit(new QLineEdit)
    , m_headerPragmaOnceCheckBox(new QCheckBox(Tr::tr("Use \"#pragma once\" instead")))
    , m_sourceSuffixComboBox(new QComboBox)
    , m_sourceSearchPathsEdit(new QLineEdit)
    , m_sourcePrefixesEdit(new QLineEdit)
    , m_lowerCaseFileNamesCheckBox(new QCheckBox(Tr::tr("&Lower case file names")))
    , m_licenseTemplatePathChooser(new PathChooser)
{
    m_headerSearchPathsEdit->setToolTip(Tr::tr("Comma-separated list of header paths.\n"
        "\n"
        "Paths can be absolute or relative to the directory of the current open document.\n"
        "\n"
        "These paths are used in addition to current directory on Switch Header/Source."));
    m_headerPrefixesEdit->setToolTip(Tr::tr("Comma-separated list of header prefixes.\n"
        "\n"
        "These prefixes are used in addition to current file name on Switch Header/Source."));
    m_headerPragmaOnceCheckBox->setToolTip(
        Tr::tr("Uses \"#pragma once\" instead of \"#ifndef\" include guards."));
    m_sourceSearchPathsEdit->setToolTip(Tr::tr("Comma-separated list of source paths.\n"
        "\n"
        "Paths can be absolute or relative to the directory of the current open document.\n"
        "\n"
        "These paths are used in addition to current directory on Switch Header/Source."));
    m_sourcePrefixesEdit->setToolTip(Tr::tr("Comma-separated list of source prefixes.\n"
        "\n"
        "These prefixes are used in addition to current file name on Switch Header/Source."));
    m_headerGuardAspect.setDisplayStyle(Utils::StringAspect::LineEditDisplay);
    m_headerGuardAspect.setMacroExpander(&m_headerGuardExpander);

    using namespace Layouting;

    Column {
        Group {
            title(Tr::tr("Headers")),
            Form {
                Tr::tr("&Suffix:"), m_headerSuffixComboBox, st, br,
                Tr::tr("S&earch paths:"), m_headerSearchPathsEdit, br,
                Tr::tr("&Prefixes:"), m_headerPrefixesEdit, br,
                Tr::tr("Include guard template:"), m_headerPragmaOnceCheckBox, m_headerGuardAspect
            },
        },
        Group {
            title(Tr::tr("Sources")),
            Form {
                Tr::tr("S&uffix:"), m_sourceSuffixComboBox, st, br,
                Tr::tr("Se&arch paths:"), m_sourceSearchPathsEdit, br,
                Tr::tr("P&refixes:"), m_sourcePrefixesEdit
            }
        },
        m_lowerCaseFileNamesCheckBox,
        Form {
            Tr::tr("License &template:"), m_licenseTemplatePathChooser
        },
        st
    }.attachTo(this);

    // populate suffix combos
    const MimeType sourceMt = Utils::mimeTypeForName(Utils::Constants::CPP_SOURCE_MIMETYPE);
    if (sourceMt.isValid()) {
        const QStringList suffixes = sourceMt.suffixes();
        for (const QString &suffix : suffixes)
            m_sourceSuffixComboBox->addItem(suffix);
    }

    const MimeType headerMt = Utils::mimeTypeForName(Utils::Constants::CPP_HEADER_MIMETYPE);
    if (headerMt.isValid()) {
        const QStringList suffixes = headerMt.suffixes();
        for (const QString &suffix : suffixes)
            m_headerSuffixComboBox->addItem(suffix);
    }
    m_licenseTemplatePathChooser->setExpectedKind(PathChooser::File);
    m_licenseTemplatePathChooser->setHistoryCompleter("Cpp.LicenseTemplate.History");
    m_licenseTemplatePathChooser->addButton(Tr::tr("Edit..."), this, [this] { slotEdit(); });

    setSettings(*m_settings);

    connect(m_headerSuffixComboBox, &QComboBox::currentIndexChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourceSuffixComboBox, &QComboBox::currentIndexChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_headerSearchPathsEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourceSearchPathsEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_headerPrefixesEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_sourcePrefixesEdit, &QLineEdit::textEdited,
            this, &CppFileSettingsWidget::userChange);
    connect(m_lowerCaseFileNamesCheckBox, &QCheckBox::stateChanged,
            this, &CppFileSettingsWidget::userChange);
    connect(m_licenseTemplatePathChooser, &PathChooser::textChanged,
            this, &CppFileSettingsWidget::userChange);
    const auto updateHeaderGuardAspectState = [this] {
        m_headerGuardAspect.setEnabled(!m_headerPragmaOnceCheckBox->isChecked());
    };
    connect(m_headerPragmaOnceCheckBox, &QCheckBox::stateChanged,
            this, [this, updateHeaderGuardAspectState] {
        updateHeaderGuardAspectState();
        emit userChange();
    });
    connect(&m_headerGuardAspect, &StringAspect::changed,
            this, &CppFileSettingsWidget::userChange);
    updateHeaderGuardAspectState();
}

FilePath CppFileSettingsWidget::licenseTemplatePath() const
{
    return m_licenseTemplatePathChooser->filePath();
}

void CppFileSettingsWidget::setLicenseTemplatePath(const FilePath &lp)
{
    m_licenseTemplatePathChooser->setFilePath(lp);
}

static QStringList trimmedPaths(const QString &paths)
{
    QStringList res;
    for (const QString &path : paths.split(QLatin1Char(','), Qt::SkipEmptyParts))
        res << path.trimmed();
    return res;
}

void CppFileSettingsWidget::apply()
{
    const CppFileSettings rc = currentSettings();
    if (rc == *m_settings)
        return;

    *m_settings = rc;
    m_settings->toSettings(Core::ICore::settings());
    m_settings->applySuffixesToMimeDB();
    clearHeaderSourceCache();
}

static inline void setComboText(QComboBox *cb, const QString &text, int defaultIndex = 0)
{
    const int index = cb->findText(text);
    cb->setCurrentIndex(index == -1 ? defaultIndex: index);
}

void CppFileSettingsWidget::setSettings(const CppFileSettings &s)
{
    const QChar comma = QLatin1Char(',');
    m_lowerCaseFileNamesCheckBox->setChecked(s.lowerCaseFiles);
    m_headerPragmaOnceCheckBox->setChecked(s.headerPragmaOnce);
    m_headerPrefixesEdit->setText(s.headerPrefixes.join(comma));
    m_sourcePrefixesEdit->setText(s.sourcePrefixes.join(comma));
    setComboText(m_headerSuffixComboBox, s.headerSuffix);
    setComboText(m_sourceSuffixComboBox, s.sourceSuffix);
    m_headerSearchPathsEdit->setText(s.headerSearchPaths.join(comma));
    m_sourceSearchPathsEdit->setText(s.sourceSearchPaths.join(comma));
    setLicenseTemplatePath(s.licenseTemplatePath);
    m_headerGuardAspect.setValue(s.headerGuardTemplate);
}

CppFileSettings CppFileSettingsWidget::currentSettings() const
{
    CppFileSettings rc;
    rc.lowerCaseFiles = m_lowerCaseFileNamesCheckBox->isChecked();
    rc.headerPragmaOnce = m_headerPragmaOnceCheckBox->isChecked();
    rc.headerPrefixes = trimmedPaths(m_headerPrefixesEdit->text());
    rc.sourcePrefixes = trimmedPaths(m_sourcePrefixesEdit->text());
    rc.headerSuffix = m_headerSuffixComboBox->currentText();
    rc.sourceSuffix = m_sourceSuffixComboBox->currentText();
    rc.headerSearchPaths = trimmedPaths(m_headerSearchPathsEdit->text());
    rc.sourceSearchPaths = trimmedPaths(m_sourceSearchPathsEdit->text());
    rc.licenseTemplatePath = licenseTemplatePath();
    rc.headerGuardTemplate = m_headerGuardAspect.value();
    return rc;
}

void CppFileSettingsWidget::slotEdit()
{
    FilePath path = licenseTemplatePath();
    if (path.isEmpty()) {
        // Pick a file name and write new template, edit with C++
        path = FileUtils::getSaveFilePath(Tr::tr("Choose Location for New License Template File"));
        if (path.isEmpty())
            return;
        FileSaver saver(path, QIODevice::Text);
        saver.write(
            Tr::tr(licenseTemplateTemplate).arg(QGuiApplication::applicationDisplayName()).toUtf8());
        if (!saver.finalize(this))
            return;
        setLicenseTemplatePath(path);
    }
    // Edit (now) existing file with C++
    Core::EditorManager::openEditor(path, CppEditor::Constants::CPPEDITOR_ID);
}

// CppFileSettingsPage

class CppFileSettingsPage final : public Core::IOptionsPage
{
public:
    CppFileSettingsPage()
    {
        setId(Constants::CPP_FILE_SETTINGS_ID);
        setDisplayName(Tr::tr("File Naming"));
        setCategory(Constants::CPP_SETTINGS_CATEGORY);
        setWidgetCreator([] { return new CppFileSettingsWidget(&globalCppFileSettings()); });
    }
};

// CppFileSettingsForProject

class CppFileSettingsForProject final
{
public:
    CppFileSettingsForProject(Project *project)
        : m_project(project)
    {
        loadSettings();
    }

    CppFileSettings settings() const;
    void setSettings(const CppFileSettings &settings);
    bool useGlobalSettings() const { return m_useGlobalSettings; }
    void setUseGlobalSettings(bool useGlobal);

private:
    void loadSettings();
    void saveSettings();

    Project * const m_project;
    CppFileSettings m_customSettings;
    bool m_useGlobalSettings = true;
};

CppFileSettings CppFileSettingsForProject::settings() const
{
    return m_useGlobalSettings ? globalCppFileSettings() : m_customSettings;
}

void CppFileSettingsForProject::setSettings(const CppFileSettings &settings)
{
    m_customSettings = settings;
    saveSettings();
}

void CppFileSettingsForProject::setUseGlobalSettings(bool useGlobal)
{
    m_useGlobalSettings = useGlobal;
    saveSettings();
}

void CppFileSettingsForProject::loadSettings()
{
    if (!m_project)
        return;

    const QVariant entry = m_project->namedSettings(projectSettingsKeyC);
    if (!entry.isValid())
        return;

    const QVariantMap data = mapEntryFromStoreEntry(entry).toMap();
    m_useGlobalSettings = data.value(useGlobalKeyC, true).toBool();
    m_customSettings.headerPrefixes = data.value(headerPrefixesKeyC,
                                                 m_customSettings.headerPrefixes).toStringList();
    m_customSettings.sourcePrefixes = data.value(sourcePrefixesKeyC,
                                                 m_customSettings.sourcePrefixes).toStringList();
    m_customSettings.headerSuffix = data.value(headerSuffixKeyC, m_customSettings.headerSuffix)
                                        .toString();
    m_customSettings.sourceSuffix = data.value(sourceSuffixKeyC, m_customSettings.sourceSuffix)
                                        .toString();
    m_customSettings.headerSearchPaths
        = data.value(headerSearchPathsKeyC, m_customSettings.headerSearchPaths).toStringList();
    m_customSettings.sourceSearchPaths
        = data.value(sourceSearchPathsKeyC, m_customSettings.sourceSearchPaths).toStringList();
    m_customSettings.lowerCaseFiles = data.value(Constants::LOWERCASE_CPPFILES_KEY,
                                                 m_customSettings.lowerCaseFiles).toBool();
    m_customSettings.headerPragmaOnce = data.value(headerPragmaOnceC,
                                                   m_customSettings.headerPragmaOnce).toBool();
    m_customSettings.headerGuardTemplate
        = data.value(headerGuardTemplateKeyC, m_customSettings.headerGuardTemplate).toString();
    m_customSettings.licenseTemplatePath
        = FilePath::fromSettings(data.value(licenseTemplatePathKeyC,
                                            m_customSettings.licenseTemplatePath.toSettings()));
}

void CppFileSettingsForProject::saveSettings()
{
    if (!m_project)
        return;

    // Optimization: Don't save anything if the user never switched away from the default.
    if (m_useGlobalSettings && !m_project->namedSettings(projectSettingsKeyC).isValid())
        return;

    QVariantMap data;
    data.insert(useGlobalKeyC, m_useGlobalSettings);
    data.insert(headerPrefixesKeyC, m_customSettings.headerPrefixes);
    data.insert(sourcePrefixesKeyC, m_customSettings.sourcePrefixes);
    data.insert(headerSuffixKeyC, m_customSettings.headerSuffix);
    data.insert(sourceSuffixKeyC, m_customSettings.sourceSuffix);
    data.insert(headerSearchPathsKeyC, m_customSettings.headerSearchPaths);
    data.insert(sourceSearchPathsKeyC, m_customSettings.sourceSearchPaths);
    data.insert(Constants::LOWERCASE_CPPFILES_KEY, m_customSettings.lowerCaseFiles);
    data.insert(headerPragmaOnceC, m_customSettings.headerPragmaOnce);
    data.insert(headerGuardTemplateKeyC, m_customSettings.headerGuardTemplate);
    data.insert(licenseTemplatePathKeyC, m_customSettings.licenseTemplatePath.toSettings());
    m_project->setNamedSettings(projectSettingsKeyC, data);
}

class CppFileSettingsForProjectWidget : public ProjectExplorer::ProjectSettingsWidget
{
public:
    CppFileSettingsForProjectWidget(const CppFileSettingsForProject &settings)
        : m_settings(settings),
        m_initialSettings(settings.settings()),
        m_widget(&m_initialSettings),
        m_wasGlobal(settings.useGlobalSettings())
    {
        setGlobalSettingsId(Constants::CPP_FILE_SETTINGS_ID);
        setUseGlobalSettings(settings.useGlobalSettings());

        const auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(&m_widget);

        connect(this, &ProjectSettingsWidget::useGlobalSettingsChanged, this, [this](bool checked) {
            m_settings.setUseGlobalSettings(checked);
            if (!checked)
                m_settings.setSettings(m_widget.currentSettings());
            maybeClearHeaderSourceCache();
            updateSubWidgetState();
        });

        connect(&m_widget, &CppFileSettingsWidget::userChange, this, [this] {
            m_settings.setSettings(m_widget.currentSettings());
            maybeClearHeaderSourceCache();
        });

        updateSubWidgetState();
    }

    void maybeClearHeaderSourceCache()
    {
        const CppFileSettings &s = m_settings.settings();
        if (m_settings.useGlobalSettings() != m_wasGlobal
            || s.headerSearchPaths != m_initialSettings.headerSearchPaths
            || s.sourceSearchPaths != m_initialSettings.sourceSearchPaths) {
            clearHeaderSourceCache();
        }
    }

    void updateSubWidgetState()
    {
        m_widget.setEnabled(!m_settings.useGlobalSettings());
    }

    CppFileSettingsForProject m_settings;
    CppFileSettings m_initialSettings;
    CppFileSettingsWidget m_widget;
    QCheckBox m_useGlobalSettingsCheckBox;
    const bool m_wasGlobal;
};

class CppFileSettingsProjectPanelFactory final : public ProjectPanelFactory
{
public:
    CppFileSettingsProjectPanelFactory()
    {
        setPriority(99);
        setDisplayName(Tr::tr("C++ File Naming"));
        setCreateWidgetFunction([](Project *project) {
            return new CppFileSettingsForProjectWidget(project);
        });
    }
};

CppFileSettings &globalCppFileSettings()
{
    // This is the global instance. There could be more.
    static CppFileSettings theGlobalCppFileSettings;
    return theGlobalCppFileSettings;
}

CppFileSettings cppFileSettingsForProject(ProjectExplorer::Project *project)
{
    return CppFileSettingsForProject(project).settings();
}

#ifdef WITH_TESTS
namespace {
class CppFileSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void testHeaderGuard_data()
    {
        QTest::addColumn<QString>("guardTemplate");
        QTest::addColumn<QString>("headerFile");
        QTest::addColumn<QString>("expectedGuard");

        QTest::newRow("default template, .h")
            << QString() << QString("/tmp/header.h") << QString("HEADER_H");
        QTest::newRow("default template, .hpp")
            << QString() << QString("/tmp/header.hpp") << QString("HEADER_HPP");
        QTest::newRow("default template, two extensions")
            << QString() << QString("/tmp/header.in.h") << QString("HEADER_IN_H");
        QTest::newRow("non-default template")
            << QString("%{JS: '%{Header:FilePath}'.toUpperCase().replace(/[.]/, '_').replace(/[/]/g, '_')}")
            << QString("/tmp/header.h") << QString("_TMP_HEADER_H");
    }

    void testHeaderGuard()
    {
        QFETCH(QString, guardTemplate);
        QFETCH(QString, headerFile);
        QFETCH(QString, expectedGuard);

        CppFileSettings settings;
        if (!guardTemplate.isEmpty())
            settings.headerGuardTemplate = guardTemplate;
        QCOMPARE(settings.headerGuard(FilePath::fromUserInput(headerFile)), expectedGuard);
    }
};
} // namespace
#endif // WITH_TESTS

void setupCppFileSettings(ExtensionSystem::IPlugin &plugin)
{
    static CppFileSettingsProjectPanelFactory theCppFileSettingsProjectPanelFactory;

    static CppFileSettingsPage theCppFileSettingsPage;

    globalCppFileSettings().fromSettings(Core::ICore::settings());
    globalCppFileSettings().addMimeInitializer();

#ifdef WITH_TESTS
    plugin.addTestCreator([] { return new CppFileSettingsTest; });
#else
    Q_UNUSED(plugin)
#endif
}

} // namespace CppEditor::Internal

#include <cppfilesettingspage.moc>
