# Decisions at runtime

[keel](../README.md) › docs › Decisions at runtime

The composition is fixed at compile time, but configuration is only known at runtime. Two
tools let configuration decide *within* a checked composition: `enabled()` and
`keel::Alternative<I>`.

## `enabled()`: a component that may opt out

Override `enabled()` to make a component conditional:

```cpp
class Uplink : public keel::Component<Config, Recorder> {
    bool enabled() const override { return !getComponent<Config>().uplinkUrl().empty(); }
    void start() override;  // only called if enabled
};
```

- `enabled()` is asked **once, just before the component's `init()`**, on the same thread.
  Its dependencies are initialized by then, so it can use them (typically a `Config`).
  keel hasn't checked them yet, though: `getAll<I>()` may be empty, and a required
  dependency that is disabled throws `keel::Error`.
- If it returns `false`, the component is **disabled**: none of its hooks run. It is still
  constructed and destroyed, so keep constructors cheap.
- Throw from `enabled()` to abort startup, as from `init()`.
- **Write `bool enabled() const override`.** A different signature (`bool enabled()`
  without `const`) is another function and is never asked. And a component that already
  has an accessor `bool enabled() const` of its own overrides the hook with it.

How dependents see a disabled component:

| Dependent declares | Result |
|---|---|
| `Optional<Uplink>` | `findComponent<Uplink>()` returns `nullptr` |
| `All<I>` (Uplink provides `I`) | Uplink is left out of `getAll<I>()`; if no provider is left, the dependent's `init()` fails |
| `Optional<All<I>>` | Uplink is left out of `getAll<I>()`, which may become empty |
| `Uplink` (required) | the dependent's `init()` fails: `keel: Dashboard uses Uplink: Uplink is disabled (its enabled() returned false)` |

The failures are `keel::Error`, thrown before the dependent's `init()` runs, with the usual
[rollback](lifecycle.md#failures-during-startup). A dependent that is disabled itself is
never checked: a disabled feature may depend on another disabled one.

## `keel::Alternative<I>`: choose an implementation by configuration

Compose several implementations of an interface; configuration enables one. Dependents
just use `I`:

```cpp
struct Cache {
    virtual ~Cache() = default;
    virtual bool get(std::string_view key, std::string& value) = 0;
};

class RedisCache : public keel::Component<Config, keel::Alternative<Cache>>, public Cache {
    bool enabled() const override { return getComponent<Config>().cache() == "redis"; }
    bool get(std::string_view key, std::string& value) override;
};

class MemoryCache : public keel::Component<Config, keel::Alternative<Cache>>, public Cache {
    bool enabled() const override { return getComponent<Config>().cache() == "memory"; }
    bool get(std::string_view key, std::string& value) override;
};

class Repository : public keel::Component<Cache> {  // doesn't know which one
    void init() override { cache_ = &getComponent<Cache>(); }
    Cache* cache_ = nullptr;
};

auto app = keel::compose<Repository, RedisCache, MemoryCache, Config>(argc, argv);
```

- **Compile time:** several providers of `Cache` are allowed because each is an
  `Alternative<Cache>`. Mixing alternatives with a plain `Provides<Cache>` is an error
  (`MixedProviders`) as soon as something needs one `Cache`.
- **Runtime:** dependents of `Cache` are ordered after every alternative, so the choice is
  made before any of them initializes. When one does:
  - one alternative enabled: it gets that one;
  - none enabled: a required dependent fails (`none of its alternatives (RedisCache,
    MemoryCache) is enabled`), an `Optional<Cache>` gets `nullptr`;
  - several enabled: `keel: Repository uses Cache: several of its alternatives are enabled
    (only one may be): RedisCache, MemoryCache`.
- `All<Cache>` gets the enabled ones.
- At the root, `app.getComponent<Cache>()` and `run(&Cache::f)` work after `init()` (they
  get the enabled one); before `init()`, `getComponent<Cache>()` throws `keel::Error`.
- A single `Alternative<I>` is allowed: a provider that configuration may turn off.

## Turning a feature off: which tool to use

To remove a component in some configurations, pick the tool by **how the components that
use it declare it**:

| Dependents declare | Do this | Effect |
|---|---|---|
| nothing, `Optional<X>`, `Optional<All<I>>`, or `All<I>` with other providers left | make `X::enabled()` return `false` | X never runs; `findComponent` gives `nullptr`; `getAll` skips it |
| `I` (a required interface) | add a do-nothing implementation; make both `Alternative<I>` with opposite `enabled()` | dependents keep calling `I`; the stub ignores the calls (the *null object* pattern) |
| the concrete `X` (required) | change the dependents to `Optional<X>`, or introduce an interface | a hard dependency on X can't be switched off at runtime |

The stub version, for an uplink that is off when the device is offline:

```cpp
struct Uplink {
    virtual ~Uplink() = default;
    virtual void send(const Sample&) = 0;
};

class NetworkUplink : public keel::Component<Config, Network, keel::Alternative<Uplink>>, public Uplink {
    bool enabled() const override { return getComponent<Config>().online(); }
    void send(const Sample& sample) override;  // the real upload
};

class NullUplink : public keel::Component<Config, keel::Alternative<Uplink>>, public Uplink {
    bool enabled() const override { return !getComponent<Config>().online(); }
    void send(const Sample&) override {}  // drop
};

class Recorder : public keel::Component<Uplink> { /* ... */ };  // unchanged either way
```

`NetworkUplink` needs `Network`, and both may be off when offline. That's fine: a
component that opts out is never checked for its dependencies.

Keep in mind:

- **It's still compiled in.** A disabled component is constructed, its code is in the
  binary, and its dependencies must still be composed. To leave a feature out of the
  binary, use a different composition
  ([Several compositions in one program](composition.md#several-compositions-in-one-program)).
- **Prefer `Optional<X>` when dependents can genuinely do without X.** A stub is for when
  "do nothing" is a correct implementation of the interface (drop uploads, skip metrics),
  so that dependents don't need to check.
- A disabled component's methods can still be called, through a reference taken before
  `init()`. If such a method looks up a dependency, it throws `keel::Error`.
