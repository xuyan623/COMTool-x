// Minimal complete plugin: checked parameters, actual computation, an action
// and a QML view, using only the installed public CoRig SDK.
#include <corig/plugin.h>
#include <QObject>
#include <algorithm>

class GainNode final : public corig::Node {
public:
    using Node::Node;
    corig::NodeResult action(const QString &name, const QVariantMap &arguments, const corig::BatchContext &context, QVariant &result) override
    {
        if (name == "reset") {
            state_["calls"] = 0; state_["last"] = 0.; result = state_;
            return corig::NodeResult::output({{"out",0.}});
        }
        return Node::action(name,arguments,context,result);
    }
protected:
    corig::NodeResult process(const corig::Values &changes, const corig::BatchContext &, bool) override
    {
        if (!changes.contains("x")) return {};
        const double value = std::clamp(number("x") * config_.value("gain").toDouble(),config_.value("minimum").toDouble(),config_.value("maximum").toDouble());
        state_["last"] = value; state_["calls"] = state_.value("calls",0).toULongLong() + 1;
        return corig::NodeResult::output({{"out",value}});
    }
};

class GainPlugin final : public QObject, public corig::NativePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID CoRigNativePlugin_iid)
    Q_INTERFACES(corig::NativePlugin)
public:
    QVariantMap configuration(const QVariantMap &definition, const QVariantMap &changes, const QVariantMap &previous) const override
    {
        const auto result = corig::checkedProperties(definition.value("properties").toMap(),changes,previous);
        if (result.value("minimum").toDouble() >= result.value("maximum").toDouble()) throw corig::Error("下界必须小于上界");
        return result;
    }
    QVariantMap presentationConfiguration(const QString &, const QVariantMap &definition, const QVariantMap &changes, const QVariantMap &previous) const override
    {
        const auto result = corig::checkedProperties(definition.value("properties").toMap(),changes,previous);
        if (result.value("display_label").toString().trimmed().isEmpty()) throw corig::Error("显示名称不能为空");
        return result;
    }
    std::unique_ptr<corig::Node> create(QVariantMap design, QVariantMap definition, QVector<corig::Port> ports, corig::NodeHost host) override
    {
        return std::make_unique<GainNode>(std::move(design),std::move(definition),std::move(ports),std::move(host));
    }
};
#include "gain.moc"
