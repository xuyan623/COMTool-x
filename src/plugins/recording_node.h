#pragma once

#include "core/node.h"
#include "core/recording.h"

namespace cx {

// A single data stream always passes through. Capture commands only control
// private recording storage, with engine-time gaps omitted during local pause.
class RecorderNode final : public Node {
public:
    RecorderNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    // Changing data type requires finishing or resetting an unfinished capture.
    void configure(Node &prepared) override;
    // Engine stop pauses capture; engine reset destroys all temporary content.
    void stop() override;
    QVariantMap snapshot(bool detailed = false) const override;
    // Start, pause, save and reset are shared by UI and external boolean inputs.
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    enum class Phase { Empty, Recording, Paused, Saved };
    // Computes accumulated active time using the engine's frozen clock.
    double durationAt(double time) const;
    RecordingWriter writer_;
    Phase phase_ = Phase::Empty;
    double accumulated_ = 0., startedAt_ = 0., lastTime_ = 0.;
    QString savedPath_, error_;
};

// File preparation and source selection are independent. Live input passes
// until cutoff is explicitly enabled; playback never resets downstream nodes.
class PlaybackNode final : public Node {
public:
    PlaybackNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
    double sourceInterval() const override;
    // Design imports replace the default; runtime imports live only in this node.
    void configure(Node &prepared) override;
    void stop() override;
    QVariantMap snapshot(bool detailed = false) const override;
    // Loading is transactional; seek/step emit only while cutoff is enabled.
    NodeResult action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result) override;
protected:
    NodeResult process(const Values &changes, const BatchContext &context, bool tick) override;
private:
    // Applies speed to scheduling, while original value timestamps stay intact.
    double positionAt(double time) const;
    std::unique_ptr<RecordingReader> reader_;
    double position_ = 0., anchor_ = 0., speed_ = 1., lastTime_ = 0.;
    qsizetype cursor_ = 0, next_ = 0;
    bool playing_ = false, cutoff_ = false;
    QString error_;
};

} // namespace cx
