These checks require Linux and use real mapped pages, protected pages and unmapped
addresses. They do not emulate Linux on macOS and do not validate rendering.

    cmake -S tests/guest_memory_posix -B /tmp/guest-memory-check
    cmake --build /tmp/guest-memory-check
    ctest --test-dir /tmp/guest-memory-check --output-on-failure

Failed copies may have transferred a prefix. Rendering callers must discard
failed input and handle failed output without assuming transactional writes.
