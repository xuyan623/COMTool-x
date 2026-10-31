#include "node.h"
#include "native_plugin.h"

namespace corig {

NodeResult NodeResult::output(const Values &values)
{
    NodeResult result;
    if (!values.isEmpty()) result.emissions.append(values);
    return result;
}

Node::Node(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : design_(std::move(design)), definition_(std::move(definition)), config_(design_.value("config").toMap()),
      id_(design_.value("id").toString()), kind_(design_.value("plugin").toString()), ports_(std::move(ports)), host_(std::move(host))
{
    for (const auto &port : ports_) if (port.direction == "in") inputs_.insert(port.id, defaultValue(port.type));
}

NodeResult Node::execute(const Values &changes, const QSet<QString> &disconnected, const BatchContext &context, bool tick)
{
    for (const auto &name : disconnected) {
        connected_.remove(name);
        for (const auto &port : ports_) if (port.id == name) inputs_.insert(name, defaultValue(port.type));
    }
    for (auto i = changes.cbegin(); i != changes.cend(); ++i) { inputs_.insert(i.key(), i.value()); connected_.insert(i.key()); }
    auto current = context; current.resumed = resumed_;
    auto result = process(changes, current, tick);
    ++executions_; resumed_ = false;
    for (const auto &emission : result.emissions) retainEmission(emission);
    for (const auto &name : result.disconnected) outputs_.remove(name);
    return result;
}

void Node::retainEmission(const Values &values)
{
    for (auto i = values.cbegin(); i != values.cend(); ++i) {
        auto port = std::find_if(ports_.cbegin(), ports_.cend(), [&](const Port &p) { return p.id == i.key() && p.direction == "out"; });
        if (port == ports_.cend() || !valueMatches(i.value(), port->type)) throw Error(id_ + " 输出类型或端口无效：" + i.key());
        outputs_.insert(i.key(), i.value());
    }
}

NodeResult Node::action(const QString &name, const QVariantMap &, const BatchContext &, QVariant &result)
{
    if (name == "read_state") { result = snapshot(true); return {}; }
    throw Error("节点不支持动作：" + name);
}

void Node::configure(Node &prepared)
{
    config_ = prepared.config_; design_ = prepared.design_; definition_ = prepared.definition_;
    ports_ = prepared.ports_; host_ = prepared.host_;
    Values retained;
    for (const auto &port : ports_) if (port.direction == "in") retained.insert(port.id, inputs_.value(port.id, defaultValue(port.type)));
    inputs_ = std::move(retained);
}

double Node::sourceInterval() const { return -1.; }
NodeResult Node::start(const BatchContext &) { resumed_ = true; return {}; }
void Node::resume() { resumed_ = true; }
void Node::pause() {}
void Node::stop() {}

QVariantMap Node::snapshot(bool) const
{
    QVariantMap inputs, outputs;
    for (auto i = inputs_.cbegin(); i != inputs_.cend(); ++i) inputs.insert(i.key(), i.value());
    for (auto i = outputs_.cbegin(); i != outputs_.cend(); ++i) outputs.insert(i.key(), i.value());
    return {{"id",id_}, {"plugin",kind_}, {"config",config_}, {"inputs",inputs}, {"outputs",outputs}, {"state",state_}, {"executions",QVariant::fromValue(executions_)}};
}

void Node::setConnections(const QSet<QString> &ports, const QSet<QString> &reset)
{
    connected_ = ports;
    for (const auto &port : ports_) if (port.direction == "in" && (!ports.contains(port.id) || reset.contains(port.id))) inputs_.insert(port.id,defaultValue(port.type));
}
QVariantMap Node::view(const QString &, const QVariantMap &) const { return snapshot(false); }
double Node::number(const QString &port) const { return finiteNumber(inputs_.value(port, 0.), port); }

std::unique_ptr<Node> makeNode(const QVariantMap &design, const Catalog &catalog, const NodeHost &host)
{
    auto normalized = design;
    const auto kind = design.value("plugin").toString();
    const auto config = catalog.checkedConfig(kind, design.value("config").toMap());
    normalized["config"] = config;
    const auto definition = catalog.definition(kind);
    auto ports = catalog.ports(kind, config);
    if (definition.value("implementation") == "python")
        return makePythonNode(normalized, definition, ports, host);
    const auto package = catalog.package(kind);
    auto scopedHost = host;
    if (host.service) scopedHost.io = [service = host.service,package](const QString &id, const QString &op, const QVariantMap &args) { return service(package,id,op,args); };
    auto node = nativePlugin(package).create(normalized,definition,ports,scopedHost);
    if (!node) throw Error("C++ 插件未创建节点：" + kind);
    return node;
}

} // namespace corig
