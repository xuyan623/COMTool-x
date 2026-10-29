#pragma once

#include "core/node.h"

namespace cx {
std::unique_ptr<Node> makeInstrumentNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host);
}
