/*
 * CCQDbClientInterface.cpp
 *
 * Comprehensive refactor of the database client:
 *
 *  P0 – DbConnectionGuard (RAII): eliminates all connection leaks
 *  P1 – getBaseTrivisionResultMapMultiDevice: one connection per device
 *  P2 – checkDbConnect: fixed via DbConnectionGuard
 *  P3 – SQL binding fix: ": taskId" → ":taskId", exec(sql) → exec()
 *  P4 – Parse helpers extracted (parseFullWorkpieceResult, etc.)
 *  P5 – Thread safety: UUID connection names + QMutexLocker on m_deviceParams
 */

#include "CCQDbClientInterface.h"

#include <QSqlError>
#include <QSqlRecord>
#include <QSqlQuery>
#include <QUuid>
#include <QThread>
#include <QtConcurrent/QtConcurrent>
#include <QFuture>
#include <QSettings>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QCoreApplication>
#include <QDir>

namespace UiDb {

// ─────────────────────────────────────────────────────────────────────────────
// DbConnectionGuard implementation
// ─────────────────────────────────────────────────────────────────────────────

DbConnectionGuard::DbConnectionGuard(const Device &device)
{
    // UUID connection name guarantees uniqueness across threads
    m_connName = QUuid::createUuid().toString(QUuid::WithoutBraces);

    m_db = QSqlDatabase::addDatabase(device.driverName, m_connName);
    m_db.setHostName(device.ip);
    m_db.setPort(device.port);
    m_db.setUserName(device.username);
    m_db.setPassword(device.password);
    m_db.setDatabaseName(device.dbName);

    m_valid = m_db.open();
}

DbConnectionGuard::~DbConnectionGuard()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Release m_db handle before removeDatabase to avoid Qt warning
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connName);
}

bool DbConnectionGuard::isOpen() const
{
    return m_valid && m_db.isOpen();
}

QSqlDatabase &DbConnectionGuard::db()
{
    return m_db;
}

QString DbConnectionGuard::connName() const
{
    return m_connName;
}

// ─────────────────────────────────────────────────────────────────────────────
// CCQDbClientInterface – singleton
// ─────────────────────────────────────────────────────────────────────────────

CCQDbClientInterface *CCQDbClientInterface::instance()
{
    static CCQDbClientInterface inst;
    return &inst;
}

CCQDbClientInterface::CCQDbClientInterface(QObject *parent)
    : QObject(parent)
{
    initParams();
}

CCQDbClientInterface::~CCQDbClientInterface() = default;

// ── Initialisation ────────────────────────────────────────────────────────────

void CCQDbClientInterface::initParams()
{
    m_configPath = QCoreApplication::applicationDirPath() + "/config";
}

void CCQDbClientInterface::initDatabaseConnections()
{
    QMutexLocker lk(&m_mutex);
    // Re-verify connectivity for every configured device and prune unreachable ones
    QList<int> toRemove;
    for (auto it = m_deviceParams.begin(); it != m_deviceParams.end(); ++it) {
        if (!checkDbConnect(it.value())) {
            toRemove.append(it.key());
        }
    }
    for (int id : toRemove) {
        m_deviceParams.remove(id);
    }
}

// P2 fix: use DbConnectionGuard so connection is always cleaned up
bool CCQDbClientInterface::checkDbConnect(const Device &device)
{
    DbConnectionGuard guard(device);
    return guard.isOpen();
    // guard destructor automatically calls close() + removeDatabase()
}

bool CCQDbClientInterface::connectLocalSql()
{
    QMutexLocker lk(&m_mutex);
    for (const Device &d : m_deviceParams) {
        if (isLocalIPAddress(d.ip)) {
            return checkDbConnect(d);
        }
    }
    return false;
}

QString CCQDbClientInterface::configPath() const
{
    return m_configPath;
}

QSqlDatabase CCQDbClientInterface::createDbConnection(
        const Device &device, const QString &connName)
{
    QSqlDatabase db = QSqlDatabase::addDatabase(device.driverName, connName);
    db.setHostName(device.ip);
    db.setPort(device.port);
    db.setUserName(device.username);
    db.setPassword(device.password);
    db.setDatabaseName(device.dbName);
    db.open();
    return db;
}

void CCQDbClientInterface::cleanupDatabaseConnection(const QString &connName)
{
    {
        QSqlDatabase db = QSqlDatabase::database(connName, false);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connName);
}

// ── Device information ────────────────────────────────────────────────────────

QMap<int, Device> CCQDbClientInterface::getDeviceMap() const
{
    QMutexLocker lk(&m_mutex);
    return m_deviceParams;
}

QList<int> CCQDbClientInterface::getDeviceIdlist() const
{
    QMutexLocker lk(&m_mutex);
    return m_deviceParams.keys();
}

QList<Device> CCQDbClientInterface::getDbConnectedDeviceList() const
{
    QMutexLocker lk(&m_mutex);
    QList<Device> result;
    for (const Device &d : m_deviceParams) {
        result.append(d);
    }
    return result;
}

bool CCQDbClientInterface::isLocalIPAddress(const QString &ip) const
{
    if (ip == QLatin1String("127.0.0.1") || ip == QLatin1String("localhost"))
        return true;
    const auto addrs = QNetworkInterface::allAddresses();
    for (const QHostAddress &addr : addrs) {
        if (addr.toString() == ip) return true;
    }
    return false;
}

void CCQDbClientInterface::setTestData(const QMap<int, Device> &deviceMap)
{
    QMutexLocker lk(&m_mutex);
    m_deviceParams = deviceMap;
}

// ─────────────────────────────────────────────────────────────────────────────
// Static parse helpers
// ─────────────────────────────────────────────────────────────────────────────

QString CCQDbClientInterface::cleanDateTimeString(const QString &raw)
{
    // Remove trailing fractional-second artefacts like ".000"
    QString s = raw.trimmed();
    int dot = s.lastIndexOf('.');
    if (dot != -1) {
        s = s.left(dot);
    }
    return s;
}

QString CCQDbClientInterface::quotedInList(const QStringList &items)
{
    QStringList quoted;
    quoted.reserve(items.size());
    for (const QString &item : items) {
        QString escaped = item;
        escaped.replace(QLatin1Char('\''), QLatin1String("''"));
        quoted.append(QLatin1Char('\'') + escaped + QLatin1Char('\''));
    }
    return quoted.join(QLatin1Char(','));
}

BaseWorkpieceInspectionResult
CCQDbClientInterface::parseFullWorkpieceResult(QSqlQuery &q)
{
    BaseWorkpieceInspectionResult r;
    r.taskId               = q.value("task_id").toInt();
    r.productCode          = q.value("product_code").toString().toStdString();
    r.workpieceId          = q.value("workpiece_id").toInt();
    r.sn                   = q.value("sn").toString().toStdString();
    r.comment              = q.value("comment").toString().toStdString();
    r.mouldNumber          = q.value("mould_number").toString().toStdString();
    r.defectCode           = q.value("defect_code").toString().toStdString();
    r.defectCode2          = q.value("defect_code2").toString().toStdString();
    r.isNg                 = q.value("is_ng").toInt();
    r.isSpotCheck          = q.value("is_spot_check").toInt();
    r.unloadingPort        = q.value("unloading_port").toInt();
    {
        QString s = q.value("classfied_datetime").toString();
        s.remove('"');
        s.replace('T', ' ');
        r.classfiedDatetime = CCDateTime::fromString(s.toStdString());
    }
    r.reinspectionOperator = q.value("reinspection_operator").toString().toStdString();
    r.reinspectionResult   = q.value("reinspection_result").toString().toStdString();
    {
        QString s = q.value("reinspection_time").toString();
        s.remove('"');
        s.replace('T', ' ');
        r.reinspectionTime = CCDateTime::fromString(s.toStdString());
    }
    r.finalResult          = q.value("final_result").toString().toStdString();
    r.sn1                  = q.value("sn1").toString().toStdString();
    r.sn2                  = q.value("sn2").toString().toStdString();
    r.sn3                  = q.value("sn3").toString().toStdString();
    r.sn4                  = q.value("sn4").toString().toStdString();
    r.sn5                  = q.value("sn5").toString().toStdString();
    r.sn6                  = q.value("sn6").toString().toStdString();
    r.sn7                  = q.value("sn7").toString().toStdString();
    r.sn8                  = q.value("sn8").toString().toStdString();
    r.sn9                  = q.value("sn9").toString().toStdString();
    r.sn10                 = q.value("sn10").toString().toStdString();
    r.description          = q.value("description").toString().toStdString();
    return r;
}

BaseWorkpieceInspectionResult1
CCQDbClientInterface::parseLiteWorkpieceResult(QSqlQuery &q)
{
    BaseWorkpieceInspectionResult1 r;
    r.taskId              = q.value("task_id").toInt();
    r.productCode         = q.value("product_code").toString().toStdString();
    r.workpieceId         = q.value("workpiece_id").toInt();
    r.sn                  = q.value("sn").toString().toStdString();
    r.defectCode          = q.value("defect_code").toString().toStdString();
    r.isNg                = q.value("is_ng").toInt();
    {
        QString s = q.value("classfied_datetime").toString();
        s.remove('"');
        s.replace('T', ' ');
        r.classfiedDatetime = CCDateTime::fromString(s.toStdString());
    }
    r.finalResult         = q.value("final_result").toString().toStdString();
    r.reinspectionResult  = q.value("reinspection_result").toString().toStdString();
    r.sn1                 = q.value("sn1").toString().toStdString();
    r.isRecheck           = !r.reinspectionResult.empty();
    return r;
}

BaseWorkpieceInspectionResult1
CCQDbClientInterface::parseLiteWorkpieceResult1(QSqlQuery &q)
{
    // Same as parseLiteWorkpieceResult but for the Result1 variant
    return parseLiteWorkpieceResult(q);
}

BaseTrivisionResult
CCQDbClientInterface::parseFullTrivisionResult(QSqlQuery &q)
{
    BaseTrivisionResult r;
    r.id                   = q.value("id").toInt();
    r.taskId               = q.value("task_id").toInt();
    r.productCode          = q.value("product_code").toString().toStdString();
    r.workpieceId          = q.value("workpiece_id").toInt();
    r.algImageId           = q.value("alg_image_id").toInt();
    r.dataType             = q.value("data_type").toInt();
    r.defectCode           = q.value("defect_code").toString().toStdString();
    r.score                = q.value("score").toFloat();
    r.pointX               = q.value("point_x").toInt();
    r.pointY               = q.value("point_y").toInt();
    r.pointZ               = q.value("point_z").toInt();
    r.width                = q.value("width").toInt();
    r.height               = q.value("height").toInt();
    r.depth                = q.value("depth").toInt();
    r.defectLength         = q.value("defect_length").toFloat();
    r.defectWidth          = q.value("defect_width").toFloat();
    r.defectArea           = q.value("defect_area").toFloat();
    r.realDefectLength     = q.value("real_defect_length").toFloat();
    r.realDefectWidth      = q.value("real_defect_width").toFloat();
    r.realDefectArea       = q.value("real_defect_area").toFloat();
    r.defectConfirm        = q.value("defect_confirm").toInt();
    r.defectPolygon        = q.value("defect_polygon").toString().toStdString();
    r.reinspectionResult   = q.value("reinspection_result").toInt();
    {
        QString s = q.value("reinspection_time").toString();
        s.remove('"');
        s.replace('T', ' ');
        r.reinspectionTime = CCDateTime::fromString(s.toStdString());
    }
    r.reinspectionOperator = q.value("reinspection_operator").toString().toStdString();
    r.finalResult          = q.value("final_result").toString().toStdString();
    return r;
}

BaseTrivisionResult
CCQDbClientInterface::parseLiteTrivisionResult(QSqlQuery &q)
{
    BaseTrivisionResult r;
    r.id          = q.value("id").toInt();
    r.taskId      = q.value("task_id").toInt();
    r.workpieceId = q.value("workpiece_id").toInt();
    r.algImageId  = q.value("alg_image_id").toInt();
    r.defectCode  = q.value("defect_code").toString().toStdString();
    r.score       = q.value("score").toFloat();
    r.defectConfirm      = q.value("defect_confirm").toInt();
    r.reinspectionResult = q.value("reinspection_result").toInt();
    r.finalResult        = q.value("final_result").toString().toStdString();
    return r;
}

BaseTask CCQDbClientInterface::parseBaseTask(QSqlQuery &q)
{
    BaseTask t;
    t.taskId          = q.value("task_id").toLongLong();
    t.productCodeList = q.value("product_code_list").toString().toStdString();
    t.comment         = q.value("comment").toString().toStdString();
    {
        QString s = q.value("start_time").toString();
        s.remove('"');
        s.replace('T', ' ');
        t.startTime = CCDateTime::fromString(s.toStdString());
    }
    {
        QString s = q.value("finish_time").toString();
        s.remove('"');
        s.replace('T', ' ');
        t.finishTime = CCDateTime::fromString(s.toStdString());
    }
    t.operatorName    = q.value("operator_name").toString().toStdString();
    t.operatorRole    = q.value("operator_role").toInt();
    t.isSpotCheck     = q.value("is_spot_check").toInt();
    t.scVersion       = q.value("sc_version").toString().toStdString();
    t.stVersion       = q.value("st_version").toString().toStdString();
    t.saVersion       = q.value("sa_version").toString().toStdString();
    t.soVersion       = q.value("so_version").toString().toStdString();
    t.trivisionVersion= q.value("trivision_version").toString().toStdString();
    t.modelVersion    = q.value("model_version").toString().toStdString();
    t.ruleVersion     = q.value("rule_version").toString().toStdString();
    t.description     = q.value("description").toString().toStdString();
    t.ctAnalysisName  = q.value("ct_analysis_name").toString().toStdString();
    return t;
}

BaseScAlarm CCQDbClientInterface::parseBaseScAlarm(QSqlQuery &q)
{
    BaseScAlarm a;
    a.id            = q.value("id").toInt();
    a.station_id    = q.value("station_id").toInt();
    a.alarm_id      = q.value("alarm_id").toInt();
    a.level         = q.value("level").toString().toStdString();
    a.module        = q.value("module").toString().toStdString();
    a.happened_time = QDateTime::fromString(
                          cleanDateTimeString(q.value("happened_time").toString()),
                          Qt::ISODate);
    a.is_false_alarm = q.value("is_false_alarm").toBool();
    a.alarm_content  = q.value("alarm_content").toString().toStdString();
    a.alarm_solution = q.value("alarm_solution").toString().toStdString();
    a.handled        = q.value("handled").toInt();
    a.handled_time   = QDateTime::fromString(
                           cleanDateTimeString(q.value("handled_time").toString()),
                           Qt::ISODate);
    a.operate_name   = q.value("operate_name").toString().toStdString();
    return a;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helper: look up a Device by id (caller must NOT hold m_mutex)
// ─────────────────────────────────────────────────────────────────────────────
static Device deviceById(const QMap<int,Device> &map, int deviceId, bool *ok = nullptr)
{
    auto it = map.find(deviceId);
    if (it == map.end()) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = true;
    return it.value();
}

// ─────────────────────────────────────────────────────────────────────────────
// Workpiece inspection result queries
// ─────────────────────────────────────────────────────────────────────────────

void CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskId(
        int taskId,
        std::function<void(QList<BaseWorkpieceInspectionResult>)> callback)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QtConcurrent::run([snapshot, taskId, callback]() {
        QList<BaseWorkpieceInspectionResult> all;
        for (const Device &device : snapshot) {
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QSqlQuery q(guard.db());
            q.prepare("SELECT * FROM base_workpiece_inspection_result "
                      "WHERE task_id = :taskId");
            q.bindValue(":taskId", taskId);
            if (q.exec()) {
                while (q.next()) {
                    all.append(CCQDbClientInterface::parseFullWorkpieceResult(q));
                }
            }
        }
        callback(all);
    });
}

void CCQDbClientInterface::getBaseWorkpieceInspectionResultBySn(
        const QString &sn,
        std::function<void(QList<BaseWorkpieceInspectionResult>)> callback)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QtConcurrent::run([snapshot, sn, callback]() {
        QList<BaseWorkpieceInspectionResult> all;
        for (const Device &device : snapshot) {
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QSqlQuery q(guard.db());
            q.prepare("SELECT * FROM base_workpiece_inspection_result "
                      "WHERE sn = :sn");
            q.bindValue(":sn", sn);
            if (q.exec()) {
                while (q.next()) {
                    all.append(CCQDbClientInterface::parseFullWorkpieceResult(q));
                }
            }
        }
        callback(all);
    });
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdIp(
        int taskId, const QString &ip)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        for (const Device &d : m_deviceParams) {
            if (d.ip == ip) { device = d; break; }
        }
    }
    if (!device.isValid()) return {};

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdIp(
        int taskId, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult1>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdIp(
        int taskId, int deviceId, bool /*lite*/)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT task_id, product_code, workpiece_id, sn, defect_code, "
              "is_ng, classfied_datetime, final_result, reinspection_result, sn1 "
              "FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<BaseWorkpieceInspectionResult1> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseLiteWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByWorkpieceIdIp(
        int workpieceId, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE workpiece_id = :wpId");
    q.bindValue(":wpId", workpieceId);

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdWorkpieceIdIp(
        int taskId, int workpieceId, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultBySnIp(
        const QString &sn, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE sn = :sn");
    q.bindValue(":sn", sn);

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultsForDeviceSince(
        int deviceId, const QDateTime &since)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE classfied_datetime >= :since");
    q.bindValue(":since", since.toString(Qt::ISODate));

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getAllProductInfoBySN(const QString &sn)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QList<BaseWorkpieceInspectionResult> result;
    for (const Device &device : snapshot) {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery q(guard.db());
        q.prepare("SELECT * FROM base_workpiece_inspection_result WHERE sn = :sn");
        q.bindValue(":sn", sn);
        if (q.exec()) {
            while (q.next()) {
                result.append(parseFullWorkpieceResult(q));
            }
        }
    }
    return result;
}

int CCQDbClientInterface::getBaseWorkpieceInspectionResultCount(
        int deviceId, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return 0;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return 0;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(*) FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(
        int deviceId, const QDateTime &dt)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE classfied_datetime = :dt");
    q.bindValue(":dt", dt.toString(Qt::ISODate));

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(
        int deviceId, const QDateTime &from, const QDateTime &to)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE classfied_datetime BETWEEN :from AND :to");
    q.bindValue(":from", from.toString(Qt::ISODate));
    q.bindValue(":to",   to.toString(Qt::ISODate));

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(
        int deviceId, int taskId,
        const QDateTime &from, const QDateTime &to)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId "
              "  AND classfied_datetime BETWEEN :from AND :to");
    q.bindValue(":taskId", taskId);
    q.bindValue(":from",   from.toString(Qt::ISODate));
    q.bindValue(":to",     to.toString(Qt::ISODate));

    QList<BaseWorkpieceInspectionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult1>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(
        int deviceId, int taskId,
        const QDateTime &from, const QDateTime &to, bool /*lite*/)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT task_id, product_code, workpiece_id, sn, defect_code, "
              "is_ng, classfied_datetime, final_result, reinspection_result, sn1 "
              "FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId "
              "  AND classfied_datetime BETWEEN :from AND :to");
    q.bindValue(":taskId", taskId);
    q.bindValue(":from",   from.toString(Qt::ISODate));
    q.bindValue(":to",     to.toString(Qt::ISODate));

    QList<BaseWorkpieceInspectionResult1> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseLiteWorkpieceResult(q));
        }
    }
    return result;
}

// P3 fix: ":taskId" (no space), and q2.exec() without argument
QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateRange(
        int deviceId,
        const QDateTime &from, const QDateTime &to,
        const QString   &task_id)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    // ── Main query ────────────────────────────────────────────────────────────
    QString sql = "SELECT * FROM base_workpiece_inspection_result "
                  "WHERE classfied_datetime BETWEEN :from AND :to";
    if (!task_id.isEmpty()) {
        // P3 fix: no space before "taskId"
        sql += " AND task_id = :taskId";
    }

    QSqlQuery q(guard.db());
    q.prepare(sql);
    q.bindValue(":from", from.toString(Qt::ISODate));
    q.bindValue(":to",   to.toString(Qt::ISODate));
    if (!task_id.isEmpty()) {
        // P3 fix: binding key matches exactly (no space)
        q.bindValue(":taskId", task_id);
    }

    QList<BaseWorkpieceInspectionResult> result;
    // P3 fix: exec() without argument so prepared bindings take effect
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullWorkpieceResult(q));
        }
    }

    // ── Re-inspection (recheck) sub-query ────────────────────────────────────
    QString reSql = "SELECT * FROM base_workpiece_inspection_result "
                    "WHERE reinspection_time BETWEEN :from AND :to";
    if (!task_id.isEmpty()) {
        // P3 fix: no space before "taskId"
        reSql += " AND task_id = :taskId";
    }

    QSqlQuery q2(guard.db());
    q2.prepare(reSql);
    q2.bindValue(":from", from.toString(Qt::ISODate));
    q2.bindValue(":to",   to.toString(Qt::ISODate));
    if (!task_id.isEmpty()) {
        // P3 fix: binding key matches exactly (no space)
        q2.bindValue(":taskId", task_id);
    }
    // P3 fix: exec() without argument
    if (q2.exec()) {
        while (q2.next()) {
            result.append(parseFullWorkpieceResult(q2));
        }
    }

    return result;
}

QList<int>
CCQDbClientInterface::getWorkpieceIdsByTaskIdIp(int taskId, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT DISTINCT workpiece_id FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<int> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(q.value(0).toInt());
        }
    }
    return result;
}

QList<QPair<int,int>>
CCQDbClientInterface::getWorkpieceIdsByTaskIdIp(
        int taskId, int deviceId, bool /*withAlgImageId*/)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT DISTINCT workpiece_id, alg_image_id "
              "FROM base_trivision_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<QPair<int,int>> result;
    if (q.exec()) {
        while (q.next()) {
            result.append({q.value(0).toInt(), q.value(1).toInt()});
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Task queries
// ─────────────────────────────────────────────────────────────────────────────

BaseTask CCQDbClientInterface::getTask(int taskId, int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_task WHERE task_id = :taskId LIMIT 1");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) {
        return parseBaseTask(q);
    }
    return {};
}

QList<BaseTask> CCQDbClientInterface::getAllTask(int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_task ORDER BY task_id DESC");

    QList<BaseTask> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseTask(q));
        }
    }
    return result;
}

QList<BaseTask> CCQDbClientInterface::getTaskByDeviceId(const std::string &ip)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        const QString qip = QString::fromStdString(ip);
        for (const Device &d : m_deviceParams) {
            if (d.ip == qip) { device = d; break; }
        }
    }
    if (!device.isValid()) return {};

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_task ORDER BY task_id DESC");

    QList<BaseTask> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseTask(q));
        }
    }
    return result;
}

QList<BaseTask> CCQDbClientInterface::getTaskByDeviceId(int device_id)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_task ORDER BY task_id DESC");

    QList<BaseTask> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseTask(q));
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Product map
// ─────────────────────────────────────────────────────────────────────────────

QMap<QString, QString> CCQDbClientInterface::getProductMap(int deviceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, deviceId, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT product_code, product_name FROM base_product");

    QMap<QString, QString> result;
    if (q.exec()) {
        while (q.next()) {
            result.insert(q.value(0).toString(), q.value(1).toString());
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Trivision result queries
// ─────────────────────────────────────────────────────────────────────────────

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }
    // No mutex held during DB access (P5: UUID connection is thread-local)

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        int device_id, int taskId, int workpieceId, int algImageId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId "
              "  AND alg_image_id = :algId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);
    q.bindValue(":algId",  algImageId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        int device_id, const QList<QPair<int,int>> &pairs)
{
    if (pairs.isEmpty()) return {};

    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QList<BaseTrivisionResult> result;
    for (const auto &p : pairs) {
        QSqlQuery q(guard.db());
        q.prepare("SELECT * FROM base_trivision_result "
                  "WHERE task_id = :taskId AND workpiece_id = :wpId");
        q.bindValue(":taskId", p.first);
        q.bindValue(":wpId",   p.second);
        if (q.exec()) {
            while (q.next()) {
                result.append(parseFullTrivisionResult(q));
            }
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        const QList<QPair<int,int>> &pairs)
{
    if (pairs.isEmpty()) return {};

    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QList<BaseTrivisionResult> result;
    for (const Device &device : snapshot) {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        for (const auto &p : pairs) {
            QSqlQuery q(guard.db());
            q.prepare("SELECT * FROM base_trivision_result "
                      "WHERE task_id = :taskId AND workpiece_id = :wpId");
            q.bindValue(":taskId", p.first);
            q.bindValue(":wpId",   p.second);
            if (q.exec()) {
                while (q.next()) {
                    result.append(parseFullTrivisionResult(q));
                }
            }
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(int device_id, int id)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result WHERE id = :id");
    q.bindValue(":id", id);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        int device_id, const QString &productCode)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE product_code = :pc");
    q.bindValue(":pc", productCode);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResult(
        const QList<QPair<int,int>> &pairs, int /*dummy*/)
{
    // Overload disambiguation – same logic as the pairs-only version
    return getBaseTrivisionResult(pairs);
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultLocal(
        int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        for (const Device &d : m_deviceParams) {
            if (isLocalIPAddress(d.ip)) { device = d; break; }
        }
    }
    if (!device.isValid()) return {};

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultResumeCapture(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultResumeCapture(
        int device_id, int taskId,
        int startWorkpieceId, int endWorkpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId "
              "  AND workpiece_id BETWEEN :start AND :end");
    q.bindValue(":taskId", taskId);
    q.bindValue(":start",  startWorkpieceId);
    q.bindValue(":end",    endWorkpieceId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultMulitDevice(
        const QList<QPair<int,int>> &pairs)
{
    return getBaseTrivisionResult(pairs); // delegates to multi-device version
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultAllDevice(
        int taskId, int workpieceId)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QList<BaseTrivisionResult> result;
    for (const Device &device : snapshot) {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery q(guard.db());
        q.prepare("SELECT * FROM base_trivision_result "
                  "WHERE task_id = :taskId AND workpiece_id = :wpId");
        q.bindValue(":taskId", taskId);
        q.bindValue(":wpId",   workpieceId);
        if (q.exec()) {
            while (q.next()) {
                result.append(parseFullTrivisionResult(q));
            }
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getBaseTrivisionResultAllDevice(
        const QList<QPair<int,int>> &pairs)
{
    return getBaseTrivisionResult(pairs);
}

QMap<QPair<int,int>, QList<BaseTrivisionResult>>
CCQDbClientInterface::getBaseTrivisionResultMap(
        int device_id, const QList<QPair<int,int>> &pairs)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QMap<QPair<int,int>, QList<BaseTrivisionResult>> result;
    for (const auto &p : pairs) {
        QSqlQuery q(guard.db());
        q.prepare("SELECT * FROM base_trivision_result "
                  "WHERE task_id = :taskId AND workpiece_id = :wpId");
        q.bindValue(":taskId", p.first);
        q.bindValue(":wpId",   p.second);
        if (q.exec()) {
            QList<BaseTrivisionResult> list;
            while (q.next()) {
                list.append(parseFullTrivisionResult(q));
            }
            result[p] = list;
        }
    }
    return result;
}

QMap<QPair<int,int>, QList<BaseTrivisionResult>>
CCQDbClientInterface::getBaseTrivisionResultMap1(
        int device_id, const QList<QPair<int,int>> &pairs)
{
    return getBaseTrivisionResultMap(device_id, pairs);
}

// P1 fix: one DbConnectionGuard per device, not one per (task,workpiece) pair
QMap<QPair<int,int>, QList<BaseTrivisionResult>>
CCQDbClientInterface::getBaseTrivisionResultMapMultiDevice(
        const QMap<int, QList<QPair<int,int>>> &devicePairs)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QMap<QPair<int,int>, QList<BaseTrivisionResult>> result;

    for (auto it = devicePairs.begin(); it != devicePairs.end(); ++it) {
        const int deviceId            = it.key();
        const QList<QPair<int,int>> &pairs = it.value();
        if (pairs.isEmpty()) continue;

        bool ok;
        const Device device = deviceById(snapshot, deviceId, &ok);
        if (!ok) continue;

        // One connection per device (P1: was one connection per pair before)
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        for (const auto &p : pairs) {
            QSqlQuery q(guard.db());
            q.prepare("SELECT * FROM base_trivision_result "
                      "WHERE task_id = :taskId AND workpiece_id = :wpId");
            q.bindValue(":taskId", p.first);
            q.bindValue(":wpId",   p.second);

            QList<BaseTrivisionResult> list;
            if (q.exec()) {
                while (q.next()) {
                    list.append(parseFullTrivisionResult(q));
                }
            }
            result[p] = list;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Deduplication
// ─────────────────────────────────────────────────────────────────────────────

QList<BaseTrivisionResult>
CCQDbClientInterface::getDeduplicationTrivisionResult(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_deduplication_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

bool CCQDbClientInterface::updateDeduplicationTrivisionResult(
        int device_id, int taskId, int workpieceId,
        const QList<BaseTrivisionResult> &results)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    guard.db().transaction();

    // Clear existing deduplication data
    QSqlQuery delQ(guard.db());
    delQ.prepare("DELETE FROM base_deduplication_trivision_result "
                 "WHERE task_id = :taskId AND workpiece_id = :wpId");
    delQ.bindValue(":taskId", taskId);
    delQ.bindValue(":wpId",   workpieceId);
    if (!delQ.exec()) {
        guard.db().rollback();
        return false;
    }

    // Insert new records
    for (const BaseTrivisionResult &r : results) {
        QSqlQuery insQ(guard.db());
        insQ.prepare(
            "INSERT INTO base_deduplication_trivision_result "
            "(task_id, workpiece_id, alg_image_id, defect_code, score, "
            " defect_confirm, reinspection_result, final_result) "
            "VALUES (:taskId, :wpId, :algId, :dc, :score, "
            "        :confirm, :reInsp, :final)");
        insQ.bindValue(":taskId",  r.taskId);
        insQ.bindValue(":wpId",    r.workpieceId);
        insQ.bindValue(":algId",   r.algImageId);
        insQ.bindValue(":dc",      QString::fromStdString(r.defectCode));
        insQ.bindValue(":score",   r.score);
        insQ.bindValue(":confirm", r.defectConfirm);
        insQ.bindValue(":reInsp",  r.reinspectionResult);
        insQ.bindValue(":final",   QString::fromStdString(r.finalResult));
        if (!insQ.exec()) {
            guard.db().rollback();
            return false;
        }
    }

    guard.db().commit();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistical counts
// ─────────────────────────────────────────────────────────────────────────────

int CCQDbClientInterface::getReviewImageCountByBaseTrivisionResult(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return 0;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return 0;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(DISTINCT alg_image_id) FROM base_trivision_result "
              "WHERE task_id = :taskId AND reinspection_result IS NOT NULL");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int CCQDbClientInterface::getTotalImageCountByBaseTrivisionResult(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return 0;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return 0;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(DISTINCT alg_image_id) FROM base_trivision_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int CCQDbClientInterface::getReviewDefectCountByBaseTrivisionResult(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return 0;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return 0;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(*) FROM base_trivision_result "
              "WHERE task_id = :taskId AND reinspection_result IS NOT NULL");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

int CCQDbClientInterface::getTotalDefectCountByBaseTrivisionResult(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return 0;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return 0;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(*) FROM base_trivision_result "
              "WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update operations
// ─────────────────────────────────────────────────────────────────────────────

bool CCQDbClientInterface::updateBaseWorkpieceInspectionResult(
        int device_id, const BaseWorkpieceInspectionResult &result)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_workpiece_inspection_result SET "
        "  reinspection_result   = :reResult, "
        "  reinspection_operator = :reOp, "
        "  reinspection_time     = :reTime, "
        "  final_result          = :finalResult "
        "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":reResult",    QString::fromStdString(result.reinspectionResult));
    q.bindValue(":reOp",        QString::fromStdString(result.reinspectionOperator));
    q.bindValue(":reTime",      result.reinspectionTime.toString(Qt::ISODate));
    q.bindValue(":finalResult", QString::fromStdString(result.finalResult));
    q.bindValue(":taskId",      result.taskId);
    q.bindValue(":wpId",        result.workpieceId);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseWorkpieceInspectionResult(
        int device_id, int taskId, int workpieceId,
        const QString &reinspectionResult,
        const QString &reinspectionOperator)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_workpiece_inspection_result SET "
        "  reinspection_result   = :reResult, "
        "  reinspection_operator = :reOp, "
        "  reinspection_time     = :reTime "
        "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":reResult", reinspectionResult);
    q.bindValue(":reOp",     reinspectionOperator);
    q.bindValue(":reTime",   QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":taskId",   taskId);
    q.bindValue(":wpId",     workpieceId);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseWorkpieceInpsectionResultByTrivisonData(
        int device_id, int taskId, int workpieceId)
{
    const auto trivisionList = getBaseTrivisionResult(device_id, taskId, workpieceId);
    if (trivisionList.isEmpty()) return true;

    // Determine aggregate final result from trivision data
    bool hasNg = false;
    for (const auto &t : trivisionList) {
        if (!t.finalResult.empty() && t.finalResult != "OK") {
            hasNg = true;
            break;
        }
    }

    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_workpiece_inspection_result SET "
        "  final_result = :finalResult "
        "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":finalResult", hasNg ? QStringLiteral("NG") : QStringLiteral("OK"));
    q.bindValue(":taskId",      taskId);
    q.bindValue(":wpId",        workpieceId);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseWorkpieceInpsectionResultByTrivisonData(
        int device_id, const BaseWorkpieceInspectionResult &result)
{
    return updateBaseWorkpieceInpsectionResultByTrivisonData(
               device_id, result.taskId, result.workpieceId);
}

bool CCQDbClientInterface::updateBaseTrivisionResult(
        int device_id, int id,
        int defectConfirm, int reinspectionResult,
        const QString &reinspectionOperator)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_trivision_result SET "
        "  defect_confirm        = :confirm, "
        "  reinspection_result   = :reResult, "
        "  reinspection_operator = :reOp, "
        "  reinspection_time     = :reTime "
        "WHERE id = :id");
    q.bindValue(":confirm",  defectConfirm);
    q.bindValue(":reResult", reinspectionResult);
    q.bindValue(":reOp",     reinspectionOperator);
    q.bindValue(":reTime",   QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":id",       id);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseTrivisionResult(
        int device_id, int taskId, int workpieceId,
        const QString &finalResult)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_trivision_result SET "
        "  final_result = :final "
        "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":final",  finalResult);
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseTrivisionResult(
        int device_id, int taskId, int workpieceId, int algImageId,
        int defectConfirm, int reinspectionResult,
        const QString &reinspectionOperator)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_trivision_result SET "
        "  defect_confirm        = :confirm, "
        "  reinspection_result   = :reResult, "
        "  reinspection_operator = :reOp, "
        "  reinspection_time     = :reTime "
        "WHERE task_id = :taskId AND workpiece_id = :wpId "
        "  AND alg_image_id = :algId");
    q.bindValue(":confirm",  defectConfirm);
    q.bindValue(":reResult", reinspectionResult);
    q.bindValue(":reOp",     reinspectionOperator);
    q.bindValue(":reTime",   QDateTime::currentDateTime().toString(Qt::ISODate));
    q.bindValue(":taskId",   taskId);
    q.bindValue(":wpId",     workpieceId);
    q.bindValue(":algId",    algImageId);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseTrivisionResult(
        int device_id, const BaseTrivisionResult &result)
{
    return updateBaseTrivisionResult(
               device_id,
               result.id,
               result.defectConfirm,
               result.reinspectionResult,
               QString::fromStdString(result.reinspectionOperator));
}

// ─────────────────────────────────────────────────────────────────────────────
// Alarm operations
// ─────────────────────────────────────────────────────────────────────────────

bool CCQDbClientInterface::insertBaseScAlarm(
        int device_id, const BaseScAlarm &alarm)
{
    return insertBaseScAlarm(
               device_id,
               alarm.station_id, alarm.alarm_id,
               QString::fromStdString(alarm.level),
               QString::fromStdString(alarm.module),
               alarm.happened_time,
               QString::fromStdString(alarm.alarm_content),
               QString::fromStdString(alarm.alarm_solution));
}

bool CCQDbClientInterface::insertBaseScAlarm(
        int device_id, int stationId, int alarmId,
        const QString &level, const QString &module,
        const QDateTime &happenedTime)
{
    return insertBaseScAlarm(device_id, stationId, alarmId, level, module,
                             happenedTime, QString(), QString());
}

bool CCQDbClientInterface::insertBaseScAlarm(
        int device_id, int stationId, int alarmId,
        const QString &level, const QString &module,
        const QDateTime &happenedTime,
        const QString &alarmContent,
        const QString &alarmSolution)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "INSERT INTO base_sc_alarm "
        "(station_id, alarm_id, level, module, happened_time, "
        " alarm_content, alarm_solution) "
        "VALUES (:stId, :alId, :level, :module, :hapTime, :content, :solution)");
    q.bindValue(":stId",     stationId);
    q.bindValue(":alId",     alarmId);
    q.bindValue(":level",    level);
    q.bindValue(":module",   module);
    q.bindValue(":hapTime",  happenedTime.toString(Qt::ISODate));
    q.bindValue(":content",  alarmContent);
    q.bindValue(":solution", alarmSolution);
    return q.exec();
}

bool CCQDbClientInterface::updateBaseScAlarm(
        int device_id, int id,
        bool handled, const QDateTime &handledTime,
        const QString &operatorName)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare(
        "UPDATE base_sc_alarm SET "
        "  handled       = :handled, "
        "  handled_time  = :handledTime, "
        "  operate_name  = :opName "
        "WHERE id = :id");
    q.bindValue(":handled",     handled ? 1 : 0);
    q.bindValue(":handledTime", handledTime.toString(Qt::ISODate));
    q.bindValue(":opName",      operatorName);
    q.bindValue(":id",          id);
    return q.exec();
}

QList<BaseScAlarm> CCQDbClientInterface::getAllBaseScAlarm(int device_id)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_sc_alarm ORDER BY happened_time DESC");

    QList<BaseScAlarm> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseScAlarm(q));
        }
    }
    return result;
}

QList<BaseScAlarm> CCQDbClientInterface::getBaseScAlarm(
        int device_id, bool handled)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_sc_alarm WHERE handled = :handled "
              "ORDER BY happened_time DESC");
    q.bindValue(":handled", handled ? 1 : 0);

    QList<BaseScAlarm> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseScAlarm(q));
        }
    }
    return result;
}

QList<BaseScAlarm> CCQDbClientInterface::getBaseScAlarm(
        int device_id, const QDateTime &from, const QDateTime &to)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_sc_alarm "
              "WHERE happened_time BETWEEN :from AND :to "
              "ORDER BY happened_time DESC");
    q.bindValue(":from", from.toString(Qt::ISODate));
    q.bindValue(":to",   to.toString(Qt::ISODate));

    QList<BaseScAlarm> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseBaseScAlarm(q));
        }
    }
    return result;
}

BaseScAlarm CCQDbClientInterface::getBaseScAlarmById(int device_id, int id)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_sc_alarm WHERE id = :id LIMIT 1");
    q.bindValue(":id", id);
    if (q.exec() && q.next()) {
        return parseBaseScAlarm(q);
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Miscellaneous
// ─────────────────────────────────────────────────────────────────────────────

bool CCQDbClientInterface::isTableUpdated(
        int device_id, const QString &tableName, const QDateTime &since)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    // Check information_schema for table update time (MySQL)
    QSqlQuery q(guard.db());
    q.prepare(
        "SELECT UPDATE_TIME FROM information_schema.TABLES "
        "WHERE TABLE_SCHEMA = :schema AND TABLE_NAME = :table");
    q.bindValue(":schema", device.dbName);
    q.bindValue(":table",  tableName);
    if (q.exec() && q.next()) {
        const QDateTime updateTime = QDateTime::fromString(
                                         cleanDateTimeString(q.value(0).toString()),
                                         Qt::ISODate);
        return updateTime >= since;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

void CCQDbClientInterface::copyTrivisionResults(
        QList<BaseTrivisionResult> &dst,
        const QList<BaseTrivisionResult> &src)
{
    dst.append(src);
}

bool CCQDbClientInterface::isDeduplicationDataExists(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare("SELECT COUNT(*) FROM base_deduplication_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

bool CCQDbClientInterface::clearDeduplicationResults(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return false;
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return false;

    QSqlQuery q(guard.db());
    q.prepare("DELETE FROM base_deduplication_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);
    return q.exec();
}

QString CCQDbClientInterface::getWorkpieceSN(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return QString();
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return QString();

    QSqlQuery q(guard.db());
    q.prepare("SELECT sn FROM base_workpiece_inspection_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId LIMIT 1");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return QString();
}

QList<QPair<int,int>>
CCQDbClientInterface::getLocalWorkpieceCombinations(int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT DISTINCT workpiece_id, alg_image_id "
              "FROM base_trivision_result WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);

    QList<QPair<int,int>> result;
    if (q.exec()) {
        while (q.next()) {
            result.append({q.value(0).toInt(), q.value(1).toInt()});
        }
    }
    return result;
}

QList<BaseWorkpieceInspectionResult>
CCQDbClientInterface::findReinspectionDataBySN(
        const QString &sn, int excludeDeviceId)
{
    QMap<int,Device> snapshot;
    {
        QMutexLocker lk(&m_mutex);
        snapshot = m_deviceParams;
    }

    QList<BaseWorkpieceInspectionResult> result;
    for (auto it = snapshot.begin(); it != snapshot.end(); ++it) {
        if (it.key() == excludeDeviceId) continue;

        DbConnectionGuard guard(it.value());
        if (!guard.isOpen()) continue;

        QSqlQuery q(guard.db());
        q.prepare("SELECT * FROM base_workpiece_inspection_result "
                  "WHERE sn = :sn AND reinspection_result IS NOT NULL");
        q.bindValue(":sn", sn);
        if (q.exec()) {
            while (q.next()) {
                result.append(parseFullWorkpieceResult(q));
            }
        }
    }
    return result;
}

QList<BaseTrivisionResult>
CCQDbClientInterface::getReinspectionTrivisionResults(
        int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare("SELECT * FROM base_trivision_result "
              "WHERE task_id = :taskId AND workpiece_id = :wpId "
              "  AND reinspection_result IS NOT NULL");
    q.bindValue(":taskId", taskId);
    q.bindValue(":wpId",   workpieceId);

    QList<BaseTrivisionResult> result;
    if (q.exec()) {
        while (q.next()) {
            result.append(parseFullTrivisionResult(q));
        }
    }
    return result;
}

QDateTime CCQDbClientInterface::getLastUpdateTimeFromDB(
        int device_id, const QString &tableName)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {};

    QSqlQuery q(guard.db());
    q.prepare(
        "SELECT UPDATE_TIME FROM information_schema.TABLES "
        "WHERE TABLE_SCHEMA = :schema AND TABLE_NAME = :table");
    q.bindValue(":schema", device.dbName);
    q.bindValue(":table",  tableName);
    if (q.exec() && q.next()) {
        return QDateTime::fromString(
                   cleanDateTimeString(q.value(0).toString()),
                   Qt::ISODate);
    }
    return {};
}

QPair<int,int> CCQDbClientInterface::getResumeCaptureTaskRange(
        int device_id, int taskId)
{
    Device device;
    {
        QMutexLocker lk(&m_mutex);
        bool ok; device = deviceById(m_deviceParams, device_id, &ok);
        if (!ok) return {0, 0};
    }

    DbConnectionGuard guard(device);
    if (!guard.isOpen()) return {0, 0};

    QSqlQuery q(guard.db());
    q.prepare("SELECT MIN(workpiece_id), MAX(workpiece_id) "
              "FROM base_trivision_result WHERE task_id = :taskId");
    q.bindValue(":taskId", taskId);
    if (q.exec() && q.next()) {
        return {q.value(0).toInt(), q.value(1).toInt()};
    }
    return {0, 0};
}

} // namespace UiDb
