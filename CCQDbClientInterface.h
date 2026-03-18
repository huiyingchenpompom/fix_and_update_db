#ifndef CCQDBCLIENTINTERFACE_H
#define CCQDBCLIENTINTERFACE_H

#include <QObject>
#include <QMutex>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QMap>
#include <QList>
#include <QString>
#include <QStringList>

#include <functional>
#include <map>
#include <vector>
#include <string>
#include <utility>

#include "cc_database_define.h"
#include "CCUiDbModel.h"

namespace UiDb {

// ─────────────────────────────────────────────────────────────────────────────
// Device  –  represents one database endpoint (one station / IP)
// ─────────────────────────────────────────────────────────────────────────────
struct Device {
    int         id       = 0;
    QString     ip;
    int         port     = 3306;
    QString     username;
    QString     password;
    QString     dbName;
    QString     driverName = QStringLiteral("QMYSQL");

    bool isValid() const { return !ip.isEmpty() && !dbName.isEmpty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// DbConnectionGuard  –  RAII wrapper that guarantees connection cleanup
//
//  • Constructed with a Device; opens a uniquely-named QSqlDatabase connection
//    using a UUID so different threads never share the same connection name.
//  • Destructor calls close() + QSqlDatabase::removeDatabase() automatically,
//    eliminating every connection leak.
// ─────────────────────────────────────────────────────────────────────────────
class UI_DATABASE_EXPORT DbConnectionGuard
{
public:
    explicit DbConnectionGuard(const Device &device);
    ~DbConnectionGuard();

    // Non-copyable, non-movable (owning resource)
    DbConnectionGuard(const DbConnectionGuard &)            = delete;
    DbConnectionGuard &operator=(const DbConnectionGuard &) = delete;
    DbConnectionGuard(DbConnectionGuard &&)                 = delete;
    DbConnectionGuard &operator=(DbConnectionGuard &&)      = delete;

    bool        isOpen()    const;
    QSqlDatabase &db();
    QString     connName()  const;

private:
    QString      m_connName;
    QSqlDatabase m_db;
    bool         m_valid = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// CCQDbClientInterface  –  singleton database client
// ─────────────────────────────────────────────────────────────────────────────
class UI_DATABASE_EXPORT CCQDbClientInterface : public QObject
{
    Q_OBJECT

public:
    static CCQDbClientInterface *instance();
    ~CCQDbClientInterface() override;

    // ── Initialisation & connection ──────────────────────────────────────────
    void initDatabaseConnections();
    bool checkDbConnect(const Device &device);
    bool connectLocalSql();
    QString configPath() const;
    /** Legacy helper kept for old call-sites; prefer DbConnectionGuard. */
    QSqlDatabase createDbConnection(const Device &device, const QString &connName);
    void cleanupDatabaseConnection(const QString &connName);

    // ── Device information ───────────────────────────────────────────────────
    QMap<int, Device>         getDeviceMap()              const;
    QList<int>                getDeviceIdlist()           const;
    QList<Device>             getDbConnectedDeviceList()  const;

    // ── Workpiece inspection result queries ──────────────────────────────────
    void getBaseWorkpieceInspectionResultByTaskId(
            int taskId,
            std::function<void(QList<BaseWorkpieceInspectionResult>)> callback);

    void getBaseWorkpieceInspectionResultBySn(
            const QString &sn,
            std::function<void(QList<BaseWorkpieceInspectionResult>)> callback);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByTaskIdIp(int taskId, const QString &ip);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByTaskIdIp(int taskId, int deviceId);

    QList<BaseWorkpieceInspectionResult1>
    getBaseWorkpieceInspectionResultByTaskIdIp(int taskId, int deviceId, bool lite);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByWorkpieceIdIp(
            int workpieceId, int deviceId);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByTaskIdWorkpieceIdIp(
            int taskId, int workpieceId, int deviceId);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultBySnIp(
            const QString &sn, int deviceId);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultsForDeviceSince(
            int deviceId, const QDateTime &since);

    QList<BaseWorkpieceInspectionResult>
    getAllProductInfoBySN(const QString &sn);

    int getBaseWorkpieceInspectionResultCount(int deviceId, int taskId);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByDateTime(
            int deviceId, const QDateTime &dt);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByDateTime(
            int deviceId, const QDateTime &from, const QDateTime &to);

    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByDateTime(
            int deviceId, int taskId, const QDateTime &from, const QDateTime &to);

    QList<BaseWorkpieceInspectionResult1>
    getBaseWorkpieceInspectionResultByDateTime(
            int deviceId, int taskId,
            const QDateTime &from, const QDateTime &to, bool lite);

    /** Fixed: ":taskId" binding bug + exec() without argument. */
    QList<BaseWorkpieceInspectionResult>
    getBaseWorkpieceInspectionResultByDateRange(
            int deviceId,
            const QDateTime &from, const QDateTime &to,
            const QString   &task_id = QString());

    QList<int>
    getWorkpieceIdsByTaskIdIp(int taskId, int deviceId);

    QList<QPair<int,int>>
    getWorkpieceIdsByTaskIdIp(int taskId, int deviceId, bool withAlgImageId);

    // ── Task queries ─────────────────────────────────────────────────────────
    BaseTask  getTask(int taskId, int deviceId);
    QList<BaseTask> getAllTask(int deviceId);
    QList<BaseTask> getTaskByDeviceId(const std::string &ip);
    QList<BaseTask> getTaskByDeviceId(int device_id);

    // ── Product map ──────────────────────────────────────────────────────────
    QMap<QString, QString> getProductMap(int deviceId);

    // ── Trivision (inference) result queries ─────────────────────────────────
    QList<BaseTrivisionResult>
    getBaseTrivisionResult(int device_id, int taskId, int workpieceId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(int device_id, int taskId, int workpieceId, int algImageId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(int device_id,
                           const QList<QPair<int,int>> &pairs);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(const QList<QPair<int,int>> &pairs);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(int device_id, int id);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(int device_id, const QString &productCode);

    QList<BaseTrivisionResult>
    getBaseTrivisionResult(const QList<QPair<int,int>> &pairs, int dummy);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultLocal(int taskId, int workpieceId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultResumeCapture(int device_id, int taskId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultResumeCapture(int device_id, int taskId,
                                        int startWorkpieceId, int endWorkpieceId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultMulitDevice(const QList<QPair<int,int>> &pairs);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultAllDevice(int taskId, int workpieceId);

    QList<BaseTrivisionResult>
    getBaseTrivisionResultAllDevice(const QList<QPair<int,int>> &pairs);

    /** Optimised version: single connection per device, instead of one per row. */
    QMap<QPair<int,int>, QList<BaseTrivisionResult>>
    getBaseTrivisionResultMap(int device_id,
                              const QList<QPair<int,int>> &pairs);

    QMap<QPair<int,int>, QList<BaseTrivisionResult>>
    getBaseTrivisionResultMap1(int device_id,
                               const QList<QPair<int,int>> &pairs);

    /** Refactored: one DbConnectionGuard per device, not one per (task,workpiece) pair. */
    QMap<QPair<int,int>, QList<BaseTrivisionResult>>
    getBaseTrivisionResultMapMultiDevice(
            const QMap<int, QList<QPair<int,int>>> &devicePairs);

    // ── Deduplication ────────────────────────────────────────────────────────
    QList<BaseTrivisionResult>
    getDeduplicationTrivisionResult(int device_id, int taskId, int workpieceId);

    bool updateDeduplicationTrivisionResult(
            int device_id, int taskId, int workpieceId,
            const QList<BaseTrivisionResult> &results);

    // ── Statistical counts ───────────────────────────────────────────────────
    int getReviewImageCountByBaseTrivisionResult(int device_id, int taskId);
    int getTotalImageCountByBaseTrivisionResult(int device_id, int taskId);
    int getReviewDefectCountByBaseTrivisionResult(int device_id, int taskId);
    int getTotalDefectCountByBaseTrivisionResult(int device_id, int taskId);

    // ── Update operations ────────────────────────────────────────────────────
    bool updateBaseWorkpieceInspectionResult(
            int device_id,
            const BaseWorkpieceInspectionResult &result);

    bool updateBaseWorkpieceInspectionResult(
            int device_id, int taskId, int workpieceId,
            const QString &reinspectionResult,
            const QString &reinspectionOperator);

    bool updateBaseWorkpieceInpsectionResultByTrivisonData(
            int device_id, int taskId, int workpieceId);

    bool updateBaseWorkpieceInpsectionResultByTrivisonData(
            int device_id,
            const BaseWorkpieceInspectionResult &result);

    bool updateBaseTrivisionResult(
            int device_id, int id,
            int defectConfirm, int reinspectionResult,
            const QString &reinspectionOperator);

    bool updateBaseTrivisionResult(
            int device_id, int taskId, int workpieceId,
            const QString &finalResult);

    bool updateBaseTrivisionResult(
            int device_id, int taskId, int workpieceId, int algImageId,
            int defectConfirm, int reinspectionResult,
            const QString &reinspectionOperator);

    bool updateBaseTrivisionResult(
            int device_id,
            const BaseTrivisionResult &result);

    // ── Alarm operations ─────────────────────────────────────────────────────
    bool insertBaseScAlarm(int device_id, const BaseScAlarm &alarm);
    bool insertBaseScAlarm(int device_id, int stationId, int alarmId,
                           const QString &level, const QString &module,
                           const QDateTime &happenedTime);
    bool insertBaseScAlarm(int device_id, int stationId, int alarmId,
                           const QString &level, const QString &module,
                           const QDateTime &happenedTime,
                           const QString &alarmContent,
                           const QString &alarmSolution);

    bool updateBaseScAlarm(int device_id, int id,
                           bool handled, const QDateTime &handledTime,
                           const QString &operatorName);

    QList<BaseScAlarm> getAllBaseScAlarm(int device_id);
    QList<BaseScAlarm> getBaseScAlarm(int device_id, bool handled);
    QList<BaseScAlarm> getBaseScAlarm(int device_id,
                                      const QDateTime &from, const QDateTime &to);
    BaseScAlarm        getBaseScAlarmById(int device_id, int id);

    // ── Miscellaneous ────────────────────────────────────────────────────────
    bool   isTableUpdated(int device_id, const QString &tableName,
                          const QDateTime &since);
    void   setTestData(const QMap<int, Device> &deviceMap);

private:
    explicit CCQDbClientInterface(QObject *parent = nullptr);

    // ── Private helpers ──────────────────────────────────────────────────────
    void   initParams();
    bool   isLocalIPAddress(const QString &ip) const;

    void   copyTrivisionResults(
               QList<BaseTrivisionResult> &dst,
               const QList<BaseTrivisionResult> &src);

    bool   isDeduplicationDataExists(int device_id, int taskId, int workpieceId);
    bool   clearDeduplicationResults(int device_id, int taskId, int workpieceId);

    QString getWorkpieceSN(int device_id, int taskId, int workpieceId);

    QList<QPair<int,int>>
    getLocalWorkpieceCombinations(int device_id, int taskId);

    QList<BaseWorkpieceInspectionResult>
    findReinspectionDataBySN(const QString &sn, int excludeDeviceId);

    QList<BaseTrivisionResult>
    getReinspectionTrivisionResults(int device_id,
                                    int taskId, int workpieceId);

    QDateTime getLastUpdateTimeFromDB(int device_id, const QString &tableName);

    QPair<int,int> getResumeCaptureTaskRange(int device_id, int taskId);

    // ── Static parse helpers (eliminate duplicated field extraction code) ─────
    static BaseWorkpieceInspectionResult  parseFullWorkpieceResult(QSqlQuery &q);
    static BaseWorkpieceInspectionResult1 parseLiteWorkpieceResult(QSqlQuery &q);
    static BaseWorkpieceInspectionResult1 parseLiteWorkpieceResult1(QSqlQuery &q);
    static BaseTrivisionResult            parseFullTrivisionResult(QSqlQuery &q);
    static BaseTrivisionResult            parseLiteTrivisionResult(QSqlQuery &q);
    static BaseTask                       parseBaseTask(QSqlQuery &q);
    static BaseScAlarm                    parseBaseScAlarm(QSqlQuery &q);

    // ── SQL utility helpers ──────────────────────────────────────────────────
    /** Build a quoted IN-list, e.g. "'A','B','C'" */
    static QString quotedInList(const QStringList &items);
    /** Strip trailing ".000" / fractional-second artefacts from datetime strings */
    static QString cleanDateTimeString(const QString &raw);

    // ── Member data ──────────────────────────────────────────────────────────
    mutable QMutex      m_mutex;
    QMap<int, Device>   m_deviceParams;   ///< guarded by m_mutex
    QString             m_localIp;
    QString             m_configPath;
    int                 m_localDeviceId = -1;
};

} // namespace UiDb

#endif // CCQDBCLIENTINTERFACE_H
