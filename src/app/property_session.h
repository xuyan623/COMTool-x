#pragma once

// One editable target owns one draft, independent of QML Loader lifetime.
// Default forms, custom panels and automation commit through this same object.
#include <QObject>
#include <QVariant>

namespace corig {
class Controller;
class PropertySession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString scope READ scope CONSTANT)
    Q_PROPERTY(QString target READ target CONSTANT)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantMap schema READ schema NOTIFY changed)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantMap draft READ draft NOTIFY changed)
    Q_PROPERTY(QVariantMap textDraft READ textDraft NOTIFY changed)
    Q_PROPERTY(QVariantMap config READ config NOTIFY changed)
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY changed)
    Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    PropertySession(Controller &owner, QString scope, QString target);
    QString scope() const { return scope_; }
    QString target() const { return target_; }
    QVariantMap info() const;
    QVariantMap schema() const;
    QVariantMap values() const;
    QVariantMap draft() const;
    QVariantMap config() const;
    QVariantMap properties() const;
    QVariantMap state() const;
    QVariantMap textDraft() const { return texts_; }
    bool dirty() const { return !changes_.isEmpty() || !texts_.isEmpty(); }
    bool pending() const { return !job_.isEmpty(); }
    bool editable() const;
    QString error() const { return error_; }
    Q_INVOKABLE QVariant stage(const QVariantMap &changes);
    Q_INVOKABLE QVariant stageText(const QString &field, const QString &text);
    Q_INVOKABLE QVariant commit(const QVariantMap &changes = {}, bool confirmed = false, const QString &revision = {});
    Q_INVOKABLE void discard();
    Q_INVOKABLE QVariant action(const QString &name, const QVariantMap &arguments = {});
    Q_INVOKABLE QVariant configure(const QVariantMap &changes);
    Q_INVOKABLE QVariant invoke(const QString &operation, const QVariantMap &arguments = {});
    QVariant apply(bool confirmed, const QString &revision);
signals:
    void changed();
private:
    Controller &owner_;
    QString scope_, target_, job_, error_;
    QVariantMap changes_, base_, texts_;
};
}
