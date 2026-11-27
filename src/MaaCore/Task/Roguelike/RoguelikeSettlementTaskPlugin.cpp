#include "RoguelikeSettlementTaskPlugin.h"

#include "Config/GeneralConfig.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "MaaUtils/ImageIo.h"
#include "Utils/DebugImageHelper.hpp"
#include "Vision/Matcher.h"
#include "Vision/RegionOCRer.h"

bool asst::RoguelikeSettlementTaskPlugin::verify(AsstMsg msg, const json::value& details) const
{
    if (msg != AsstMsg::SubTaskStart || details.get("subtask", std::string()) != "ProcessTask") {
        return false;
    }

    const auto task_name = details.get("details", "task", "");
    const RoguelikeMode mode = m_config->get_mode();
    const bool report_settlement = mode == RoguelikeMode::Exp || mode == RoguelikeMode::BlackFlowBabyAnimal ||
                                   mode == RoguelikeMode::BlackFlowStartRun;
    if (task_name.ends_with("Roguelike@GamePass")) {
        m_game_pass = true;
        return report_settlement;
    }
    else if (task_name.ends_with("Roguelike@MissionFailedFlag2")) {
        m_game_pass = false;
        return report_settlement;
    }
    else {
        return false;
    }
}

bool asst::RoguelikeSettlementTaskPlugin::_run()
{
    LogTraceFunction;
    const auto& task = Task.get("RoguelikeSettlementConfirm");
    auto json_msg = basic_info_with_what("RoguelikeSettlement");
    json_msg["details"]["game_pass"] = m_game_pass;

    if (!sleep(task->special_params[0])) {
        return true;
    }

    cv::Mat image;
    static const std::string next_page_task = "Sami@RoguelikeSettlementNextPage";
    if (m_config->get_theme() == RoguelikeTheme::Sami && Task.is_explicit_task(next_page_task)) {
        image = read_sami_settlement_page(next_page_task);
    }
    else {
        if (m_game_pass) {
            save_img(ctrler()->get_image(), utils::path("achievement") / utils::path("roguelike"), "Page1");
        }
        const auto& rect = Task.get("Roguelike@ClickToStartPoint")->specific_rect;
        ctrler()->click(rect);
        if (sleep(task->pre_delay) && wait_for_whole_page() && sleep(task->post_delay)) {
            image = ctrler()->get_image();
        }
    }

    if (need_exit()) {
        return true;
    }
    if (image.empty()) {
        Log.error(__FUNCTION__, "wait for whole page failed");
        save_img(ctrler()->get_image(), utils::path("debug") / utils::path("roguelike"), "Page2_Error");
        return true;
    }

    if (m_game_pass) {
        save_img(image, utils::path("achievement") / utils::path("roguelike"), "Page2");
    }
    get_settlement_info(json_msg, image);
    if (!need_exit()) {
        callback(AsstMsg::SubTaskExtraInfo, json_msg);
    }
    return true;
}

bool asst::RoguelikeSettlementTaskPlugin::get_settlement_info(json::value& info, const cv::Mat& image)
{
    const auto& number_replace = Task.get<OcrTaskInfo>("NumberOcrReplace")->replace_map;
    const auto& append_data = [&](std::string task_name, const std::string& ocr_result) {
        int num = -1;
        if (!utils::chars_to_number(ocr_result, num)) {
            Log.error(__FUNCTION__, "convert str to int failed, task:", task_name, ", string:", ocr_result);
            return;
        }

        utils::tolowers(task_name);
        info["details"][task_name] = num;
    };

    auto analyze_battle_data = [&](const std::string& task_name) {
        RegionOCRer ocr(image);
        ocr.set_task_info("RoguelikeSettlementOcr-" + task_name);
        ocr.set_bin_threshold(0, 160);
        auto merge_map = number_replace;
        merge_map.emplace_back("[^\\d]+", "");
        ocr.set_replace(merge_map);
        if (!ocr.analyze()) {
            Log.error(__FUNCTION__, "analyze battle data failed, task:", task_name);
            return;
        }
        append_data(task_name, ocr.get_result().text);
    };

    auto analyze_text_data = [&](const std::string& task_name, bool is_number) {
        RegionOCRer ocr(image);
        ocr.set_task_info(m_config->get_theme() + "@RoguelikeSettlementOcr-" + task_name);
        ocr.set_bin_threshold(50, 255);
        const auto& task_replace = Task.get<OcrTaskInfo>("RoguelikeSettlementOcr-" + task_name)->replace_map;
        auto merge_map = number_replace;
        std::ranges::copy(task_replace, std::back_inserter(merge_map));
        ocr.set_replace(merge_map);
        if (!ocr.analyze()) {
            Log.error(__FUNCTION__, "analyze battle data failed, task:", task_name);
            return;
        }

        if (is_number) {
            append_data(task_name, ocr.get_result().text);
        }
        else {
            std::string tag = task_name;
            utils::tolowers(tag);
            info["details"][tag] = ocr.get_result().text;
        }
    };

    static const auto& battle_tasks =
        std::vector<std::string> { "Floor", "Step", "Combat", "Recruit", "Collection", "BOSS", "Emergency" };
    static const auto& text_tasks = std::vector<std::pair<std::string, bool>> { { "Difficulty", true },
                                                                                { "Score", true },
                                                                                { "Exp", false },
                                                                                { "Skill", false } };

    std::ranges::for_each(battle_tasks, analyze_battle_data);

    using Theme = RoguelikeTheme;
    for (const auto& [task, is_number] :
         text_tasks | std::views::take(m_config->get_theme() == Theme::Phantom ? 3 : 4)) {
        analyze_text_data(task, is_number);
    }

    return true;
}

bool asst::RoguelikeSettlementTaskPlugin::wait_for_whole_page()
{
    int retry = 0;
    cv::Mat image;
    do {
        image = ctrler()->get_image();
        Matcher matcher(image);
        matcher.set_task_info(m_config->get_theme() + "@RoguelikeSettlementConfirm");
        if (matcher.analyze()) {
            return true;
        }
        Log.error(__FUNCTION__, "RoguelikeSettlementConfirm match failed, retry:", retry);
        ++retry;
        sleep(Config.get_options().task_delay);
    } while (!need_exit() && retry < 20);
    return false;
}

cv::Mat asst::RoguelikeSettlementTaskPlugin::read_sami_settlement_page(const std::string& next_page_task)
{
    const auto timing = Task.get("RoguelikeSettlementConfirm");
    const std::string heading = m_game_pass ? "Sami@Roguelike@GamePass" : "Sami@Roguelike@MissionFailedFlag2";
    bool advanced = false;
    bool result_seen = false;
    int retries = 0;
    while (retries < 20 && !need_exit()) {
        const auto frame = ctrler()->get_image_if_available();
        if (frame.empty()) {
            break;
        }
        Matcher matcher(frame);
        matcher.set_task_info(heading);
        if (matcher.analyze()) {
            matcher.set_task_info("Sami@RoguelikeSettlementConfirm");
            if (matcher.analyze()) {
                if (result_seen) {
                    return frame;
                }
                result_seen = true;
                if (!sleep(timing->post_delay)) {
                    break;
                }
                continue;
            }
            if (!advanced && !result_seen) {
                matcher.set_task_info(next_page_task);
                if (const auto next = matcher.analyze(); next && !need_exit()) {
                    if (m_game_pass) {
                        save_img(frame, utils::path("achievement") / utils::path("roguelike"), "Page1");
                    }
                    if (need_exit() || !ctrler()->click(next->rect) || !sleep(timing->pre_delay)) {
                        break;
                    }
                    advanced = true;
                    continue;
                }
            }
        }
        else {
            matcher.set_task_info("Sami@Roguelike@StartExplore");
            if (matcher.analyze()) {
                break;
            }
            matcher.set_task_info("Sami@RoguelikeSettlementRecordPopup");
            if (matcher.analyze()) {
                LogInfo << __FUNCTION__ << "Sami expedition record is already open";
                break;
            }
        }
        ++retries;
        if (!sleep(Config.get_options().task_delay)) {
            break;
        }
    }
    return {};
}

void asst::RoguelikeSettlementTaskPlugin::save_img(
    const cv::Mat& image,
    const std::filesystem::path& relative_dir,
    std::string suffix)
{
    utils::save_debug_image(image, relative_dir, true, "", suffix);
}
