#ifndef CCUIDBMODEL_H
#define CCUIDBMODEL_H
#include <QVariant>
#include <QDateTime>
#include <QDate>

#include <regex>
#include <string>
#include <cmath>
#include <vector>
#include <list>

namespace UiDb {

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

struct CCProductMap
{
    std::string     productCode;
    std::string     productName;
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

} // namespace UiDb

#endif // CCUIDBMODEL_H
