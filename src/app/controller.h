#pragma once

// The application has one checked command interface for QML and automation.
// Candidate designs become visible only after the runtime accepts them; view
// refreshes use immutable observations and never lock the graph worker.
#include "core/engine.h"
#include "core/python_host.h"
#include "core/native_plugin.h"
#include "model_commands.h"
#include "plugin_store.h"
#include <QObject>
#include <QSettings>
#include <QTimer>
#include <QPointer>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QElapsedTimer>

class QQmlEngine;
class QQuickItem;
namespace corig {
class Workspaces;
class ApiServer;

class Controller final : public QObject {
    Q_OBJECT
    // Property accessors form the Qt meta-object interface consumed by QML.
    Q_PROPERTY(QVariantMap project READ project NOTIFY projectChanged)
    Q_PROPERTY(QVariantList graphNodes READ graphNodes NOTIFY projectChanged)
    Q_PROPERTY(QVariantMap runtime READ runtime NOTIFY runtimeChanged)
    Q_PROPERTY(QVariantList registry READ registry NOTIFY catalogChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY projectChanged)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY preferencesChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool executionPending READ executionPending NOTIFY runtimeChanged)
    Q_PROPERTY(QString theme READ theme NOTIFY preferencesChanged)
    Q_PROPERTY(QString pythonPath READ pythonPath NOTIFY preferencesChanged)
    Q_PROPERTY(QString selected READ selected NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedKind READ selectedKind NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedNodes READ selectedNodes NOTIFY selectionChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString noticeLevel READ noticeLevel NOTIFY noticeChanged)
    Q_PROPERTY(QObject *workspaces READ workspaces CONSTANT)
    Q_PROPERTY(QVariantMap libraryPreferences READ libraryPreferences NOTIFY preferencesChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString libraryTarget READ libraryTarget NOTIFY preferencesChanged)
    Q_PROPERTY(QVariantMap pluginState READ pluginState NOTIFY pluginsChanged)
    Q_PROPERTY(QVariantMap pendingPluginOpen READ pendingPluginOpen NOTIFY pluginOpenRequested)
public:
    explicit Controller(QString dataDirectory, QObject *parent = nullptr, const QVariantMap &initialCatalog = {});
    ~Controller() override;
    void attachUi(QQmlEngine *engine);
    void shutdown();
    QVariant command(const QString &operation, const QVariantMap &arguments = {});
    QVariantMap schema() const;
    bool confirmExit(const QString &window, bool discard);
    QString dataDirectory() const { return dataDirectory_; }
    QVariantMap project() const { return project_; }
    QVariantList graphNodes() const { return graphNodes_; }
    QVariantMap runtime() const { return runtime_; }
    QVariantList registry() const { return catalog_.describe(); }
    QString projectPath() const { return projectPath_; }
    bool dirty() const { return dirty_; }
    bool readOnly() const { return readOnly_; }
    bool busy() const { return busy_; }
    bool executionPending() const { return runtimePending_ > 0; }
    QString theme() const { return theme_; }
    QString pythonPath() const { return python_->interpreter(); }
    QString selected() const { return selected_; }
    QString selectedKind() const { return selectedKind_; }
    QStringList selectedNodes() const { return selectedNodes_; }
    QString notice() const { return notice_; }
    QString noticeLevel() const { return noticeLevel_; }
    QObject *workspaces() const;
    QVariantMap libraryPreferences() const;
    QString libraryTarget() const { return libraryTarget_; }
    QVariantMap pluginState() const { return plugins_->state(project_); }
    QVariantMap pendingPluginOpen() const { return pendingPluginOpen_; }

    Q_INVOKABLE QVariant invoke(const QString &operation, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap job(const QString &id) const;
    Q_INVOKABLE QVariantMap nodeDesign(const QString &id) const;
    Q_INVOKABLE QVariantMap nodeState(const QString &id) const;
    Q_INVOKABLE QVariantMap definition(const QString &id) const;
    Q_INVOKABLE QVariantMap uiItem(const QString &id) const;
    Q_INVOKABLE QVariantMap inspector() const;
    Q_INVOKABLE QVariantList catalogEntries(const QString &query, const QStringList &category = {}, const QString &scope = "all") const;
    Q_INVOKABLE QVariantMap previewGeometry(const QString &plugin, const QVariantMap &config = {}) const;
    Q_INVOKABLE QVariantMap connectionOptions(const QString &node, const QString &port) const;
    Q_INVOKABLE void openLibrary(const QString &page);
    Q_INVOKABLE QString hitWire(double x, double y, double scale) const;
    Q_INVOKABLE QVariantMap hitPort(double x, double y, double scale) const;
    Q_INVOKABLE QVariantMap wireEditState(const QString &wire, double x = 0., double y = 0.) const;
    Q_INVOKABLE void dragPlugin(const QString &plugin);
    Q_INVOKABLE void dragUiItem(const QString &item);
    Q_INVOKABLE void dragObject(const QString &page, const QString &item);
    Q_INVOKABLE void observeView(const QString &node, const QString &view, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariantMap viewState(const QString &node, const QString &view) const;
    Q_INVOKABLE void registerView(QObject *item, const QString &node, const QString &view);
    Q_INVOKABLE QString userViewUrl(const QString &node, const QString &view);
    Q_INVOKABLE QString pluginViewUrl(const QString &plugin, const QString &view);
    Q_INVOKABLE QObject *userViewBridge(const QString &node, const QString &view);
signals:
    void projectChanged();
    void runtimeChanged();
    void catalogChanged();
    void selectionChanged();
    void preferencesChanged();
    void noticeChanged();
    void busyChanged();
    void viewChanged(const QString &key);
    void jobFinished(const QString &id);
    void exitConfirmation(const QString &window);
    void graphFocusRequested(const QString &node);
    void pluginsChanged();
    void pluginOpenRequested();
    void pluginSettingsRequested();
    void pluginExportRequested(const QString &scope, const QString &plugin);
private:
    friend class Workspaces;
    QVariantMap appState() const;
    QVariantMap launchEngine(const QString &operation, const QVariantMap &arguments, std::function<void(const QVariant &,const QString &)> completed = {});
    QVariantMap launchTask(const QString &operation, std::function<QVariant()> task, std::function<void(const QVariant &,const QString &)> completed = {});
    void finishJob(const QString &id, const QVariant &result, const QString &error);
    QVariantMap commit(EditResult edit, const QString &label, bool remember = true, bool runtimeOnly = false);
    void adopt(const QVariantMap &project, bool dirty, const QString &selection = {});
    void refreshGraph();
    void tell(const QString &message, const QString &level = "info");
    QVariant controls(const QString &operation, const QVariantMap &args);
    QVariant editorView(const QString &operation, const QVariantMap &args);
    QVariant configureWindow(const QVariantMap &args);
    QVariant capture(const QVariantMap &arguments);
    QVariant viewCall(const QVariantMap &arguments);
    QVariantList viewActions(const QString &node, const QString &view) const;
    QVariantMap importPlugin(const QString &path) const;
    QVariantMap pluginService(const PluginPackage &package, const QString &node, const QString &operation, const QVariantMap &arguments);
    void requireEditable(bool layout = false) const;
    QString dataDirectory_, projectPath_, selected_, selectedKind_ = "node", theme_ = "dark", notice_ = "就绪", noticeLevel_ = "info";
    QStringList selectedNodes_;
    QString libraryTarget_ = "graph";
    QVariantMap project_, runtime_;
    QVariantList graphNodes_;
    Catalog catalog_{QList<PluginPackage>{}};
    std::unique_ptr<QSettings> settings_;
    std::unique_ptr<PluginStore> plugins_;
    QVariantMap pendingPluginOpen_;
    std::unique_ptr<PythonHost> python_;
    QMap<QString,std::shared_ptr<PluginService>> services_;
    std::mutex servicesMutex_;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<Workspaces> workspaces_;
    std::unique_ptr<ApiServer> api_;
    QPointer<QQmlEngine> qmlEngine_;
    QTimer poll_;
    QThreadPool tasks_;
    QHash<quint64,std::function<void(const QVariant &,const QString &)>> callbacks_;
    QHash<QString,QVariantMap> jobs_, views_, viewArguments_;
    QSet<QString> viewPending_;
    QHash<QString,QList<QPointer<QObject>>> viewItems_;
    QHash<QString,QPointer<QObject>> viewBridges_;
    QHash<QString,std::shared_ptr<QTemporaryDir>> pluginDirectories_;
    QVector<QVariantMap> undo_, redo_;
    QElapsedTimer viewClock_;
    QHash<QString,qint64> viewRequestedAt_;
    QVariantMap observedViews_;
    bool dirty_ = false, readOnly_ = false, busy_ = false, exitDiscarded_ = false;
    int runtimePending_ = 0;
    std::atomic_bool closing_{false};
};

} // namespace corig
