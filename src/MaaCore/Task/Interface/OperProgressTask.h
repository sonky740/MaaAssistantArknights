#pragma once
#include "Task/InterfaceTask.h"

#include <array>
#include <optional>
#include <variant>

#include "Common/AsstBattleDef.h"

namespace asst
{
class OperProgressProcessTask;

class OperProgressTask final : public InterfaceTask
{
public:
    // level 必须与 elite 一同指定：精英化会重置等级。
    struct ProgressPlan
    {
        battle::Role role = battle::Role::Unknown;
        std::string name;
        std::optional<int> elite;
        std::optional<int> level;
        std::optional<int> skill_level;                  // 仅用于单技能升级，表示目标等级
        std::optional<std::array<int, 3>> skill_mastery; // 仅用于专精升级，表示目标专精等级数组

        std::optional<bool> allow_chip_purchase;

        MEO_TOJSON(
            MEO_OPT role,
            name,
            MEO_OPT elite,
            MEO_OPT level,
            MEO_OPT skill_level,
            MEO_OPT skill_mastery,
            MEO_OPT allow_chip_purchase);
        MEO_FROMJSON(
            MEO_OPT role,
            name,
            MEO_OPT elite,
            MEO_OPT level,
            MEO_OPT skill_level,
            MEO_OPT skill_mastery,
            MEO_OPT allow_chip_purchase);
    };

public:
    inline static constexpr std::string_view TaskType = "OperProgress";

    OperProgressTask(const AsstCallback& callback, Assistant* inst);
    virtual ~OperProgressTask() override = default;

    static int level_cap(int rarity, int phase);
    virtual bool set_params(const json::value& params) override;

private:
    std::shared_ptr<OperProgressProcessTask> m_process_task_ptr;
};
} // namespace asst
