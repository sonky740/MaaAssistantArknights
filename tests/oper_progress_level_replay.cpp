#include "Assistant.h"
#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "Config/TemplResource.h"
#include "Controller/Controller.h"
#include "Task/OperProgress/OperProgressProcessTask.h"
#include "Vision/Matcher.h"
#include "Vision/Oper/OperBoxImageAnalyzer.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

class ReplayController final : public asst::ControllerAPI
{
public:
    ReplayController()
    {
        stamp(profile, "OperFilesElite2.png", 880, 240);
        stamp(profile, "OperFilesEnterLevelUpButton.png", 1160, 119);
    }

    static void stamp(cv::Mat& frame, const char* name, int x, int y)
    {
        const auto& templ = asst::TemplResource::get_instance().get_templ(name);
        templ.copyTo(frame(cv::Rect(x, y, templ.cols, templ.rows)));
    }

    bool screencap(cv::Mat& frame, bool) override
    {
        if (skill_mode) {
            if (opened) {
                frame = cv::Mat::zeros(720, 1280, CV_8UC3);
                stamp(frame, "EliteUpPageConfirm.png", 1010, 615);
                stamp(frame, "Return.png", 20, 15);
            }
            else {
                profile.copyTo(frame);
                stamp(frame, "OperInformationMenu.png", 465, 18);
                stamp(frame, "OperFilesEnterSkillUpButton.png", 1160, 362);
                cv::putText(
                    frame,
                    std::to_string(rank),
                    { 1216, 377 },
                    cv::FONT_HERSHEY_SIMPLEX,
                    0.65,
                    cv::Scalar::all(255),
                    1,
                    cv::LINE_AA);
            }
            return true;
        }
        if (!opened) {
            profile.copyTo(frame);
            return true;
        }
        frame = cv::Mat::zeros(720, 1280, CV_8UC3);
        stamp(frame, "LevelUpPageSelectMax.png", 1085, 167);
        stamp(frame, "LevelUpPageConfirm.png", 1130, 459);
        const auto text = std::to_string(level);
        cv::putText(frame, text, { 856, 357 }, cv::FONT_HERSHEY_SIMPLEX, 2.1, cv::Scalar::all(255), 4, cv::LINE_AA);
        cv::putText(frame, text, { 700, 426 }, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar::all(255), 1, cv::LINE_AA);
        return true;
    }

    bool click(const asst::Point& p) override
    {
        if (skill_mode) {
            if (!opened && p.x > 1150 && p.y > 349 && p.y < 440) {
                opened = true;
                return true;
            }
            if (opened && p.x > 950 && p.y > 580) {
                ++confirmations;
                rank += step;
                if (rank == 7) {
                    opened = false;
                }
                return true;
            }
            if (opened && p.x < 180 && p.y < 80) {
                opened = false;
                return true;
            }
            ++unexpected;
            return false;
        }
        if (!opened && p.x > 1100 && p.y < 190) {
            opened = true;
            return true;
        }
        if (opened && p.x > 1070 && p.y > 425) {
            ++confirmations;
            if (accept_confirmation) {
                opened = false;
                stamp(profile, "OperInformationMenu.png", 465, 18);
                return true;
            }
            return false;
        }
        ++unexpected;
        return false;
    }

    bool swipe(const asst::Point& from, const asst::Point& to, int, asst::SwipeExtraDirection, double, double, bool)
        override
    {
        ++swipes;
        int advance = step;
        if (responsive && std::abs(to.y - from.y) < 100) {
            advance = 1;
        }
        level += to.y > from.y ? advance : -advance;
        return true;
    }

    bool connect(const std::string&, const std::string&, const std::string&) override { return false; }

    bool inited() const noexcept override { return true; }

    const std::string& get_uuid() const override { return uuid; }

    size_t get_pipe_data_size() const noexcept override { return 0; }

    size_t get_version() const noexcept override { return 0; }

    bool start_game(const std::string&) override { return false; }

    bool stop_game(const std::string&) override { return false; }

    bool input(const std::string&) override { return false; }

    bool inject_input_event(const asst::InputEvent&) override { return false; }

    bool press_esc() override { return false; }

    asst::ControlFeat::Feat support_features() const noexcept override { return {}; }

    std::pair<int, int> get_screen_res() const noexcept override { return { 1280, 720 }; }

    cv::Mat profile = cv::Mat::zeros(720, 1280, CV_8UC3);
    bool opened = false, skill_mode = false, responsive = false, accept_confirmation = false;
    int rank = 4;
    int level = 58, step = 3, swipes = 0, confirmations = 0, unexpected = 0;
    const std::string uuid = "offline";
};

int main(int argc, char** argv)
{
    if ((argc != 4 && argc != 6) || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) ||
        !AsstLoadResource(argv[3])) {
        return 2;
    }
    asst::Assistant assistant;
    asst::OperProgressTask task(nullptr, &assistant);
    const auto accepted = [&](const std::string& fields, const std::string& name = "娜仁图亚") {
        return task.set_params(*json::parse("{\"plans\":[{\"name\":\"" + name + "\"," + fields + "}]}"));
    };
    if (!accepted("\"elite\":2,\"level\":60,\"skill_level\":7") || !accepted("\"elite\":0,\"level\":50") ||
        !accepted("\"elite\":2") || !accepted("\"skill_mastery\":[0,0,3]") || accepted("\"elite\":0") ||
        accepted("\"level\":1") || accepted("\"elite\":2,\"level\":0") || accepted("\"elite\":2,\"level\":91") ||
        accepted("\"elite\":0,\"level\":51") || accepted("\"elite\":1,\"level\":1", "巡林者") ||
        accepted("\"elite\":1,\"level\":\"1\"")) {
        return 1;
    }
    if (accepted("\"elite\":2,\"allow_chip_purchase\":1") || !accepted("\"elite\":2,\"allow_chip_purchase\":false") ||
        task.m_process_task_ptr->m_plan.at(0).allow_chip_purchase.value_or(true)) {
        return 1;
    }
    json::value detail;
    auto cb = [&](asst::AsstMsg, const json::value& info, asst::Assistant*) {
        detail = info;
    };
    asst::OperProgressProcessTask process(cb, &assistant, "OperProgress");
    using Status = asst::OperProgressProcessTask::ResultDetail;
    process.report_elite_result(asst::battle::Role::Sniper, "娜仁图亚", Status::AlreadySatisfied, 2, 60);
    const auto result = detail.at("details");
    if (result.at("result").as_string() != "Success" || result.at("result_detail").as_string() != "AlreadySatisfied" ||
        result.at("level").as_integer() != 60 || result.at("elite").as_integer() != 2) {
        return 1;
    }
    process.report_elite_result(asst::battle::Role::Sniper, "娜仁图亚", Status::ChipManufacturing, 2, 60);
    if (detail.at("details").at("result").as_string() != "Skipped" || detail.at("details").contains("level")) {
        return 1;
    }
    const auto inspect = *json::parse(R"({"inspect_only":true,"inspection_skill":3,"plans":[{"name":"娜仁图亚"}]})");
    if (!task.set_params(inspect) || !task.m_process_task_ptr->m_inspect_only ||
        task.m_process_task_ptr->m_inspection_skill != 3 ||
        task.set_params(*json::parse(R"({"inspect_only":"true","plans":[{"name":"娜仁图亚"}]})"))) {
        return 1;
    }
    for (const int step : { 3, 2, 0 }) {
        auto device = std::make_shared<ReplayController>();
        device->step = step;
        assistant.ctrler()->m_controller = device;
        assistant.ctrler()->m_scale_proxy =
            std::make_shared<asst::ControlScaleProxy>(device, asst::ControllerType::Adb, nullptr);
        const auto state = process.execute_level(asst::battle::Role::Sniper, "娜仁图亚", 2, 60, 58);
        if (state != Status::RecognitionFailed || device->unexpected || device->confirmations != (step == 2) ||
            device->swipes != (step == 3 ? 2 : 1)) {
            std::cerr << "Bad dial transition: " << step << ", swipes=" << device->swipes
                      << ", confirms=" << device->confirmations << '\n';
            return 1;
        }
        if (process.execute_skill(7) != Status::RecognitionFailed) {
            return 1;
        }
    }
    auto calibrated = std::make_shared<ReplayController>();
    calibrated->opened = calibrated->responsive = calibrated->accept_confirmation = true;
    assistant.ctrler()->m_controller = calibrated;
    assistant.ctrler()->m_scale_proxy =
        std::make_shared<asst::ControlScaleProxy>(calibrated, asst::ControllerType::Adb, nullptr);
    if (process.select_level_target(58, 60) != Status::Completed || calibrated->level != 60 ||
        calibrated->swipes != 2 || calibrated->confirmations != 1 || calibrated->unexpected) {
        return 1;
    }
    for (const int step : { 0, 1, 2 }) {
        auto device = std::make_shared<ReplayController>();
        device->skill_mode = true;
        device->step = step;
        assistant.ctrler()->m_controller = device;
        assistant.ctrler()->m_scale_proxy =
            std::make_shared<asst::ControlScaleProxy>(device, asst::ControllerType::Adb, nullptr);
        const auto result = process.execute_skill(6);
        if (result != (step == 1 ? Status::Completed : Status::RecognitionFailed) ||
            device->confirmations != (step == 1 ? 2 : 1) || device->unexpected) {
            std::cerr << "Bad rank transition: " << step << ", confirms=" << device->confirmations << '\n';
            return 1;
        }
    }
    asst::OperBoxInfo unknown;
    unknown.level = 1;
    if (process.inspect_operator(unknown, asst::battle::Role::Sniper)) {
        return 1;
    }
    auto full = std::make_shared<ReplayController>();
    full->profile.setTo(0);
    full->stamp(full->profile, "OperFilesElite0.png", 880, 240);
    full->stamp(full->profile, "OperFilesLevelMax.png", 980, 122);
    assistant.ctrler()->m_controller = full;
    assistant.ctrler()->m_scale_proxy =
        std::make_shared<asst::ControlScaleProxy>(full, asst::ControllerType::Adb, nullptr);
    asst::OperBoxInfo card;
    card.id = "char_503_rang";
    card.name = "巡林者";
    card.rarity = 2;
    card.level = 30;
    card.level_recognized = true;
    if (!process.inspect_operator(card, asst::battle::Role::Sniper) || full->opened || full->confirmations ||
        full->unexpected || !detail.at("details").at("verified").as_boolean() ||
        detail.at("details").at("id").as_string() != card.id || detail.at("details").at("exp").as_integer() != 0) {
        return 1;
    }
    if (process.manufacture_dual_chip(asst::battle::Role::Sniper, "娜仁图亚", false) != Status::RecognitionFailed ||
        full->opened || full->confirmations || full->unexpected) {
        return 1;
    }
    const auto count_frame = [&](int count) {
        cv::Mat screen = cv::Mat::zeros(720, 1280, CV_8UC3);
        cv::putText(
            screen,
            std::to_string(count),
            { 357, 678 },
            cv::FONT_HERSHEY_SIMPLEX,
            0.68,
            cv::Scalar::all(255),
            1,
            cv::LINE_AA);
        return screen;
    };
    if (process.strict_number(count_frame(0), "OperProgress@DualchipBadgeCount", 0) != 0) {
        return 1;
    }
    full->profile = count_frame(6);
    if (process.manufacture_dual_chip(asst::battle::Role::Sniper, "娜仁图亚", false) != Status::RecognitionFailed ||
        full->opened || full->confirmations || full->unexpected) {
        return 1;
    }
    card.elite = 1;
    if (process.inspect_operator(card, asst::battle::Role::Sniper)) {
        return 1;
    }
    cv::Mat frame;
    int expected = 60;
    if (argc == 6) {
        frame = cv::imread(argv[4]);
        if (frame.empty()) {
            return 2;
        }
        cv::resize(frame, frame, { 1280, 720 });
        expected = std::stoi(argv[5]);
        asst::Matcher page(frame);
        page.set_task_info("OperProgress@LevelUpPageSelectMax");
        std::cout << "Observed current EXP: " << process.dial_experience(frame).value_or(-1) << '\n';
        if (!page.analyze()) {
            std::cerr << "Level dial anchor not recognized\n";
            return 1;
        }
    }
    else {
        frame = cv::Mat::zeros(720, 1280, CV_8UC3);
        cv::putText(frame, "60", { 856, 357 }, cv::FONT_HERSHEY_SIMPLEX, 2.1, cv::Scalar::all(255), 4, cv::LINE_AA);
        cv::putText(frame, "60", { 700, 426 }, cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar::all(255), 1, cv::LINE_AA);
    }
    const auto observed = process.selected_level(frame);
    if (observed != expected) {
        std::cerr << "Unexpected selected level: " << observed.value_or(-1) << '\n';
        return 1;
    }
    for (const auto& field : { "OperProgress@LevelDialNumber", "OperProgress@LevelPreviewNumber" }) {
        auto missing = frame.clone();
        const auto rect = asst::Task.get(field)->roi;
        missing(cv::Rect(rect.x, rect.y, rect.width, rect.height)).setTo(0);
        if (process.selected_level(missing)) {
            return 1;
        }
    }
    const auto rect = asst::Task.get("OperProgress@LevelPreviewNumber")->roi;
    frame(cv::Rect(rect.x, rect.y, rect.width, rect.height)).setTo(0);
    cv::putText(
        frame,
        expected == 42 ? "41" : "42",
        { 700, 426 },
        cv::FONT_HERSHEY_SIMPLEX,
        0.7,
        cv::Scalar::all(255),
        1,
        cv::LINE_AA);
    if (process.selected_level(frame)) {
        return 1;
    }
    std::cout << "PASS plan validation, callback, target OCR, missing and conflicting level readings\n";
}
