#include "Assistant.h"
#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "Task/Interface/RoguelikeTask.h"
#include "Task/ProcessTask.h"
#include "Task/Roguelike/JieGarden/RoguelikeCoppersTaskPlugin.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>

namespace
{
struct ExcessInput
{
};

class FrameController final : public asst::ControllerAPI
{
public:
    bool screencap(cv::Mat& image, bool) override
    {
        if (fail_capture_after && clicks >= *fail_capture_after) {
            return false;
        }
        image = frames.at(std::min(advance_on_capture ? captures : clicks, frames.size() - 1));
        ++captures;
        return true;
    }

    bool click(const asst::Point&) override
    {
        if (++clicks > budget) {
            throw ExcessInput {};
        }
        return true;
    }

    bool connect(const std::string&, const std::string&, const std::string&) override { return false; }

    bool inited() const noexcept override { return true; }

    const std::string& get_uuid() const override { return uuid; }

    size_t get_pipe_data_size() const noexcept override { return 0; }

    size_t get_version() const noexcept override { return 0; }

    bool start_game(const std::string&) override { return false; }

    bool stop_game(const std::string&) override { return false; }

    bool input(const std::string&) override { throw ExcessInput {}; }

    bool swipe(const asst::Point&, const asst::Point&, int, asst::SwipeExtraDirection, double, double, bool) override
    {
        throw ExcessInput {};
    }

    bool inject_input_event(const asst::InputEvent&) override { throw ExcessInput {}; }

    bool press_esc() override { throw ExcessInput {}; }

    asst::ControlFeat::Feat support_features() const noexcept override { return {}; }

    std::pair<int, int> get_screen_res() const noexcept override { return { 1280, 720 }; }

    std::vector<cv::Mat> frames;
    bool advance_on_capture = false;
    size_t captures = 0;
    size_t clicks = 0;
    size_t budget = 0;
    std::optional<size_t> fail_capture_after;

private:
    const std::string uuid = "offline";
};

cv::Mat make_page(const std::string& node, const std::string& label = {}, int shift = 0)
{
    const auto task = asst::Task.get<asst::MatchTaskInfo>(node);
    const auto& button = asst::TemplResource::get_instance().get_templ(task->templ_names.front());
    cv::Mat frame(720, 1280, CV_8UC3, cv::Scalar::all(24));
    const int x = task->roi.x + task->roi.width - button.cols - 3;
    const int y = task->roi.y + std::min(task->roi.height / 2 + shift, task->roi.height - button.rows);
    button.copyTo(frame(cv::Rect(x, y, button.cols, button.rows)));
    cv::putText(frame, label, { 825, y + button.rows / 2 + 9 }, cv::FONT_HERSHEY_SIMPLEX, 0.8, { 255, 255, 255 }, 2);
    return frame;
}
}

int main(int argc, char** argv)
{
    if ((argc != 5 && argc != 6) || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) ||
        !AsstLoadResource(argv[3])) {
        return 2;
    }
    const std::string theme = argv[4], prefix = theme + "@Roguelike@";
    asst::Assistant assistant;
    const auto backend = std::make_shared<FrameController>();
    assistant.ctrler()->m_controller = backend;
    assistant.ctrler()->m_scale_proxy =
        std::make_shared<asst::ControlScaleProxy>(backend, asst::ControllerType::Adb, nullptr);
    int errors = 0;
    int auth_errors = 0;
    auto callback = [&](asst::AsstMsg msg, const json::value& detail, asst::Assistant*) {
        if (msg == asst::AsstMsg::SubTaskError && detail.get("subtask", std::string()) == "ProcessTask") {
            ++errors;
            auth_errors += detail.get("what", std::string()) == "RoguelikeAuthenticationFailed";
        }
    };
    asst::RoguelikeTask roguelike(callback, &assistant);
    if (!roguelike.set_params(json::object { { "theme", theme }, { "mode", 0 } })) {
        return 2;
    }
    auto& process = *roguelike.m_roguelike_task_ptr;
    process.clear_plugin();
    process.set_task_delay(0).set_retry_times(0);
    const auto run = [&](const std::string& start,
                         std::vector<cv::Mat> frames,
                         size_t expected_clicks,
                         bool success,
                         std::optional<size_t> fail_capture_after = std::nullopt) {
        backend->frames = std::move(frames);
        backend->clicks = 0;
        backend->captures = 0;
        backend->budget = expected_clicks;
        backend->fail_capture_after = fail_capture_after;
        errors = 0;
        process.set_tasks({ start });
        bool passed = false;
        try {
            const bool result = process.run();
            passed = result == success && backend->clicks == expected_clicks && errors == int(!success);
        }
        catch (const ExcessInput&) {
            // A broken fallback can otherwise click the same offline frame forever.
        }
        std::cout << "Fallback " << start << " clicks=" << backend->clicks << " expected=" << expected_clicks
                  << " errors=" << errors << " expectSuccess=" << success << " pass=" << passed << std::endl;
        return passed;
    };
    const auto auth_frame = argc == 6 ? cv::imread(argv[5]) : make_page("RoguelikeAuthenticationFailed");
    if (auth_frame.empty() || auth_frame.size() != cv::Size(1280, 720)) {
        return 2;
    }
    const std::string node = prefix + (theme == "JieGarden" ? "CoppersAbandonExchange" : "StageEncounterLeaveConfirm");
    if (!process.override_next(node, {})) {
        return 2;
    }
    process.set_retry_times(2);
    for (int i = 1; i <= 2; ++i) {
        if (!run(node, { auth_frame }, 0, false) || auth_errors != i || backend->captures != 1) {
            return 1;
        }
    }
    if (!run(node, { make_page(node) }, 1, true) || auth_errors != 2) {
        return 1;
    }
    if (theme == "JieGarden") {
        auto coppers = process.register_plugin<asst::RoguelikeCoppersTaskPlugin>(
            roguelike.m_config_ptr, roguelike.m_control_ptr);
        const auto take = prefix + "CoppersTakeFlag";
        backend->advance_on_capture = true;
        if (!run(take, { make_page(take), auth_frame }, 0, false) || auth_errors != 3) {
            return 1;
        }
        backend->advance_on_capture = false;
        if (!run(node, { make_page(node) }, 1, true)) {
            return 1;
        }

        // A failed type probe must never turn an authentication dialog into a last-column swipe or click.
        backend->frames = { auth_frame };
        backend->clicks = backend->budget = backend->captures = 0;
        if (coppers->run_copper_task(prefix + "CoppersAnalyzer-TypeSelected", 2) || !process.failure_requested() ||
            backend->captures != 1 || coppers->swipe_copper_list_to_rightmost(2)) {
            return 1;
        }
        coppers->click_copper_at_position(1, 1);
        if (backend->captures != 1 || !run(node, { make_page(node) }, 1, true) || auth_errors != 3) {
            return 1;
        }
        cv::Mat exchange(720, 1280, CV_8UC3, cv::Scalar::all(24));
        cv::putText(exchange, ">>>", { 588, 594 }, cv::FONT_HERSHEY_SIMPLEX, 1.0, { 255, 255, 255 }, 2);
        backend->advance_on_capture = true;
        backend->frames = { exchange, auth_frame };
        backend->clicks = backend->budget = backend->captures = 0;
        if (coppers->run_copper_task(prefix + "CoppersExchangeFinish", 0) || !process.failure_requested() ||
            backend->captures != 2 || backend->clicks) {
            return 1;
        }
        backend->advance_on_capture = false;
        if (!run(node, { make_page(node) }, 1, true)) {
            return 1;
        }
        process.clear_plugin();
    }
    if (argc == 6) {
        return 0;
    }
    process.remove_override_next(node);
    process.set_retry_times(0);
    const auto confirm = prefix + "StageEncounterLeaveConfirm";
    int failures = 0;
    const std::vector<std::string> options = theme == "BlackFlow"
                                                 ? std::vector<std::string> { confirm }
                                                 : std::vector<std::string> { prefix + "StageEncounterOptionLeave",
                                                                              prefix + "StageEncounterOptionFree",
                                                                              prefix + "StageEncounterOptionUnknown",
                                                                              confirm };
    for (const auto& node : options) {
        process.set_post_delay(node, 0);
        const auto frame = make_page(node);
        if (theme != "BlackFlow" && !process.override_next(node, { node })) {
            return 2;
        }
        failures += !run(node, { frame }, 2, false);
        const auto first = make_page(node, "ORANGE");
        const auto shifted = make_page(node, "ORANGE", 2);
        failures += !run(node, { first, shifted, first }, 2, false);
        const auto second = make_page(node, "WINTER");
        failures += !run(node, { first, first, second, second }, 4, false);
        failures += !run(node, { first, second, first, second, first }, 4, false);
        if (theme == "BlackFlow") {
            const auto blank = cv::Mat::zeros(frame.size(), frame.type());
            failures += !run(node, { frame, frame, blank }, 2, true);
            failures += process.get_last_task_name() != prefix + "StageEncounterLeaveConfirmCompleted";
        }
        else {
            process.remove_override_next(node);
        }
    }
    failures += !run(confirm, { make_page(confirm) }, 1, false, 1);
    if (theme != "BlackFlow") {
        const auto leave = options.front();
        const auto frame = make_page(leave), confirmed = make_page(confirm);
        const auto destination = prefix + "ChooseOperFlag";
        const auto first = make_page(leave, "ORANGE"), second = make_page(leave, "WINTER");
        const auto third = make_page(leave, "CLOUD"), confirmation = make_page(confirm, "CONFIRM");
        if (!process.override_next(destination, {})) {
            return 2;
        }
        failures += !run(
            leave,
            { first, confirmation, second, confirmation, third, confirmation, make_page(destination) },
            6,
            true);
        failures += process.get_last_task_name() != destination;
        if (!process.override_next(confirm, { confirm, leave })) {
            return 2;
        }
        failures +=
            !run(leave, { first, confirmation, confirmation, second, confirmation, first, confirmation }, 6, false);
        failures +=
            !run(leave, { first, confirmation, make_page(confirm, "ACCEPT"), make_page(confirm, "FINISH") }, 3, false);
        if (!process.override_next(confirm, {})) {
            return 2;
        }
        failures += !run(leave, { frame, frame, confirmed }, 3, true);
        // Keep the same input budget through passive recognition and alternating fallback candidates.
        const auto passive = "Roguelike@StageEncounterLoadingCompleted";
        const auto free_option = prefix + "StageEncounterOptionFree";
        if (!process.override_next(leave, { passive }) || !process.override_next(passive, { free_option, leave }) ||
            !process.override_next(free_option, { leave })) {
            return 2;
        }
        failures += !run(leave, { frame }, 2, false);
        const auto free_page = make_page(free_option);
        failures += !run(leave, { frame, free_page, frame, free_page, frame }, 4, false);
    }
    return failures ? 1 : 0;
}
