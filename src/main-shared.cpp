#include "main-shared.h"

#include <QStandardPaths>
#include <QFile>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>
#include <QLocale>
#include <QTranslator>
#include <QLibraryInfo>


namespace Application {


QString userConfigPath()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DataLocation);
    QString oldPath = path;
    oldPath.replace(QStringLiteral("Ricochet"), QStringLiteral("Torsion"), Qt::CaseInsensitive);
    if (QFile::exists(oldPath))
        return oldPath;
    return path;
}

#ifdef Q_OS_MAC
QString appBundlePath()
{
    QString path = QApplication::applicationDirPath();
    int p = path.lastIndexOf(QLatin1String(".app/"));
    if (p >= 0)
    {
        p = path.lastIndexOf(QLatin1Char('/'), p);
        path = path.left(p+1);
    }

    return path;
}
#endif

bool initSettings(SettingsFile *settings, QLockFile **lockFile, QString &errorMessage)
{
    /* If built in portable mode (default), configuration is stored in the 'config'
     * directory next to the binary. If not writable, launching fails.
     *
     * Portable OS X is an exception. In that case, configuration is stored in a
     * 'config.ricochet' folder next to the application bundle, unless the application
     * path contains "/Applications", in which case non-portable mode is used.
     *
     * When not in portable mode, a platform-specific per-user config location is used.
     *
     * This behavior may be overriden by passing a folder path as the first argument.
     */

    QString configPath;
    QStringList args = qApp->arguments();
    if (args.size() > 1) {
        configPath = args[1];
    } else {
#ifndef RICOCHET_NO_PORTABLE
# ifdef Q_OS_MAC
        if (!qApp->applicationDirPath().contains(QStringLiteral("/Applications"))) {
            // Try old configuration path first
            configPath = appBundlePath() + QStringLiteral("config.torsion");
            if (!QFile::exists(configPath))
                configPath = appBundlePath() + QStringLiteral("config.ricochet");
        }
# else
        configPath = qApp->applicationDirPath() + QStringLiteral("/config");
# endif
#endif
        if (configPath.isEmpty())
            configPath = userConfigPath();
    }

    QDir dir(configPath);
    if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
        errorMessage = QStringLiteral("Cannot create directory: %1").arg(dir.path());
        return false;
    }

    // Reset to config directory for consistency; avoid depending on this behavior for paths
    if (QDir::setCurrent(dir.absolutePath()) && dir.isRelative())
        dir.setPath(QStringLiteral("."));

    QLockFile *lock = new QLockFile(dir.filePath(QStringLiteral("ricochet.json.lock")));
    *lockFile = lock;
    lock->setStaleLockTime(0);
    if (!lock->tryLock()) {
        if (lock->error() == QLockFile::LockFailedError) {
            // This happens if a stale lock file exists and another process uses that PID.
            // Try removing the stale file, which will fail if a real process is holding a
            // file-level lock. A false error is more problematic than not locking properly
            // on corner-case systems.
            if (!lock->removeStaleLockFile() || !lock->tryLock()) {
                errorMessage = QStringLiteral("Configuration file is already in use");
                return false;
            } else
                qDebug() << "Removed stale lock file";
        } else {
            errorMessage = QStringLiteral("Cannot write configuration file (failed to acquire lock)");
            return false;
        }
    }

    settings->setFilePath(dir.filePath(QStringLiteral("ricochet.json")));
    if (settings->hasError()) {
        errorMessage = settings->errorMessage();
        return false;
    }

    if (settings->root()->data().isEmpty()) {
        QString filePath = dir.filePath(QStringLiteral("Torsion.ini"));
        if (!QFile::exists(filePath))
            filePath = dir.filePath(QStringLiteral("ricochet.ini"));
        if (QFile::exists(filePath))
            importLegacySettings(settings, filePath);
    }

    return true;
}

void copyKeys(QSettings &old, SettingsObject *object)
{
    foreach (const QString &key, old.childKeys()) {
        QVariant value = old.value(key);
        if ((QMetaType::Type)value.type() == QMetaType::QDateTime)
            object->write(key, value.toDateTime());
        else if ((QMetaType::Type)value.type() == QMetaType::QByteArray)
            object->write(key, Base64Encode(value.toByteArray()));
        else
            object->write(key, value.toString());
    }
}

bool importLegacySettings(SettingsFile *settings, const QString &oldPath)
{
    QSettings old(oldPath, QSettings::IniFormat);
    SettingsObject *root = settings->root();
    QVariant value;

    qDebug() << "Importing legacy format settings from" << oldPath;

    if (!(value = old.value(QStringLiteral("tor/controlIp"))).isNull())
        root->write("tor.controlAddress", value.toString());
    if (!(value = old.value(QStringLiteral("tor/controlPort"))).isNull())
        root->write("tor.controlPort", value.toInt());
    if (!(value = old.value(QStringLiteral("tor/authPassword"))).isNull())
        root->write("tor.controlPassword", value.toString());
    if (!(value = old.value(QStringLiteral("tor/socksIp"))).isNull())
        root->write("tor.socksAddress", value.toString());
    if (!(value = old.value(QStringLiteral("tor/socksPort"))).isNull())
        root->write("tor.socksPort", value.toInt());
    if (!(value = old.value(QStringLiteral("tor/executablePath"))).isNull())
        root->write("tor.executablePath", value.toString());
    if (!(value = old.value(QStringLiteral("core/neverPublishService"))).isNull())
        root->write("tor.neverPublishServices", value.toBool());
    if (!(value = old.value(QStringLiteral("identity/0/dataDirectory"))).isNull())
        root->write("identity.dataDirectory", value.toString());
    if (!(value = old.value(QStringLiteral("identity/0/createNewService"))).isNull())
        root->write("identity.initializing", value.toBool());
    if (!(value = old.value(QStringLiteral("core/listenIp"))).isNull())
        root->write("identity.localListenAddress", value.toString());
    if (!(value = old.value(QStringLiteral("core/listenPort"))).isNull())
        root->write("identity.localListenPort", value.toInt());

    {
        old.beginGroup(QStringLiteral("contacts"));
        QStringList ids = old.childGroups();
        foreach (const QString &id, ids) {
            old.beginGroup(id);
            SettingsObject userObject(root, QStringLiteral("contacts.%1").arg(id));

            copyKeys(old, &userObject);

            if (old.childGroups().contains(QStringLiteral("request"))) {
                old.beginGroup(QStringLiteral("request"));
                QStringList requestKeys = old.childKeys();
                foreach (const QString &key, requestKeys)
                    userObject.write(QStringLiteral("request.") + key, old.value(key).toString());
                old.endGroup();
            }

            old.endGroup();
        }
        old.endGroup();
    }

    {
        old.beginGroup(QStringLiteral("contactRequests"));
        QStringList contacts = old.childGroups();

        foreach (const QString &hostname, contacts) {
            old.beginGroup(hostname);
            SettingsObject requestObject(root, QStringLiteral("contactRequests.%1").arg(hostname));
            copyKeys(old, &requestObject);
            old.endGroup();
        }

        old.endGroup();
    }

    if (!(value = old.value(QStringLiteral("core/hostnameBlacklist"))).isNull()) {
        QStringList blacklist = value.toStringList();
        root->write("identity.hostnameBlacklist", QJsonArray::fromStringList(blacklist));
    }

    return true;
}

void initTranslation()
{
    QTranslator *translator = new QTranslator;

    bool ok = false;
    QString appPath = qApp->applicationDirPath();
    QString resPath = QLatin1String(":/lang/");

    QLocale locale = QLocale::system();
    if (!qgetenv("RICOCHET_LOCALE").isEmpty()) {
        locale = QLocale(QString::fromLatin1(qgetenv("RICOCHET_LOCALE")));
        qDebug() << "Forcing locale" << locale << "from environment" << locale.uiLanguages();
    }

    SettingsObject settings;
    QString settingsLanguage(settings.read("ui.language").toString());

    if (!settingsLanguage.isEmpty()) {
        locale = settingsLanguage;
    } else {
        //write an empty string to get "System default" language selected automatically in preferences
        settings.write(QStringLiteral("ui.language"), QString());
    }

    ok = translator->load(locale, QStringLiteral("ricochet"), QStringLiteral("_"), appPath);
    if (!ok)
        ok = translator->load(locale, QStringLiteral("ricochet"), QStringLiteral("_"), resPath);

    if (ok) {
        qApp->installTranslator(translator);

        QTranslator *qtTranslator = new QTranslator;
        ok = qtTranslator->load(QStringLiteral("qt_") + locale.name(), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
        if (ok)
            qApp->installTranslator(qtTranslator);
        else
            delete qtTranslator;
    } else
        delete translator;
}


} // namespace Application
