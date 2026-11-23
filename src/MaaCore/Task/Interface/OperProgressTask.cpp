#include "OperProgressTask.h"

#include <ranges>

#include "Config/Miscellaneous/BattleDataConfig.h"
#include "Task/OperProgress/OperProgressProcessTask.h"
#include "Utils/Logger.hpp"

asst::OperProgressTask::OperProgressTask(const AsstCallback& callback, Assistant* inst) :
    InterfaceTask(callback, inst, TaskType),
    m_process_task_ptr(std::make_shared<OperProgressProcessTask>(callback, inst, TaskType))
{
    m_process_task_ptr->set_retry_times(0);
    m_subtasks.emplace_back(m_process_task_ptr);
}

int asst::OperProgressTask::level_cap(int rarity, int phase)
{
    constexpr std::array<std::array<int, 3>, 6> caps { {
        { 30, 0, 0 },
        { 30, 0, 0 },
        { 40, 55, 0 },
        { 45, 60, 70 },
        { 50, 70, 80 },
        { 50, 80, 90 },
    } };
    return rarity >= 1 && rarity <= 6 && phase >= 0 && phase <= 2 ? caps[rarity - 1][phase] : 0;
}

namespace json::ext
{
template <>
class jsonization<asst::OperProgressTask::ProgressPlan>
{
public:
    bool check_json(const json::value& json) const
    {
        static constexpr std::array<const char*, 7> allowed_keys = {
            "role", "name", "elite", "level", "skill_level", "skill_mastery", "allow_chip_purchase",
        };

        if (!json.is_object()) {
            return false;
        }
        const auto& obj = json.as_object();
        for (const auto& kv : obj) {
            if (std::ranges::find(allowed_keys, kv.first) == allowed_keys.end()) {
                return false;
            }
        }

        bool ret = true;
        const auto check_field = [&]<typename T>(const char* key, bool required = true) -> std::optional<T> {
            const auto& found = json.find_value(key);
            if (!found) {
                ret = ret && !required;
            }
            else if (!found->is<T>()) {
                ret = false;
            }
            else {
                return found->as<T>();
            }
            return std::nullopt;
        };
        // 养成动作一律是整数，显式写 null 时 is<int>() 为假，与类型错误同等拒绝，不会被当成未配置。
        [[maybe_unused]] const auto& role_opt = check_field.template operator()<asst::battle::Role>("role", false);
        [[maybe_unused]] const auto& name_opt = check_field.template operator()<std::string>("name", true);
        [[maybe_unused]] const auto& elite_opt = check_field.template operator()<int>("elite", false);
        const auto level_opt = check_field.template operator()<int>("level", false);
        [[maybe_unused]] const auto& skill_level_opt = check_field.template operator()<int>("skill_level", false);
        [[maybe_unused]] const auto& skill_mastery_opt =
            check_field.template operator()<std::array<int, 3>>("skill_mastery", false);

        check_field.template operator()<bool>("allow_chip_purchase", false);
        if (!ret) {
            return false;
        }
        if (name_opt->empty() || std::ranges::all_of(*name_opt, [](unsigned char ch) { return std::isspace(ch); })) {
            LogError << __FUNCTION__ << "name must be non-empty";
            return false;
        }

        if (level_opt && (!elite_opt || *level_opt < 1 || *level_opt > 90)) {
            return false;
        }
        if (elite_opt && (*elite_opt < (level_opt ? 0 : 1) || *elite_opt > 2)) {
            LogError << __FUNCTION__ << "invalid target elite phase";
            return false;
        }
        if (skill_level_opt && (*skill_level_opt < 2 || *skill_level_opt > 7)) {
            LogError << __FUNCTION__ << "skill_level must be between 2 and 7";
            return false;
        }
        if (skill_mastery_opt &&
            std::ranges::any_of(*skill_mastery_opt, [](int level) { return level < 0 || level > 3; })) {
            LogError << __FUNCTION__ << "skill_mastery must be between 0 and 3";
            return false;
        }
        return true;
    }
};
} // namespace json::ext

bool asst::OperProgressTask::set_params(const json::value& params)
{
    LogTraceFunction;
    if (m_running) {
        LogError << __FUNCTION__ << "task is running, cannot set params";
        return false;
    }

    const auto& plans = params.find<std::vector<ProgressPlan>>("plans");
    if (!plans) {
        LogError << __FUNCTION__ << "missing plans, or format is error";
        return false;
    }
    const auto inspect = params.find<bool>("inspect_only");
    const auto slot = params.find<int>("inspection_skill");
    if ((params.contains("inspect_only") && !inspect) ||
        (params.contains("inspection_skill") && (!slot || *slot < 0 || *slot > 3 || !inspect.value_or(false)))) {
        return false;
    }
    std::vector<ProgressPlan> validated_plans;
    for (const auto& plan : *plans) {
        battle::Role role = plan.role;
        if (role == battle::Role::Unknown) {
            const auto& roles = BattleData.get_roles(plan.name, true);
            if (roles.empty() || roles.size() > 1) {
                LogError << __FUNCTION__ << "oper name:" << plan.name << "with multi role, and not specific";
                return false;
            }
            role = *roles.begin();
        }
        else if (role == battle::Role::Drone) {
            // 专精对象只能是干员，装置/召唤物没有技能专精
            LogError << __FUNCTION__ << "oper plan does not support drone:" << plan.name;
            return false;
        }
        else if (asst::BattleData.find_opers(role, plan.name).empty()) {
            LogError << __FUNCTION__ << "unknown oper name: " << plan.name << ", role:" << role;
            return false;
        }
        if (plan.level && *plan.level > level_cap(BattleData.get_rarity(role, plan.name), *plan.elite)) {
            LogError << __FUNCTION__ << "unreachable level for" << plan.name;
            return false;
        }
        validated_plans.emplace_back(
            ProgressPlan {
                .role = role,
                .name = plan.name,
                .elite = plan.elite,
                .level = plan.level,
                .skill_level = plan.skill_level,
                .skill_mastery = plan.skill_mastery,
                .allow_chip_purchase = plan.allow_chip_purchase,
            });
    }

    m_process_task_ptr->set_plan(std::move(validated_plans), inspect.value_or(false), slot.value_or(0));
    return true;
}
