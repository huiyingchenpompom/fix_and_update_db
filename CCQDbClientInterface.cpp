#include "CCQDbClientInterface.h"
#include <QSettings>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QSqlError>
#include <QUuid>
#include <QDebug>
#include <QTimer>
#include <QElapsedTimer>
#include <QCoreApplication>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QtConcurrent>
#include <QThread>
#include <random>

// ==================== DbConnectionGuard 实现 ====================

DbConnectionGuard::DbConnectionGuard(const Device &device)
{
    m_connName = QString::fromStdString(device.ip) + "_" + QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase("QMYSQL", m_connName);
    m_db.setHostName(QString::fromStdString(device.ip));
    m_db.setPort(3306);
    m_db.setDatabaseName("sc_");
    m_db.setUserName("root");
    m_db.setPassword("Cube1234");
    m_db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=5");
    m_valid = m_db.open();
}

DbConnectionGuard::~DbConnectionGuard()
{
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Release the QSqlDatabase handle before calling removeDatabase to avoid Qt warning
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

// ==================== CCQDbClientInterface 实现 ====================

CCQDbClientInterface* CCQDbClientInterface::instance()
{
    static CCQDbClientInterface instance;
    return &instance;
}

CCQDbClientInterface::CCQDbClientInterface(QObject *parent)
    : QObject(parent)
{
    initParams();
}

CCQDbClientInterface::~CCQDbClientInterface()
{
    QMutexLocker lock(&m_mutex);
    m_deviceParams.clear();
}

bool CCQDbClientInterface::initDatabaseConnections(const std::map<int, Device> &deviceMap)
{
    bool allSuccess = true;
    auto map = deviceMap;
    m_deviceParams.clear();
    for (auto& pair : map) {
        int deviceId = pair.first;
        Device& device = pair.second;
        QString connName = QString::fromStdString(device.ip) + "_" + QUuid::createUuid().toString();

        QElapsedTimer timer;
        timer.start();
        {
            QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connName);
            db.setHostName(QString::fromStdString(device.ip));
            db.setPort(3306);
            db.setDatabaseName("sc_");
            db.setUserName("root");
            db.setPassword("Cube1234");
            db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=5");

            if (!db.open()) {
                pair.second.success_connect = false;
                m_deviceParams[deviceId] = pair.second;
                allSuccess = false;
                qDebug() << "sql connect failed: IP=" << QString::fromStdString(device.ip);
            } else {
                pair.second.success_connect = true;
                m_deviceParams[deviceId] = pair.second;
                qDebug() << "sql connect success: IP=" << QString::fromStdString(device.ip)
                         << " DB=" << "sc_";
            }

            db.close();
        }
        if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
            QSqlDatabase::removeDatabase(connName);
        }
    }

    return allSuccess;
}

// 修复：原来 return true 会导致 removeDatabase 永远不被调用（连接泄漏）
// 改用 DbConnectionGuard 自动管理
bool CCQDbClientInterface::checkDbConnect(QString ip)
{
    Device device;
    device.ip = ip.toStdString();
    DbConnectionGuard guard(device);
    return guard.isOpen();
}

bool CCQDbClientInterface::connectLocalSql()
{
    QMutexLocker lock(&m_mutex);
    for (auto& pair : m_deviceParams) {
        if (pair.second.local_device) {
            return checkDbConnect(QString::fromStdString(pair.second.ip));
        }
    }
    return false;
}

QString CCQDbClientInterface::configPath() const
{
    return QCoreApplication::applicationDirPath() + "/config";
}

std::map<int, Device> CCQDbClientInterface::getDeviceMap()
{
    QMutexLocker lock(&m_mutex);
    return m_deviceParams;
}

QStringList CCQDbClientInterface::getDeviceIdlist()
{
    QMutexLocker lock(&m_mutex);
    QStringList list;
    for (auto& pair : m_deviceParams) {
        list << QString::number(pair.first);
    }
    return list;
}

QList<Device> CCQDbClientInterface::getDbConnectedDeviceList()
{
    QMutexLocker lock(&m_mutex);
    QList<Device> list;
    for (auto& pair : m_deviceParams) {
        if (pair.second.success_connect) {
            list << pair.second;
        }
    }
    return list;
}

QSqlDatabase CCQDbClientInterface::createDbConnection(const Device &device, QString &connName)
{
    connName = QString::fromStdString(device.ip) + "_" + QUuid::createUuid().toString();
    QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL", connName);
    db.setHostName(QString::fromStdString(device.ip));
    db.setPort(3306);
    db.setDatabaseName("sc_");
    db.setUserName("root");
    db.setPassword("Cube1234");
    db.setConnectOptions("MYSQL_OPT_RECONNECT=1;MYSQL_OPT_CONNECT_TIMEOUT=5");
    db.open();
    return db;
}

void CCQDbClientInterface::cleanupDatabaseConnection(QSqlDatabase &db, const QString &connName)
{
    if (db.isOpen()) {
        db.close();
    }
    if (!connName.isEmpty() && QSqlDatabase::contains(connName)) {
        QSqlDatabase::removeDatabase(connName);
    }
}

// ==================== 解析辅助函数 ====================

static QString parseDateTimeStr(const QString &raw)
{
    QString s = raw;
    s.remove('"');
    s.replace('T', ' ');
    int dot = s.lastIndexOf('.');
    if (dot != -1) s = s.left(dot);
    return s;
}

UiDb::BaseWorkpieceInspectionResult CCQDbClientInterface::parseFullWorkpieceResult(QSqlQuery &query)
{
    UiDb::BaseWorkpieceInspectionResult r;
    r.taskId               = query.value("task_id").toInt();
    r.productCode          = query.value("product_code").toString().toStdString();
    r.workpieceId          = query.value("workpiece_id").toInt();
    r.sn                   = query.value("sn").toString().toStdString();
    r.comment              = query.value("comment").toString().toStdString();
    r.mouldNumber          = query.value("mould_number").toString().toStdString();
    r.defectCode           = query.value("defect_code").toString().toStdString();
    r.defectCode2          = query.value("defect_code2").toString().toStdString();
    r.isNg                 = query.value("is_ng").toInt();
    r.isSpotCheck          = query.value("is_spot_check").toInt();
    r.unloadingPort        = query.value("unloading_port").toInt();
    r.classfiedDatetime    = QDateTime::fromString(parseDateTimeStr(query.value("classfied_datetime").toString()), "yyyy-MM-dd HH:mm:ss");
    r.reinspectionOperator = query.value("reinspection_operator").toString().toStdString();
    r.reinspectionResult   = query.value("reinspection_result").toString().toStdString();
    r.reinspectionTime     = QDateTime::fromString(parseDateTimeStr(query.value("reinspection_time").toString()), "yyyy-MM-dd HH:mm:ss");
    r.finalResult          = query.value("final_result").toString().toStdString();
    r.sn1                  = query.value("sn1").toString().toStdString();
    r.sn2                  = query.value("sn2").toString().toStdString();
    r.sn3                  = query.value("sn3").toString().toStdString();
    r.sn4                  = query.value("sn4").toString().toStdString();
    r.sn5                  = query.value("sn5").toString().toStdString();
    r.sn6                  = query.value("sn6").toString().toStdString();
    r.sn7                  = query.value("sn7").toString().toStdString();
    r.sn8                  = query.value("sn8").toString().toStdString();
    r.sn9                  = query.value("sn9").toString().toStdString();
    r.sn10                 = query.value("sn10").toString().toStdString();
    r.description          = query.value("description").toString().toStdString();
    return r;
}

UiDb::BaseWorkpieceInspectionResult CCQDbClientInterface::parseLiteWorkpieceResult(QSqlQuery &query)
{
    UiDb::BaseWorkpieceInspectionResult r;
    r.taskId      = query.value("task_id").toInt();
    r.productCode = query.value("product_code").toString().toStdString();
    r.workpieceId = query.value("workpiece_id").toInt();
    r.sn          = query.value("sn").toString().toStdString();
    r.defectCode  = query.value("defect_code").toString().toStdString();
    r.isNg        = query.value("is_ng").toInt();
    r.classfiedDatetime = QDateTime::fromString(parseDateTimeStr(query.value("classfied_datetime").toString()), "yyyy-MM-dd HH:mm:ss");
    r.finalResult        = query.value("final_result").toString().toStdString();
    r.reinspectionResult = query.value("reinspection_result").toString().toStdString();
    r.sn1         = query.value("sn1").toString().toStdString();
    return r;
}

UiDb::BaseWorkpieceInspectionResult1 CCQDbClientInterface::parseLiteWorkpieceResult1(QSqlQuery &query)
{
    UiDb::BaseWorkpieceInspectionResult1 r;
    r.taskId      = query.value("task_id").toInt();
    r.productCode = query.value("product_code").toString().toStdString();
    r.workpieceId = query.value("workpiece_id").toInt();
    r.sn          = query.value("sn").toString().toStdString();
    r.defectCode  = query.value("defect_code").toString().toStdString();
    r.isNg        = query.value("is_ng").toInt();
    r.classfiedDatetime = QDateTime::fromString(parseDateTimeStr(query.value("classfied_datetime").toString()), "yyyy-MM-dd HH:mm:ss");
    r.finalResult        = query.value("final_result").toString().toStdString();
    r.reinspectionResult = query.value("reinspection_result").toString().toStdString();
    r.sn1         = query.value("sn1").toString().toStdString();
    r.isRecheck   = !r.reinspectionResult.empty();
    return r;
}

UiDb::BaseTrivisionResult CCQDbClientInterface::parseFullTrivisionResult(QSqlQuery &query)
{
    UiDb::BaseTrivisionResult r;
    r.id                   = query.value("id").toInt();
    r.taskId               = query.value("task_id").toInt();
    r.productCode          = query.value("product_code").toString().toStdString();
    r.workpieceId          = query.value("workpiece_id").toInt();
    r.algImageId           = query.value("alg_image_id").toInt();
    r.dataType             = query.value("data_type").toInt();
    r.defectCode           = query.value("defect_code").toString().toStdString();
    r.score                = query.value("score").toFloat();
    r.pointX               = query.value("point_x").toInt();
    r.pointY               = query.value("point_y").toInt();
    r.pointZ               = query.value("point_z").toInt();
    r.width                = query.value("width").toInt();
    r.height               = query.value("height").toInt();
    r.depth                = query.value("depth").toInt();
    r.defectLength         = query.value("defect_length").toFloat();
    r.defectWidth          = query.value("defect_width").toFloat();
    r.defectArea           = query.value("defect_area").toFloat();
    r.realDefectLength     = query.value("real_defect_length").toFloat();
    r.realDefectWidth      = query.value("real_defect_width").toFloat();
    r.realDefectArea       = query.value("real_defect_area").toFloat();
    r.defectConfirm        = query.value("defect_confirm").toInt();
    r.defectPolygon        = query.value("defect_polygon").toString().toStdString();
    r.reinspectionResult   = query.value("reinspection_result").toInt();
    r.reinspectionTime     = QDateTime::fromString(parseDateTimeStr(query.value("reinspection_time").toString()), "yyyy-MM-dd HH:mm:ss");
    r.reinspectionOperator = query.value("reinspection_operator").toString().toStdString();
    r.finalResult          = query.value("final_result").toString().toStdString();
    return r;
}

UiDb::BaseTrivisionResult CCQDbClientInterface::parseLiteTrivisionResult(QSqlQuery &query)
{
    UiDb::BaseTrivisionResult r;
    r.id                 = query.value("id").toInt();
    r.taskId             = query.value("task_id").toInt();
    r.workpieceId        = query.value("workpiece_id").toInt();
    r.algImageId         = query.value("alg_image_id").toInt();
    r.defectCode         = query.value("defect_code").toString().toStdString();
    r.score              = query.value("score").toFloat();
    r.defectConfirm      = query.value("defect_confirm").toInt();
    r.reinspectionResult = query.value("reinspection_result").toInt();
    r.finalResult        = query.value("final_result").toString().toStdString();
    return r;
}

UiDb::BaseTask CCQDbClientInterface::parseBaseTask(QSqlQuery &query)
{
    UiDb::BaseTask t;
    t.taskId           = query.value("task_id").toLongLong();
    t.productCodeList  = query.value("product_code_list").toString().toStdString();
    t.comment          = query.value("comment").toString().toStdString();
    t.startTime        = QDateTime::fromString(parseDateTimeStr(query.value("start_time").toString()), "yyyy-MM-dd HH:mm:ss");
    t.finishTime       = QDateTime::fromString(parseDateTimeStr(query.value("finish_time").toString()), "yyyy-MM-dd HH:mm:ss");
    t.operatorName     = query.value("operator_name").toString().toStdString();
    t.operatorRole     = query.value("operator_role").toInt();
    t.isSpotCheck      = query.value("is_spot_check").toInt();
    t.scVersion        = query.value("sc_version").toString().toStdString();
    t.stVersion        = query.value("st_version").toString().toStdString();
    t.saVersion        = query.value("sa_version").toString().toStdString();
    t.soVersion        = query.value("so_version").toString().toStdString();
    t.trivisionVersion = query.value("trivision_version").toString().toStdString();
    t.modelVersion     = query.value("model_version").toString().toStdString();
    t.ruleVersion      = query.value("rule_version").toString().toStdString();
    t.description      = query.value("description").toString().toStdString();
    t.ctAnalysisName   = query.value("ct_analysis_name").toString().toStdString();
    return t;
}

UiDb::BaseTask CCQDbClientInterface::parseUiBaseTask(QSqlQuery &query)
{
    return parseBaseTask(query);
}

UiDb::BaseScAlarm CCQDbClientInterface::parseBaseScAlarm(QSqlQuery &query)
{
    UiDb::BaseScAlarm a;
    a.id             = query.value("id").toInt();
    a.station_id     = query.value("station_id").toInt();
    a.alarm_id       = query.value("alarm_id").toInt();
    a.level          = query.value("level").toString().toStdString();
    a.module         = query.value("module").toString().toStdString();
    a.happened_time  = QDateTime::fromString(parseDateTimeStr(query.value("happened_time").toString()), "yyyy-MM-dd HH:mm:ss");
    a.is_false_alarm = query.value("is_false_alarm").toBool();
    a.alarm_content  = query.value("alarm_content").toString().toStdString();
    a.alarm_solution = query.value("alarm_solution").toString().toStdString();
    a.handled        = query.value("handled").toInt();
    a.handled_time   = QDateTime::fromString(parseDateTimeStr(query.value("handled_time").toString()), "yyyy-MM-dd HH:mm:ss");
    a.operate_name   = query.value("operate_name").toString().toStdString();
    return a;
}

QString CCQDbClientInterface::quotedInList(const QStringList &items)
{
    QStringList quoted;
    quoted.reserve(items.size());
    for (const QString &item : items) {
        QString escaped = item;
        escaped.replace('\'', "''");
        quoted.append('\'' + escaped + '\'');
    }
    return quoted.join(',');
}

// ==================== 工件检测结果查询 ====================

void CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskId(
    int beginTaskId, int endTaskId,
    std::function<void(std::vector<UiDb::BaseWorkpieceInspectionResult>)> callback)
{
    // 为线程安全，先拷贝设备参数快照
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    QtConcurrent::run([snapshot, beginTaskId, endTaskId, callback]() {
        std::vector<UiDb::BaseWorkpieceInspectionResult> all;
        for (auto& pair : snapshot) {
            const Device &device = pair.second;
            // 替换 createDbConnection + 手动清理 为 DbConnectionGuard
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE task_id >= :beginTaskId AND task_id <= :endTaskId");
            query.bindValue(":beginTaskId", beginTaskId);
            query.bindValue(":endTaskId", endTaskId);
            if (query.exec()) {
                while (query.next()) {
                    all.push_back(CCQDbClientInterface::parseFullWorkpieceResult(query));
                }
            }
        }
        callback(all);
    });
}

void CCQDbClientInterface::getBaseWorkpieceInspectionResultBySn(
    const std::string &sn,
    std::function<void(std::vector<UiDb::BaseWorkpieceInspectionResult>)> callback)
{
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    QtConcurrent::run([snapshot, sn, callback]() {
        std::vector<UiDb::BaseWorkpieceInspectionResult> all;
        for (auto& pair : snapshot) {
            const Device &device = pair.second;
            // 替换 createDbConnection + 手动清理 为 DbConnectionGuard
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE sn = :sn");
            query.bindValue(":sn", QString::fromStdString(sn));
            if (query.exec()) {
                while (query.next()) {
                    all.push_back(CCQDbClientInterface::parseFullWorkpieceResult(query));
                }
            }
        }
        callback(all);
    });
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdIp(int device_id, int beginTaskId, int endTaskId)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE task_id >= :beginTaskId AND task_id <= :endTaskId");
        query.bindValue(":beginTaskId", beginTaskId);
        query.bindValue(":endTaskId", endTaskId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdIp(int device_id, int beginTaskId, int endTaskId, QString productCode)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE task_id >= :beginTaskId AND task_id <= :endTaskId AND product_code = :productCode");
        query.bindValue(":beginTaskId", beginTaskId);
        query.bindValue(":endTaskId", endTaskId);
        query.bindValue(":productCode", productCode);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByWorkpieceIdIp(int device_id, int startWorkpieceId, int endWorkpieceId)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE workpiece_id >= :startWorkpieceId AND workpiece_id <= :endWorkpieceId");
        query.bindValue(":startWorkpieceId", startWorkpieceId);
        query.bindValue(":endWorkpieceId", endWorkpieceId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByTaskIdWorkpieceIdIp(int device_id, int taskId, int workpieceId)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultBySnIp(const std::string &ip, const std::string &sn)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        for (auto& pair : m_deviceParams) {
            if (pair.second.ip == ip) {
                device = pair.second;
                break;
            }
        }
    }
    if (device.ip.empty()) return results;

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE sn = :sn");
        query.bindValue(":sn", QString::fromStdString(sn));
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

QList<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultsForDeviceSince(int device_id, QDateTime &time)
{
    QList<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE classfied_datetime >= :since");
        query.bindValue(":since", time.toString("yyyy-MM-dd HH:mm:ss"));
        if (query.exec()) {
            while (query.next()) {
                results.append(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

QStringList CCQDbClientInterface::getAllProductInfoBySN(const QString &sn)
{
    QStringList results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE sn = :sn");
        query.bindValue(":sn", sn);
        if (query.exec()) {
            while (query.next()) {
                results << query.value("product_code").toString();
            }
        }
    }
    return results;
}

int CCQDbClientInterface::getBaseWorkpieceInspectionResultCount(int device_id)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return 0;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return 0;

        QSqlQuery query(guard.db());
        query.prepare("SELECT COUNT(*) FROM base_workpiece_inspection_result");
        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
    }
    return 0;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(int device_id, QDateTime &start, QDateTime &end)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE classfied_datetime BETWEEN :start AND :end");
        query.bindValue(":start", start.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":end", end.toString("yyyy-MM-dd HH:mm:ss"));
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(QDateTime &start, QDateTime &end)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE classfied_datetime BETWEEN :start AND :end");
        query.bindValue(":start", start.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":end", end.toString("yyyy-MM-dd HH:mm:ss"));
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(const QString &productCode, QDateTime &start, QDateTime &end)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE product_code = :productCode AND classfied_datetime BETWEEN :start AND :end");
        query.bindValue(":productCode", productCode);
        query.bindValue(":start", start.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":end", end.toString("yyyy-MM-dd HH:mm:ss"));
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult> CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateTime(int device_id, const QString &productCode, QDateTime &start, QDateTime &end)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_workpiece_inspection_result WHERE product_code = :productCode AND classfied_datetime BETWEEN :start AND :end");
        query.bindValue(":productCode", productCode);
        query.bindValue(":start", start.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":end", end.toString("yyyy-MM-dd HH:mm:ss"));
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullWorkpieceResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseWorkpieceInspectionResult1> CCQDbClientInterface::getBaseWorkpieceInspectionResultByDateRange(
    const QStringList &deviceIdList,
    const QStringList &productCodes,
    const QDateTime &beginDate,
    const QDateTime &endDate,
    QMap<int, QSet<int>> &firstInspectInfo,
    const bool &fullTrace,
    int task_id)
{
    std::vector<UiDb::BaseWorkpieceInspectionResult1> results;

    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (const QString &deviceIdStr : deviceIdList) {
        int deviceId = deviceIdStr.toInt();
        auto it = snapshot.find(deviceId);
        if (it == snapshot.end()) continue;
        const Device &device = it->second;

        {
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QString sql = "SELECT task_id, product_code, workpiece_id, sn, defect_code, is_ng, classfied_datetime, final_result, reinspection_result, sn1"
                          " FROM base_workpiece_inspection_result WHERE classfied_datetime BETWEEN :beginDate AND :endDate";
            if (!productCodes.isEmpty()) {
                sql += QString(" AND product_code IN (%1)").arg(quotedInList(productCodes));
            }
            // 修复：原来是 ": taskId"（冒号后有空格），绑定会失败，已修正为 ":taskId"
            if (task_id >= 0) {
                sql += QString(" AND task_id = :taskId");
            }

            QSqlQuery q(guard.db());
            q.prepare(sql);
            q.bindValue(":beginDate", beginDate.toString("yyyy-MM-dd HH:mm:ss"));
            q.bindValue(":endDate", endDate.toString("yyyy-MM-dd HH:mm:ss"));
            if (task_id >= 0) {
                q.bindValue(":taskId", task_id);
            }

            if (q.exec()) {
                while (q.next()) {
                    UiDb::BaseWorkpieceInspectionResult1 r = parseLiteWorkpieceResult1(q);
                    if (firstInspectInfo.contains(deviceId)) {
                        r.isRecheck = firstInspectInfo[deviceId].contains(r.workpieceId);
                    }
                    results.push_back(r);
                }
            }

            if (fullTrace) {
                // 复检查询：修复 SQL 绑定错误（原来是 ": taskId" 有空格）
                QString reSql = "SELECT task_id, product_code, workpiece_id, sn, defect_code, is_ng, classfied_datetime, final_result, reinspection_result, sn1"
                                " FROM base_workpiece_inspection_result WHERE reinspection_time BETWEEN :beginDate AND :endDate";
                if (!productCodes.isEmpty()) {
                    reSql += QString(" AND product_code IN (%1)").arg(quotedInList(productCodes));
                }
                // 修复：原来是 ": taskId"（冒号后有空格），绑定会失败，已修正为 ":taskId"
                if (task_id >= 0) {
                    reSql += QString(" AND task_id = :taskId");
                }

                QSqlQuery q2(guard.db());
                q2.prepare(reSql);
                q2.bindValue(":beginDate", beginDate.toString("yyyy-MM-dd HH:mm:ss"));
                q2.bindValue(":endDate", endDate.toString("yyyy-MM-dd HH:mm:ss"));
                if (task_id >= 0) {
                    q2.bindValue(":taskId", task_id);
                }

                if (q2.exec()) {
                    while (q2.next()) {
                        UiDb::BaseWorkpieceInspectionResult1 r = parseLiteWorkpieceResult1(q2);
                        r.isRecheck = true;
                        results.push_back(r);
                    }
                }
            }
        }
    }
    return results;
}

QMap<int, QList<int>> CCQDbClientInterface::getWorkpieceIdsByTaskIdIp(int device_id, int beginTaskId, int endTaskId)
{
    QMap<int, QList<int>> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT task_id, workpiece_id FROM base_workpiece_inspection_result WHERE task_id >= :beginTaskId AND task_id <= :endTaskId");
        query.bindValue(":beginTaskId", beginTaskId);
        query.bindValue(":endTaskId", endTaskId);
        if (query.exec()) {
            while (query.next()) {
                int taskId = query.value("task_id").toInt();
                int workpieceId = query.value("workpiece_id").toInt();
                results[taskId].append(workpieceId);
            }
        }
    }
    return results;
}

QMap<int, QList<int>> CCQDbClientInterface::getWorkpieceIdsByTaskIdIp(int device_id, int beginTaskId, int endTaskId, const QString &productCode)
{
    QMap<int, QList<int>> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT task_id, workpiece_id FROM base_workpiece_inspection_result WHERE task_id >= :beginTaskId AND task_id <= :endTaskId AND product_code = :productCode");
        query.bindValue(":beginTaskId", beginTaskId);
        query.bindValue(":endTaskId", endTaskId);
        query.bindValue(":productCode", productCode);
        if (query.exec()) {
            while (query.next()) {
                int taskId = query.value("task_id").toInt();
                int workpieceId = query.value("workpiece_id").toInt();
                results[taskId].append(workpieceId);
            }
        }
    }
    return results;
}

// ==================== 任务查询 ====================

QMap<int, UiDb::BaseTask> CCQDbClientInterface::getTask(const QStringList &deviceIdList, const QDate &beginDate, const QDate &endDate)
{
    QMap<int, UiDb::BaseTask> results;

    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (const QString &deviceIdStr : deviceIdList) {
        int deviceId = deviceIdStr.toInt();
        auto it = snapshot.find(deviceId);
        if (it == snapshot.end()) continue;
        const Device &device = it->second;

        // 替换 do { ... } while(false) 手动连接模式 为 DbConnectionGuard
        {
            DbConnectionGuard guard(device);
            if (!guard.isOpen()) continue;

            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_task WHERE DATE(start_time) BETWEEN :beginDate AND :endDate");
            query.bindValue(":beginDate", beginDate.toString("yyyy-MM-dd"));
            query.bindValue(":endDate", endDate.toString("yyyy-MM-dd"));
            if (query.exec()) {
                while (query.next()) {
                    UiDb::BaseTask task = parseUiBaseTask(query);
                    results[(int)task.taskId] = task;
                }
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTask> CCQDbClientInterface::getAllTask()
{
    std::vector<UiDb::BaseTask> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_task ORDER BY task_id DESC");
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseTask(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTask> CCQDbClientInterface::getTaskByDeviceId(const std::string &ip)
{
    std::vector<UiDb::BaseTask> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        for (auto& pair : m_deviceParams) {
            if (pair.second.ip == ip) {
                device = pair.second;
                break;
            }
        }
    }
    if (device.ip.empty()) return results;

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_task ORDER BY task_id DESC");
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseTask(query));
            }
        }
    }
    return results;
}

std::vector<std::pair<int, QStringList>> CCQDbClientInterface::getTaskByDeviceId(int device_id)
{
    std::vector<std::pair<int, QStringList>> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT task_id, product_code_list FROM base_task ORDER BY task_id DESC");
        if (query.exec()) {
            while (query.next()) {
                int taskId = query.value("task_id").toInt();
                QString productCodeListStr = query.value("product_code_list").toString();
                QStringList productCodeList = productCodeListStr.split(',', Qt::SkipEmptyParts);
                results.push_back({taskId, productCodeList});
            }
        }
    }
    return results;
}

// ==================== 产品映射 ====================

std::vector<UiDb::CCProductMap> CCQDbClientInterface::getProductMap(int device_id)
{
    std::vector<UiDb::CCProductMap> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT product_code, product_name FROM base_product");
        if (query.exec()) {
            while (query.next()) {
                UiDb::CCProductMap pm;
                pm.productCode = query.value("product_code").toString().toStdString();
                pm.productName = query.value("product_name").toString().toStdString();
                results.push_back(pm);
            }
        }
    }
    return results;
}

// ==================== 推理结果查询 ====================

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResult(int device_id, int taskId, int workpieceId)
{
    std::vector<UiDb::BaseTrivisionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResult(int device_id, int taskId, int workpieceId, int algImageId)
{
    std::vector<UiDb::BaseTrivisionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algImageId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        query.bindValue(":algImageId", algImageId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResult(int device_id, const std::vector<std::pair<int, int>> &pairs)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    if (pairs.empty()) return results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        for (const auto &p : pairs) {
            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
            query.bindValue(":taskId", p.first);
            query.bindValue(":workpieceId", p.second);
            if (query.exec()) {
                while (query.next()) {
                    results.push_back(parseFullTrivisionResult(query));
                }
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResult(const std::vector<std::pair<int, int>> &pairs)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    if (pairs.empty()) return results;

    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        for (const auto &p : pairs) {
            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
            query.bindValue(":taskId", p.first);
            query.bindValue(":workpieceId", p.second);
            if (query.exec()) {
                while (query.next()) {
                    results.push_back(parseFullTrivisionResult(query));
                }
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultLocal(const std::vector<std::pair<int, int>> &pairs)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    if (pairs.empty()) return results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        for (auto& pair : m_deviceParams) {
            if (pair.second.local_device) {
                device = pair.second;
                break;
            }
        }
    }
    if (device.ip.empty()) return results;

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        for (const auto &p : pairs) {
            QSqlQuery query(guard.db());
            query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
            query.bindValue(":taskId", p.first);
            query.bindValue(":workpieceId", p.second);
            if (query.exec()) {
                while (query.next()) {
                    results.push_back(parseFullTrivisionResult(query));
                }
            }
        }
    }
    return results;
}

UiDb::BaseTrivisionResult CCQDbClientInterface::getBaseTrivisionResult(int device_id, int id)
{
    UiDb::BaseTrivisionResult result;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return result;
        device = it->second;
    }

    // 原来只有 QSqlDatabase::removeDatabase(connName)，没有完整清理，改用 guard
    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return result;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE id = :id");
        query.bindValue(":id", id);
        if (query.exec() && query.next()) {
            result = parseFullTrivisionResult(query);
        }
    }
    return result;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResult(int device_id, const QString &productCode)
{
    std::vector<UiDb::BaseTrivisionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE product_code = :productCode");
        query.bindValue(":productCode", productCode);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultResumeCapture(int device_id, int taskId, int workpieceId, const QString &sn)
{
    std::vector<UiDb::BaseTrivisionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND sn = :sn");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        query.bindValue(":sn", sn);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultResumeCapture(int device_id, int taskId, int workpieceId, int algImageId, const QString &sn)
{
    std::vector<UiDb::BaseTrivisionResult> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algImageId AND sn = :sn");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        query.bindValue(":algImageId", algImageId);
        query.bindValue(":sn", sn);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultMulitDevice(int device_id, int taskId, int workpieceId)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultAllDevice(int taskId, int workpieceId, int algImageId, const QString &defect_code)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algImageId AND defect_code = :defectCode");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        query.bindValue(":algImageId", algImageId);
        query.bindValue(":defectCode", defect_code);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getBaseTrivisionResultAllDevice(int taskId, int workpieceId, int algImageId, int id)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algImageId AND id = :id");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        query.bindValue(":algImageId", algImageId);
        query.bindValue(":id", id);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseFullTrivisionResult(query));
            }
        }
    }
    return results;
}

QMap<QString, std::vector<UiDb::BaseTrivisionResult>> CCQDbClientInterface::getBaseTrivisionResultMap(int device_id)
{
    QMap<QString, std::vector<UiDb::BaseTrivisionResult>> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result");
        if (query.exec()) {
            while (query.next()) {
                UiDb::BaseTrivisionResult r = parseFullTrivisionResult(query);
                QString key = QString("%1_%2").arg(r.taskId).arg(r.workpieceId);
                results[key].push_back(r);
            }
        }
    }
    return results;
}

QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> CCQDbClientInterface::getBaseTrivisionResultMap1(int device_id)
{
    QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result");
        if (query.exec()) {
            while (query.next()) {
                UiDb::BaseTrivisionResult r = parseFullTrivisionResult(query);
                QString outerKey = QString("%1_%2").arg(r.taskId).arg(r.workpieceId);
                QString innerKey = QString::fromStdString(r.defectCode);
                results[outerKey][innerKey].push_back(r);
            }
        }
    }
    return results;
}

QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> CCQDbClientInterface::getBaseTrivisionResultMapMultiDevice(int device_id)
{
    QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        // 每个设备只建立一次连接（原来是每个 (task,workpiece) 对一次连接）
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_trivision_result");
        if (query.exec()) {
            while (query.next()) {
                UiDb::BaseTrivisionResult r = parseFullTrivisionResult(query);
                QString outerKey = QString("%1_%2").arg(r.taskId).arg(r.workpieceId);
                QString innerKey = QString::fromStdString(r.defectCode);
                results[outerKey][innerKey].push_back(r);
            }
        }
    }
    return results;
}

// ==================== 去重推理结果 ====================

std::vector<UiDb::BaseTrivisionResult> CCQDbClientInterface::getDeduplicationTrivisionResult(
    const QStringList &deviceIdList, const QStringList &productCodes,
    const std::vector<std::pair<int, int>> &pairs, const QString &defectName, bool searchDeduplicationTable)
{
    std::vector<UiDb::BaseTrivisionResult> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    QString tableName = searchDeduplicationTable ? "base_deduplication_trivision_result" : "base_trivision_result";

    for (const QString &deviceIdStr : deviceIdList) {
        int deviceId = deviceIdStr.toInt();
        auto it = snapshot.find(deviceId);
        if (it == snapshot.end()) continue;
        const Device &device = it->second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        for (const auto &p : pairs) {
            QString sql = QString("SELECT * FROM %1 WHERE task_id = :taskId AND workpiece_id = :workpieceId").arg(tableName);
            if (!defectName.isEmpty()) {
                sql += " AND defect_code = :defectCode";
            }
            if (!productCodes.isEmpty()) {
                sql += QString(" AND product_code IN (%1)").arg(quotedInList(productCodes));
            }

            QSqlQuery query(guard.db());
            query.prepare(sql);
            query.bindValue(":taskId", p.first);
            query.bindValue(":workpieceId", p.second);
            if (!defectName.isEmpty()) {
                query.bindValue(":defectCode", defectName);
            }
            if (query.exec()) {
                while (query.next()) {
                    results.push_back(parseFullTrivisionResult(query));
                }
            }
        }
    }
    return results;
}

void CCQDbClientInterface::updateDeduplicationTrivisionResult(int device_id, int taskId, int workpieceId, int algid)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    // 替换原来的 createDbConnection + sleep + removeDatabase 模式 为 DbConnectionGuard
    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        // 先获取源数据
        QSqlQuery srcQuery(guard.db());
        srcQuery.prepare("SELECT * FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algid");
        srcQuery.bindValue(":taskId", taskId);
        srcQuery.bindValue(":workpieceId", workpieceId);
        srcQuery.bindValue(":algid", algid);

        if (!srcQuery.exec()) return;

        // 清理目标表中旧数据
        QSqlQuery delQuery(guard.db());
        delQuery.prepare("DELETE FROM base_deduplication_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algid");
        delQuery.bindValue(":taskId", taskId);
        delQuery.bindValue(":workpieceId", workpieceId);
        delQuery.bindValue(":algid", algid);
        delQuery.exec();

        int copiedCount = 0;
        while (srcQuery.next()) {
            UiDb::BaseTrivisionResult r = parseFullTrivisionResult(srcQuery);
            QSqlQuery insQuery(guard.db());
            insQuery.prepare("INSERT INTO base_deduplication_trivision_result "
                             "(task_id, workpiece_id, alg_image_id, product_code, defect_code, score, "
                             " point_x, point_y, point_z, width, height, depth, "
                             " defect_length, defect_width, defect_area, defect_confirm, defect_polygon, "
                             " reinspection_result, final_result) "
                             "VALUES (:taskId, :workpieceId, :algImageId, :productCode, :defectCode, :score, "
                             " :pointX, :pointY, :pointZ, :width, :height, :depth, "
                             " :defectLength, :defectWidth, :defectArea, :defectConfirm, :defectPolygon, "
                             " :reinspectionResult, :finalResult)");
            insQuery.bindValue(":taskId", r.taskId);
            insQuery.bindValue(":workpieceId", r.workpieceId);
            insQuery.bindValue(":algImageId", r.algImageId);
            insQuery.bindValue(":productCode", QString::fromStdString(r.productCode));
            insQuery.bindValue(":defectCode", QString::fromStdString(r.defectCode));
            insQuery.bindValue(":score", r.score);
            insQuery.bindValue(":pointX", r.pointX);
            insQuery.bindValue(":pointY", r.pointY);
            insQuery.bindValue(":pointZ", r.pointZ);
            insQuery.bindValue(":width", r.width);
            insQuery.bindValue(":height", r.height);
            insQuery.bindValue(":depth", r.depth);
            insQuery.bindValue(":defectLength", r.defectLength);
            insQuery.bindValue(":defectWidth", r.defectWidth);
            insQuery.bindValue(":defectArea", r.defectArea);
            insQuery.bindValue(":defectConfirm", r.defectConfirm);
            insQuery.bindValue(":defectPolygon", QString::fromStdString(r.defectPolygon));
            insQuery.bindValue(":reinspectionResult", r.reinspectionResult);
            insQuery.bindValue(":finalResult", QString::fromStdString(r.finalResult));
            if (insQuery.exec()) copiedCount++;
        }
        qDebug() << "updateDeduplicationTrivisionResult: copied" << copiedCount << "records";
    }
}

// ==================== 统计计数 ====================

int CCQDbClientInterface::getReviewImageCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return 0;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return 0;

        QSqlQuery query(guard.db());
        query.prepare("SELECT COUNT(DISTINCT alg_image_id) FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND reinspection_result IS NOT NULL");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
    }
    return 0;
}

int CCQDbClientInterface::getTotalImageCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return 0;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return 0;

        QSqlQuery query(guard.db());
        query.prepare("SELECT COUNT(DISTINCT alg_image_id) FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
    }
    return 0;
}

int CCQDbClientInterface::getReviewDefectCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return 0;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return 0;

        QSqlQuery query(guard.db());
        query.prepare("SELECT COUNT(*) FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND reinspection_result IS NOT NULL");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
    }
    return 0;
}

int CCQDbClientInterface::getTotalDefectCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return 0;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return 0;

        QSqlQuery query(guard.db());
        query.prepare("SELECT COUNT(*) FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec() && query.next()) {
            return query.value(0).toInt();
        }
    }
    return 0;
}

// ==================== 更新操作 ====================

void CCQDbClientInterface::updateBaseWorkpieceInspectionResult(int device_id, const UiDb::BaseWorkpieceInspectionResult &result)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_workpiece_inspection_result SET "
                      "reinspection_operator = :reinspectionOperator, "
                      "reinspection_result = :reinspectionResult, "
                      "reinspection_time = :reinspectionTime, "
                      "final_result = :finalResult "
                      "WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":reinspectionOperator", QString::fromStdString(result.reinspectionOperator));
        query.bindValue(":reinspectionResult", QString::fromStdString(result.reinspectionResult));
        query.bindValue(":reinspectionTime", result.reinspectionTime.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":finalResult", QString::fromStdString(result.finalResult));
        query.bindValue(":taskId", result.taskId);
        query.bindValue(":workpieceId", result.workpieceId);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseWorkpieceInspectionResult(int device_id, int task_id, int workpiece_id, const QString &sn)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_workpiece_inspection_result SET sn = :sn WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":sn", sn);
        query.bindValue(":taskId", task_id);
        query.bindValue(":workpieceId", workpiece_id);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseWorkpieceInpsectionResultByTrivisonData(int device_id, int task_id, int workpiece_id, const QString &sn)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        // 根据推理结果更新工件检测结果中的复检字段
        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_workpiece_inspection_result SET "
                      "final_result = (SELECT MAX(final_result) FROM base_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId), "
                      "sn = :sn "
                      "WHERE task_id = :taskId AND workpiece_id = :workpieceId");
        query.bindValue(":taskId", task_id);
        query.bindValue(":workpieceId", workpiece_id);
        query.bindValue(":sn", sn);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseWorkpieceInpsectionResultByTrivisonData(int device_id, int id, const QString &sn)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_workpiece_inspection_result SET sn = :sn WHERE id = :id");
        query.bindValue(":sn", sn);
        query.bindValue(":id", id);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseTrivisionResult(int device_id, const UiDb::BaseTrivisionResult &result)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_trivision_result SET "
                      "defect_confirm = :defectConfirm, "
                      "reinspection_result = :reinspectionResult, "
                      "reinspection_time = :reinspectionTime, "
                      "reinspection_operator = :reinspectionOperator, "
                      "final_result = :finalResult "
                      "WHERE id = :id");
        query.bindValue(":defectConfirm", result.defectConfirm);
        query.bindValue(":reinspectionResult", result.reinspectionResult);
        query.bindValue(":reinspectionTime", result.reinspectionTime.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":reinspectionOperator", QString::fromStdString(result.reinspectionOperator));
        query.bindValue(":finalResult", QString::fromStdString(result.finalResult));
        query.bindValue(":id", result.id);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseTrivisionResult(int device_id, int task_id, int workpiece_id, int alg_image_id, int reinspection_result, const QString &sn)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_trivision_result SET reinspection_result = :reinspectionResult WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algImageId AND sn = :sn");
        query.bindValue(":reinspectionResult", reinspection_result);
        query.bindValue(":taskId", task_id);
        query.bindValue(":workpieceId", workpiece_id);
        query.bindValue(":algImageId", alg_image_id);
        query.bindValue(":sn", sn);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseTrivisionResult(int device_id, int task_id, int workpiece_id, int reinspection_result, const QString &sn)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_trivision_result SET reinspection_result = :reinspectionResult WHERE task_id = :taskId AND workpiece_id = :workpieceId AND sn = :sn");
        query.bindValue(":reinspectionResult", reinspection_result);
        query.bindValue(":taskId", task_id);
        query.bindValue(":workpieceId", workpiece_id);
        query.bindValue(":sn", sn);
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseTrivisionResult(int device_id, int id, int reinspection_result)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_trivision_result SET reinspection_result = :reinspectionResult WHERE id = :id");
        query.bindValue(":reinspectionResult", reinspection_result);
        query.bindValue(":id", id);
        query.exec();
    }
}

// ==================== 报警相关 ====================

void CCQDbClientInterface::insertBaseScAlarm(int device_id, const UiDb::BaseScAlarm &result)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("INSERT INTO base_sc_alarm (station_id, alarm_id, level, module, happened_time, is_false_alarm, alarm_content, alarm_solution) "
                      "VALUES (:stationId, :alarmId, :level, :module, :happenedTime, :isFalseAlarm, :alarmContent, :alarmSolution)");
        query.bindValue(":stationId", result.station_id);
        query.bindValue(":alarmId", result.alarm_id);
        query.bindValue(":level", QString::fromStdString(result.level));
        query.bindValue(":module", QString::fromStdString(result.module));
        query.bindValue(":happenedTime", result.happened_time.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":isFalseAlarm", result.is_false_alarm ? 1 : 0);
        query.bindValue(":alarmContent", QString::fromStdString(result.alarm_content));
        query.bindValue(":alarmSolution", QString::fromStdString(result.alarm_solution));
        query.exec();
    }
}

void CCQDbClientInterface::insertBaseScAlarm(QMap<int, QList<UiDb::BaseScAlarm>> allData)
{
    for (auto it = allData.begin(); it != allData.end(); ++it) {
        for (const auto &alarm : it.value()) {
            insertBaseScAlarm(it.key(), alarm);
        }
    }
}

void CCQDbClientInterface::insertLocalBaseScAlarm(const UiDb::BaseScAlarm &result)
{
    Device localDevice;
    {
        QMutexLocker lock(&m_mutex);
        localDevice = m_localDevice;
    }
    if (localDevice.ip.empty()) return;

    {
        DbConnectionGuard guard(localDevice);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("INSERT INTO base_sc_alarm (station_id, alarm_id, level, module, happened_time, is_false_alarm, alarm_content, alarm_solution) "
                      "VALUES (:stationId, :alarmId, :level, :module, :happenedTime, :isFalseAlarm, :alarmContent, :alarmSolution)");
        query.bindValue(":stationId", result.station_id);
        query.bindValue(":alarmId", result.alarm_id);
        query.bindValue(":level", QString::fromStdString(result.level));
        query.bindValue(":module", QString::fromStdString(result.module));
        query.bindValue(":happenedTime", result.happened_time.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":isFalseAlarm", result.is_false_alarm ? 1 : 0);
        query.bindValue(":alarmContent", QString::fromStdString(result.alarm_content));
        query.bindValue(":alarmSolution", QString::fromStdString(result.alarm_solution));
        query.exec();
    }
}

void CCQDbClientInterface::updateBaseScAlarm(int device_id, const UiDb::BaseScAlarm &result)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return;

        QSqlQuery query(guard.db());
        query.prepare("UPDATE base_sc_alarm SET "
                      "handled = :handled, "
                      "handled_time = :handledTime, "
                      "operate_name = :operateName "
                      "WHERE id = :id");
        query.bindValue(":handled", result.handled);
        query.bindValue(":handledTime", result.handled_time.toString("yyyy-MM-dd HH:mm:ss"));
        query.bindValue(":operateName", QString::fromStdString(result.operate_name));
        query.bindValue(":id", result.id);
        query.exec();
    }
}

std::vector<UiDb::BaseScAlarm> CCQDbClientInterface::getAllBaseScAlarm()
{
    std::vector<UiDb::BaseScAlarm> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_sc_alarm ORDER BY happened_time DESC");
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseScAlarm(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseScAlarm> CCQDbClientInterface::getBaseScAlarm(int device_id)
{
    std::vector<UiDb::BaseScAlarm> results;

    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return results;
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return results;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_sc_alarm ORDER BY happened_time DESC");
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseScAlarm(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseScAlarm> CCQDbClientInterface::getBaseScAlarm(
    const QStringList &deviceIdList, const QDate &beginDate, const QDate &endDate,
    QString level, QString module, QString falseAlarm)
{
    std::vector<UiDb::BaseScAlarm> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (const QString &deviceIdStr : deviceIdList) {
        int deviceId = deviceIdStr.toInt();
        auto it = snapshot.find(deviceId);
        if (it == snapshot.end()) continue;
        const Device &device = it->second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QString sql = "SELECT * FROM base_sc_alarm WHERE DATE(happened_time) BETWEEN :beginDate AND :endDate";
        if (!level.isEmpty()) sql += " AND level = :level";
        if (!module.isEmpty()) sql += " AND module = :module";
        if (!falseAlarm.isEmpty()) sql += " AND is_false_alarm = :isFalseAlarm";

        QSqlQuery query(guard.db());
        query.prepare(sql);
        query.bindValue(":beginDate", beginDate.toString("yyyy-MM-dd"));
        query.bindValue(":endDate", endDate.toString("yyyy-MM-dd"));
        if (!level.isEmpty()) query.bindValue(":level", level);
        if (!module.isEmpty()) query.bindValue(":module", module);
        if (!falseAlarm.isEmpty()) query.bindValue(":isFalseAlarm", falseAlarm == "1" ? 1 : 0);

        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseScAlarm(query));
            }
        }
    }
    return results;
}

std::vector<UiDb::BaseScAlarm> CCQDbClientInterface::getBaseScAlarmById(int id, int pre_count, int back_count)
{
    std::vector<UiDb::BaseScAlarm> results;
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (auto& pair : snapshot) {
        const Device &device = pair.second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT * FROM base_sc_alarm WHERE id BETWEEN :minId AND :maxId ORDER BY id");
        query.bindValue(":minId", id - pre_count);
        query.bindValue(":maxId", id + back_count);
        if (query.exec()) {
            while (query.next()) {
                results.push_back(parseBaseScAlarm(query));
            }
        }
    }
    return results;
}

// ==================== 表更新检测 ====================

bool CCQDbClientInterface::isTableUpdated(const QStringList &deviceIdList)
{
    std::map<int, Device> snapshot;
    {
        QMutexLocker lock(&m_mutex);
        snapshot = m_deviceParams;
    }

    for (const QString &deviceIdStr : deviceIdList) {
        int deviceId = deviceIdStr.toInt();
        auto it = snapshot.find(deviceId);
        if (it == snapshot.end()) continue;
        const Device &device = it->second;

        DbConnectionGuard guard(device);
        if (!guard.isOpen()) continue;

        QSqlQuery query(guard.db());
        query.prepare("SELECT UPDATE_TIME FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'sc_' AND TABLE_NAME = 'base_workpiece_inspection_result'");
        if (query.exec() && query.next()) {
            QDateTime updateTime = QDateTime::fromString(query.value(0).toString(), "yyyy-MM-dd HH:mm:ss");
            if (updateTime > m_lastUpdateTime) {
                m_lastUpdateTime = updateTime;
                return true;
            }
        }
    }
    return false;
}

// ==================== 测试 ====================

void CCQDbClientInterface::setTestData()
{
    // 测试数据初始化（可根据需要添加）
}

// ==================== 私有辅助函数 ====================

void CCQDbClientInterface::initParams()
{
    m_connect_local_db = false;
    m_searchTaskCount = 10;
}

bool CCQDbClientInterface::isLocalIPAddress(const QString &ipAddress)
{
    if (ipAddress == "127.0.0.1" || ipAddress == "localhost") return true;
    QList<QHostAddress> addressList = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addressList) {
        if (address.toString() == ipAddress) return true;
    }
    return false;
}

bool CCQDbClientInterface::copyTrivisionResults(QSqlQuery &query, const QString &sourceTable, const QString &targetTable, int taskId, int workpieceId, int algid, int &copiedCount)
{
    Q_UNUSED(query)
    Q_UNUSED(sourceTable)
    Q_UNUSED(targetTable)
    Q_UNUSED(taskId)
    Q_UNUSED(workpieceId)
    Q_UNUSED(algid)
    copiedCount = 0;
    // 保留此接口以兼容原始头文件声明。
    // 实际的去重数据复制逻辑已合并至 updateDeduplicationTrivisionResult 中，
    // 该函数直接在 DbConnectionGuard guard 下执行删除+插入操作。
    return false;
}

bool CCQDbClientInterface::isDeduplicationDataExists(QSqlDatabase &db, int taskId, int workpieceId, int algid)
{
    QSqlQuery query(db);
    query.prepare("SELECT COUNT(*) FROM base_deduplication_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algid");
    query.bindValue(":taskId", taskId);
    query.bindValue(":workpieceId", workpieceId);
    query.bindValue(":algid", algid);
    if (query.exec() && query.next()) {
        return query.value(0).toInt() > 0;
    }
    return false;
}

bool CCQDbClientInterface::clearDeduplicationResults(QSqlDatabase &db, int taskId, int workpieceId, int algid)
{
    QSqlQuery query(db);
    query.prepare("DELETE FROM base_deduplication_trivision_result WHERE task_id = :taskId AND workpiece_id = :workpieceId AND alg_image_id = :algid");
    query.bindValue(":taskId", taskId);
    query.bindValue(":workpieceId", workpieceId);
    query.bindValue(":algid", algid);
    return query.exec();
}

QString CCQDbClientInterface::getWorkpieceSN(int device_id, int taskId, int workpieceId)
{
    Device device;
    {
        QMutexLocker lock(&m_mutex);
        auto it = m_deviceParams.find(device_id);
        if (it == m_deviceParams.end()) return QString();
        device = it->second;
    }

    {
        DbConnectionGuard guard(device);
        if (!guard.isOpen()) return QString();

        QSqlQuery query(guard.db());
        query.prepare("SELECT sn FROM base_workpiece_inspection_result WHERE task_id = :taskId AND workpiece_id = :workpieceId LIMIT 1");
        query.bindValue(":taskId", taskId);
        query.bindValue(":workpieceId", workpieceId);
        if (query.exec() && query.next()) {
            return query.value(0).toString();
        }
    }
    return QString();
}

QDateTime CCQDbClientInterface::getLastUpdateTimeFromDB(const QString &connName, QSqlDatabase &db)
{
    QSqlQuery query(db);
    query.prepare("SELECT UPDATE_TIME FROM information_schema.TABLES WHERE TABLE_SCHEMA = 'sc_' AND TABLE_NAME = 'base_workpiece_inspection_result'");
    if (query.exec() && query.next()) {
        return QDateTime::fromString(query.value(0).toString(), "yyyy-MM-dd HH:mm:ss");
    }
    return QDateTime();
}

QVector<int> CCQDbClientInterface::getResumeCaptureTaskRange(QSqlDatabase &db, int taskId, const QString &sn)
{
    QVector<int> range;
    QSqlQuery query(db);
    query.prepare("SELECT DISTINCT task_id FROM base_trivision_result WHERE sn = :sn ORDER BY task_id");
    query.bindValue(":sn", sn);
    if (query.exec()) {
        while (query.next()) {
            range.append(query.value(0).toInt());
        }
    }
    return range;
}
