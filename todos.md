### Branch 1: `refactor/media-recorder-architecture`
*Focuses on state management, pointers, and initialization in the MediaRecorder component.*

1. **Group `RecordingConfig` into Sub-configs**: Separate `struct RecordingConfig` into logical sub-structs (similar to what was done in `settings.hpp/cpp`) for better maintainability and code structure.
2. **Make `AudioCapturer` Optional**: In the Media Recorder, change the `AudioCapturer` member to be a `std::optional<AudioCapturer>` to clearly express its initialization state.
3. **Audit and Reduce Unique Pointers**: Audit the usage of `std::unique_ptr` in the Media Recorder. Try to reduce heap allocations by using direct values or `std::optional`. Also, check if newly initialized `unique_ptr` members should explicitly trigger `.get()` or be initialized with `nullptr` for safety.
4. **Remove `enable_auto_start`**: Remove the `enable_auto_start` configuration/flag. Keep it simple: simply fetch or instantiate `media_recorder` immediately at construction. 
5. **Separate Directory Functions for Segments**: In the rolling segments logic, extract the logic for creating and removing temporary directories into separate, explicitly named functions to improve readability.
6. **Refactor Audio Mixer `jthread` Stop Request**: Review the `jthread` lifecycle in the audio mixer. The current stop request and join mechanism is counter-intuitive. Ensure we are correctly leveraging the `std::jthread` stop token.

### Branch 2: `cleanup/dead-code-removal`
*Focuses on removing unused classes, variables, and features.*

1. **Delete `ClockMapper`**: Remove the `ClockMapper` class entirely since it is no longer being used anywhere.
2. **Remove `seq` logic**: Remove instances of the `seq` (sequence) variable since sequence tracking is no longer used.
3. **Remove Sync Plan**: Delete the "sync plan" features and related data structures, as they are unused.
4. **Remove `seg_ms` in favor of `buffer_duration`**: Remove the `seg_ms` variable and rely exclusively on `buffer duration` to prevent having redundant time-tracking variables.

### Branch 3: `refactor/time-and-sync-logic`
*Focuses on renaming time variables and validating clock sync logic.*

1. **Validate Clock Sync Functionality**: Recheck if the clock synchronization mechanism actually provides the expected value and functions correctly under standard loads.
2. **Rename `unix_now_ns`**: The name `unix_now_ns` is ambiguous. Rename it to `system_clock_now_as_int64` (or similar) to explicitly show its unit and clock source.
3. **Rename `t1/t0/t2` to Semantic Names**: Give the generic time variables (`t1`, `t0`, `t2`) much clearer, domain-specific names (e.g. `client_sent_time`, `server_recv_time`) so the sync protocol is readable.
4. **Audit `Sync::steady_deadline_from_unix_ns`**: Check if `steady_deadline_from_unix_ns` is actually necessary. Simplify the time domain conversions if possible.
5. **Rename and Document `protocol export at`**: The `protocol export at` field needs a clearer name representing its function, along with an inline comment detailing what it handles.
6. **Verify Sync Offset Strategy**: Audit the time offset strategy. Explicitly test/handle both edge cases: when the client is lagging vs when the server is lagging.

### Branch 4: `refactor/network-session-handling`
*Focuses on deduplication, timers, and handling asynchronous read/write states safely.*

1. **Deduplicate Client/Server Sync Functions**: Both the sync client and sync server share a ton of similarities. Refactor these overlapping functions into a shared common file or base class.
2. **Refactor `do_read` to Separate Concerns**: Break apart the `do_read` logic. Strictly handle errors inside the function itself, and move all string formatting responsibilities to a separate common string utility function.
3. **Consolidate Reconnect and Cmd Timers**: `reconnect timer` and `cmd timer` do not need to be distinct. Merge them into a single timer object to simplify state cleanup.
4. **Move `close` into `schedule_reconnect`**: Re-structure the connection flow so the closing of the socket automatically and seamlessly happens inside the `schedule_reconnect` function.
5. **Remove Graceful Shutdown `stop_at`**: There is no need for custom `stop_at` logic for a graceful shutdown. Rely fully on standard connection aborted events from the network layer.
6. **Check `queue is empty` solely in `do_write`**: Remove any double-checking logic regarding the write queue. Verify if the queue is empty exclusively inside the `do_write` operation.
7. **Change Session `dispatch`**: Review the `boost::asio::dispatch` usage inside the `Session` handlers. Change/fix the context execution to avoid issues.
8. **Investigate `unique_ptr` message buffer on send**: Consider using a `std::unique_ptr` for the message buffer that goes out on `send` (for the sync and worker client) to prevent pointer issues.

### Branch 5: `cleanup/code-quality-and-logging`
*Focuses on code improvements, strictly typed configs, and enhanced system observability.*

1. **Centralize Log Paths (`artifactexporter.cpp`)**: Extract the hardcoded path formatting (`"{}/versions/vs-{}/logs"`) from `artifactexporter.cpp` and move it fully into the global settings configuration.
2. **Use `constexpr char*` for JSON Fields**: Update the JSON formatting and parsing to use `constexpr char` variables instead of raw string literals (e.g. `"status"`) to prevent typo-related bugs.
3. **Use `try_as` for JSON Parsing**: Simplify the code by using the `try_as` function to parse JSON values, making the decoding block shorter and significantly safer.
4. **Use `std::variant` for App Context**: Change the application context from holding a struct full of nullable raw pointers to a strictly typed `std::variant` specifying whether it behaves as a sync server or a media recorder.
5. **Add Ping-Pong Comment Context**: Add a thorough comment detailing why the ping/pong delay exists, allowing future developers to understand the intentional connection delay overhead.
6. **Improve Boost Handler Cleanup Logs**: Add debug logs to the boost async cleanup handlers. Specifically log `asio::error` codes like connection drops.
7. **Improve Sync Master Message Logs**: Add detailed message handling logs inside the sync master class so message streams are easier to trace.
8. **Rename `buffer duration`**: Change the variable name `buffer duration` to something more descriptive like `recording_length_seconds`.
9. **Unify threads to C++20 `std::jthread`**: Do a sweep of the codebase and replace any standard `std::thread` instances with `std::jthread` to centralize behavior around automatic cancellation and join on destruction.
