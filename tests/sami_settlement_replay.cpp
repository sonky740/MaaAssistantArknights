#include "Assistant.h"
#include "AsstCaller.h"
#include "Controller/Controller.h"
#include "Task/Roguelike/RoguelikeSettlementTaskPlugin.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <optional>

namespace
{
struct UnexpectedInput
{
};

class SettlementController final : public asst::ControllerAPI
{
public:
    bool screencap(cv::Mat& image, bool) override
    {
        ++captures;
        image = frames.at(std::min(position, frames.size() - 1));
        if (!awaiting_advance) {
            ++position;
        }
        return true;
    }

    bool click(const asst::Point& point) override
    {
        ++clicks;
        if (!awaiting_advance || point.x < 1188 || point.x >= 1278 || point.y < 306 || point.y >= 416) {
            throw UnexpectedInput {};
        }
        awaiting_advance = false;
        position = 1;
        return true;
    }

    bool connect(const std::string&, const std::string&, const std::string&) override { return false; }

    bool inited() const noexcept override { return true; }

    const std::string& get_uuid() const override { return m_uuid; }

    size_t get_pipe_data_size() const noexcept override { return 0; }

    size_t get_version() const noexcept override { return 0; }

    bool start_game(const std::string&) override { return false; }

    bool stop_game(const std::string&) override { return false; }

    bool input(const std::string&) override { throw UnexpectedInput {}; }

    bool swipe(const asst::Point&, const asst::Point&, int, asst::SwipeExtraDirection, double, double, bool) override
    {
        throw UnexpectedInput {};
    }

    bool inject_input_event(const asst::InputEvent&) override { throw UnexpectedInput {}; }

    bool press_esc() override { throw UnexpectedInput {}; }

    asst::ControlFeat::Feat support_features() const noexcept override { return {}; }

    std::pair<int, int> get_screen_res() const noexcept override { return { 1280, 720 }; }

    std::vector<cv::Mat> frames;
    bool awaiting_advance = false;
    size_t position = 0;
    size_t captures = 0;
    int clicks = 0;

private:
    const std::string m_uuid = "offline";
};

cv::Mat read_frame(const char* path)
{
    auto frame = cv::imread(path);
    if (frame.empty() || frame.cols * 9 != frame.rows * 16) {
        return {};
    }
    cv::resize(frame, frame, { 1280, 720 }, 0, 0, cv::INTER_AREA);
    return frame;
}
}

int main(int argc, char** argv)
{
    if ((argc != 11 && argc != 12) || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) ||
        !AsstLoadResource(argv[3])) {
        return 2;
    }
    const bool game_pass = argc == 12 && std::string_view(argv[11]) == "win";
    if (argc == 12 && !game_pass) {
        return 2;
    }
    const auto first = read_frame(argv[4]);
    const auto loading = read_frame(argv[5]);
    const auto result = read_frame(argv[6]);
    const auto popup = read_frame(argv[7]);
    const auto home = read_frame(argv[8]);
    if (first.empty() || loading.empty() || result.empty() || popup.empty() || home.empty()) {
        return 2;
    }
    const int expected_score = std::stoi(argv[9]);
    const std::string expected_exp = argv[10];

    asst::Assistant assistant;
    const auto backend = std::make_shared<SettlementController>();
    assistant.ctrler()->m_controller = backend;
    assistant.ctrler()->m_scale_proxy =
        std::make_shared<asst::ControlScaleProxy>(backend, asst::ControllerType::Adb, nullptr);
    const auto config = std::make_shared<asst::RoguelikeConfig>();
    if (!config->verify_and_load_params(json::object { { "theme", "Sami" }, { "mode", 0 } })) {
        return 2;
    }

    int reports = 0;
    bool correct_stats = false;
    const auto callback = [&](asst::AsstMsg msg, const json::value& info, asst::Assistant*) {
        if (msg == asst::AsstMsg::SubTaskExtraInfo && info.get("what", std::string()) == "RoguelikeSettlement") {
            ++reports;
            correct_stats = info.get("details", "game_pass", !game_pass) == game_pass &&
                            info.get("details", "score", -1) == expected_score &&
                            info.get("details", "exp", std::string()) == expected_exp;
        }
    };
    asst::RoguelikeSettlementTaskPlugin plugin(callback, &assistant, "Roguelike", config, nullptr);
    const auto trigger = game_pass ? "Sami@Roguelike@GamePass" : "Sami@Roguelike@MissionFailedFlag2";
    if (!plugin.verify(
            asst::AsstMsg::SubTaskStart,
            json::object { { "subtask", "ProcessTask" }, { "details", json::object { { "task", trigger } } } })) {
        return 2;
    }
    const auto run = [&](std::string_view label,
                         std::vector<cv::Mat> frames,
                         bool press_next,
                         bool report,
                         std::optional<size_t> max_captures = std::nullopt) {
        backend->frames = std::move(frames);
        backend->awaiting_advance = press_next;
        backend->position = 0;
        backend->captures = 0;
        backend->clicks = 0;
        reports = 0;
        correct_stats = false;
        bool ran = false;
        try {
            ran = plugin.run();
        }
        catch (const UnexpectedInput&) {
        }
        const bool passed = ran && backend->clicks == int(press_next) && reports == int(report) &&
                            (!report || correct_stats) && (!max_captures || backend->captures <= *max_captures);
        std::cout << label << ": captures=" << backend->captures << " clicks=" << backend->clicks
                  << " reports=" << reports << " correct=" << correct_stats << " pass=" << passed << '\n';
        return int(!passed);
    };

    int failures = 0;
    failures += run("first page", { first, loading, result }, true, true);
    failures += run("loading result", { loading, result }, false, true);
    failures += run("ready result", { result }, false, true);
    failures += run("result returns after loading", { result, loading, result }, false, true);
    failures += run("first page after result", { result, first, result }, false, true);
    failures += run("first page persists", { first, first }, true, false);
    failures += run("record popup", { popup }, false, false, 2);
    auto unknown = popup.clone();
    unknown(cv::Rect(571, 353, 154, 66)).setTo(cv::Scalar::all(0));
    failures += run("unrecognized frame before result", { unknown, result }, false, true);
    failures += run("Sami home", { home }, false, false);
    failures += run("result disappears", { result, popup }, false, false, 3);
    return failures == 0 ? 0 : 1;
}
