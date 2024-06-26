// Copyright (C) 2024 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR GPL-3.0-only WITH Qt-GPL-exception-1.0

#include "luapluginspec.h"

#include "luaengine.h"
#include "luatr.h"

#include <extensionsystem/extensionsystemtr.h>

#include <utils/algorithm.h>
#include <utils/appinfo.h>
#include <utils/expected.h>

#include <QJsonDocument>
#include <QLoggingCategory>

Q_LOGGING_CATEGORY(luaPluginSpecLog, "qtc.lua.pluginspec", QtWarningMsg)

using namespace ExtensionSystem;
using namespace Utils;

namespace Lua {

class LuaScriptPluginPrivate
{
public:
    QString name;
    QList<QString> cppDepends;
    sol::function setup;
    sol::environment pluginEnvironment;
};

class LuaPluginSpecPrivate
{
public:
    FilePath pluginScriptPath;
    bool printToOutputPane = false;
    std::unique_ptr<sol::state> activeLuaState;
};

LuaPluginSpec::LuaPluginSpec()
    : d(new LuaPluginSpecPrivate())
{}

expected_str<LuaPluginSpec *> LuaPluginSpec::create(const FilePath &filePath, sol::table pluginTable)
{
    std::unique_ptr<LuaPluginSpec> pluginSpec(new LuaPluginSpec());

    if (!pluginTable.get_or<sol::function>("setup", {}))
        return make_unexpected(QString("Plugin info table did not contain a setup function"));

    QJsonValue v = LuaEngine::toJson(pluginTable);
    if (luaPluginSpecLog().isDebugEnabled()) {
        qCDebug(luaPluginSpecLog).noquote()
            << "Plugin info table:" << QJsonDocument(v.toObject()).toJson(QJsonDocument::Indented);
    }

    QJsonObject obj = v.toObject();
    obj["SoftLoadable"] = true;

    auto r = pluginSpec->PluginSpec::readMetaData(obj);
    if (!r)
        return make_unexpected(r.error());

    pluginSpec->setFilePath(filePath);
    pluginSpec->setLocation(filePath.parentDir());

    pluginSpec->d->pluginScriptPath = filePath;
    pluginSpec->d->printToOutputPane = pluginTable.get_or("printToOutputPane", false);

    return pluginSpec.release();
}

ExtensionSystem::IPlugin *LuaPluginSpec::plugin() const
{
    return nullptr;
}

bool LuaPluginSpec::provides(PluginSpec *spec, const PluginDependency &dependency) const
{
    if (QString::compare(dependency.name, spec->name(), Qt::CaseInsensitive) != 0)
        return false;

    // Since we first released the lua support with Qt Creator 14.0.0, but the internal version
    // number was still 13.0.82, we needed to special case this version.
    if (versionCompare(dependency.version, "14.0.0") <= 0)
        return true;

    return (versionCompare(spec->version(), dependency.version) >= 0);
}

// LuaPluginSpec::For internal use {}
bool LuaPluginSpec::loadLibrary()
{
    // We are actually already loaded, but we need to set the state to loaded as well.
    // We cannot set it earlier as it is used as a state machine that would break for earlier steps.
    setState(PluginSpec::State::Loaded);
    return true;
}
bool LuaPluginSpec::initializePlugin()
{
    QTC_ASSERT(!d->activeLuaState, return false);

    std::unique_ptr<sol::state> activeLuaState = std::make_unique<sol::state>();

    expected_str<sol::protected_function> setupResult
        = LuaEngine::instance().prepareSetup(*activeLuaState, *this);

    if (!setupResult) {
        setError(Lua::Tr::tr("Failed to prepare plugin setup: %1").arg(setupResult.error()));
        return false;
    }

    auto result = setupResult->call();

    if (result.get_type() == sol::type::boolean && result.get<bool>() == false) {
        setError(Lua::Tr::tr("Plugin setup function returned false"));
        return false;
    } else if (result.get_type() == sol::type::string) {
        std::string error = result.get<sol::error>().what();
        if (!error.empty()) {
            setError(Lua::Tr::tr("Plugin setup function returned error: %1")
                         .arg(QString::fromStdString(error)));
            return false;
        }
    }

    d->activeLuaState = std::move(activeLuaState);

    setState(PluginSpec::State::Initialized);
    return true;
}

bool LuaPluginSpec::initializeExtensions()
{
    setState(PluginSpec::State::Running);
    return true;
}

bool LuaPluginSpec::delayedInitialize()
{
    return true;
}
ExtensionSystem::IPlugin::ShutdownFlag LuaPluginSpec::stop()
{
    d->activeLuaState->stack_clear();
    return ExtensionSystem::IPlugin::ShutdownFlag::SynchronousShutdown;
}

void LuaPluginSpec::kill() {}

bool LuaPluginSpec::printToOutputPane() const
{
    return d->printToOutputPane;
}

} // namespace Lua
