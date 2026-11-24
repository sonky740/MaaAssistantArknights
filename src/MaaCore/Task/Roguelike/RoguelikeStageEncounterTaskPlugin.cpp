#include "RoguelikeStageEncounterTaskPlugin.h"

#include <algorithm>
#include <chrono>

#include "Config/Roguelike/RoguelikeStageEncounterConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "MaaUtils/NoWarningCV.hpp"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSession.h"
#include "Task/Roguelike/Map/RoguelikeBoskyPassageMap.h"
#include "Utils/DebugImageHelper.hpp"
#include "Utils/Logger.hpp"
#include "Vision/Matcher.h"
#include "Vision/MultiMatcher.h"
#include "Vision/RegionOCRer.h"

namespace
{
// 黑流事件收尾的占位任务，本插件按实际选择改写它的 baseTask。
constexpr std::string_view BlackFlowResultTask = "BlackFlow@Roguelike@StageEncounterResult";
constexpr std::string_view BlackFlowOptionTask = "BlackFlow@Roguelike@StageEncounterOption-Enter";
constexpr std::string_view BlackFlowRecoveryFailedTask = "BlackFlow@Roguelike@RecoveryFailed";
constexpr std::string_view BlackFlowRewardTask = "BlackFlow@Roguelike@StageEncounterReward";
constexpr std::string_view BlackFlowAbandonTask = "BlackFlow@Roguelike@ExitThenAbandon-Enter";
constexpr std::string_view BlackFlowTerminatedTask = "BlackFlow@Roguelike@StrategyTerminated-Enter";
constexpr std::string_view BlackFlowStagesTask = "BlackFlow@Roguelike@Stages_default";
constexpr std::string_view BlackFlowLeaveConfirmCompletedTask =
    "BlackFlow@Roguelike@StageEncounterLeaveConfirmCompleted";
constexpr std::string_view BlackFlowContinueTask = "BlackFlow@Roguelike@StageEncounterContinue";
constexpr std::string_view BlackFlowContinueMapReadyTask = "BlackFlow@Roguelike@StageEncounterContinueMapReady";
constexpr std::string_view BlackFlowContinueOptionsReadyTask = "BlackFlow@Roguelike@StageEncounterContinueOptionsReady";
constexpr std::string_view BlackFlowTitleOcrTask = "BlackFlow@Roguelike@StageEncounterOcr";
constexpr std::string_view EncounterLoadingCompletedTask = "Roguelike@StageEncounterLoadingCompleted";

asst::TaskPtr recognize_fixed_layout(
    const std::string& theme,
    const cv::Mat& image,
    const asst::RoguelikeStageEncounterConfig::RoguelikeEvent& event,
    size_t choice)
{
    const auto prefix = theme + "@Roguelike@StageEncounterOption";
    const auto header_name = theme + "@Roguelike@StageEncounterFixedLayoutHeader";
    const auto header =
        asst::Task.is_explicit_task(header_name) ? asst::Task.get<asst::MatchTaskInfo>(header_name) : nullptr;
    std::vector<int> marker_y, header_y;
    bool has_leave = false;
    for (const auto& name : asst::Task.get(prefix + "s")->next) {
        if (!name.starts_with(prefix)) {
            continue;
        }
        // The KR Sami Unknown template marks a header, above the clickable reward row.
        const bool is_header = header && name == prefix + "Unknown";
        asst::MultiMatcher matcher(image);
        matcher.set_task_info(is_header ? header : asst::Task.get(name));
        if (const auto matches = matcher.analyze()) {
            for (const auto& match : *matches) {
                if (is_header) {
                    header_y.push_back(match.rect.y + header->rect_move.y);
                }
                else {
                    marker_y.push_back(match.rect.y + match.rect.height / 2);
                }
            }
            has_leave |= !matches->empty() && name == prefix + "Leave";
        }
    }
    if ((marker_y.empty() && header_y.empty()) || (header_y.size() == 1 && !has_leave)) {
        return nullptr;
    }

    if (!header_y.empty()) {
        if (const auto reward =
                asst::Task.get<asst::MatchTaskInfo>(theme + "@Roguelike@StageEncounterFixedLayoutReward")) {
            asst::MultiMatcher matcher(image);
            matcher.set_task_info(reward);
            if (const auto matches = matcher.analyze()) {
                for (const auto& match : *matches) {
                    marker_y.push_back(match.rect.y + match.rect.height / 2);
                }
            }
        }
    }

    auto layouts = event.fallback_choices;
    layouts.emplace_back(event.option_num, choice);
    auto common_target = cv::Rect(0, 0, image.cols, image.rows);
    asst::TaskPtr complete;
    for (const auto& [count, item] : layouts) {
        if (!header_y.empty() && header_y.size() != count) {
            continue;
        }
        // 鲍勃的三个或四个货品均为藏品；单独出现的离开标记不属于货品页。
        if (count > 1 && event.name == "鲍勃杂货店" && has_leave) {
            continue;
        }
        const auto name = theme + "@Roguelike@OptionChoose" + std::to_string(count) + "-";
        const auto target = asst::Task.get(name + std::to_string(item));
        if (!target) {
            continue;
        }
        std::vector<int> remaining = marker_y;
        size_t seen = 0;
        for (size_t row = 1; row <= count; ++row) {
            const auto row_task = asst::Task.get(name + std::to_string(row));
            if (!row_task) {
                break;
            }
            const auto rect = row_task->specific_rect;
            const auto in_row = [&](int y) {
                return rect.y <= y && y < rect.y + rect.height;
            };
            const bool has_icon = std::erase_if(remaining, in_row) > 0;
            if (header_y.empty() ? has_icon : std::count_if(header_y.begin(), header_y.end(), in_row) == 1) {
                ++seen;
            }
        }
        if (!remaining.empty()) {
            continue;
        }
        const auto rect = target->specific_rect;
        if (header) {
            const bool target_active =
                std::ranges::any_of(marker_y, [&](int y) { return rect.y <= y && y < rect.y + rect.height; });
            if (seen != count || !target_active) {
                return nullptr;
            }
        }
        common_target &= cv::Rect(rect.x, rect.y, rect.width, rect.height);
        if (seen == count) {
            complete = target;
        }
    }
    // 有些行坐标重叠。至少一个布局完整，且所有可能布局的目标有交集，才能安全点选。
    if (!complete || common_target.empty()) {
        return nullptr;
    }
    auto result = std::make_shared<asst::TaskInfo>(*complete);
    result->specific_rect = { common_target.x, common_target.y, common_target.width, common_target.height };
    return result;
}
}

bool asst::RoguelikeStageEncounterTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    // 安全屋，掷骰子之类的带选项的也都是视为不期而遇了
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    if (!RoguelikeConfig::is_valid_theme(m_config->get_theme())) {
        Log.error("Roguelike name doesn't exist!");
        return false;
    }
    const std::string roguelike_name = m_config->get_theme() + "@";
    const std::string& task = details.get("details", "task", "");
    std::string_view task_view = task;
    if (task_view.starts_with(roguelike_name)) {
        task_view.remove_prefix(roguelike_name.length());
    }
    if (task_view == "Roguelike@StageEncounterJudgeOption") {
        return true;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeStageEncounterTaskPlugin::_run()
{
    LogTraceFunction;

    const std::string& theme = m_config->get_theme();
    std::vector<std::string> event_names = RoguelikeStageEncounter.get_event_names(theme);

    if (theme == RoguelikeTheme::BlackFlow) {
        // 标题识别失败时不会进入 handle_single_event，这里先复位收尾占位，避免沿用上一次事件的去向。
        set_blackflow_result(BlackFlowRecoveryFailedTask);
    }

    const std::string themed_ocr_task = theme + "@Roguelike@StageEncounterOcr";
    const auto event_name_task_ptr =
        Task.get(themed_ocr_task) != nullptr ? Task.get(themed_ocr_task) : Task.get("Roguelike@StageEncounterOcr");
    sleep(event_name_task_ptr->pre_delay);

    if (need_exit()) {
        return false;
    }

    cv::Mat image = capture_encounter_image();
    if (image.empty()) {
        return false;
    }
    OCRer name_analyzer(image);
    name_analyzer.set_task_info(event_name_task_ptr);
    name_analyzer.set_required(event_names);

    if (!name_analyzer.analyze()) {
        Log.error("Unknown Event");
        callback(AsstMsg::SubTaskExtraInfo, basic_info_with_what("EncounterOcrError"));
        return true;
    }

    const auto& result_vec = name_analyzer.get_result();
    if (result_vec.empty()) {
        Log.error("Unknown Event");
        return true;
    }

    std::string current_event_name = result_vec.front().text;
    m_reported_event_name.clear();

    // 处理主事件及其链式 next_event
    while (!current_event_name.empty() && !need_exit()) {
        auto next = handle_single_event(current_event_name);
        if (!next) {
            break;
        }
        current_event_name = next.value();
    }

    return true;
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::handle_single_event(const std::string& event_name)
{
    const std::string& theme = m_config->get_theme();
    const RoguelikeMode& mode = m_config->get_mode();
    if (theme == RoguelikeTheme::BlackFlow) {
        set_blackflow_result(BlackFlowRecoveryFailedTask);
    }
    const auto& event_map = RoguelikeStageEncounter.get_events(theme, mode);

    auto it = event_map.find(event_name);
    if (it == event_map.end()) {
        Log.error("Unknown event:", event_name);
        return std::nullopt;
    }

    const auto& event = it->second;

    cv::Mat image = capture_encounter_image();
    if (image.empty()) {
        return std::nullopt;
    }

    int special_val = 0;
    // 水月的不好识别，先试试萨米能不能用
    if (m_config->get_theme() == RoguelikeTheme::Sami) {
        OCRer analyzer(image);
        analyzer.set_task_info(m_config->get_theme() + "@Roguelike@SpecialValRecognition");
        analyzer.set_replace(Task.get<OcrTaskInfo>("NumberOcrReplace")->replace_map);
        analyzer.set_use_char_model(true);
        if (!analyzer.analyze()) {
            // return std::nullopt;
            Log.error("Failed to recognize special value for event:", event.name);
        }
        else {
            utils::chars_to_number(analyzer.get_result().front().text, special_val);
        }
    }

    size_t choose_option = process_task(event, special_val);
    Log.info("Event:", event.name, "special_val", special_val, "choose option", choose_option);

    // 连续单选项事件会按页重复进入本函数，同一事件只向界面报告一次。
    if (m_reported_event_name != event.name) {
        m_reported_event_name = event.name;
        auto info = basic_info_with_what("RoguelikeEvent");
        info["details"]["name"] = event.name;
        info["details"]["default_choose"] = event.default_choose;
        info["details"]["choose_option"] = choose_option;
        callback(AsstMsg::SubTaskExtraInfo, info);
    }

    // 萨卡兹内容拓展 II，#11861
    if (event.name.starts_with("魂灵见闻：")) {
        Matcher matcher(image);
        matcher.set_task_info("Sarkaz@Roguelike@CloseCollectionClose");
        if (matcher.analyze()) {
            Log.trace("Found extra 'Plans', click CloseCollectionClose and StageEncounterJudgeClick");
            ctrler()->click(matcher.get_result().rect);
            ProcessTask(*this, { "Roguelike@StageEncounterJudgeClick" }).run();
            ProcessTask(*this, { "Roguelike@StageEncounterJudgeClick2" }).run();
        }
    }

    // 界园树洞
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        auto& bosky_map = RoguelikeBoskyPassageMap::get_instance();
        if (event.name == "掷地有声") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Ling);
        }
        else if (event.name == "种因得果") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Shu);
        }
        else if (event.name == "三缺一") {
            bosky_map.set_node_subtype(bosky_map.get_curr_pos(), RoguelikeBoskySubNodeType::Nian);
        }

        if (bosky_map.get_target_subtype() != RoguelikeBoskySubNodeType::Unknown) {
            if (bosky_map.get_node_subtype(bosky_map.get_curr_pos()) == bosky_map.get_target_subtype()) {
                Log.info(__FUNCTION__, "| Found target playtime node, completing task and exiting");

                auto target_info = basic_info_with_what("RoguelikeJieGardenTargetFound");
                target_info["details"]["target_subtype"] = subtype2name(bosky_map.get_target_subtype());
                callback(AsstMsg::SubTaskExtraInfo, target_info);

                // 完成任务，退出
                m_control_ptr->exit_then_stop();
                m_task_ptr->set_enable(false);
                return std::nullopt;
            }
        }
    }

    if (theme == RoguelikeTheme::BlackFlow) {
        return select_blackflow_option(event, choose_option);
    }

    // 界园只操作本次识别的选项，失败后结束当前事件处理。
    if (theme == RoguelikeTheme::JieGarden) {
        reset_option_list_and_view_data();
        if (update_option_list()) {
            if (const auto selected = select_event_option(plan_from_event(event, choose_option), event.name)) {
                report_selected_option(event, *selected, nullptr);
                return next_event(event);
            }
        }
        return std::nullopt;
    }

    const auto click_option_task_name = [&](size_t item, size_t total) {
        if (item > total) {
            Log.warn("Event:", event.name, "Total:", total, "Choice", item, "out of range, switch to choice", total);
            item = total;
        }
        return m_config->get_theme() + "@Roguelike@OptionChoose" + std::to_string(total) + "-" + std::to_string(item);
    };

    if (theme != RoguelikeTheme::Sarkaz) {
        auto option = Task.get(click_option_task_name(choose_option, event.option_num));
        if (!event.fallback_choices.empty()) {
            image = get_event_image();
            option = image.empty() ? nullptr : recognize_fixed_layout(theme, image, event, choose_option);
            if (!option) {
                if (auto* task = dynamic_cast<ProcessTask*>(m_task_ptr); task && !need_exit()) {
                    task->request_failure();
                    LogError << "Encounter option layout is unrecognized" << event.name;
                    if (!image.empty()) {
                        save_img(image, "unrecognized option layout");
                    }
                }
                return std::nullopt;
            }
        }
        const bool verify_retry = !event.fallback_choices.empty() &&
                                  Task.is_explicit_task(theme + "@Roguelike@StageEncounterFixedLayoutHeader");
        if (option &&
            select_option_with_confirmation(choose_option - 1, option, image, verify_retry ? &event : nullptr)) {
            return next_event(event);
        }
        return std::nullopt;
    }

    for (int j = 0; j < 2; ++j) {
        ProcessTask(*this, { click_option_task_name(choose_option, event.option_num) }).run();
        sleep(300);
    }

    sleep(1500);

    // 判断是否点击成功，成功进入对话后左上角的生命值会消失
    image = ctrler()->get_image();
    bool hp_disappeared = (hp(image) < 0);
    // fallback 可变选项，临时处理，之后还得改成更通用的方式
    if (!hp_disappeared) {
        for (const auto& [total, item] : event.fallback_choices) {
            Log.info("Trying fallback choice", total, "-", item);
            for (int j = 0; j < 2; ++j) {
                ProcessTask(*this, { click_option_task_name(item, total) }).run();
                sleep(300);
            }
            sleep(500);
            image = ctrler()->get_image();
            if (hp(image) < 0) {
                Log.info("Fallback choice success");
                hp_disappeared = true;
                break;
            }
        }
    }

    if (hp_disappeared) {
        return next_event(event);
    }

    // 兜底处理，从 option_num-option_num 点到 1-1
    size_t max_time = event.option_num;
    while (max_time > 0) {
        for (size_t i = max_time; i > 0; --i) {
            bool ret =
                ProcessTask(*this, { "Roguelike@TutorialButton" }).set_reusable_image(image).set_retry_times(3).run();
            if (ret) {
                return std::nullopt;
            }

            for (int j = 0; j < 2; ++j) {
                ProcessTask(*this, { click_option_task_name(i, max_time) }).run();
                sleep(300);
            }

            if (need_exit()) {
                return std::nullopt;
            }

            sleep(500);
            image = ctrler()->get_image();
            if (hp(image) < 0) {
                return std::nullopt;
            }
        }
        --max_time;
    }

    return event.next_event.empty() ? std::nullopt : std::optional { event.next_event };
}

bool asst::RoguelikeStageEncounterTaskPlugin::satisfies_condition(
    const Config::ChoiceRequire& requirement,
    const int special_val)
{
    int value = 0;
    bool ret = utils::chars_to_number(requirement.vision.value, value);
    Log.trace("special_val: ", special_val, "value: ", value);
    switch (requirement.vision.type) {
    case Config::ComparisonType::GreaterThan:
        ret &= special_val > value;
        Log.trace("special_val > value: ", special_val > value ? "true" : "false");
        break;
    case Config::ComparisonType::LessThan:
        ret &= special_val < value;
        Log.trace("special_val < value: ", special_val < value ? "true" : "false");
        break;
    case Config::ComparisonType::Equal:
        ret &= special_val == value;
        Log.trace("special_val == value: ", special_val == value ? "true" : "false");
        break;
    case Config::ComparisonType::None:
        Log.warn("no vision type");
        break;
    case Config::ComparisonType::Unsupported:
        Log.warn("unsupported vision type");
        return false;
    }
    /*
    switch (requirement.hp.type) {
        // ...
    }
    */
    if (!ret) {
        return false;
    }
    return true;
}

size_t asst::RoguelikeStageEncounterTaskPlugin::process_task(const Config::RoguelikeEvent& event, const int special_val)
{
    for (const auto& requirement : event.choice_require) {
        if (requirement.choose == -1) {
            continue;
        }
        if (satisfies_condition(requirement, special_val)) {
            return requirement.choose;
        }
    }
    return event.default_choose;
}

int asst::RoguelikeStageEncounterTaskPlugin::hp(const cv::Mat& image) const
{
    LogTraceFunction;

    if (m_config->get_theme() != RoguelikeTheme::Sarkaz) {
        Matcher hp_flag(image);
        hp_flag.set_task_info("Roguelike@HpFlag");
        return hp_flag.analyze() ? 0 : -1;
    }
    if (!ProcessTask(*this, { "Roguelike@HpFlag" }).run()) {
        Log.info("Not found HpFlag");
        return -1;
    }

    auto task = Task.get<OcrTaskInfo>("Roguelike@HpRecognition");
    std::vector<std::pair<std::string, std::string>> merged_map;
    merged_map.insert(merged_map.end(), task->replace_map.begin(), task->replace_map.end());
    merged_map.emplace_back("(.*)/.*", "$1");

    auto roi_image = make_roi(image, task->roi).clone();
    cv::Mat r_channel;
    cv::extractChannel(roi_image, r_channel, 2);
    cv::Mat mask;
    cv::threshold(r_channel, mask, 50, 255, cv::THRESH_BINARY);
    cv::Mat inv_mask;
    cv::bitwise_not(mask, inv_mask);
    roi_image.setTo(cv::Scalar(0, 0, 0), mask);

    RegionOCRer analyzer(roi_image);
    analyzer.set_replace(merged_map);
    analyzer.set_use_char_model(true);
    analyzer.set_bin_threshold(60); // 血量没有红色通道，虽然它看着很明显，但实际上在灰度中只有 2/3

    auto res_vec_opt = analyzer.analyze();
    if (!res_vec_opt) {
        return 0;
    }

    int hp_val;
    return utils::chars_to_number(res_vec_opt->text, hp_val) ? hp_val : 0;
}

bool asst::RoguelikeStageEncounterTaskPlugin::update_option_list()
{
    LogTraceFunction;

    const std::string& theme = m_config->get_theme();

    // 不期而遇默认位置会在选项列表中央, 为了从上到下检视选项列表, 需要先向上滑动
    if (!move_to_option_list_head()) {
        return false;
    }

    cv::Mat image = capture_encounter_image();
    if (image.empty()) {
        return false;
    }
    RoguelikeEncounterOptionAnalyzer analyzer(image);
    analyzer.set_theme(theme);
    for (size_t swipe_times = 0; swipe_times < MAX_SWIPE_TIMES && !need_exit(); ++swipe_times) {
        if (!move_forward()) {
            return false;
        }
        image = capture_encounter_image();
        if (image.empty()) {
            return false;
        }
        const std::optional<int> ret = analyzer.merge_image(image);
        if (!ret) {
            return false;
        }
        if (ret.value() <= 0) {
            break;
        }
    }

    if (need_exit() || !analyzer.analyze()) {
        return false;
    }

    m_option_list = analyzer.get_result();
    report_analyzed_options();

    return update_view(image);
}

asst::RoguelikeStageEncounterTaskPlugin::SelectionPlan
    asst::RoguelikeStageEncounterTaskPlugin::plan_from_event(const Config::RoguelikeEvent& event, size_t choose_option)
{
    return SelectionPlan {
        .option_text = event.option_text,
        .option_num = event.option_num,
        .choose = choose_option,
        .fallback_choices = event.fallback_choices,
        .allow_fallback = true,
        .continue_single_option = event.continue_single_option,
    };
}

asst::RoguelikeStageEncounterTaskPlugin::SelectionPlan
    asst::RoguelikeStageEncounterTaskPlugin::plan_from_rule(const blackflow::EncounterRule& rule)
{
    // 规则命中后只按规则选择，事件默认配置的编号与兜底表不再参与。
    return SelectionPlan {
        .option_text = rule.option_text,
        .option_num = rule.option_num,
        .choose = rule.choose,
        .allow_fallback = rule.allow_fallback,
    };
}

void asst::RoguelikeStageEncounterTaskPlugin::set_blackflow_result(std::string_view base_task)
{
    Task.set_task_base(std::string(BlackFlowResultTask), std::string(base_task));
}

void asst::RoguelikeStageEncounterTaskPlugin::report_selected_option(
    const Config::RoguelikeEvent& event,
    const SelectedOption& selected,
    const blackflow::EncounterRule* rule)
{
    auto info = basic_info_with_what("RoguelikeEventSelected");
    info["details"] = json::object {
        { "name", event.name },
        { "option_num", static_cast<std::int64_t>(m_option_list.size()) },
        { "choose_option", static_cast<std::int64_t>(selected.index + 1) },
        { "option_text", m_option_list[selected.index].text },
        { "rule_id", rule != nullptr ? rule->id : std::string() },
        { "rule_description", rule != nullptr ? rule->description : std::string() },
        { "used_fallback", selected.used_fallback },
    };
    callback(AsstMsg::SubTaskExtraInfo, info);
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::select_blackflow_option(
    const Config::RoguelikeEvent& event,
    size_t choose_option)
{
    reset_option_list_and_view_data();
    if (!update_option_list()) {
        LogError << "BlackFlow encounter option analysis failed";
        return std::nullopt;
    }
    for (const auto& [text, task] : event.option_tasks) {
        if (Task.get(task) == nullptr) {
            LogError << "Encounter result task is unavailable" << event.name << text << task;
            return std::nullopt;
        }
    }
    if (m_blackflow_session == nullptr) {
        LogError << "BlackFlow encounter session is unavailable";
        return std::nullopt;
    }

    std::vector<std::string> available_options;
    for (const auto& option : m_option_list) {
        if (option.enabled) {
            available_options.emplace_back(option.text);
        }
    }
    std::string error;
    const auto context =
        m_blackflow_session->prepare_encounter(event.name, m_option_list.size(), available_options, &error);
    if (!context.has_value()) {
        LogError << "BlackFlow encounter decision failed" << error;
        return std::nullopt;
    }
    const blackflow::EncounterRule* rule = context->rule.has_value() ? &*context->rule : nullptr;
    SelectionPlan plan = rule != nullptr ? plan_from_rule(*rule) : plan_from_event(event, choose_option);
    plan.continue_single_option = event.continue_single_option;

    const auto selected = select_event_option(plan, event.name);
    if (!selected.has_value()) {
        if (const auto* task = dynamic_cast<ProcessTask*>(m_task_ptr); task && !task->failure_requested()) {
            set_blackflow_result(BlackFlowAbandonTask);
        }
        return std::nullopt;
    }
    // 保留黑流确认完成后的页面稳定等待，再记录策略结果。
    if (!sleep(Task.get(std::string(BlackFlowLeaveConfirmCompletedTask))->post_delay)) {
        return std::nullopt;
    }
    const std::string& selected_text = m_option_list[selected->index].text;
    blackflow::EncounterSelection selection {
        .event_name = event.name,
        .option_num = m_option_list.size(),
        .choose = selected->index + 1,
        .option_text = selected_text,
        .rule_id = rule != nullptr ? rule->id : std::string(),
        .used_fallback = selected->used_fallback,
    };
    if (!m_blackflow_session->apply_encounter_selection(*context, std::move(selection), &error)) {
        LogError << "BlackFlow encounter result failed" << error;
        return std::nullopt;
    }
    report_selected_option(event, *selected, rule);

    if (m_blackflow_session->terminated()) {
        set_blackflow_result(BlackFlowTerminatedTask);
        return std::nullopt;
    }
    if (event.continue_single_option) {
        return continue_blackflow_event(event);
    }
    if (const auto result_task = event.option_tasks.find(selected_text); result_task != event.option_tasks.end()) {
        // 经独立入口转发，保留目标任务的模板、回调名称和执行次数。
        if (!Task.lazy_parse(
                json::object {
                    { std::string(BlackFlowOptionTask),
                      json::object { { "next", json::array { result_task->second } } } },
                })) {
            LogError << __FUNCTION__ << "Failed to configure encounter option task" << result_task->second;
            return std::nullopt;
        }
        set_blackflow_result(BlackFlowOptionTask);
        return std::nullopt;
    }
    set_blackflow_result(BlackFlowRewardTask);
    return next_event(event);
}

std::optional<asst::RoguelikeStageEncounterTaskPlugin::SelectedOption>
    asst::RoguelikeStageEncounterTaskPlugin::select_event_option(const SelectionPlan& plan, std::string_view event_name)
{
    if (plan.continue_single_option) {
        const auto enabled = [](const OptionAnalyzer::Option& option) {
            return option.enabled;
        };
        if (std::ranges::count_if(m_option_list, enabled) != 1) {
            LogError << "RoguelikeEncounter | Continuous event requires exactly one enabled option" << event_name;
            return std::nullopt;
        }
        const size_t index = std::distance(m_option_list.begin(), std::ranges::find_if(m_option_list, enabled));
        if (select_option_with_confirmation(index)) {
            return SelectedOption { index, false };
        }
        return std::nullopt;
    }

    size_t choice = 0; // 以 0 作为无效 index。
    bool used_fallback = false;
    if (!plan.option_text.empty()) {
        for (const std::string& target : plan.option_text) {
            const auto option_it = std::ranges::find_if(m_option_list, [&target](const OptionAnalyzer::Option& option) {
                return option.text == target && option.enabled;
            });
            if (option_it != m_option_list.end()) {
                choice = std::distance(m_option_list.begin(), option_it) + 1;
                break;
            }
        }
    }
    else if (plan.option_num == m_option_list.size()) {
        choice = plan.choose;
    }
    else {
        for (const auto& [total, item] : plan.fallback_choices) {
            if (total == m_option_list.size()) {
                choice = item;
                used_fallback = true;
                break;
            }
        }
    }

    if (choice == 0) {
        LogInfo << "RoguelikeEncounter | No configured choice for" << event_name << "with" << m_option_list.size()
                << "option(s)";
    }
    if (choice == 0 || choice > m_option_list.size() || !m_option_list[choice - 1].enabled) {
        if (!plan.allow_fallback) {
            LogError << "RoguelikeEncounter | Target option unavailable and fallback disabled" << event_name;
            return std::nullopt;
        }
        // 点击前确定可用备选，执行失败后不再操作另一条旧选项。
        choice = m_option_list.size();
        while (choice > 0 && !m_option_list[choice - 1].enabled) {
            --choice;
        }
        used_fallback = true;
    }
    if (choice == 0 || !select_option_with_confirmation(choice - 1)) {
        return std::nullopt;
    }
    return SelectedOption { choice - 1, used_fallback };
}

bool asst::RoguelikeStageEncounterTaskPlugin::select_option_with_confirmation(
    size_t index,
    const TaskPtr& fixed_option,
    cv::Mat first_image,
    const Config::RoguelikeEvent* fixed_event)
{
    LogTraceFunction;

    if (!fixed_option && index >= m_option_list.size()) [[unlikely]] {
        LogError << __FUNCTION__ << "Attempt to select option" << index + 1 << "out of" << m_option_list.size();
        return false;
    }
    if (!fixed_option && !m_option_list[index].enabled) {
        LogInfo << __FUNCTION__ << "Attempt to select disabled option" << index + 1;
        return false;
    }

    using Clock = std::chrono::steady_clock;
    constexpr auto ClickRetryDelay = std::chrono::seconds(3);
    constexpr unsigned ObservationDelay = 500;
    constexpr int MaxClicks = 2;

    auto* task = dynamic_cast<ProcessTask*>(m_task_ptr);
    if (!task) {
        LogError << __FUNCTION__ << "Encounter selection requires a ProcessTask";
        return false;
    }
    const auto confirm_task = Task.get(m_config->get_theme() + "@Roguelike@StageEncounterLeaveConfirm");
    const std::string& option_name = fixed_option ? fixed_option->name : m_option_list[index].text;
    cv::Mat fixed_option_image;
    bool had_hp = false;
    auto next_click = Clock::time_point {};
    int option_clicks = 0;
    int confirmation_clicks = 0;
    const auto stop_selection = [&](const cv::Mat& image, std::string_view reason) {
        task->request_failure();
        LogError << "Encounter selection stopped:" << reason << option_name << "option clicks" << option_clicks
                 << "confirmation clicks" << confirmation_clicks;
        if (!image.empty()) {
            save_img(image, reason);
        }
        return false;
    };
    while (!need_exit()) {
        const auto observed_at = Clock::now();
        const cv::Mat image = first_image.empty() ? get_event_image() : std::exchange(first_image, cv::Mat {});
        if (need_exit()) {
            return false;
        }

        if (image.empty()) {
            return option_clicks > 0 ? stop_selection(image, "encounter capture failed") : false;
        }
        Matcher::ResultOpt confirmation;
        if (option_clicks > 0) {
            // 点选后每张新图先检查确认，滑动定位期间也能接上晚到的确认。
            Matcher confirm(image);
            confirm.set_task_info(confirm_task->name);
            confirmation = confirm.analyze();
            if (need_exit()) {
                return false;
            }
            if (confirmation_clicks > 0 && !confirmation) {
                return true;
            }
        }

        if (fixed_option && !confirmation && (option_clicks == 0 || had_hp)) {
            Matcher hp_flag(image);
            hp_flag.set_task_info("Roguelike@HpFlag");
            had_hp = hp_flag.analyze().has_value();
            if (option_clicks > 0 && !had_hp) {
                return true;
            }
        }
        if (confirmation) {
            if (confirmation_clicks == 0 || observed_at >= next_click) {
                // 最后一次点击也先观察完整一轮，确认提前消失时仍按成功处理。
                if (confirmation_clicks >= std::min(MaxClicks, confirm_task->max_times)) {
                    return stop_selection(image, "confirmation still visible");
                }
                LogInfo << __FUNCTION__ << "Confirming option" << option_name;
                if (!ctrler()->click(confirmation->rect)) {
                    return stop_selection(image, "confirmation click failed");
                }
                ++confirmation_clicks;
                next_click = Clock::now() + ClickRetryDelay;
            }
        }
        else if (option_clicks == 0 || observed_at >= next_click) {
            if (option_clicks >= MaxClicks) {
                return stop_selection(image, "option still unconfirmed");
            }
            if (fixed_option) {
                const Rect& roi = fixed_option->specific_rect;
                if (fixed_option_image.empty()) {
                    fixed_option_image = make_roi(image, roi).clone();
                }
                else {
                    if (fixed_event) {
                        const auto current =
                            recognize_fixed_layout(m_config->get_theme(), image, *fixed_event, index + 1);
                        if (!current || current->name != fixed_option->name || current->specific_rect != roi) {
                            return stop_selection(image, "encounter layout changed");
                        }
                    }
                    Matcher same_option(image);
                    same_option.set_task_info("Roguelike@StageEncounterOption");
                    same_option.set_roi(roi);
                    same_option.set_templ(fixed_option_image);
                    if (!same_option.analyze()) {
                        return stop_selection(image, "encounter option changed");
                    }
                }
            }
            else {
                if (!update_view(image)) {
                    return option_clicks > 0 ? stop_selection(image, "encounter analysis failed") : false;
                }
                if (m_view_end == 0 || (option_clicks > 0 && m_option_y_in_view[index] == UNDEFINED)) {
                    return stop_selection(image, "encounter options disappeared");
                }

                // 每轮只推进一次定位，继续寻找同一目标；滑动和匹配不刷新点击计时。
                if (index < m_view_begin) {
                    if (!move_backward()) {
                        return false;
                    }
                    continue;
                }
                if (index >= m_view_end) {
                    if (!move_forward()) {
                        return false;
                    }
                    continue;
                }
                if (m_option_y_in_view[index] == UNDEFINED) {
                    LogError << __FUNCTION__ << "Target option is missing within the visible range" << index + 1;
                    return false;
                }
            }
            const bool clicked =
                fixed_option ? ctrler()->click(fixed_option->specific_rect) : click_analyzed_option(index);
            if (!clicked) {
                return stop_selection(image, "option click failed");
            }
            ++option_clicks;
            next_click = Clock::now() + ClickRetryDelay;
        }
        if (!sleep(ObservationDelay)) {
            return false;
        }
    }
    return false;
}

bool asst::RoguelikeStageEncounterTaskPlugin::click_analyzed_option(size_t index)
{
    if (need_exit()) {
        return false;
    }

    LogInfo << __FUNCTION__ << "Clicking option" << index + 1 << m_option_list[index].text;
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        Rect click_rect = Task.get("JieGarden@RoguelikeEncounter-ClickOption")->specific_rect;
        click_rect.y = m_option_y_in_view[index];
        return ctrler()->click(click_rect);
    }

    const Rect& header_rect = m_option_rect_in_view[index];
    if (header_rect.x == UNDEFINED) {
        LogError << __FUNCTION__ << "BlackFlow option header is unavailable";
        return false;
    }
    const Point click_point {
        header_rect.x + header_rect.width / 2 + 100,
        header_rect.y + header_rect.height / 2,
    };
    return ctrler()->click(click_point);
}

cv::Mat asst::RoguelikeStageEncounterTaskPlugin::capture_encounter_image()
{
    if (m_config->get_theme() == RoguelikeTheme::Sarkaz) {
        return ctrler()->get_image();
    }
    cv::Mat image = ctrler()->get_image_if_available();
    if (image.empty() && !need_exit()) {
        if (auto* task = dynamic_cast<ProcessTask*>(m_task_ptr)) {
            task->request_failure();
        }
    }
    return image;
}

cv::Mat asst::RoguelikeStageEncounterTaskPlugin::get_event_image()
{
    while (!need_exit()) {
        cv::Mat image = capture_encounter_image();
        if (image.empty()) {
            LogError << __FUNCTION__ << "Failed to capture the encounter page";
            return {};
        }

        OCRer loading(image);
        loading.set_task_info("LoadingText");
        if (!loading.analyze()) {
            return image;
        }

        // 加载结束后只返回本次事件，由调用者继续观察确认或下一组选项。
        ProcessTask wait_task(*this, { std::string(EncounterLoadingCompletedTask) + "@LoadingText" });
        wait_task.set_reusable_image(image);
        if (!wait_task.run() || need_exit() || wait_task.get_last_task_name() != EncounterLoadingCompletedTask) {
            return {};
        }
    }
    return {};
}

void asst::RoguelikeStageEncounterTaskPlugin::reset_option_list_and_view_data()
{
    m_option_list.clear();
    reset_view();
}

void asst::RoguelikeStageEncounterTaskPlugin::report_analyzed_options()
{
    std::vector<json::value> options;

    Log.info("Analyzed Options");
    Log.info(std::string(40, '-'));
    Log.info(std::format("{:^9} | {}", "Enabled", "Text"));
    Log.info(std::string(40, '-'));
    for (const auto& [enabled, templ, text, merged_y] : m_option_list) {
        json::value option = json::object {
            { "enabled", enabled },
            { "text", text },
        };
        options.emplace_back(std::move(option));
        Log.info(std::format("{:^9} | {}", enabled ? "Y" : "N", text));
    }
    Log.info(std::string(40, '-'));

    json::value info = basic_info_with_what("RoguelikeEncounterOptions");
    info["details"]["options"] = std::move(options);
    callback(AsstMsg::SubTaskExtraInfo, info);
}

bool asst::RoguelikeStageEncounterTaskPlugin::update_view(const cv::Mat& image)
{
    LogTraceFunction;

    reset_view();
    cv::Mat img = image.empty() ? capture_encounter_image() : image;
    if (img.empty() || need_exit()) {
        return false;
    }

    if (anchored_view_enabled(m_config->get_theme())) {
        update_anchored_view(img);
    }
    else {
        for (size_t i = 0; i < m_option_list.size() && !need_exit(); ++i) {
            if (const auto option_match = RoguelikeEncounterOptionAnalyzer::match_option(
                    m_config->get_theme(),
                    img,
                    m_option_list[i].templ)) {
                m_view_begin = std::min(m_view_begin, i);
                m_view_end = std::max(m_view_end, i + 1);
                m_option_y_in_view[i] = option_match->rect.y;
                m_option_rect_in_view[i] = option_match->rect;
            }
        }
    }

    LogInfo << __FUNCTION__ << std::format("| Current view is [{}, {}]", m_view_begin + 1, m_view_end);
    return !need_exit();
}

// 滑出画面的选项仍会在相似条带上取得过阈值的分数；只信任得分最高的一项，
// 其余选项按拼接图中的相对位置整体平移，落在搜索范围内才算可见。
void asst::RoguelikeStageEncounterTaskPlugin::update_anchored_view(const cv::Mat& image)
{
    const std::string& theme = m_config->get_theme();

    size_t anchor = m_option_list.size();
    Matcher::Result anchor_match;
    for (size_t i = 0; i < m_option_list.size() && !need_exit(); ++i) {
        const auto option_match = RoguelikeEncounterOptionAnalyzer::match_option(theme, image, m_option_list[i].templ);
        if (option_match && (anchor == m_option_list.size() || option_match->score > anchor_match.score)) {
            anchor = i;
            anchor_match = *option_match;
        }
    }
    if (anchor == m_option_list.size() || need_exit()) {
        return;
    }

    const int offset = anchor_match.rect.y - m_option_list[anchor].merged_y;
    const Rect roi = RoguelikeEncounterOptionAnalyzer::option_roi(theme, image.rows);
    for (size_t i = 0; i < m_option_list.size(); ++i) {
        Rect rect = anchor_match.rect;
        rect.y = m_option_list[i].merged_y + offset;
        if (rect.y < roi.y || rect.y + rect.height > roi.y + roi.height) {
            continue;
        }
        m_view_begin = std::min(m_view_begin, i);
        m_view_end = std::max(m_view_end, i + 1);
        m_option_y_in_view[i] = rect.y;
        m_option_rect_in_view[i] = rect;
    }

    LogInfo << __FUNCTION__ << "| Anchor option" << anchor + 1 << "score" << anchor_match.score << "offset" << offset;
}

void asst::RoguelikeStageEncounterTaskPlugin::reset_view()
{
    m_view_begin = m_option_list.size();
    m_view_end = 0;
    m_option_y_in_view.assign(m_option_list.size(), UNDEFINED);
    m_option_rect_in_view.assign(m_option_list.size(), Rect { UNDEFINED, UNDEFINED, 0, 0 });
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_to_option_list_head()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-InitialMoveUp" }).run() &&
           !need_exit();
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_forward()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-MoveDown" }).run() &&
           !need_exit();
}

bool asst::RoguelikeStageEncounterTaskPlugin::move_backward()
{
    LogTraceFunction;

    return !need_exit() && ProcessTask(*this, { m_config->get_theme() + "@RoguelikeEncounter-MoveUp" }).run() &&
           !need_exit();
}

std::optional<std::string>
    asst::RoguelikeStageEncounterTaskPlugin::continue_blackflow_event(const Config::RoguelikeEvent& event)
{
    ProcessTask continuation(*this, { std::string(BlackFlowContinueTask) });
    if (!continuation.run() || need_exit()) {
        return std::nullopt;
    }

    const std::string& last_task = continuation.get_last_task_name();
    if (last_task == BlackFlowContinueMapReadyTask) {
        set_blackflow_result(BlackFlowStagesTask);
        return std::nullopt;
    }
    if (last_task != BlackFlowContinueOptionsReadyTask) {
        LogError << "Continuous encounter did not reach another option page or the map" << event.name << last_task;
        return std::nullopt;
    }

    const cv::Mat image = capture_encounter_image();
    if (image.empty()) {
        return std::nullopt;
    }
    OCRer title(image);
    title.set_task_info(std::string(BlackFlowTitleOcrTask));
    title.set_required({ event.name });
    if (!title.analyze() || title.get_result().empty() || title.get_result().front().text != event.name) {
        LogError << "Continuous encounter title changed or could not be recognized" << event.name;
        return std::nullopt;
    }
    return event.name;
}

std::optional<std::string> asst::RoguelikeStageEncounterTaskPlugin::next_event(const Config::RoguelikeEvent& event)
{
    LogTraceFunction;

    if (event.next_event.empty()) {
        return std::nullopt;
    }
    if (m_config->get_theme() == RoguelikeTheme::JieGarden) {
        return next_jiegarden_event(event);
    }

    const auto& task = Task.get("Roguelike@StageEncounterJudgeClick");
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            ctrler()->click(task->specific_rect);
            sleep(500);
        }
        const cv::Mat image = capture_encounter_image();
        if (image.empty()) {
            return std::nullopt;
        }
        if (hp(image) >= 0) {
            Log.debug("HP restored, going to next_event:", event.next_event);
            // 多点一次，确保选项恢复
            ctrler()->click(task->specific_rect);
            sleep(500);
            return event.next_event;
        }
    }

    return std::nullopt;
}

std::optional<std::string>
    asst::RoguelikeStageEncounterTaskPlugin::next_jiegarden_event(const Config::RoguelikeEvent& event)
{
    // 保留原有三轮、每轮两次的叙述推进次数，每次点击前检查下一组选项。
    constexpr int MaxAdvanceClicks = 3 * 2;
    const auto click_task = Task.get("Roguelike@StageEncounterJudgeClick");
    for (int clicks = 0; clicks <= MaxAdvanceClicks && !need_exit(); ++clicks) {
        const cv::Mat image = get_event_image();
        if (image.empty() || need_exit()) {
            return std::nullopt;
        }

        Matcher options(image);
        options.set_task_info("JieGarden@RoguelikeEncounterOptionAnalyzer-OptionHeaderBar");
        if (options.analyze()) {
            return event.next_event;
        }
        Matcher map(image);
        map.set_task_info("Roguelike@TutorialButton");
        if (map.analyze() || clicks == MaxAdvanceClicks) {
            return std::nullopt;
        }
        if (need_exit() || !ctrler()->click(click_task->specific_rect) || !sleep(500)) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

bool asst::RoguelikeStageEncounterTaskPlugin::save_img(const cv::Mat& image, const std::string_view description)
{
    return utils::save_debug_image(image, utils::path("debug") / "roguelike" / "encounter", true, description);
}
