#include "Assistant.h"
#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "Controller/Controller.h"
#include "Task/Roguelike/BlackFlow/BlackFlowSession.h"
#include "Task/Roguelike/RoguelikeStageEncounterTaskPlugin.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

enum class Fault
{
    None,
    ChangedPage,
    CaptureFailed,
    OptionFailed,
    ConfirmationFailed,
    DirectTransition,
    NoAnchors
};

struct Scenario
{
    int missed_options;
    int missed_confirmations;
    Fault fault = Fault::None;
    int pages = 1;
    bool blackflow_route = false;
};

struct UnexpectedInput
{
};

class SelectionController final : public asst::ControllerAPI
{
public:
    SelectionController(const std::string& theme, const cv::Mat& button, const cv::Mat& hp, bool fixed)
    {
        page = cv::Mat::zeros(720, 1280, CV_8UC3);
        dialogue = page.clone();
        if (fixed) {
            hp.copyTo(page(cv::Rect(120, 15, hp.cols, hp.rows)));
        }
        other_page = page.clone();
        cv::RNG rng(42);
        if (fixed) {
            const auto rect = asst::Task.get(theme + "@Roguelike@OptionChoose1-1")->specific_rect;
            option_rect = cv::Rect(rect.x, rect.y, rect.width, rect.height);
            auto strip = page(option_rect);
            rng.fill(strip, cv::RNG::UNIFORM, 0, 255);
        }
        else {
            const auto header = asst::Task.get(theme + "@RoguelikeEncounterOptionAnalyzer-Option")->specific_rect;
            cv::Mat strip = page(cv::Rect(header.x, 288, header.width, header.height));
            rng.fill(strip, cv::RNG::UNIFORM, 0, 255);
            option = { true, strip.clone(), "option", 288 };
            option_rect = theme == "JieGarden"
                              ? cv::Rect(1175, 288, 100, 43)
                              : cv::Rect(header.x + header.width / 2 + 100, 288 + header.height / 2, 1, 1);
        }
        pressed = page.clone();
        confirmation_rect = cv::Rect(1280 - button.cols - 8, 190, button.cols, button.rows);
        button.copyTo(pressed(confirmation_rect));
    }

    void set_layout(
        const std::string& theme,
        int rows,
        int chosen,
        int visible,
        const cv::Mat& marker,
        const cv::Mat& button,
        int first_visible = 1,
        int vision = 0)
    {
        page = other_page.clone();
        for (int i = 1; i <= rows; ++i) {
            const auto rect =
                asst::Task.get(theme + "@Roguelike@OptionChoose" + std::to_string(rows) + "-" + std::to_string(i))
                    ->specific_rect;
            if (i == chosen) {
                option_rect = cv::Rect(rect.x, rect.y, rect.width, rect.height);
            }
            if (i >= first_visible && i < first_visible + visible) {
                marker.copyTo(page(cv::Rect(1180, rect.y + (rect.height - marker.rows) / 2, marker.cols, marker.rows)));
            }
        }
        if (vision) {
            cv::putText(
                page,
                std::to_string(vision),
                { 625, 36 },
                cv::FONT_HERSHEY_SIMPLEX,
                1.25,
                cv::Scalar(255, 255, 255),
                2,
                cv::LINE_AA);
        }
        pressed = page.clone();
        button.copyTo(pressed(confirmation_rect));
    }

    void reset(Scenario scenario)
    {
        current = scenario;
        saw_failed_reconnect = false;
        showing_confirmation = false;
        options = confirms = followups = unexpected = frames_after_click = 0;
        previous_options = previous_confirms = 0;
    }

    bool screencap(cv::Mat& image, bool reconnect) override
    {
        if (options > 0 && current.fault == Fault::CaptureFailed) {
            saw_failed_reconnect |= reconnect;
            return false;
        }
        showing_confirmation = false;
        // A confirmation can first appear on a later screenshot after the last allowed click.
        if (options > 0 && current.fault == Fault::DirectTransition) {
            dialogue.copyTo(image);
        }
        else if (current.fault == Fault::NoAnchors || (options > 0 && current.fault == Fault::ChangedPage)) {
            other_page.copyTo(image);
        }
        else if (
            options - previous_options > current.missed_options &&
            confirms - previous_confirms <= current.missed_confirmations && ++frames_after_click > 1) {
            showing_confirmation = true;
            pressed.copyTo(image);
        }
        else {
            page.copyTo(image);
            if (confirms - previous_confirms > current.missed_confirmations) {
                previous_options = options;
                previous_confirms = confirms;
            }
        }
        return true;
    }

    bool click(const asst::Point& point) override
    {
        const cv::Point p(point.x, point.y);
        if (showing_confirmation) {
            if (!confirmation_rect.contains(p)) {
                ++unexpected;
                return false;
            }
            ++confirms;
            return confirms - previous_confirms <= 2 && current.fault != Fault::ConfirmationFailed;
        }
        if (option_rect.contains(p)) {
            ++options;
            frames_after_click = 0;
            if (options - previous_options > 2) {
                throw UnexpectedInput {};
            }
            return current.fault != Fault::OptionFailed;
        }
        if (cv::Rect(635, 355, 11, 11).contains(p)) {
            ++followups;
            return true;
        }
        ++unexpected;
        return false;
    }

    bool connect(const std::string&, const std::string&, const std::string&) override { return false; }

    bool inited() const noexcept override { return true; }

    const std::string& get_uuid() const override { return uuid; }

    size_t get_pipe_data_size() const noexcept override { return 0; }

    size_t get_version() const noexcept override { return 0; }

    bool start_game(const std::string&) override { return false; }

    bool stop_game(const std::string&) override { return false; }

    bool input(const std::string&) override { return false; }

    bool swipe(const asst::Point&, const asst::Point&, int, asst::SwipeExtraDirection, double, double, bool) override
    {
        if (current.blackflow_route && options == 0) {
            return true;
        }
        ++unexpected;
        throw UnexpectedInput {};
    }

    bool inject_input_event(const asst::InputEvent&) override { return false; }

    bool press_esc() override { return false; }

    asst::ControlFeat::Feat support_features() const noexcept override { return {}; }

    std::pair<int, int> get_screen_res() const noexcept override { return { 1280, 720 }; }

    asst::RoguelikeEncounterOptionAnalyzer::Option option;
    bool saw_failed_reconnect = false;
    int options = 0, confirms = 0, followups = 0, unexpected = 0;

private:
    const std::string uuid = "offline";
    cv::Mat page, pressed, other_page, dialogue;
    cv::Rect option_rect, confirmation_rect;
    Scenario current {};
    bool showing_confirmation = false;
    int frames_after_click = 0;
    int previous_options = 0, previous_confirms = 0;
};

class SelectionPlugin final : public asst::RoguelikeStageEncounterTaskPlugin
{
public:
    using RoguelikeStageEncounterTaskPlugin::RoguelikeStageEncounterTaskPlugin;

    bool _run() override
    {
        for (int page = 0; page < pages; ++page) {
            if (!event.empty()) {
                handle_single_event(event);
                if (dynamic_cast<asst::ProcessTask*>(m_task_ptr)->failure_requested()) {
                    return false;
                }
            }
            else if (!select_option_with_confirmation(0)) {
                return false;
            }
        }
        return true;
    }

    std::string event;
    int pages = 1;
};

int main(int argc, char** argv)
{
    if (argc != 6 || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) || !AsstLoadResource(argv[3])) {
        return 2;
    }
    const std::string theme = argv[4];
    const auto button = cv::imread(argv[5]);
    auto config = std::make_shared<asst::RoguelikeConfig>();
    if (button.empty() || !config->verify_and_load_params(json::object { { "theme", theme }, { "mode", 0 } })) {
        return 2;
    }
    asst::Assistant assistant;
    const std::map<std::string, std::string> fixed_events { { "Phantom", "扎营" },
                                                            { "Mizuki", "悬高之葬" },
                                                            { "Sami", "度假胜地" } };
    const bool fixed = fixed_events.contains(theme);
    const auto hp = cv::imread(std::string(argv[2]) + "/resource/template/Roguelike/base/Roguelike@HpFlag.png");
    if (hp.empty()) {
        return 2;
    }
    auto backend = std::make_shared<SelectionController>(theme, button, hp, fixed);
    assistant.ctrler()->m_controller = backend;
    assistant.ctrler()->m_scale_proxy =
        std::make_shared<asst::ControlScaleProxy>(backend, asst::ControllerType::Adb, nullptr);
    int process_errors = 0;
    auto cb = [&](asst::AsstMsg msg, const json::value& detail, asst::Assistant*) {
        if (msg == asst::AsstMsg::SubTaskError && detail.get("subtask", std::string()) == "ProcessTask") {
            ++process_errors;
        }
    };
    asst::ProcessTask task(cb, &assistant, "Roguelike");
    const auto plugin = task.register_plugin<SelectionPlugin>(config, nullptr);
    plugin->set_retry_times(0);
    plugin->m_option_list = { backend->option };
    if (fixed) {
        plugin->event = fixed_events.at(theme);
    }
    const std::string trigger = theme + "@Roguelike@StageEncounterJudgeOption";
    const std::string followup = "Roguelike@StageEncounterJudgeClick";
    if (!task.override_next(trigger, { followup }) || !task.override_next(followup, {})) {
        return 2;
    }
    task.set_tasks({ trigger }).set_task_delay(0);
    int failures = 0;
    std::vector<Scenario> cases { { 0, 0 },
                                  { 1, 0 },
                                  { 0, 1 },
                                  { 99, 0 },
                                  { 0, 99 },
                                  { 0, 0, Fault::ChangedPage },
                                  { 0, 0, Fault::CaptureFailed },
                                  { 0, 0, Fault::OptionFailed },
                                  { 0, 0, Fault::ConfirmationFailed },
                                  { 1, 0, Fault::None, 2 } };
    if (fixed) {
        cases.push_back({ 0, 0, Fault::DirectTransition });
    }
    if (!fixed) {
        cases.push_back({ 0, 0, Fault::NoAnchors });
    }
    if (theme == "BlackFlow") {
        cases.push_back({ 99, 0, Fault::None, 1, true });
    }
    for (const Scenario scenario : cases) {
        if (scenario.blackflow_route) {
            const auto marker = cv::imread(
                std::string(argv[2]) +
                "/resource/template/Roguelike/BlackFlow/"
                "BlackFlow@RoguelikeEncounterOptionAnalyzer-OptionHeaderBar-40.png");
            if (marker.empty()) {
                return 2;
            }
            marker.copyTo(backend->page(cv::Rect(835, 288, marker.cols, marker.rows)));
            auto session = std::make_shared<asst::blackflow::BlackFlowSession>();
            session->m_policy.emplace();
            session->m_page_context.emplace().stage = asst::blackflow::PageExecutionStage::Running;
            session->m_transaction.emplace().m_stage = asst::blackflow::MoveTransactionStage::Committed;
            plugin->event = "未涉足之树";
            plugin->set_blackflow_session(session);
        }
        backend->reset(scenario);
        plugin->pages = scenario.pages;
        process_errors = 0;
        const bool success = (scenario.fault == Fault::None || scenario.fault == Fault::DirectTransition) &&
                             scenario.missed_options < 2 && scenario.missed_confirmations < 2;
        bool result = false;
        try {
            result = task.run();
        }
        catch (const UnexpectedInput&) {
            // Bound unpatched retries so the negative control can finish.
        }
        const int confirms = (scenario.fault != Fault::None && scenario.fault != Fault::ConfirmationFailed) ||
                                     scenario.missed_options >= 2
                                 ? 0
                                 : std::min(2, scenario.missed_confirmations + 1);
        const bool route_preserved = !scenario.blackflow_route ||
                                     asst::Task.m_json_all_tasks_info.at("BlackFlow@Roguelike@StageEncounterResult")
                                             .get("baseTask", std::string()) == "BlackFlow@Roguelike@RecoveryFailed";
        const int options =
            scenario.fault == Fault::NoAnchors ? 0 : scenario.pages * std::min(2, scenario.missed_options + 1);
        const bool passed = route_preserved && result == success && backend->options == options &&
                            backend->confirms == scenario.pages * confirms && backend->followups == int(success) &&
                            backend->unexpected == 0 && process_errors == int(!success) &&
                            backend->saw_failed_reconnect == (scenario.fault == Fault::CaptureFailed);
        std::cout << "Selection " << theme << " missedOptions=" << scenario.missed_options
                  << " missedConfirms=" << scenario.missed_confirmations
                  << " fault=" << static_cast<int>(scenario.fault) << " pages=" << scenario.pages
                  << " result=" << result << " options=" << backend->options << " confirms=" << backend->confirms
                  << " followups=" << backend->followups << " unexpected=" << backend->unexpected
                  << " processErrors=" << process_errors << " routePreserved=" << route_preserved << " pass=" << passed
                  << std::endl;
        failures += !passed;
    }
    if (theme == "Phantom" || theme == "Sami") {
        plugin->event = theme == "Phantom" ? "解脱" : "探寻前路";
        const int standard_rows = theme == "Phantom" ? 2 : 3;
        const auto path = std::string(argv[2]) + "/resource/template/Roguelike/" + theme + "/" + theme +
                          "@Roguelike@StageEncounterOptionLeave.png";
        const auto marker = cv::imread(path);
        if (marker.empty()) {
            return 2;
        }
        for (const auto [rows, visible] : std::vector<std::pair<int, int>> { { standard_rows, standard_rows },
                                                                             { standard_rows - 1, standard_rows - 1 },
                                                                             { standard_rows, 1 },
                                                                             { standard_rows, 0 } }) {
            backend->reset({ 0, 0 });
            plugin->pages = 1;
            backend->set_layout(theme, rows, std::min(rows, 2), visible, marker, button);
            process_errors = 0;
            const bool success = rows == visible;
            const bool result = task.run();
            const bool passed = result == success && backend->options == int(success) &&
                                backend->confirms == int(success) && backend->followups == int(success) &&
                                backend->unexpected == 0 && process_errors == int(!success);
            std::cout << "Selection layout " << theme << " rows=" << rows << " visible=" << visible
                      << " result=" << result << " options=" << backend->options << " confirms=" << backend->confirms
                      << " unexpected=" << backend->unexpected << " pass=" << passed << std::endl;
            failures += !passed;
        }
    }
    if (theme == "Sami") {
        plugin->event = "鲍勃杂货店";

        struct ShopLayout
        {
            int rows, choice, visible, first_visible, vision;
            bool leave, success;
        };

        const auto base =
            std::string(argv[2]) + "/resource/template/Roguelike/Sami/Sami@Roguelike@StageEncounterOption";
        const auto leave = cv::imread(base + "Leave.png"), relic = cv::imread(base + "Free.png");
        if (leave.empty() || relic.empty()) {
            return 2;
        }
        for (const ShopLayout layout : std::vector<ShopLayout> { { 3, 1, 3, 1, 0, false, true },
                                                                 { 4, 4, 4, 1, 0, false, true },
                                                                 { 1, 1, 1, 1, 0, true, true },
                                                                 { 3, 2, 1, 2, 0, false, false },
                                                                 { 3, 2, 1, 2, 2, false, true } }) {
            backend->reset({ 0, 0 });
            plugin->pages = 1;
            backend->set_layout(
                theme,
                layout.rows,
                layout.choice,
                layout.visible,
                layout.leave ? leave : relic,
                button,
                layout.first_visible,
                layout.vision);
            if (layout.vision == 2) {
                backend->option_rect &= cv::Rect(1175, 335, 100, 60);
            }
            process_errors = 0;
            const bool result = task.run();
            const bool passed = result == layout.success && backend->options == int(layout.success) &&
                                backend->confirms == int(layout.success) && backend->followups == int(layout.success) &&
                                backend->unexpected == 0 && process_errors == int(!layout.success);
            std::cout << "Selection shop rows=" << layout.rows << " visible=" << layout.visible
                      << " vision=" << layout.vision << " leave=" << layout.leave << " result=" << result
                      << " options=" << backend->options << " unexpected=" << backend->unexpected << " pass=" << passed
                      << std::endl;
            failures += !passed;
        }
    }
    return failures ? 1 : 0;
}
