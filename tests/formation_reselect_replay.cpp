#include "Assistant.h"
#include "AsstCaller.h"
#include "Controller/Controller.h"
#include "Task/Interface/RoguelikeTask.h"
#include "Task/Roguelike/RoguelikeFormationTaskPlugin.h"

#include <atomic>
#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <thread>

class ReselectController final : public asst::ControllerAPI
{
public:
    ReselectController(cv::Mat overview, cv::Mat panel, int miss_close, int miss_reopen) :
        overview(std::move(overview)),
        panel(std::move(panel)),
        miss_close(miss_close),
        miss_reopen(miss_reopen)
    {
    }

    bool connect(const std::string&, const std::string&, const std::string&) override { return false; }

    bool inited() const noexcept override { return true; }

    const std::string& get_uuid() const override { return uuid; }

    size_t get_pipe_data_size() const noexcept override { return 0; }

    size_t get_version() const noexcept override { return 0; }

    bool screencap(cv::Mat& image, bool) override
    {
        if (test_cap) {
            image = cv::Mat::zeros(panel.size(), panel.type());
        }
        else {
            image = in_panel ? panel : overview;
        }
        return true;
    }

    bool start_game(const std::string&) override { return false; }

    bool stop_game(const std::string&) override { return false; }

    bool click(const asst::Point& point) override
    {
        const cv::Point p(point.x, point.y);
        if (in_panel && cv::Rect(821, 608, 139, 112).contains(p)) {
            ++clear;
        }
        else if (in_panel && cv::Rect(1015, 601, 265, 119).contains(p)) {
            if (reopened) {
                ++final_confirm;
                in_panel = false;
            }
            else {
                in_panel = ++close <= miss_close;
            }
        }
        else if (!in_panel && cv::Rect(817, 0, 402, 146).contains(p)) {
            in_panel = ++reopen > miss_reopen;
            reopened = in_panel;
        }
        else if (in_panel && cv::Rect(360, 70, 850, 500).contains(p)) {
            ++cards;
        }
        else {
            ++unexpected;
        }
        test_cap = close > 2 || reopen > 2;
        return true;
    }

    bool input(const std::string&) override { return false; }

    bool swipe(const asst::Point&, const asst::Point&, int, asst::SwipeExtraDirection, double, double, bool) override
    {
        ++swipes;
        return true;
    }

    bool inject_input_event(const asst::InputEvent&) override { return false; }

    bool press_esc() override { return false; }

    asst::ControlFeat::Feat support_features() const noexcept override { return {}; }

    std::pair<int, int> get_screen_res() const noexcept override { return { 1280, 720 }; }

    int clear = 0;
    int close = 0;
    int reopen = 0;
    int final_confirm = 0;
    int cards = 0;
    int unexpected = 0;
    int swipes = 0;
    bool test_cap = false;

private:
    const std::string uuid = "offline";
    cv::Mat overview, panel;
    int miss_close, miss_reopen;
    bool in_panel = true;
    bool reopened = false;
};

bool replay_chain_restart(const cv::Mat& overview, const cv::Mat& panel)
{
    using namespace std::chrono_literals;
    using asst::AsstMsg;
    std::atomic<int> terminal = 0;
    asst::Assistant assistant(
        [](AsstMsgId msg, const char*, void* state) {
            if (msg == static_cast<int>(AsstMsg::TaskChainError) ||
                msg == static_cast<int>(AsstMsg::TaskChainCompleted) ||
                msg == static_cast<int>(AsstMsg::TaskChainStopped)) {
                static_cast<std::atomic<int>*>(state)->store(msg);
            }
        },
        &terminal);
    for (const int misses : { 2, 0 }) {
        terminal = 0;
        auto backend = std::make_shared<ReselectController>(overview, panel, misses, 0);
        assistant.ctrler()->m_controller = backend;
        assistant.ctrler()->m_scale_proxy =
            std::make_shared<asst::ControlScaleProxy>(backend, asst::ControllerType::Adb, nullptr);
        if (!assistant.append_task("Roguelike", R"({"theme":"Sami","mode":0})")) {
            return false;
        }
        auto chain = std::dynamic_pointer_cast<asst::RoguelikeTask>(assistant.m_tasks_list.back().second);
        auto& process = *chain->m_roguelike_task_ptr;
        process.set_tasks({ "Sami@Roguelike@FormationReady" }).set_retry_times(1);
        if (!process.override_next("Sami@Roguelike@FormationConfirm", {}) || !assistant.start()) {
            return false;
        }
        const auto deadline = std::chrono::steady_clock::now() + 60s;
        while (assistant.running() || terminal == 0) {
            if (std::chrono::steady_clock::now() >= deadline) {
                assistant.stop();
                return false;
            }
            std::this_thread::sleep_for(20ms);
        }
        const auto expected = misses ? AsstMsg::TaskChainError : AsstMsg::TaskChainCompleted;
        std::cout << "Sami chain missed=" << misses << " terminal=" << terminal.load() << std::endl;
        if (terminal != static_cast<int>(expected) || backend->test_cap || backend->unexpected ||
            backend->final_confirm != (misses ? 0 : 1)) {
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc != 7 || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) || !AsstLoadResource(argv[3])) {
        return 2;
    }
    auto overview = cv::imread(argv[4]);
    auto panel = cv::imread(argv[5]);
    for (auto* frame : { &overview, &panel }) {
        if (frame->empty() || frame->cols * 9 != frame->rows * 16) {
            return 2;
        }
        cv::resize(*frame, *frame, { 1280, 720 }, 0, 0, cv::INTER_AREA);
    }
    // Simulate a card that remains unselected after a tap on the recorded two-card panel.
    panel(cv::Rect(520, 162, 3, 3)).setTo(cv::Scalar::all(255));
    asst::Assistant assistant;
    auto config = std::make_shared<asst::RoguelikeConfig>();
    if (!config->verify_and_load_params(json::object { { "theme", argv[6] }, { "mode", 0 } })) {
        return 2;
    }
    int failures = 0;
    const auto callback = [](asst::AsstMsg, const json::value&, asst::Assistant*) {
    };
    asst::ProcessTask task(callback, &assistant, "Roguelike");
    const std::string prefix = std::string(argv[6]) + "@Roguelike@";
    task.set_tasks({ prefix + "FormationReady" }).set_task_delay(0).set_retry_times(1);
    task.register_plugin<asst::RoguelikeFormationTaskPlugin>(config, nullptr)->set_retry_times(1);
    if (!task.override_next(prefix + "FormationConfirm", {})) {
        return 2;
    }
    for (auto [miss_close, miss_reopen] : { std::pair { 0, 0 }, { 0, 99 }, { 99, 0 }, { 1, 1 } }) {
        auto backend = std::make_shared<ReselectController>(overview, panel, miss_close, miss_reopen);
        assistant.ctrler()->m_controller = backend;
        assistant.ctrler()->m_scale_proxy =
            std::make_shared<asst::ControlScaleProxy>(backend, asst::ControllerType::Adb, nullptr);
        const bool result = task.run();
        const bool success = miss_close < 2 && miss_reopen < 2;
        const bool passed =
            result == success && task.failure_requested() == !success && !backend->test_cap && backend->clear == 1 &&
            backend->close == std::min(2, miss_close + 1) &&
            backend->reopen == (miss_close < 2 ? std::min(2, miss_reopen + 1) : 0) && backend->unexpected == 0 &&
            backend->final_confirm == (success ? 1 : 0) &&
            (success ? (backend->swipes > 0 && backend->cards == 3) : (backend->swipes == 0 && backend->cards == 1));
        std::cout << "Reselect " << argv[6] << " missed=" << miss_close << ',' << miss_reopen << " result=" << result
                  << " clear=" << backend->clear << " close=" << backend->close << " reopen=" << backend->reopen
                  << " final=" << backend->final_confirm << " swipes=" << backend->swipes << " cards=" << backend->cards
                  << " other=" << backend->unexpected << " cap=" << backend->test_cap << " pass=" << passed
                  << std::endl;
        failures += !passed;
    }
    if (std::string_view(argv[6]) == "Sami" && !replay_chain_restart(overview, panel)) {
        ++failures;
    }
    return failures ? 1 : 0;
}
