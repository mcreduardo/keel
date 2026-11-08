// Must not compile: Writer requires one Sink, and its only provider may be turned off.
#include <keel.hpp>

struct Sink {
    virtual ~Sink() = default;
};

class FileSink : public keel::Component<>, public keel::Provides<Sink>, public Sink, public keel::Conditional {
    bool enabled() const override { return true; }
};

class Writer : public keel::Component<Sink> {};

static_assert(!keel::isValid<Writer, FileSink>, "isValid disagrees with compose");

int main() {
    auto app = keel::compose<Writer, FileSink>();
}
