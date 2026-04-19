## MessagingBus Semantics

`MessagingBus` uses a **synchronous publish model**.

When `publish()` is called, all registered subscribers for that message type are executed immediately on the same thread. The function returns only after all callbacks have completed.

### How it works

- **Synchronous dispatch**  
  `publish()` does not use a queue or background worker. All callbacks are executed inline on the caller’s thread.

- **Sequential execution**  
  Callbacks are invoked one by one, in subscription order.  
  Each callback must complete before the next one starts.

- **Blocking behavior**  
  If a subscriber is slow or blocking, it directly delays:
  - the remaining callbacks
  - the original caller of `publish()`

### Thread safety

- A mutex protects the internal subscriber list.
- `subscribe()` is thread-safe and updates the subscriber list under lock.
- `publish()` briefly locks only to copy the subscriber list, then releases the lock before executing callbacks.
- This ensures that callbacks do **not** run under a mutex.

### Concurrency model

- Multiple threads can call `publish()` at the same time.
- Each `publish()` call operates independently using its own snapshot of subscribers.
- Callbacks from different threads may run concurrently.

### Important limitations

- **No parallel fan-out**  
  For a single message, callbacks are always executed sequentially.

- **Backpressure is implicit**  
  Slow subscribers increase latency for the publisher.

- **No buffering or async delivery**  
  There is no queue, retry mechanism, or decoupling between producer and consumer.