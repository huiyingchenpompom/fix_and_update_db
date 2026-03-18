#ifndef CCQDBCLIENTINTERFACE_H
#define CCQDBCLIENTINTERFACE_H
#include <QMutex>
#include <QObject>
#include <QDateTime>
#include <QDate>
#include <QList>
#include <QtSql/QSqlDatabase>
#include <QtSql/QSqlQuery>
#include <QtSql/QSqlError>
#include <QDebug>
#include "CCUiDbModel.h"
#include "cc_database_define.h"
#include <QMap>
#include <QSet>
#include <QStringList>
#include <map>
#include <vector>
#include <functional>

struct Device {
    std::string ip;
    int id = 0;
    bool success_connect = false;
    bool local_device = false;
};

// ==================== RAII 数据库连接守卫 ====================
// 自动管理连接的创建和销毁，彻底杜绝连接泄漏
class UI_DATABASE_EXPORT DbConnectionGuard {
public:
    DbConnectionGuard(const Device &device);
    ~DbConnectionGuard();

    DbConnectionGuard(const DbConnectionGuard &) = delete;
    DbConnectionGuard &operator=(const DbConnectionGuard &) = delete;

    bool isOpen() const;
    QSqlDatabase &db();
    QString connName() const;

private:
    QString m_connName;
    QSqlDatabase m_db;
    bool m_valid = false;
};

class UI_DATABASE_EXPORT CCQDbClientInterface: public QObject
{
    Q_OBJECT
public:
    static CCQDbClientInterface* instance();
    ~CCQDbClientInterface();

    // ---- 初始化与连接管理 ----
    bool initDatabaseConnections(const std::map<int, Device> &deviceMap);
    bool checkDbConnect(QString ip);
    bool connectLocalSql();
    QString configPath() const;

    // ---- 设备信息 ----
    std::map<int, Device> getDeviceMap();
    QStringList getDeviceIdlist();
    QList<Device> getDbConnectedDeviceList();

    // ---- 工件检测结果查询 ----
    void getBaseWorkpieceInspectionResultByTaskId(
        int beginTaskId, int endTaskId,
        std::function<void(std::vector<UiDb::BaseWorkpieceInspectionResult>)> callback);

    void getBaseWorkpieceInspectionResultBySn(
        const std::string &sn,
        std::function<void(std::vector<UiDb::BaseWorkpieceInspectionResult>)> callback);

    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByTaskIdIp(int device_id, int beginTaskId, int endTaskId);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByTaskIdIp(int device_id, int beginTaskId, int endTaskId, QString productCode);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByWorkpieceIdIp(int device_id, int startWorkpieceId, int endWorkpieceId);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByTaskIdWorkpieceIdIp(int device_id, int taskId, int workpieceId);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultBySnIp(const std::string &ip, const std::string &sn);

    QList<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultsForDeviceSince(int device_id, QDateTime &time);
    QStringList getAllProductInfoBySN(const QString &sn);
    int getBaseWorkpieceInspectionResultCount(int device_id);

    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByDateTime(int device_id, QDateTime &start, QDateTime &end);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByDateTime(QDateTime &start, QDateTime &end);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByDateTime(const QString &productCode, QDateTime &start, QDateTime &end);
    std::vector<UiDb::BaseWorkpieceInspectionResult> getBaseWorkpieceInspectionResultByDateTime(int device_id, const QString &productCode, QDateTime &start, QDateTime &end);

    std::vector<UiDb::BaseWorkpieceInspectionResult1> getBaseWorkpieceInspectionResultByDateRange(
        const QStringList &deviceIdList,
        const QStringList &productCodes,
        const QDateTime &beginDate,
        const QDateTime &endDate,
        QMap<int, QSet<int>> &firstInspectInfo,
        const bool &fullTrace,
        int task_id = -1);

    QMap<int, QList<int>> getWorkpieceIdsByTaskIdIp(int device_id, int beginTaskId, int endTaskId);
    QMap<int, QList<int>> getWorkpieceIdsByTaskIdIp(int device_id, int beginTaskId, int endTaskId, const QString &productCode);

    // ---- 任务查询 ----
    QMap<int, UiDb::BaseTask> getTask(const QStringList &deviceIdList, const QDate &beginDate, const QDate &endDate);
    std::vector<UiDb::BaseTask> getAllTask();
    std::vector<UiDb::BaseTask> getTaskByDeviceId(const std::string &ip);
    std::vector<std::pair<int, QStringList>> getTaskByDeviceId(int device_id);

    // ---- 产品映射 ----
    std::vector<UiDb::CCProductMap> getProductMap(int device_id);

    // ---- 推理结果查询 ----
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResult(int device_id, int taskId, int workpieceId);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResult(int device_id, int taskId, int workpieceId, int algImageId);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResult(int device_id, const std::vector<std::pair<int, int>> &pairs);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResult(const std::vector<std::pair<int, int>> &pairs);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultLocal(const std::vector<std::pair<int, int>> &pairs);
    UiDb::BaseTrivisionResult getBaseTrivisionResult(int device_id, int id);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResult(int device_id, const QString &productCode);

    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultResumeCapture(int device_id, int taskId, int workpieceId, const QString &sn);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultResumeCapture(int device_id, int taskId, int workpieceId, int algImageId, const QString &sn);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultMulitDevice(int device_id, int taskId, int workpieceId);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultAllDevice(int taskId, int workpieceId, int algImageId, const QString &defect_code);
    std::vector<UiDb::BaseTrivisionResult> getBaseTrivisionResultAllDevice(int taskId, int workpieceId, int algImageId, int id);

    QMap<QString, std::vector<UiDb::BaseTrivisionResult>> getBaseTrivisionResultMap(int device_id);
    QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> getBaseTrivisionResultMap1(int device_id);
    QMap<QString, QMap<QString, std::vector<UiDb::BaseTrivisionResult>>> getBaseTrivisionResultMapMultiDevice(int device_id);

    // ---- 去重推理结果 ----
    std::vector<UiDb::BaseTrivisionResult> getDeduplicationTrivisionResult(
        const QStringList &deviceIdList, const QStringList &productCodes,
        const std::vector<std::pair<int, int>> &pairs, const QString &defectName, bool searchDeduplicationTable);
    void updateDeduplicationTrivisionResult(int device_id, int taskId, int workpieceId, int algid);

    // ---- 统计计数 ----
    int getReviewImageCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId);
    int getTotalImageCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId);
    int getReviewDefectCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId);
    int getTotalDefectCountByBaseTrivisionResult(int device_id, int taskId, int workpieceId);

    // ---- 更新操作 ----
    void updateBaseWorkpieceInspectionResult(int device_id, const UiDb::BaseWorkpieceInspectionResult &result);
    void updateBaseWorkpieceInspectionResult(int device_id, int task_id, int workpiece_id, const QString &sn);
    void updateBaseWorkpieceInpsectionResultByTrivisonData(int device_id, int task_id, int workpiece_id, const QString &sn);
    void updateBaseWorkpieceInpsectionResultByTrivisonData(int device_id, int id, const QString &sn);
    void updateBaseTrivisionResult(int device_id, const UiDb::BaseTrivisionResult &result);
    void updateBaseTrivisionResult(int device_id, int task_id, int workpiece_id, int alg_image_id, int reinspection_result, const QString &sn);
    void updateBaseTrivisionResult(int device_id, int task_id, int workpiece_id, int reinspection_result, const QString &sn);
    void updateBaseTrivisionResult(int device_id, int id, int reinspection_result);

    // ---- 报警相关 ----
    void insertBaseScAlarm(int device_id, const UiDb::BaseScAlarm &result);
    void insertBaseScAlarm(QMap<int, QList<UiDb::BaseScAlarm>> allData);
    void insertLocalBaseScAlarm(const UiDb::BaseScAlarm &result);
    void updateBaseScAlarm(int device_id, const UiDb::BaseScAlarm &result);
    std::vector<UiDb::BaseScAlarm> getAllBaseScAlarm();
    std::vector<UiDb::BaseScAlarm> getBaseScAlarm(int device_id);
    std::vector<UiDb::BaseScAlarm> getBaseScAlarm(
        const QStringList &deviceIdList, const QDate &beginDate, const QDate &endDate,
        QString level, QString module, QString falseAlarm);
    std::vector<UiDb::BaseScAlarm> getBaseScAlarmById(int id, int pre_count, int back_count);

    // ---- 表更新检测 ----
    bool isTableUpdated(const QStringList &deviceIdList);

    // ---- 测试 ----
    void setTestData();

    // ---- 公开的辅助方法（供 lambda 内使用）----
    QSqlDatabase createDbConnection(const Device &device, QString &connName);

private:
    explicit CCQDbClientInterface(QObject *parent = nullptr);
    void initParams();
    bool isLocalIPAddress(const QString &ipAddress);
    void cleanupDatabaseConnection(QSqlDatabase &db, const QString &connName);

    // ---- 解析辅助函数（消除重复代码）----
    static UiDb::BaseWorkpieceInspectionResult parseFullWorkpieceResult(QSqlQuery &query);
    static UiDb::BaseWorkpieceInspectionResult parseLiteWorkpieceResult(QSqlQuery &query);
    static UiDb::BaseWorkpieceInspectionResult1 parseLiteWorkpieceResult1(QSqlQuery &query);
    static UiDb::BaseTrivisionResult parseFullTrivisionResult(QSqlQuery &query);
    static UiDb::BaseTrivisionResult parseLiteTrivisionResult(QSqlQuery &query);
    static UiDb::BaseTask parseBaseTask(QSqlQuery &query);
    static UiDb::BaseTask parseUiBaseTask(QSqlQuery &query);
    static UiDb::BaseScAlarm parseBaseScAlarm(QSqlQuery &query);
    static QString quotedInList(const QStringList &items);

    // ---- 内部辅助 ----
    bool copyTrivisionResults(QSqlQuery &query, const QString &sourceTable, const QString &targetTable, int taskId, int workpieceId, int algid, int &copiedCount);
    bool isDeduplicationDataExists(QSqlDatabase &db, int taskId, int workpieceId, int algid);
    bool clearDeduplicationResults(QSqlDatabase &db, int taskId, int workpieceId, int algid);
    QString getWorkpieceSN(int device_id, int taskId, int workpieceId);
    QDateTime getLastUpdateTimeFromDB(const QString &connName, QSqlDatabase &db);
    QVector<int> getResumeCaptureTaskRange(QSqlDatabase &db, int taskId, const QString &sn);

    QMutex m_mutex;
    std::map<int, Device> m_deviceParams;
    Device m_localDevice;
    bool m_connect_local_db = false;
    QDateTime m_lastUpdateTime;
    int m_searchTaskCount = 10;

};

#endif // CCQDBCLIENTINTERFACE_H
