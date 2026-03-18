#ifndef CCUIDBMODEL_H
#define CCUIDBMODEL_H
#include <QVariant>
#include <QDateTime>

#include <regex>
#include <string>
#include <cmath>
#include <vector>
#include <list>

namespace UiDb {

// ─────────────────────────────────────────────────────────────────────────────
// CCDateTime  –  helper for parsing database datetime strings
// ─────────────────────────────────────────────────────────────────────────────
struct CCDateTime
{
    // Parse a datetime string of the form "yyyy-MM-dd HH:mm:ss"
    // (quotes are stripped and the ISO 'T' separator is already replaced
    //  by a space before calling this function).
    static QDateTime fromString(const std::string &s)
    {
        return QDateTime::fromString(QString::fromStdString(s),
                                     QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    }
};

// 任务表
struct BaseTask
{
    int64_t         taskId = 0;
    std::string     productCodeList;
    std::string     comment;
    QDateTime       startTime;
    QDateTime       finishTime;
    std::string     operatorName;
    int             operatorRole = 0;
    int             isSpotCheck = 0;
    std::string     scVersion;
    std::string     stVersion;
    std::string     saVersion;
    std::string     soVersion;
    std::string     trivisionVersion;
    std::string     modelVersion;
    std::string     ruleVersion;
    std::string     description;
    std::string     ctAnalysisName;
    BaseTask() {}
};

// 推理结果
struct BaseTrivisionResult
{
    int             id = 0;
    int             taskId = 0;
    std::string     productCode;
    int             workpieceId = 0;
    int             algImageId = 0;
    int             dataType = 0;
    std::string     defectCode;
    float           score = 0.0f;
    int             pointX = 0;
    int             pointY = 0;
    int             pointZ = 0;
    int             width = 0;
    int             height = 0;
    int             depth = 0;
    float           defectLength = 0.0f;
    float           defectWidth = 0.0f;
    float           defectArea = 0.0f;
    float           realDefectLength = 0.0f;
    float           realDefectWidth = 0.0f;
    float           realDefectArea = 0.0f;
    int             defectConfirm = 0;
    std::string     defectPolygon;
    int             reinspectionResult = 0;
    QDateTime       reinspectionTime;
    std::string     reinspectionOperator;
    std::string     finalResult;
};

// 工件判定结果
struct BaseWorkpieceInspectionResult {
    int             taskId = 0;
    std::string     productCode;
    int             workpieceId = 0;
    std::string     sn;
    std::string     comment;
    std::string     mouldNumber;
    std::string     defectCode;
    std::string     defectCode2;
    int             isNg = 0;
    int             isSpotCheck = 0;
    int             unloadingPort = 0;
    QDateTime       classfiedDatetime;
    std::string     reinspectionOperator;
    std::string     reinspectionResult;
    QDateTime       reinspectionTime;
    std::string     finalResult;
    std::string     sn1;
    std::string     sn2;
    std::string     sn3;
    std::string     sn4;
    std::string     sn5;
    std::string     sn6;
    std::string     sn7;
    std::string     sn8;
    std::string     sn9;
    std::string     sn10;
    std::string     description;
};

struct BaseWorkpieceInspectionResult1{
    int             taskId = 0;
    std::string     productCode;
    int             workpieceId = 0;
    std::string     sn;
    std::string     defectCode;
    int             isNg = 0;
    QDateTime       classfiedDatetime;
    std::string     finalResult;
    std::string     reinspectionResult;
    std::string     sn1;
    bool            isRecheck = false;
};

struct BaseCtAnalysisRecord
{
    int              taskId = 0;
    int              workpieceId = 0;
    int              algImageId = 0;
    std::string      timestamps;
};

struct BaseCtRecord
{
    int             taskId = 0;
    int             cycleId = 0;
    float           cycleTime = 0;
};

struct BaseBashRecord
{
    std::string     pictureName;
    int             taskId = 0;
    int             workpieceId = 0;
    int             algImageId = 0;
    int             modelId = 0;
    int             modelResult = 0;
    int             bashType = 0;
    int             bashFlag = 0;
    std::string     description;
    int             discard = 0;
    QDateTime       datetime;
    BaseBashRecord() {}
};

struct BaseReinspectionRecord
{
    int             taskId = 0;
    int             workpieceId = 0;
    int             productId = 0;
    std::string     aiResult;
    std::string     humanResult;
    QDateTime       datetime;
    std::string     finalResult;
    BaseReinspectionRecord() {}
};

struct BasePlcAlarm
{
    std::string     id;
    QDateTime       happenedTime;
    QDateTime       canceledTime;
    int             alarmNumber = -1;
    std::string     type;
    std::string     alarmContent;
    std::string     alarmSolution;
    int             handled = 0;
    QDateTime       handledTime;
    std::string     operatorName;
};

struct BaseScAlarm
{
    int             id = 0;
    int             station_id = 0;
    int             alarm_id = 0;
    std::string     level;
    std::string     module;
    QDateTime       happened_time;
    bool            is_false_alarm = false;
    std::string     alarm_content;
    std::string     alarm_solution;
    int             handled = 0;
    QDateTime       handled_time;
    std::string     operate_name;
};

struct BaseUnloadExceptionRecord {
    int             taskId = 0;
    int             cycleId = 0;
    int             exceptionType = 0;
    std::string     exceptionInfo;
    QDateTime       occurredDateTime;
};

struct BaseUserLoginExceptionRecord
{
    int             id = 0;
    std::string     userName;
    std::string     roleName;
    QDateTime       eventDateTime;
    std::string     eventType;
    std::string     eventContent;
};

struct BasePerformanceWatchRecord
{
    int             id = 0;
    QDateTime       datetime;
    std::string     information;
    std::string     displayName;
};

struct BaseQualityAlarmRecord
{
    int             id = 0;
    std::string     productCodeList;
    std::string     productNameList;
    QDateTime       datetime;
    std::string     alarmType;
    std::string     logContent;
    std::string     deviceName;
    std::string     assetNumber;
    std::string     deviceSn;
};

struct BaseMachineStatusRecord
{
    int             id = 0;
    std::string     machineStatus;
    QDateTime       changeTime;
};

struct BaseMachineStatusChangeRecord
{
    int             id = 0;
    int             type = 0;
    std::string     name;
    QDateTime       datetime;
    std::string     description;
    BaseMachineStatusChangeRecord() {}
};

struct BaseOsStartupRecord
{
    int             id = 0;
    QDateTime       startupTime;
};

struct BaseSoftwareUpgradeRecord
{
    int             id = 0;
    std::string     softwareName;
    std::string     softwareVersion;
    QDateTime       updateTime;
};

struct BaseUserLoginRecord
{
    int             id = 0;
    std::string     userName;
    std::string     roleName;
    QDateTime       loginDateTime;
    QDateTime       logoutDateTime;
    std::string     reserve1;
    std::string     reserve2;
    std::string     reserve3;
    std::string     reserve4;
    std::string     reserve5;
};

struct BaseDeviceSoptCheckRecord
{
    int             recordId = 0;
    int             itemId = 0;
    std::string     itemContent;
    QDateTime       spotCheckDateTime;
    std::string     operatorName;
    int             result = 0;
};

struct BaseSampleSpotCheckRecord
{
    int             taskId = 0;
    int             spotCheckOk = 0;
    int             spotCheckNg = 0;
    int             userInputOk = 0;
    int             userInputNg = 0;
    int             result = 0;
    std::string     operatorName;
    QDateTime       datetime;
    int             spentTime = 0;
};

struct BaseVulnerablePartReplaceRecord
{
    int             recordId  = 0;
    int             itemId = 0;
    std::string     itemContent;
    QDateTime       replaceDateTime;
    std::string     description;
    std::string     operatorName;
    std::string     limitDescription;
    std::string     changeDescription;
};

struct BaseMaintenanceRecord
{
    int             recordId = 0;
    int             itemId = 0;
    std::string     itemContent;
    QDateTime       maintenanceDateTime;
    std::string     operatorName;
};

struct BaseHumanMachieCollaborationRecord
{
    int             taskId = 0;
    int             workpieceId = 0;
    int             locationId = 0;
    std::string     defectCode;
    std::string     productCode;
    int             type = 0;
    std::string     sn;
    std::string     operatorName;
    QDateTime       datetime;
    BaseHumanMachieCollaborationRecord() {}
};

struct BaseUserItem
{
    std::string userName = "";
    std::string password = "";
    std::string roleName = "";
    bool enable{false};
    BaseUserItem(){};
};

struct BaseRoleItem
{
    std::string roleName = "";
    std::string menuPermissions;
    std::string toolPermissions;
    int waitTime = 0;
    BaseRoleItem(){};
};

struct BaseRoleToTask
{
    std::string roleName = "";
    std::string userName = "";
    int taskId = 0;
    BaseRoleToTask(){};
};

struct BaseDefectData
{
    std::string defectName = "";
    int defectNum = 0;
};

struct BaseAlImageDefectNumData
{
    int algImageId = 0;
    int64_t defectNum = 0;
};

struct BaseGlassHistoryPlcPickResult
{
    int         taskId = 0;
    int         srcTrayIndex = 0;
    int         srcHoleIndex = 0;
    std::string destTrayType;
    int         destTrayIndex = 0;
    int         destHoleIndex = 0;
};

struct BaseHistoryCiState
{
    int             id = 0;
    int             state = 0;
    QDateTime       dateTime;
};

struct BaseHistoryLoadingState
{
    int             id = 0;
    int             task_id = 0;
    std::string     state;
    QDateTime       dateTime;
};

struct BaseHistoryOfflineState
{
    int             id = 0;
    QDateTime       start_time;
    QDateTime       end_time;
};

} // namespace UiDb

#endif // CCUIDBMODEL_H
