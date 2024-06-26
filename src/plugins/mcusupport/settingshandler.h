// Copyright (C) 2022 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0
#pragma once

#include <utils/storekey.h>

#include <QSettings>

namespace Utils { class FilePath; }

namespace McuSupport::Internal {

class SettingsHandler
{
public:
    using Ptr = std::shared_ptr<SettingsHandler>;
    virtual ~SettingsHandler() = default;
    virtual Utils::FilePath getPath(const Utils::Key &settingsKey,
                                    QSettings::Scope scope,
                                    const Utils::FilePath &m_defaultPath) const;
    Utils::Key getVersionedKey(const Utils::Key &plainKey,
                               QSettings::Scope scope,
                               const QStringList &versions,
                               bool allowNewer) const;

    virtual bool write(const Utils::Key &settingsKey,
                       const Utils::FilePath &path,
                       const Utils::FilePath &defaultPath) const;

    virtual bool isAutomaticKitCreationEnabled() const;
    void setAutomaticKitCreation(bool isEnabled);
    void setInitialPlatformName(const QString &platform);
    QString initialPlatformName() const;
};

} // namespace McuSupport::Internal
