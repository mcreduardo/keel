// Builds against the development headers in include/keel/ instead of the generated keel.hpp,
// so that the parts keep compiling on their own (the amalgamate_check test keeps keel.hpp
// in sync with them). Touches each part: markers, interfaces, collections, conditional
// and async components, the lifecycle, introspection and run().
#include <keel/keel.hpp>

#include <cstdio>
#include <string>
#include <vector>

namespace split {

struct Config : keel::Component<> {
    bool uplink = false;
};

struct Sink {
    virtual ~Sink() = default;
    virtual void write(const std::string& line) = 0;
};

class Recorder : public keel::Component<Config>, public keel::Provides<Sink>, public Sink, public keel::AsyncInit {
public:
    void write(const std::string& line) override { lines.push_back(line); }
    std::vector<std::string> lines;
};

class Uplink : public keel::Component<Config>, public keel::Conditional {
    bool enabled() const override { return getComponent<Config>().uplink; }
};

struct Check {
    virtual ~Check() = default;
    virtual bool ok() const = 0;
};

class DiskCheck : public keel::Component<>, public keel::Provides<Check>, public Check {
    bool ok() const override { return true; }
};

class Service : public keel::Component<Sink, keel::Optional<Uplink>, keel::All<Check>, keel::After<Config>> {
public:
    int serve() {
        int healthy = 0;
        for (const Check* check : getAll<Check>()) healthy += check->ok() ? 1 : 0;
        getComponent<Sink>().write(findComponent<Uplink>() == nullptr ? "local" : "uplink");
        return healthy;
    }
};

using Core = keel::Bundle<Config, Recorder>;
using ServiceApp = keel::App<Service, Uplink, DiskCheck, Core>;
static_assert(keel::isValid<Service, Uplink, DiskCheck, Core>);
static_assert(!keel::isValid<Service>);
static_assert(keel::requireValid<Core>);
static_assert(ServiceApp::initOrder().back() == keel::typeName<Service>());

}  // namespace split

int main() {
    split::ServiceApp app;
    const int healthy = app.run(&split::Service::serve);
    const bool ok = healthy == 1 && app.getComponent<split::Recorder>().lines == std::vector<std::string>{"local"} &&
                    !app.timings().empty() && app.toDot().find("provides split::Sink") != std::string::npos;
    std::printf("%s\n", ok ? "ok" : "FAILED");
    return ok ? 0 : 1;
}
