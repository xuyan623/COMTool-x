#include "math_node.h"
#include <QJSEngine>
#include <QJSValue>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>

namespace corig {
static constexpr double Pi = std::numbers::pi;

MathNode::MathNode(QVariantMap design, QVariantMap definition, QVector<Port> ports, NodeHost host)
    : Node(std::move(design), std::move(definition), std::move(ports), std::move(host)), random_(config_.value("seed",0).toULongLong())
{
    resetHistory();
    if (kind_ == "function.expression") compileExpressions();
}
MathNode::~MathNode() = default;

void MathNode::resetHistory()
{
    previousTime_ = previousX_ = 0.; anchored_ = false; window_.clear();
    value_ = config_.value("initial",0.).toDouble();
    state_ = {{"value",value_}, {"samples",0}, {"waiting_for_time",true}};
}

void MathNode::compileExpressions()
{
    expressionInputs_ = config_.value("inputs").toStringList();
    const auto expressions = config_.value("outputs").toMap();
    if (expressions.isEmpty() || expressions.size() > 64) throw Error("需要 1～64 个输出公式");
    static const QStringList functions{"sin","cos","tan","asin","acos","atan","atan2","sinh","cosh","tanh","asinh","acosh","atanh","sqrt","exp","expm1","log","log10","log2","log1p","floor","ceil","trunc","abs","min","max","round","pow","hypot","sign"};
    const QSet<QString> allowed = QSet<QString>(functions.cbegin(), functions.cend()) + QSet<QString>(expressionInputs_.cbegin(), expressionInputs_.cend()) + QSet<QString>{"pi","e","tau","true","false","degrees","radians"};
    QStringList formulas;
    expressionOutputs_.clear();
    for (auto i = expressions.cbegin(); i != expressions.cend(); ++i) {
        const auto formula = i.value().toString();
        if (formula.size() > 4096 || formula.contains(QRegularExpression("[;{}\\[\\]\\\"'`]|(?<![=!<>])=(?!=)"))) throw Error("公式只能包含数值、变量、运算符和数学函数");
        const auto tokens = QRegularExpression("(?:\\d+(?:\\.\\d*)?|\\.\\d+)(?:[eE][+-]?\\d+)?|([A-Za-z_][A-Za-z_0-9]*)|([^\\s])").globalMatch(formula);
        auto iterator = tokens;
        while (iterator.hasNext()) {
            const auto token = iterator.next();
            if (!token.captured(1).isEmpty() && !allowed.contains(token.captured(1))) throw Error("公式包含未知名称：" + token.captured(1));
            if (token.captured(2) == ".") throw Error("公式不允许访问对象属性");
        }
        expressionOutputs_.append(i.key()); formulas.append("(" + formula + ")");
    }
    expressionEngine_ = std::make_unique<QJSEngine>();
    QString prelude = "const pi=Math.PI,e=Math.E,tau=2*Math.PI;const degrees=x=>x*180/Math.PI,radians=x=>x*Math.PI/180;";
    for (const auto &function : functions) prelude += "const " + function + "=Math." + function + ";";
    auto function = expressionEngine_->evaluate("(function(" + expressionInputs_.join(',') + "){" + prelude + "return [" + formulas.join(',') + "];})");
    if (function.isError() || !function.isCallable()) throw Error("数学表达式无效：" + function.toString());
    expression_ = std::make_unique<QJSValue>(function);
}

double MathNode::sourceInterval() const
{
    if (kind_ == "source.constant") return 0.;
    if (kind_ == "source.time") return config_.value("period_ms").toDouble() / 1000.;
    return -1.;
}

void MathNode::resume()
{
    Node::resume(); anchored_ = false;
}

void MathNode::pause() { anchored_ = false; }

void MathNode::configure(Node &prepared)
{
    // Preparation already succeeded before the live node adopts any edit.
    auto &candidate = dynamic_cast<MathNode &>(prepared);
    if (kind_ == "function.expression") {
        expression_ = std::move(candidate.expression_);
        expressionEngine_ = std::move(candidate.expressionEngine_);
        expressionInputs_ = candidate.expressionInputs_; expressionOutputs_ = candidate.expressionOutputs_;
    }
    if (config_.value("time_unit") != prepared.config().value("time_unit")) anchored_ = false;
    Node::configure(prepared);
}

NodeResult MathNode::process(const Values &changes, const BatchContext &context, bool)
{
    if (kind_ == "source.constant") {
        Values values;
        const auto constants = config_.value("values").toMap();
        for (auto i = constants.cbegin(); i != constants.cend(); ++i) values.insert(i.key(), i.value());
        return NodeResult::output(values);
    }
    if (kind_ == "source.time") {
        const double system = QDateTime::currentMSecsSinceEpoch() / 1000.;
        Values values;
        for (const auto &item : config_.value("outputs").toList()) {
            const auto output = item.toMap();
            values.insert(output.value("name").toString(), (output.value("clock") == "elapsed" ? context.time : system) / timeUnit(output.value("unit").toString()));
        }
        state_["elapsed_seconds"] = context.time; state_["system_seconds"] = system;
        state_["ticks"] = state_.value("ticks").toULongLong() + 1;
        return NodeResult::output(values);
    }
    if (kind_ == "function.expression") {
        QJSValueList args;
        for (const auto &input : expressionInputs_) args.append(number(input));
        const auto evaluated = expression_->call(args);
        if (evaluated.isError()) throw Error("公式计算失败：" + evaluated.toString());
        Values outputs; QVariantMap samples;
        for (qsizetype i = 0; i < expressionOutputs_.size(); ++i) {
            const double value = evaluated.property(static_cast<quint32>(i)).toNumber();
            if (!std::isfinite(value)) throw Error("公式结果不是有限数值：" + expressionOutputs_[i]);
            outputs.insert(expressionOutputs_[i], value); samples.insert(expressionOutputs_[i], value);
        }
        state_["values"] = samples;
        outputs.insert("samples", QVariantMap{{"time",context.time}, {"values",samples}});
        return NodeResult::output(outputs);
    }
    const auto kind = kind_.mid(9);
    double x = number("x"), result = 0.;
    if (kind == "integral" || kind == "derivative" || kind == "lowpass" || kind == "moving_average") {
        if (kind == "moving_average") {
            window_.push_back(x);
            while (window_.size() > static_cast<size_t>(config_.value("window").toInt())) window_.pop_front();
            value_ = std::accumulate(window_.begin(), window_.end(), 0.) / window_.size();
        } else {
            if (!connected_.contains("t")) {
                state_["waiting_for_time"] = true;
                return NodeResult::output({{"out",value_}});
            }
            if (!anchored_ && !changes.contains("t")) return NodeResult::output({{"out",value_}});
            const auto time = number("t") * timeUnit(config_.value("time_unit").toString());
            if (!anchored_) { previousTime_ = time; previousX_ = x; anchored_ = true; }
            else {
                const auto dt = time - previousTime_;
                if (dt < 0.) throw Error("输入时间回退，请检查时间源或手动重置");
                if (dt > 0.) {
                    if (kind == "integral") value_ += (config_.value("method") == "trapezoid" ? (previousX_ + x) * .5 : previousX_) * dt;
                    else if (kind == "derivative") value_ = (x - previousX_) / dt;
                    else value_ += -std::expm1(-2 * Pi * config_.value("cutoff_hz").toDouble() * dt) * (x - value_);
                    previousTime_ = time; previousX_ = x;
                }
            }
            state_["waiting_for_time"] = false;
        }
        result = value_; state_["samples"] = state_.value("samples").toULongLong() + 1;
        state_["previous_time"] = anchored_ ? QVariant(previousTime_) : QVariant{};
        state_["previous_x"] = anchored_ ? QVariant(previousX_) : QVariant{};
        state_["retained_samples"] = static_cast<qulonglong>(window_.size());
    } else if (kind == "random") {
        if (config_.value("distribution") == "uniform") result = std::uniform_real_distribution<double>(-1.,1.)(random_);
        else result = std::normal_distribution<double>(0.,1.)(random_);
    } else if (kind == "limit") {
        if (number("minimum") > number("maximum")) throw Error("限幅下界超过上界");
        result = std::clamp(x, number("minimum"), number("maximum"));
    } else if (kind == "linear_map") {
        const auto span = number("in_max") - number("in_min");
        if (span == 0.) throw Error("线性映射输入范围不能为零");
        result = number("out_min") + (x - number("in_min")) / span * (number("out_max") - number("out_min"));
    } else if (kind.endsWith("_wave") || kind == "ramp") {
        const auto t = number("t") * timeUnit(config_.value("time_unit").toString());
        if (kind == "ramp") result = number("slope") * t + number("offset");
        else {
            const auto phase = 2 * Pi * number("frequency") * t + number("phase");
            double wave;
            const auto cycle = phase / (2 * Pi) - std::floor(phase / (2 * Pi));
            if (kind == "sine_wave") wave = std::sin(phase);
            else if (kind == "square_wave") wave = cycle < config_.value("duty").toDouble() ? 1. : -1.;
            else if (kind == "triangle_wave") wave = 2 / Pi * std::asin(std::sin(phase));
            else wave = 2 * cycle - 1;
            result = number("amplitude") * wave + number("offset");
        }
    } else {
        static const QHash<QString, double(*)(double)> unary{
            {"sin",std::sin}, {"cos",std::cos}, {"tan",std::tan}, {"asin",std::asin}, {"acos",std::acos}, {"atan",std::atan},
            {"abs",std::fabs}, {"sqrt",std::sqrt}, {"exp",std::exp}, {"log",std::log}, {"log10",std::log10},
            {"floor",std::floor}, {"ceil",std::ceil}};
        if (unary.contains(kind)) result = unary.value(kind)(x);
        else if (kind == "negate") result = -x;
        else if (kind == "degrees") result = x * 180 / Pi;
        else if (kind == "radians") result = x * Pi / 180;
        else if (kind == "sign") result = (x > 0.) - (x < 0.);
        else {
            QVector<double> values;
            for (const auto &name : config_.value("inputs").toStringList()) values.append(number(name));
            if (values.size() < 2) throw Error("多输入运算至少需要两个输入");
            const double a = values[0], b = values[1];
            if (kind == "greater" || kind == "less" || kind == "equal") {
                const bool value = kind == "greater" ? a > b : kind == "less" ? a < b : a == b;
                state_["value"] = value;
                return NodeResult::output({{"out",value}});
            }
            if (kind == "add") result = std::accumulate(values.cbegin(), values.cend(), 0.);
            else if (kind == "multiply") result = std::accumulate(values.cbegin(), values.cend(), 1., std::multiplies<double>{});
            else if (kind == "minimum") result = *std::min_element(values.cbegin(), values.cend());
            else if (kind == "maximum") result = *std::max_element(values.cbegin(), values.cend());
            else if (kind == "subtract") result = a - b;
            else if (kind == "divide") { if (b == 0.) throw Error("除数为零"); result = a / b; }
            else if (kind == "power") result = std::pow(a,b);
            else if (kind == "atan2") result = std::atan2(a,b);
            else throw Error("未注册的数学运算：" + kind);
        }
    }
    if (!std::isfinite(result)) throw Error("计算结果不是有限数值：" + id_);
    state_["value"] = result;
    return NodeResult::output({{"out",result}});
}

NodeResult MathNode::action(const QString &name, const QVariantMap &args, const BatchContext &context, QVariant &result)
{
    if (name == "reset_state") { resetHistory(); random_.seed(config_.value("seed",0).toULongLong()); result = snapshot(true); return {}; }
    return Node::action(name,args,context,result);
}

QVariantMap MathNode::snapshot(bool detailed) const
{
    auto result = Node::snapshot(detailed);
    if (detailed) { QVariantList history; for (const auto x : window_) history.append(x); result["history"] = history; }
    return result;
}

} // namespace corig
