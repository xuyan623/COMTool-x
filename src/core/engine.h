#pragma once

// The worker is the sole owner of live nodes. Producers enqueue bounded events;
// the GUI reads immutable observations without holding the computation lock.
#include "node.h"
#include <QThread>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <map>
#include <optional>

namespace cx {

class Engine final : public QThread {
    Q_OBJECT
public:
    explicit Engine(NodeHost host, QObject *parent = nullptr);
    ~Engine() override;
    quint64 submit(const QString &operation, const QVariantMap &arguments = {});
    void receive(const QString &node, const Values &outputs);
    void ioObservation(const QString &node, const QVariantMap &state);
    QVariantMap observation() const;
    void shutdown();
    bool interrupted() const { return quitting_ || stopRequested_; }
signals:
    void completed(quint64 id, const QVariant &result, const QString &error);
protected:
    void run() override;
private:
    using Clock = std::chrono::steady_clock;
    struct Request { quint64 id; QString operation; QVariantMap arguments; };
    struct Event { QString source; Values outputs; bool tick = false; };
    struct Route { QString id, source, sourcePort, target, targetPort; quint64 count = 0, steps = 0; bool connected = false; };
    struct Batch {
        BatchContext context;
        QString source;
        bool tick = false;
        qsizetype cursor = 0, ioCursor = 0;
        QHash<QString, Values> changes;
        QHash<QString, QSet<QString>> invalid;
        QSet<QString> touched;
        QStringList deferredIO;
    };
    QVariant dispatch(const Request &request);
    void applyProject(const QVariantMap &project);
    void beginSession(bool debug);
    void endSession();
    void beginBatch(const Event &event, bool stepping);
    bool advanceBatch(int nodeLimit = -1);
    void route(const QString &source, const NodeResult &result);
    void publishAndDrain(const QString &source, const Values &values, const BatchContext &context);
    void rebuildSources(Clock::time_point now);
    std::optional<Event> nextEvent(Clock::time_point now);
    void publishObservation(Clock::time_point now, bool force = false);
    void record(const QString &level, const QString &message, const QString &node = {}, const QVariantMap &detail = {});
    double elapsed(Clock::time_point now) const;
    bool cancelled(const QString &node) const;

    NodeHost host_;
    Catalog catalog_;
    QVariantMap project_;
    std::map<QString, std::unique_ptr<Node>> nodes_;
    QVector<QString> order_;
    QVector<Route> routes_;
    QHash<QString, QVector<qsizetype>> outgoing_;
    std::unique_ptr<Batch> batch_;
    QHash<QString, Clock::time_point> deadlines_;
    QStringList readySources_;
    QVariantList availableSources_;
    qsizetype readyCursor_ = 0;
    int schedulerCursor_ = 0;
    std::deque<Event> pendingFrames_;
    std::deque<QVariantMap> journal_;
    qsizetype journalBytes_ = 0;
    quint64 journalCursor_ = 0, epoch_ = 0, batchSequence_ = 0, processed_ = 0, failed_ = 0;
    bool active_ = false, running_ = false, debug_ = false, journalDetail_ = false;
    QString currentNode_;
    QVariantMap nodeErrors_;
    double elapsedBase_ = 0.;
    Clock::time_point resumedAt_, lastPublished_;
    mutable std::mutex queueMutex_;
    std::condition_variable condition_;
    std::deque<Request> requests_;
    std::deque<Event> inputQueue_;
    QHash<QString, QVariantMap> ioStates_;
    QSet<QString> cancellations_;
    qsizetype queuedBytes_ = 0;
    QString overflowError_;
    std::atomic_bool quitting_{false}, accepting_{false}, stopRequested_{false};
    std::atomic<quint64> requestSequence_{0};
    std::atomic<std::shared_ptr<const QVariantMap>> published_;
};

} // namespace cx
