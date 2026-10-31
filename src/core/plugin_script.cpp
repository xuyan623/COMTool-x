#include "plugin_script.h"
#include "resource_policy.h"
#include <QJSEngine>
#include <QJSValue>
#include <condition_variable>
#include <thread>

namespace corig {
QVariant pluginScript(const PluginPackage &package, const QString &file, const QString &entry, const QVariantList &arguments)
{
    const auto source = QString::fromUtf8(package.file(file));
    QJSEngine engine;
    std::mutex mutex; std::condition_variable finished; bool complete = false;
    // Qt permits setInterrupted from another thread, including during loading.
    std::thread watchdog([&] {
        std::unique_lock guard(mutex);
        if (!finished.wait_for(guard,std::chrono::milliseconds(resourceLimit("callback_timeout_ms")),[&] { return complete; })) engine.setInterrupted(true);
    });
    QString error; QVariant returned;
    auto result = engine.evaluate(source,file);
    if (result.isError()) error = result.toString();
    else {
        const auto function = engine.globalObject().property(entry);
        if (!function.isCallable()) error = "脚本未提供函数："+entry;
        else {
            QJSValueList values; for (const auto &argument : arguments) values.append(engine.toScriptValue(argument));
            result = function.call(values);
            if (result.isError() || result.isUndefined()) error = result.isError() ? result.toString() : "脚本必须返回结果："+entry;
            else returned = result.toVariant();
        }
    }
    { std::lock_guard guard(mutex); complete = true; } finished.notify_one(); watchdog.join();
    if (engine.isInterrupted()) throw Error("插件脚本超过 callback_timeout_ms 额度："+file);
    if (!error.isEmpty()) throw Error(file+"："+error);
    return returned;
}
}
