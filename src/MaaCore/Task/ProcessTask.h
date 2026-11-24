#pragma once

#include <functional>
#include <unordered_map>

#include "AbstractTask.h"
#include "Common/AsstTypes.h"
#include "MaaUtils/NoWarningCVMat.hpp"

namespace asst
{
// 流程任务，按照配置文件里的设置的流程运行
class ProcessTask final : public AbstractTask
{
public:
    enum class TimesLimitType
    {
        Pre,
        Post,
    };

    struct TimesLimitData
    {
        int times = 0;
        TimesLimitType type = TimesLimitType::Pre;
    };

public:
    using ActionGuard = std::function<bool(const TaskInfo&, const Rect&, const cv::Mat*)>;
    using AbstractTask::AbstractTask;
    ProcessTask(const AbstractTask& abs, std::vector<std::string> tasks_name);
    ProcessTask(AbstractTask&& abs, std::vector<std::string> tasks_name) noexcept;

    virtual ~ProcessTask() override = default;

    virtual bool run() override;

    void request_failure() noexcept { m_failure_requested = true; }

    bool failure_requested() const noexcept { return m_failure_requested; }

    void set_terminal_screen(std::string name) { m_terminal_screen = std::move(name); }
    bool run_with_terminal_parent(ProcessTask* parent);
    bool allow_direct_input();

    void set_action_guard(ActionGuard guard) { m_action_guard = std::move(guard); }

    ProcessTask& set_task_delay(int delay) noexcept;
    ProcessTask& set_tasks(std::vector<std::string> tasks_name) noexcept;
    ProcessTask& set_times_limit(std::string name, int limit, TimesLimitType type = TimesLimitType::Pre);
    ProcessTask& set_post_delay(std::string name, int delay);
    ProcessTask& set_reusable_image(const cv::Mat& reusable);
    // 设定某个任务的 next 列表, 返回值表示是否成功覆盖; 任务名需要为实际执行任务名, 不支持@, #next 等语法
    bool override_next(std::string_view name, std::vector<std::string> next_tasks);
    // 移除某个任务的 next 列表覆盖, 返回值表示是否成功移除; 任务名需要为实际执行任务名, 不支持@, #next 等语法
    bool remove_override_next(std::string_view name);

    const std::string& get_last_task_name() const noexcept { return m_last_task_name; }

    const auto& get_last_hit() const noexcept { return m_last_hit_detail; }

protected:
    enum class NodeStatus
    {
        Success,
        RetryFailed, // 重试次数达到上限，对应 on_error_next
        Runout,      // 执行次数达到上限，对应 exceeded_next
        Interrupted,
        InternalError,
        TerminalScreen,
    };

    struct HitDetail
    {
        std::shared_ptr<cv::Mat> image = nullptr;
        Rect rect;
        std::shared_ptr<AnalyzerResult> reco_detail = nullptr;
        TaskConstPtr task_ptr = nullptr;
    };

    virtual bool _run() override;
    virtual json::value basic_info() const override;

    HitDetail find_first(const TaskList& list);
    NodeStatus run_action(const HitDetail& hits) const;
    NodeStatus run_task(const HitDetail& hits, ActionGuard& guard);
    std::pair<NodeStatus, TaskConstPtr> find_and_run_task(const TaskList& list, ActionGuard& guard);
    // for fast init only, not for runtime use
    ProcessTask& set_override_next(std::unordered_map<std::string, TaskList> next_override);

    TimesLimitData calc_time_limit(TaskConstPtr task) const;
    int calc_post_delay(TaskConstPtr task) const;

    void exec_click_task(const Rect& matched_rect) const;
    void exec_input_task(const std::string& text) const;
    void exec_swipe_task(
        const Rect& r1,
        const Rect& r2,
        int duration,
        SwipeExtraDirection extra_swipe,
        double slope_in,
        double slope_out,
        bool high_resolution_swipe_fix) const;

    std::string m_terminal_screen;
    ProcessTask* m_terminal_parent = nullptr;
    bool m_terminal_detected = false;
    ActionGuard m_action_guard;
    TaskList m_begin_task_list;
    bool m_failure_requested = false;
    std::string m_pre_task_name;
    std::string m_last_task_name;
    std::string m_last_matched_task_name;
    std::unordered_map<std::string, int> m_post_delay;
    std::unordered_map<std::string, TimesLimitData> m_times_limit;
    std::unordered_map<std::string, int> m_exec_times;
    static constexpr int TaskDelayUnsetted = -1;
    int m_task_delay = TaskDelayUnsetted;
    cv::Mat m_reusable;
    std::shared_ptr<HitDetail> m_last_hit_detail = nullptr;
    std::unordered_map<std::string, TaskList> m_next_override;
};
}
