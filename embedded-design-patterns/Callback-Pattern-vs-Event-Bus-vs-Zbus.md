# Callback Pattern vs. Event Bus vs. Zephyr Zbus

A three-way comparison of notification architectures in embedded C systems,
using the BLE Environmental Sensor firmware as a concrete reference.

## Overview

All three patterns solve the same core problem: **how does module A inform
module B that something happened, without creating a tangled dependency
graph?** They differ in mechanism, abstraction level, and trade-offs.

### Callback Pattern (Observer Pattern)

The subject maintains a **linked list of callback structs** (`sys_slist`).
When a state change occurs, the subject iterates the list and invokes each
callback directly. Observers embed the callback struct inside their own data
and use `CONTAINER_OF` to recover their private context. No queue, no
buffering -- synchronous, direct invocation in the caller's execution
context. This is what our firmware currently uses.

### Event Bus (Message Queue)

A producer pushes an **event struct** into a shared message queue (`k_msgq`
in Zephyr). A consumer thread blocks on the queue and dispatches events
through a central `switch` statement. Events are copied into a buffer.
Producer and consumer are decoupled in both time and execution context. This
is what the firmware used before the callback refactoring.

### Zephyr Zbus (Publish-Subscribe Bus)

Zbus is Zephyr's **built-in publish-subscribe framework**. Producers publish
typed messages to named **channels**. **Observers** -- registered at compile
time or runtime -- are notified automatically. Zbus sits between the
simplicity of a message queue and the flexibility of the callback pattern,
adding framework-level features like channel validation, priority boost, and
multiple observer types. It is available in this SDK (`zephyr/subsys/zbus/`).

```
Callback Pattern              Event Bus                     Zbus
+----------+                  +----------+                  +---------+
| Subject  |--cb()-->[ Obs ]  | Producer |--push-->[ Q ]    | Publish |-->[ Channel ]
|          |--cb()-->[ Obs ]  +----------+    k_msgq  |     +---------+       |
+----------+                              pop--+      |                       |
  Direct invocation via                        v      |     +--------+  +--------+  +--------+
  linked list of callbacks             [ Consumer ]   |     |Listener|  |Subscr. |  |MsgSub. |
                                         switch(evt)  |     | (sync) |  | (async)|  | (async)|
                                                      |     +--------+  +--------+  +--------+
                                       Serialized       Three observer types with
                                       message queue    automatic fan-out (one-to-many)
```

---

## How Zbus Works

### Channels

A **channel** is a named, typed message slot. It holds exactly one message
(the latest published value). Channels are defined at compile time:

```c
/* Message type */
struct sensor_msg {
    float temperature;
    float humidity;
};

/* Channel definition */
ZBUS_CHAN_DEFINE(sensor_chan,        /* channel name */
    struct sensor_msg,              /* message type */
    NULL,                           /* validator (optional) */
    NULL,                           /* user data */
    ZBUS_OBSERVERS(main_sub, led_listener),  /* static observers */
    ZBUS_MSG_INIT(.temperature = 0, .humidity = 0)  /* initial value */
);
```

Each channel allocates:
- A static message buffer (`sizeof(message type)`)
- A `struct zbus_channel_data` (semaphore, observer indices, optional HLP
  and runtime observer fields)
- A `struct zbus_channel` (name pointer, message pointer, size, validator,
  data pointer)

Publishing copies a message into the channel and triggers the **VDED**
(Virtual Decentralized Event Dispatcher) to notify all observers:

```c
struct sensor_msg msg = { .temperature = 23.5f, .humidity = 55.0f };
zbus_chan_pub(&sensor_chan, &msg, K_MSEC(100));
```

Reading retrieves the current channel value without triggering notifications:

```c
struct sensor_msg msg;
zbus_chan_read(&sensor_chan, &msg, K_MSEC(100));
```

### Observer Types

Zbus provides three observer types, each with different execution semantics:

**1. Listener (synchronous callback)**

```c
ZBUS_LISTENER_DEFINE(led_listener, on_sensor_data);

static void on_sensor_data(const struct zbus_channel *chan) {
    struct sensor_msg msg;
    zbus_chan_read(chan, &msg, K_NO_WAIT);
    /* runs in publisher's thread context */
}
```

Executes synchronously in the publisher's thread, like the callback pattern.
The listener callback receives a channel pointer and must call
`zbus_chan_read()` to get the message. Lightweight but blocks the publisher.

**2. Subscriber (async, channel pointer via `k_msgq`)**

```c
ZBUS_SUBSCRIBER_DEFINE(main_sub, 4);  /* queue depth = 4 */

/* In the subscriber's thread: */
void subscriber_thread(void) {
    const struct zbus_channel *chan;
    while (zbus_sub_wait(&main_sub, &chan, K_FOREVER) == 0) {
        if (chan == &sensor_chan) {
            struct sensor_msg msg;
            zbus_chan_read(chan, &msg, K_MSEC(100));
            /* process */
        }
    }
}
```

The VDED enqueues a **channel pointer** into the subscriber's `k_msgq`. The
subscriber thread wakes, identifies which channel was published, and reads
the current value. The subscriber receives the *latest* value, not
necessarily the value at publish time (if multiple publishes happen before
the subscriber wakes). Allocates a `k_msgq` with `queue_depth *
sizeof(channel_pointer)`.

**3. Message Subscriber (async, full message via `k_fifo` + `net_buf`)**

```c
ZBUS_MSG_SUBSCRIBER_DEFINE(logger_msub);

void msg_subscriber_thread(void) {
    const struct zbus_channel *chan;
    struct sensor_msg msg;
    while (zbus_sub_wait_msg(&logger_msub, &chan, &msg, K_FOREVER) == 0) {
        /* msg contains the exact value at publish time */
    }
}
```

The VDED clones the message into a `net_buf`, enqueues it into the
subscriber's `k_fifo`. The subscriber receives the **exact message at
publish time**, not the latest. Requires `CONFIG_ZBUS_MSG_SUBSCRIBER` and
pulls in `CONFIG_NET_BUF`. Needs a `net_buf` pool (global or per-channel).

### VDED (Virtual Decentralized Event Dispatcher)

The VDED is zbus's core notification engine. When `zbus_chan_pub()` is
called:

1. Acquire channel semaphore (with optional priority boost)
2. Copy message into channel's static buffer
3. Iterate static observers (linker-section indexed):
   - **Listener:** call `obs->callback(chan)` synchronously
   - **Subscriber:** `k_msgq_put(obs->queue, &chan, timeout)`
   - **Msg Subscriber:** clone message into `net_buf`, `k_fifo_put()`
4. Iterate runtime observers (if `CONFIG_ZBUS_RUNTIME_OBSERVERS`)
5. Release channel semaphore (restore priority if boosted)

### Priority Boost (Highest Locker Protocol)

Enabled by default (`CONFIG_ZBUS_PRIORITY_BOOST`). When a low-priority
thread publishes to a channel observed by a high-priority subscriber:

1. Channel tracks `highest_observer_priority` (lowest numeric value)
2. Publisher compares its priority to the channel's HOP
3. If publisher is lower priority, it gets temporarily elevated to
   `HOP - 1`
4. After notification completes, original priority is restored

This prevents **priority inversion**: a low-priority publisher holding the
channel semaphore while a high-priority subscriber waits.

### Channel Validation

Channels can have a validator function that rejects invalid messages at
publish time:

```c
static bool validate_sensor(const void *msg, size_t msg_size) {
    const struct sensor_msg *m = msg;
    return (m->temperature > -40.0f && m->temperature < 125.0f);
}

ZBUS_CHAN_DEFINE(sensor_chan, struct sensor_msg, validate_sensor, ...);
```

`zbus_chan_pub()` returns `-ENOMSG` if validation fails.

---

## Three-Way Comparison

| Aspect | Callback Pattern | Event Bus (`k_msgq`) | Zbus |
|---|---|---|---|
| **Coupling** | Subject knows nothing about observers. Shared contract is the callback struct. | Producer must know event enum. Consumer must handle all types in a switch. | Producer and consumer share channel definitions. Observers registered via macros -- no direct dependency. |
| **One-to-many** | Native. Multiple observers append to linked list. All notified in sequence. | Not native. `k_msgq_get()` consumes the event. Fan-out requires duplication. | Native. Multiple observers per channel, all notified by VDED automatically. |
| **Data passing** | Direct via callback parameters (`float temperature, float humidity`). Type-safe. | Serialized into event struct. Must unpack on consumer side. | Read from channel via `zbus_chan_read()`. Type-safe per channel message type. Msg subscribers get a copy. |
| **Execution context** | Callback runs in caller's thread. Observer must defer if needed (`k_work_submit`). | Consumer always runs in its own thread. Producer can post from any context. | **Listener:** publisher's thread (like callback). **Subscriber/Msg Subscriber:** own thread (like event bus). Mix and match. |
| **Thread safety** | Manual. Caller must ensure list stability. Observer must handle context constraints. | Automatic. `k_msgq` is ISR-safe and thread-safe. | Automatic. Channel semaphore protects access. Priority boost prevents inversion. |
| **Buffering** | None. Synchronous fire-and-forget. | FIFO queue (bounded by depth). Events queue up. | **Listener:** none. **Subscriber:** `k_msgq` (channel pointers, may lose intermediate values). **Msg subscriber:** `net_buf` FIFO (full messages preserved). |
| **RAM cost** | `sys_snode_t` per registration (4 bytes on ARM32) + function pointers (4 bytes each). Embedded in already-allocated observer structs. | `sizeof(event) * queue_depth` allocated at compile time. Fixed cost regardless of usage. | Channel metadata struct (~48-64 bytes per channel) + message buffer + semaphore + observer structs (~24-32 bytes each) + subscriber `k_msgq` buffers + msg subscriber `net_buf` pool (default 16 buffers). |
| **Flash cost** | Near zero. `sys_slist` iteration is a few instructions. | Minimal. `k_msgq` is part of Zephyr core. | `CONFIG_ZBUS` pulls in zbus subsystem code (VDED, channel init, observer management, iteration helpers). |
| **Scalability** | Adding an observer requires zero changes to subject. No central dispatch. | Adding an event type requires changing the enum, producer, and consumer switch. Central dispatch bottleneck. | Adding a channel or observer requires only a macro definition. No central dispatch. Scales well to many-to-many. |
| **Complexity** | Low. `sys_slist` + `CONTAINER_OF`. Developer must understand callback lifecycle. | Low. One queue, one loop, one switch. Easiest to understand. | Medium. Framework with macros, linker sections, three observer types, optional net_buf, HLP. Learning curve. |
| **Power** | Near-zero overhead. No framework structs, no channel metadata, no semaphore acquisition for simple notifications. | Minimal. Queue operations are lightweight. | Semaphore acquire/release per publish. HLP priority calculation. Observer iteration through linker sections. Small but measurable overhead per publish. |
| **Debugging** | Call stack: subject -> list iteration -> handler. `CONTAINER_OF` can look cryptic. | Clear: event posted -> main loop -> switch case. Linear trace. | Call stack: publisher -> `zbus_chan_pub` -> VDED -> observer dispatch. Framework code in the middle. Zbus has built-in logging (`CONFIG_ZBUS_LOG_LEVEL`). |
| **Validation** | None built in. Subject can validate before notifying. | None built in. Consumer validates after dequeue. | Built in. Channel validator rejects bad messages at publish time (`-ENOMSG`). |
| **Priority inversion** | Not addressed. Callback runs at caller's priority. | Not addressed. Consumer runs at its own fixed priority. | Addressed via HLP (Highest Locker Protocol). Publisher elevated to prevent inversion. |
| **Runtime observers** | Always dynamic. `sys_slist_append()` / `find_and_remove()`. | N/A (single consumer). | Optional. `CONFIG_ZBUS_RUNTIME_OBSERVERS` enables `zbus_chan_add_obs()` / `zbus_chan_rm_obs()`. Static observers are default. |

---

## What Zbus Would Look Like in This Project

For reference, here is how the firmware's current events would map to zbus
channels and observers. **This is illustrative, not a recommendation.**

### Channel Definitions

```c
/* ble_channels.h */
#include <zephyr/zbus/zbus.h>

/* BLE connection state */
struct ble_conn_msg {
    bool connected;
};
ZBUS_CHAN_DECLARE(ble_conn_chan);

/* Sensor measurement */
struct sensor_msg {
    float temperature;
    float humidity;
};
ZBUS_CHAN_DECLARE(sensor_chan);

/* Button event */
struct button_msg {
    enum button_evt evt;
};
ZBUS_CHAN_DECLARE(button_chan);
```

```c
/* ble_channels.c */
ZBUS_CHAN_DEFINE(ble_conn_chan,
    struct ble_conn_msg, NULL, NULL,
    ZBUS_OBSERVERS(main_ble_sub),
    ZBUS_MSG_INIT(.connected = false)
);

ZBUS_CHAN_DEFINE(sensor_chan,
    struct sensor_msg, NULL, NULL,
    ZBUS_OBSERVERS(main_sensor_listener),
    ZBUS_MSG_INIT(.temperature = 0, .humidity = 0)
);

ZBUS_CHAN_DEFINE(button_chan,
    struct button_msg, NULL, NULL,
    ZBUS_OBSERVERS(main_button_listener, led_button_listener),
    ZBUS_MSG_INIT(.evt = BUTTON_EVT_NONE)
);
```

### Observer Definitions

```c
/* main.c */

/* Listener for sensor data (runs in sensor thread context) */
ZBUS_LISTENER_DEFINE(main_sensor_listener, on_sensor_data);

static void on_sensor_data(const struct zbus_channel *chan)
{
    struct sensor_msg msg;
    zbus_chan_read(chan, &msg, K_NO_WAIT);
    ble_svc_update_temperature_value(msg.temperature);
    ble_svc_update_humidity_value(msg.humidity);
}

/* Listener for button events */
ZBUS_LISTENER_DEFINE(main_button_listener, on_button_event);

static void on_button_event(const struct zbus_channel *chan)
{
    struct button_msg msg;
    zbus_chan_read(chan, &msg, K_NO_WAIT);
    /* handle button press */
}

/* Subscriber for BLE connection (needs own thread) */
ZBUS_SUBSCRIBER_DEFINE(main_ble_sub, 4);

/* LED listener for button events (one-to-many) */
ZBUS_LISTENER_DEFINE(led_button_listener, on_led_button);

static void on_led_button(const struct zbus_channel *chan)
{
    ui_flash_status_led(STATUS_LED_ON_TIME_FOR_STARTUP_MSEC);
}
```

### Publisher Side

```c
/* ble_svc.c - no callback list needed */
static void connected(struct bt_conn *conn, uint8_t err)
{
    struct ble_conn_msg msg = { .connected = true };
    zbus_chan_pub(&ble_conn_chan, &msg, K_NO_WAIT);
}

/* humidity_temperature_svc.c */
static void publish_measurement(float temp, float hum)
{
    struct sensor_msg msg = { .temperature = temp, .humidity = hum };
    zbus_chan_pub(&sensor_chan, &msg, K_NO_WAIT);
}

/* user_interface.c */
static void button_handler(struct k_work *work)
{
    struct button_msg msg = { .evt = data.btn_event };
    zbus_chan_pub(&button_chan, &msg, K_NO_WAIT);
}
```

### Kconfig Additions Required

```
# prj.conf
CONFIG_ZBUS=y
CONFIG_ZBUS_CHANNEL_NAME=n          # save RAM, disable debug names
CONFIG_ZBUS_OBSERVER_NAME=n         # save RAM, disable debug names
CONFIG_ZBUS_PRIORITY_BOOST=y        # default, prevents priority inversion
CONFIG_ZBUS_RUNTIME_OBSERVERS=n     # not needed, all observers are static
CONFIG_ZBUS_MSG_SUBSCRIBER=n        # not needed, listeners/subscribers suffice
```

### RAM Cost Estimate for Zbus Version

| Item | Count | Est. Size | Total |
|------|-------|-----------|-------|
| Channel metadata (`zbus_channel_data`) | 3 | ~48 bytes | ~144 bytes |
| Channel structs (`zbus_channel`) | 3 | ~24 bytes | ~72 bytes |
| Message buffers | 3 | 8+8+4 bytes | ~20 bytes |
| Observer structs (`zbus_observer`) | 4 | ~16 bytes | ~64 bytes |
| Observer data (`zbus_observer_data`) | 4 | ~8 bytes | ~32 bytes |
| Observation mappings | 4 | ~8 bytes | ~32 bytes |
| Subscriber msgq (main_ble_sub, depth 4) | 1 | 4 * 4 bytes | ~16 bytes |
| Semaphores (one per channel) | 3 | ~20 bytes | ~60 bytes |
| **Total** | | | **~440 bytes** |

Compare to the callback pattern: 3 `sys_slist_t` heads (12 bytes) + 4
callback structs embedded in existing objects (~52 bytes total) = **~64
bytes** of notification infrastructure.

---

## Recommendation for This Application

**The callback pattern is the best choice for this firmware.** Here is why:

### 1. Ultra-Low Power Budget

This device runs on a CR2032 coin cell, targeting 6-11 uA average current.
Every byte of RAM and every instruction matters. Zbus adds framework
overhead that provides no benefit at this scale:

- ~440 bytes of channel/observer metadata vs. ~64 bytes for callbacks
- Semaphore acquire/release on every publish (even for synchronous listeners)
- HLP priority calculation on every publish
- VDED observer iteration through linker-section indirection

The callback pattern has near-zero overhead: a linked-list walk and direct
function calls.

### 2. Small Scale (4 Modules, 4 Event Types, 4 Observers)

Zbus shines when you have many-to-many communication across 10+ modules
where the channel abstraction pays for itself in reduced coupling. This
firmware has:

- 3 subjects: `ble_svc`, `humidity_temperature_svc`, `user_interface`
- 4 observers: `main_data`, `led_observer` (and potentially more)
- 4 event types: BLE connect, BLE disconnect, measurement ready, button press

At this scale, the callback pattern is simpler, lighter, and equally well
decoupled.

### 3. RAM is Precious

The nRF52833 has 128 KB of RAM. The firmware already uses ~45 KB (34%).
Zbus would add ~440 bytes of notification infrastructure where the callback
pattern uses ~64 bytes. More importantly, enabling `CONFIG_ZBUS` pulls in
the zbus subsystem's static data structures and initialization code.

### 4. Flash Budget

174 KB of 512 KB flash is used (33%). Enabling `CONFIG_ZBUS` pulls in the
VDED, channel initialization, observer management, and iteration helpers.
Not critical, but wasteful when the same functionality is achieved with
`sys_slist` iteration (a few instructions).

### 5. No Many-to-Many Communication

The data flow is strictly **one-to-few**:

```
humidity_temperature_svc  -->  main_data
ble_svc                   -->  main_data
user_interface            -->  main_data + led_observer
```

No channel has multiple *producers*. No event needs to fan out to more than
two observers. Zbus's many-to-many channel architecture is designed for a
communication graph that does not exist here.

### 6. Already Working and Clean

The callback pattern is implemented, tested, builds cleanly, and
demonstrates proper decoupling. The subjects (`ble_svc`,
`humidity_temperature_svc`, `user_interface`) know nothing about their
observers. Adding a new observer requires zero changes to any subject.

---

## When Zbus Would Be the Right Choice

Zbus is not overkill -- it is a well-designed framework. It becomes the
right choice when the project outgrows what manual callbacks can comfortably
manage:

### Many-to-Many Communication

When multiple producers publish to the same channel and multiple consumers
need to react. Example: a sensor hub where 5 sensor drivers publish to a
`raw_data_chan` and 3 processing modules (filter, logger, transmitter) all
observe it.

### 10+ Modules with Cross-Cutting Concerns

When the number of modules grows to the point where manually wiring
callbacks between them becomes error-prone. Zbus channels act as a contract:
modules only need to agree on the channel definition, not on each other's
APIs.

### Multiple Teams

When different teams develop different modules and need a decoupled
integration point. Channel definitions become the interface contract.
Observers can be added without coordinating with the publishing team.

### Priority Inversion is a Real Risk

When high-priority threads observe channels published by low-priority
threads and bounded latency is required. Zbus's HLP (Highest Locker
Protocol) prevents a low-priority publisher from blocking a high-priority
subscriber indefinitely.

### Channel Validation

When you want to reject invalid messages at the publish point rather than
detecting bad data downstream. Zbus validators run synchronously at publish
time and return `-ENOMSG` on failure.

### Message Subscriber Guarantees

When observers need the **exact message at publish time**, not just the
latest value. Regular subscribers read the channel after waking -- if
multiple publishes happened, intermediate values are lost. Message
subscribers (`ZBUS_MSG_SUBSCRIBER_DEFINE`) get a `net_buf` clone of each
published message.

### Built-in Observability

Zbus provides `zbus_iterate_over_channels()` and
`zbus_iterate_over_observers()` for runtime introspection. Combined with
`CONFIG_ZBUS_CHANNEL_NAME` and `CONFIG_ZBUS_OBSERVER_NAME`, this enables
shell commands or logging that dump the entire pub-sub graph at runtime.

---

## Summary

| Criteria | Callback Pattern | Event Bus | Zbus |
|---|:---:|:---:|:---:|
| Decoupling of subject | +++ | + | +++ |
| One-to-many | +++ | - | +++ |
| Thread safety (automatic) | - | +++ | +++ |
| Buffering | - | +++ | ++ |
| Scalability (many modules) | ++ | - | +++ |
| RAM efficiency | +++ | ++ | + |
| Flash efficiency | +++ | +++ | ++ |
| Simplicity | ++ | +++ | + |
| Data passing | +++ | + | ++ |
| Priority inversion prevention | - | - | +++ |
| Validation | - | - | ++ |
| Power efficiency | +++ | ++ | ++ |

**For this firmware** (4 modules, CR2032, 6-11 uA): **Callback Pattern.**

**For a growing product** (10+ modules, multiple teams, many-to-many): **Zbus.**

**For a quick prototype** (one consumer, few events, simplicity first): **Event Bus.**
