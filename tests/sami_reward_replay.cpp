#include "AsstCaller.h"
#include "Config/TaskData.h"
#include "Config/TemplResource.h"
#include "Vision/Miscellaneous/PipelineAnalyzer.h"

#include <iostream>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

namespace
{
const std::string prefix = "Sami@Roguelike@";
const std::string marker = "DropsFlagOnLeaveButton";

bool expect_next(const cv::Mat& frame, const std::string& from, const std::string& expected)
{
    asst::PipelineAnalyzer pipeline(frame);
    pipeline.set_tasks(asst::Task.get(prefix + from)->next);
    const auto match = pipeline.analyze();
    const std::string actual = match ? match->task_ptr->name : "<none>";
    std::cout << from << " -> " << actual << " (expected " << expected << ")\n";
    return actual == prefix + expected;
}

void put_button(cv::Mat& frame, const std::string& task, cv::Point top_left)
{
    const auto& image = asst::TemplResource::get_instance().get_templ(prefix + task + ".png");
    image.copyTo(frame(cv::Rect(top_left, image.size())));
}
}

int main(int argc, char** argv)
{
    if ((argc != 4 && argc != 5) || !AsstSetUserDir(argv[1]) || !AsstLoadResource(argv[2]) ||
        !AsstLoadResource(argv[3])) {
        return 2;
    }

    cv::Mat encounter(720, 1280, CV_8UC3, cv::Scalar::all(32));
    put_button(encounter, "StageEncounterOptionUnknown", { 1241, 297 });
    if (!expect_next(encounter, "StagesChaosZero", "StageEncounterOptionUnknown")) {
        return 1;
    }

    cv::Mat reward = encounter.clone();
    put_button(reward, "GetDropLeave", { 962, 248 });
    put_button(reward, "GetDrop1", { 34, 500 });
    if (argc == 5) {
        const auto incident = cv::imread(argv[4]);
        if (incident.empty() || incident.cols * 9 != incident.rows * 16) {
            return 2;
        }
        cv::resize(incident, reward, { 1280, 720 }, 0, 0, cv::INTER_AREA);
    }

    if (!expect_next(reward, "StagesChaosZero", marker) ||
        asst::Task.get(prefix + marker)->action != asst::ProcessTaskAction::DoNothing ||
        !expect_next(reward, marker, "GetDrop1")) {
        return 1;
    }
    asst::Task.set_task_base(prefix + "DropsFlag", prefix + "DropsFlag_mode1");
    return expect_next(reward, marker, "GetDropLeave") ? 0 : 1;
}
