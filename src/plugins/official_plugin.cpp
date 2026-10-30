// Official nodes use the public SDK and the same dynamic import path as a
// user's plugin. The application does not link this provider or its devices.
#include "core/native_plugin.h"
#include "math_node.h"
#include "logic_node.h"
#include "timer_node.h"
#include "recording_node.h"
#include "data_node.h"
#include "protocol_node.h"
#include "instrument_node.h"
#include "io_hub.h"
#include "official_catalog.h"

namespace corig {
class OfficialIo final : public PluginService {
public:
    explicit OfficialIo(const PluginEvents &events) { hub_.setCallbacks(events.receive,events.observe); }
    QVariantMap transact(const QString &node, const QString &operation, const QVariantMap &arguments) override { return hub_.transact(node,operation,arguments); }
    void shutdown() override { hub_.shutdown(); }
private:
    IoHub hub_;
};

class OfficialPlugin final : public QObject, public NativePlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID CoRigNativePlugin_iid)
    Q_INTERFACES(corig::NativePlugin)
public:
    QVariantMap configuration(const QVariantMap &spec, const QVariantMap &changes, const QVariantMap &previous) const override
    { return officialConfiguration(spec,changes,previous); }
    QVector<Port> ports(const QVariantMap &spec, const QVariantMap &config, QVector<Port> declared) const override
    { return officialPorts(spec,config,std::move(declared)); }
    std::unique_ptr<Node> create(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host) override
    {
        const auto kind = design.value("plugin").toString();
        if (kind == "function.python" || kind == "protocol.python") return makePythonNode(design,definition,ports,host);
        if (kind.startsWith("logic.")) return std::make_unique<LogicNode>(design,definition,ports,host);
        if (kind == "source.timer") return std::make_unique<TimerNode>(design,definition,ports,host);
        if (kind == "data.record") return std::make_unique<RecorderNode>(design,definition,ports,host);
        if (kind == "data.playback") return std::make_unique<PlaybackNode>(design,definition,ports,host);
        if (kind.startsWith("function.") || kind == "source.constant" || kind == "source.time") return std::make_unique<MathNode>(design,definition,ports,host);
        if (kind.startsWith("protocol.")) return std::make_unique<ProtocolNode>(design,definition,ports,host);
        if (kind.startsWith("plot.") || kind == "terminal.ansi") return makeInstrumentNode(design,definition,ports,host);
        return std::make_unique<DataNode>(design,definition,ports,host);
    }
    std::unique_ptr<PluginService> createService(const PluginEvents &events) override { return std::make_unique<OfficialIo>(events); }
};
}
#include "official_plugin.moc"
