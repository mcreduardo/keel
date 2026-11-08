# Decisions at runtime

[keel](../README.md) › docs › Decisions at runtime

The composition is fixed at compile time, but configuration is only known at runtime.
There are two ways to let configuration decide *within* a checked composition:

- [choose an implementation](#choose-an-implementation) in a plain component (a factory),
  or with `keel::Alternative<I>`;
- [turn a component off](#keelconditional-a-component-that-may-be-off) with
  `keel::Conditional`.

keel keeps the compile-time guarantee in both: a component that requires something gets
it. The only composition failures left to runtime are those of `Alternative<I>`
(none or several enabled), and only where the alternatives are used.

## Choose an implementation

### First choice: a component that creates the configured one

When the implementations need the same dependencies, make them plain classes, and let one
component create the configured one in `init()` and forward the interface to it:

```cpp
struct Cache {
    virtual ~Cache() = default;
    virtual bool get(std::string_view key, std::string& value) = 0;
};

class RedisCache : public Cache { /* ... */ };   // plain classes, not components
class MemoryCache : public Cache { /* ... */ };

class CacheComponent : public keel::Component<Config, Logger>, public keel::Provides<Cache>, public Cache {
public:
    bool get(std::string_view key, std::string& value) override { return impl_->get(key, value); }

private:
    void init() override {
        const Config& config = getComponent<Config>();
        if (config.cache() == "redis") {
            impl_ = std::make_unique<RedisCache>(config.redisUrl(), getComponent<Logger>());
        } else {
            impl_ = std::make_unique<MemoryCache>(config.cacheSize());
        }
    }
    std::unique_ptr<Cache> impl_;
};

class Repository : public keel::Component<Cache> { /* ... */ };  // gets the CacheComponent
```

It is ordinary C++, with nothing new to learn, and there is nothing to get wrong at
runtime: there is always exactly one `Cache`. The trade-offs:

- The factory declares the **union of the dependencies** of every implementation (`Logger`
  above, even when the memory cache doesn't use it), so they are all composed and
  initialized first.
- The implementations **aren't components**: they don't get their own lifecycle hooks,
  `keel::AsyncInit`, timings, or a box in `toDot()`. The factory forwards what they need
  (its `start()` and `stop()` call theirs).
- Forwarding the interface is boilerplate, one line per member function.

### `keel::Alternative<I>`: implementations that are components of their own

When the implementations need different dependencies, or each needs its own lifecycle,
async init or introspection, compose all of them and let configuration enable one. Each
lists `keel::Alternative<I>` and derives from `keel::Conditional`:

```cpp
class RedisCache : public keel::Component<Config, Network, keel::Alternative<Cache>>, public Cache,
                   public keel::Conditional {
    bool enabled() const override { return getComponent<Config>().cache() == "redis"; }
    bool get(std::string_view key, std::string& value) override;
};

class MemoryCache : public keel::Component<Config, keel::Alternative<Cache>>, public Cache,
                    public keel::Conditional {
    bool enabled() const override { return getComponent<Config>().cache() == "memory"; }
    bool get(std::string_view key, std::string& value) override;
};

class Repository : public keel::Component<Cache> {  // doesn't know which one
    void init() override { cache_ = &getComponent<Cache>(); }
    Cache* cache_ = nullptr;
};

auto app = keel::compose<Repository, RedisCache, MemoryCache, Network, Config>(argc, argv);
```

- **Compile time:** several providers of `Cache` are allowed because each is an
  `Alternative<Cache>`. An alternative that doesn't derive from `keel::Conditional` is an
  error (`AlternativeNotConditional`), and so is mixing alternatives with a plain
  `Provides<Cache>` base (`MixedProviders`) as soon as something needs one `Cache`.
- **Runtime:** dependents of `Cache` are ordered after every alternative, so the choice is
  made before any of them initializes. When one does:
  - one alternative enabled: it gets that one;
  - none enabled: a required dependent fails (`none of its alternatives (RedisCache,
    MemoryCache) is enabled`), an `Optional<Cache>` gets `nullptr`;
  - several enabled: `keel: Repository uses Cache: several of its alternatives are enabled
    (only one may be): RedisCache, MemoryCache`, also for an `Optional<Cache>`.

  The failures are `keel::Error`, thrown before the dependent's `init()` runs, with the
  usual [rollback](lifecycle.md#failures-during-startup).
- `Optional<All<Cache>>` gets the enabled ones. `All<Cache>` doesn't compile when every
  provider is an alternative, since they may all be off (`OnlyConditionalProviders`).
- At the root, `app.getComponent<Cache>()` and `run(&Cache::f)` work after `init()` (they
  get the enabled one); before `init()`, `getComponent<Cache>()` throws `keel::Error`.
- A single `Alternative<I>` is allowed (a test composition may compose just one): a
  provider that configuration may turn off, for dependents that must have it. They fail at
  startup if it is off. This (also a component that lists `Alternative<Self>`, required as
  `Self`) is the only way to require a conditional component; a required dependency on a
  concrete alternative (`Component<RedisCache>`) is `RequiredConditional`.

## `keel::Conditional`: a component that may be off

Derive from `keel::Conditional` too, and override `enabled()`:

```cpp
class Uplink : public keel::Component<Config, Recorder>, public keel::Conditional {
    bool enabled() const override { return !getComponent<Config>().uplinkUrl().empty(); }
    void start() override;  // only called if enabled
};
```

- `enabled()` is asked **once, just before the component's `init()`**, on the same thread
  (its own thread with `keel::AsyncInit`). Its dependencies are initialized by then, so it
  can use them (typically a `Config`).
- If it returns `false`, the component is **disabled**: none of its hooks run. It is still
  constructed and destroyed, so keep constructors cheap.
- Throw from `enabled()` to abort startup, as from `init()`.
- The override must be `bool enabled() const`. Another signature (no `const`) doesn't
  override it, and the component stays abstract: `AbstractComponent`, at compile time.
- Derive publicly, once. Privately (the default for a `class`) or twice is
  `InaccessibleConditional`.
- Only components that derive from `keel::Conditional` are asked. A component that has an
  `enabled()` of its own for another purpose keeps it: keel never calls it.
- An interface that declares its own `virtual bool enabled() const`, implemented by a
  component that also derives from `keel::Conditional`, shares one override with it.

**Nothing may require a conditional component, except through `keel::Alternative<I>`,
whose choice is checked at startup.** Dependents declare it so that they work without it,
and the compiler checks that they do:

| Dependent declares | Result |
|---|---|
| `Optional<Uplink>` | `findComponent<Uplink>()` returns `nullptr` while Uplink is disabled |
| `InitAfter<Uplink>` | ordering only, as usual |
| `Optional<All<I>>` (Uplink provides `I`) | Uplink is left out of `getAll<I>()`, which may become empty |
| `All<I>` | Uplink is left out of `getAll<I>()`; compiles only if some provider of `I` isn't conditional (`OnlyConditionalProviders`), so the list is never empty |
| `Uplink` (required) | compile error: `RequiredConditional<Dashboard, Uplink>` |
| `I`, provided only by Uplink (a `Provides<I>` base) | compile error: `RequiredConditionalProvider<Writer, I, Uplink>` |
| `I`, provided by `Alternative<I>`s | the enabled one; checked at startup (see above) |

This holds for a conditional dependent too: if `Recorder` were conditional, the
conditional `Uplink` above couldn't require it (each may be off on its own). It uses it
through `Optional<Recorder>` instead, and decides in `enabled()` or `init()`:

```cpp
class Uplink : public keel::Component<Config, keel::Optional<Recorder>>, public keel::Conditional {
    bool enabled() const override {
        return findComponent<Recorder>() != nullptr && !getComponent<Config>().uplinkUrl().empty();
    }
    void init() override { recorder_ = findComponent<Recorder>(); }  // not nullptr: enabled() checked
    Recorder* recorder_ = nullptr;
};
```

The composition root may still get a conditional component with `app.getComponent<T>()`,
e.g. to configure it before `init()`; after `init()`, a disabled one throws `keel::Error`.
`run(&T::f)` on a conditional `T` doesn't compile (`RunTargetConditional`): call it from
a lambda that checks first.

## Turning a feature off: which tool to use

To remove a component in some configurations, pick the tool by **how the components that
use it declare it**:

| Dependents declare | Do this | Effect |
|---|---|---|
| nothing, `Optional<X>`, `InitAfter<X>`, `Optional<All<I>>`, or `All<I>` with a provider that is always there | make `X` a `keel::Conditional` | X never runs; `findComponent` gives `nullptr`; `getAll` skips it |
| `I` (a required interface) | add a do-nothing implementation; in a factory, create it when the feature is off (or make both `Alternative<I>` with opposite `enabled()`) | dependents keep calling `I`; the stub ignores the calls (the *null object* pattern) |
| the concrete `X` (required) | change the dependents to `Optional<X>`, or introduce an interface | the compiler won't let a required `X` be conditional |

The stub version with a factory, for an uplink that is off when the device is offline:

```cpp
struct Uplink {
    virtual ~Uplink() = default;
    virtual void send(const Sample&) = 0;
};

class NetworkUplink : public Uplink { /* the real upload */ };
class NullUplink : public Uplink {
    void send(const Sample&) override {}  // drop
};

class UplinkComponent : public keel::Component<Config, Network>, public keel::Provides<Uplink>, public Uplink {
    void send(const Sample& sample) override { impl_->send(sample); }
    void init() override {
        if (getComponent<Config>().online()) {
            impl_ = std::make_unique<NetworkUplink>(getComponent<Network>());
        } else {
            impl_ = std::make_unique<NullUplink>();
        }
    }
    std::unique_ptr<Uplink> impl_;
};

class Recorder : public keel::Component<Uplink> { /* ... */ };  // unchanged either way
```

`Network` is composed and initialized even when offline (the factory declares it). If
that matters, make the two uplinks components, `Alternative<Uplink>` and
`keel::Conditional` each: then `NetworkUplink` can list `Optional<Network>`, and the
`init()` of a `Network` that is conditional itself, off when offline, never runs (it is
still constructed).

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
