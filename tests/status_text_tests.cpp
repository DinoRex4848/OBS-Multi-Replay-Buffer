#include "status_text.hpp"
#include "test_support.hpp"
#include <clocale>
#include <iostream>

using namespace replay;
int main()
{
  try {
    // Numbers match QString::number: '.' in every locale, ties rounded away from zero.
    std::setlocale(LC_ALL, "de_DE.UTF-8");
    CHECK(text::seconds(fromSeconds(2)) == "2.0 s");
    CHECK(text::seconds(1250000) == "1.3 s");
    CHECK(text::seconds(1449999) == "1.4 s");
    CHECK(text::seconds(0) == "0.0 s");
    CHECK(text::fixed(9.96, 1) == "10.0");
    CHECK(text::fixed(0.25, 2) == "0.25");
    CHECK(text::fixed(1234567890 / 1e9, 3) == "1.235");
    std::setlocale(LC_ALL, "C");

    CHECK(text::presetLabel(0, "Replay 1") == "Preset 1 — Replay 1");
    CHECK(text::saveTooltip(fromSeconds(60), fromSeconds(60)) ==
          "Full replay available\nRequested 60.0 s; available 60.0 s");
    CHECK(text::saveTooltip(fromSeconds(60), fromSeconds(10)).rfind("Partial replay available\n", 0) == 0);
    CHECK(text::saveTooltip(fromSeconds(60), 0).rfind("No playable replay available yet\n", 0) == 0);
    CHECK(text::details(fromSeconds(3), fromSeconds(600), 1234567890, 6, 2, "") ==
          "Available 3.0 s / target 600.0 s\nReplay memory 1.235 / 6.00 GB · queue 2");
    CHECK(text::details(0, 0, 0, 0.25, 1, "Replay 60s") ==
          "Available 0.0 s / target 0.0 s\nReplay memory 0.000 / 0.25 GB · queue 1\nWriting Replay 60s");
    const std::array<std::string, presetCount> names = {"A", "B", "C"};
    const std::array<std::filesystem::path, presetCount> folders = {"/a", "/b", "/c"};
    CHECK(text::destinations(false, names, folders) == "\nDestination: /a");
    CHECK(text::destinations(true, names, folders) ==
          "\nDestinations:\nPreset 1 — A: /a\nPreset 2 — B: /b\nPreset 3 — C: /c");

    text::StatusInputs in;
    auto out = text::status(in);
    CHECK(out.state == "Stopped" && out.tooltip == "Stopped" && out.notice.empty());
    in.captureMessage = "Buffering";
    in.collecting = true;
    CHECK(text::status(in).state == "Buffering");
    in.paused = true;
    CHECK(text::status(in).state == "Paused with recording");
    in.paused = false;
    in.degraded = true;
    CHECK(text::status(in).state == "Buffering · Memory pressure shortened history; partial clips may be saved.");
    in.degraded = false;
    // Save feedback shows in the dock only while current; Diagnostics keeps it.
    in.feedback = "Saved a.mkv";
    in.feedbackDetail = "Requested 2.0 s, actual 2.5 s";
    in.feedbackCurrent = true;
    out = text::status(in);
    CHECK(out.state == "Saved a.mkv" && out.tooltip == "Saved a.mkv\nRequested 2.0 s, actual 2.5 s");
    in.feedbackCurrent = false;
    out = text::status(in);
    CHECK(out.state == "Buffering" && out.notice == "\nSaved a.mkv\nRequested 2.0 s, actual 2.5 s");
    // A warning outranks feedback, and an error outranks both.
    in.feedback.clear();
    in.feedbackDetail.clear();
    in.warning = "Recording settings changed.";
    CHECK(text::status(in).state == "Buffering · Recording settings changed.");
    in.error = "Disk full";
    out = text::status(in);
    CHECK(out.state == "Error: Disk full" && out.notice == "Error: Disk full\nRecording settings changed.");
    // A capture failure is reported ahead of other warnings.
    in = {};
    in.captureMessage = "Replay encoder stopped delivering packets";
    in.captureFailed = true;
    in.warning = "Recording settings changed.";
    CHECK(text::status(in).notice == "Replay encoder stopped delivering packets\nRecording settings changed.");
    in.captureFailed = false;
    CHECK(text::status(in).notice == "Recording settings changed.");
    in = {};
    in.draining = true;
    in.captureMessage = "Draining accepted saves";
    CHECK(text::status(in).state == "Draining accepted saves");
    std::cout << "PASS: status text, number formatting and message priority\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "FAIL: " << e.what() << '\n';
    return 1;
  }
}
